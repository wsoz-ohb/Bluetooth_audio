# LVGL 汉字库显示方案设计文档

> 项目:Bluetooth_audio(STM32F407 + RT-Thread + LVGL 8.3.11)
> 日期:2026-07-26
> 状态:设计评审稿(未实现)
>
> 目标:在 LVGL 界面上显示中文(主要场景:AVRCP 拿到的歌曲名/歌手名),
> 字库存放在板载 W25Q128 的 `font` 裸分区(FAL 分区表 0 ~ 2MB),
> 并给出字库文件的生成与烧写(写入 Flash)的完整方案。

---

## 1. 整体架构一图流

```
【PC 侧,一次性】                        【MCU 侧,运行时】

TTF 字体(思源黑体等)                    AVRCP 曲名 (UTF-8 字符串)
      │                                        │
      ▼ Python 生成脚本                        ▼
  font.bin                              lv_label_set_text(label, "晴天 - 周杰伦")
(头部+Unicode索引+点阵)                        │
      │                                        ▼
      ▼ 串口 YMODEM 传输                 LVGL 内部把 UTF-8 逐字解码为 Unicode 码点
  msh> font_update                             │
      │                                        ▼
      ▼                                 my_font.get_glyph_dsc(0x6674 '晴')
fal_partition_write("font",...)                │
      │                                        ▼
      ▼                                 font_app: 在索引表中二分查找码点
┌─────────────────────┐                        │ 命中 → 计算点阵地址
│ W25Q128  font 分区   │ ◄──────────────  fal_partition_read("font", offset, buf, 32)
│ 0x000000 ~ 0x200000 │                        │
└─────────────────────┘                        ▼
                                        get_glyph_bitmap 返回 32 字节点阵
                                               │
                                               ▼
                                        LVGL 渲染进 draw_buf → LCD_ShowPicture 刷屏
```

核心思想一句话:**LVGL 每画一个字就通过我们注册的两个回调"现场提问",
我们的回调从 Flash 裸分区把这个字的点阵读出来交给它,RAM 里几乎不驻留任何字库数据。**

---

## 2. 为什么选这条路 —— 方案对比

### 2.1 候选方案

| 方案 | 原理 | RAM 占用 | Flash(片内) | 显示效果 | 结论 |
|---|---|---|---|---|---|
| A. `lv_font_conv` 生成 C 数组编译进固件 | 官方工具把 TTF 转成 .c 文件 | 0(XIP 直读片内) | **全量 6763 字约 300KB+** | 好,支持抗锯齿 | ❌ 片内 Flash 装不下(BT-STACK+LVGL+提示音已很挤),且换字库要重烧固件 |
| B. `lv_font_conv` 生成 .bin + `lv_font_load()` | 运行时从文件系统加载官方二进制字体 | **整包读进 RAM,约 260KB+** | 0 | 好 | ❌ F407 只有 192KB RAM,直接爆;砍到几百字则失去意义 |
| C. FreeType / lv_tiny_ttf 实时渲染 TTF | 矢量字体运行时光栅化 | 数百 KB + 大量 CPU | 0 | 最好,任意字号 | ❌ F407 无 MMU 小 RAM 平台跑不动,还要和 SBC 解码抢 CPU |
| D. 只显示拼音/英文,不做中文 | 回避问题 | 0 | 0 | 差 | ❌ 曲名大量是中文,体验不可接受 |
| **E. 自定义点阵字库存外部 Flash + 自定义字体回调(本方案)** | 固定尺寸点阵,按需单字读取 | **约 100B~14KB(可调)** | 约 1KB 驱动代码 | 16px 点阵,1bpp 无抗锯齿 | ✅ 唯一在本硬件上全字符集可行的方案 |

> 方案 E 的代价是显示效果:1bpp 点阵没有抗锯齿,16px 下汉字边缘有锯齿感。
> 这是 192KB RAM 平台的合理取舍;后续想提升可升级为 2bpp/4bpp 灰度点阵(体积 ×2/×4,
> font 分区 2MB 完全放得下,回调逻辑不变,只改 bpp 字段和生成脚本)。

### 2.2 字库编码排序的选择:Unicode 而非 GB2312

取模软件(PCtoLCD 等)传统输出按 GB2312 区位码排序,但本项目**必须按 Unicode 码点排序**:

- 数据源:AVRCP `GetElementAttributes` 返回的曲名是 **UTF-8**(蓝牙规范 CharsetID 106 = UTF-8);
- 渲染端:LVGL 内部统一用 **Unicode 码点**调用字体回调(`letter` 参数就是码点);
- 若字库按 GB2312 排序,每个字都要先做 Unicode→GB2312 查表转换,需额外约 27KB 转换表,纯属浪费。

按 Unicode 排序后:UTF-8 → Unicode(LVGL 已做)→ 二分查索引 → 读点阵,全链路零转换。

---

## 3. 字库文件格式(font.bin)

自定义格式,定长记录,尽量简单:

```
┌──────────────────────────────────────────────────────────────┐
│ Header(16 字节)                                              │
│   [0..3]  magic      = "ZBFT" (0x5A 0x42 0x46 0x54)          │
│   [4]     version    = 1                                     │
│   [5]     width      = 16      字宽(px,全角)               │
│   [6]     height     = 16      字高(px)                     │
│   [7]     bpp        = 1                                     │
│   [8..11] glyph_cnt  = N       字符总数(小端 uint32)        │
│   [12..15] crc32     = 索引表+点阵区的 CRC32(烧写校验用)   │
├──────────────────────────────────────────────────────────────┤
│ 索引表(N × 2 字节)                                          │
│   uint16 unicode[N]  Unicode 码点,严格升序(供二分查找)    │
│   (BMP 内码点即可覆盖 GB2312 全部汉字,uint16 足够)         │
├──────────────────────────────────────────────────────────────┤
│ 点阵数据区(N × 32 字节)                                     │
│   第 i 条记录 = 索引表第 i 个码点的点阵                       │
│   16×16 1bpp = 32 字节,逐行扫描,每行 2 字节,MSB 在左       │
│   ASCII 等半角字符同样占 32 字节槽位,但 adv_w 按 8px 处理    │
└──────────────────────────────────────────────────────────────┘
```

- **字符集**:ASCII 可见字符(95 个)+ GB2312 一级+二级汉字(6763 个)+ 常用全角标点,
  约 6900 字 ≈ 14KB 索引 + 216KB 点阵 ≈ **230KB**,font 分区 2MB 占用 11%。
- **定长 32B 记录**的意义:点阵地址 = `头部16 + N×2 + i×32`,一次乘加即得,无需逐条遍历。
- **ASCII 为什么也放进来**:让中文字体自带英文能力,一个 `lv_font_t` 就能显示
  "晴天 - Jay Chou";界面其他地方仍可继续用 Montserrat(见 §4.4 fallback 讨论)。

---

## 4. MCU 侧显示链路(font_app 模块设计)

新建 `applications/font_app.c/h`,对外只暴露:

```c
rt_err_t   font_app_init(void);        /* 校验分区里的字库,注册 lv_font_t */
const lv_font_t *font_app_get16(void); /* 给 GUI 代码取字体指针 */
rt_bool_t  font_app_is_ready(void);
```

### 4.1 初始化流程(font_app_init)

```
fal_partition_find("font")
  → 读 16B Header,校验 magic/version/width/height
  → 记录 glyph_cnt、索引表偏移、点阵区偏移到静态上下文
  → (可选)按 RAM 余量决定是否把 14KB 索引表整体读进 RAM 缓存
  → 填充 lv_font_t:
       .get_glyph_dsc    = font_get_glyph_dsc_cb
       .get_glyph_bitmap = font_get_glyph_bitmap_cb
       .line_height      = 16
       .base_line        = 2      (基线预留,汉字场景不敏感)
       .dsc              = &s_font_ctx
```

失败(分区空/magic 不对)时 `font_app_get16()` 返回 `RT_NULL`,
GUI 层判空回退到 Montserrat 纯英文显示,**字库未烧写不能导致界面崩溃**。

### 4.2 get_glyph_dsc 回调(LVGL 问:这个字有吗?多宽?)

```
输入 letter(Unicode 码点)
  → 在索引表二分查找
       RAM 缓存了索引:纯内存二分,log2(6900)≈13 次比较,微秒级
       未缓存:直接在 Flash 上二分,13 次 × 2 字节 fal 小读
  → 未命中 → return false(LVGL 自动走 fallback 字体或画占位符)
  → 命中,记下 glyph_index 存入上下文(供紧接着的 bitmap 回调复用,省一次查找)
  → 填 dsc_out:
       adv_w = (letter < 0x80) ? 8 : 16     ← 半角/全角步进
       box_w = box_h = 16, ofs_x = ofs_y = 0, bpp = 1
  → return true
```

### 4.3 get_glyph_bitmap 回调(LVGL 问:点阵给我)

```
用上一步记下的 glyph_index 计算偏移:
    offset = 16 + glyph_cnt*2 + glyph_index*32
fal_partition_read("font", offset, s_glyph_buf, 32)
return s_glyph_buf;      ← 静态 32 字节缓冲
```

关键约束:**LVGL 拿到指针后立即使用、用完即弃,且回调全部发生在 LVGL
单线程内(lv_timer_handler 上下文),所以一个静态缓冲安全**,不存在并发覆写。

### 4.4 与现有英文字体的关系:fallback 链

LVGL 8.3 的 `lv_font_t` 自带 `fallback` 字段(已在源码确认)。两种接法:

| 接法 | 效果 | 建议 |
|---|---|---|
| 曲名 Label 直接用中文字体 | 中英文都走点阵字库,风格统一但英文是 16px 点阵 | ✅ 曲名/歌手名标签用这个 |
| `montserrat_14.fallback = 中文字体` | 英文走 Montserrat(抗锯齿),缺字才落到点阵 | 行高 14 vs 16 不一致,基线有轻微错位,谨慎使用 |

**建议:只在需要显示中文的 Label 上 `lv_obj_set_style_text_font(label, font_app_get16(), 0)`,
界面其余部分维持现状(Montserrat 14),不做全局替换、不碰 fallback。**改动面最小。

### 4.5 性能与并发深入分析(review 重点)

**每字开销估算**(SPI1 @ 20MHz,含 FAL/SFUD 层开销):
- 索引 RAM 缓存时:1 次 32B 读 ≈ 几十 µs;
- 索引不缓存时:13 次 2B 读 + 1 次 32B 读 ≈ 200~400µs(每次读有 CS/命令字节固定开销)。

一屏曲名按 20 个字算:缓存索引 ≈ 1ms;不缓存 ≈ 8ms。250ms 刷新周期下两者都可接受,
但**曲名滚动动画**(marquee,LVGL 每帧重排版)时不缓存方案每帧多 8ms,会压缩渲染预算。

**决策:先不缓存索引(省 14KB RAM,BT-STACK 吃 RAM 很凶),跑通后实测滚动帧率,
不够再加"索引缓存"或"最近 N 字 LRU 点阵缓存"(每字 34B,32 字缓存也才 1KB)。**
LVGL 自身还有 `LV_USE_FONT_PLACEHOLDER` 和 label 内部的重绘合并,实际取字频率低于理论值。

**SPI1 总线竞争**:LCD(st7789)和 Flash(w25q)共 SPI1。
- 同线程串行:LVGL 渲染(取字)与 flush(刷 LCD)在同一线程先后发生,不并发;
- 跨线程:RT-Thread SPI 总线自带互斥锁,若其他线程(如以后 littlefs 写配置)与取字撞车,
  只是排队等锁,不会数据错乱。唯一注意:**不要在音频回调等高优先级上下文里碰文件系统**,
  避免优先级反转拖累取字/刷屏(现有代码没有这么做,保持即可)。

**与 A2DP 音频的关系**:音频走 I2S/SAI + ES8311,不占 SPI1,取字不影响播放链路。✅

### 4.6 UTF-8 入口确认

`lv_label_set_text()` 接受 UTF-8(LVGL 默认 `LV_TXT_ENC_UTF8`),AVRCP 返回的曲名
字节流可直接塞给 label,**不需要任何转码**。仅需注意 AVRCP 属性值不带 '\0',
拷贝时手动补终止符,并对超长曲名截断(建议上限 64 字节)。

---

## 5. 字库的生成(PC 侧)

不用取模软件,写 Python 脚本 `tools/font_gen.py`(依赖 `freetype-py` 或 `Pillow`):

```
输入:开源 TTF(思源黑体 / 文泉驿微米黑,注意商用授权)+ 字符集文本文件
流程:逐字符 → FreeType 渲染 16×16 单色位图 → 按 Unicode 升序排列
     → 打包 Header + 索引表 + 点阵区 → 计算 CRC32 回填 Header
输出:font.bin(约 230KB)
```

脚本化的好处:换字号(16→24)、换 bpp(1→2 抗锯齿)、增删字符集都是改参数重跑,
不被取模软件的格式绑死。字符集文件建议直接用 GB2312 全集 + ASCII,一劳永逸。

---

## 6. 字库写入 Flash 的方案(重点对比)

MCU 在板上,font.bin 在 PC 上,怎么把 230KB 送进 W25Q128 的 font 分区?

| 方案 | 原理 | 优点 | 缺点 | 结论 |
|---|---|---|---|---|
| **A. 串口 YMODEM(推荐)** | PC 终端(XShell/MobaXterm/Tera Term)用 YMODEM 协议发文件,MCU 端 msh 命令 `font_update` 接收并边收边写 FAL 分区 | 零额外硬件;RT-Thread 自带 ymodem 组件(源码已确认存在);带 CRC 校验;以后 OTA 固件也能复用同一条通道传进 fw_a | 115200 波特率下 230KB 约 25~30 秒(可把该串口临时提到 921600,秒级) | ✅ **主方案** |
| B. 调试器直烧外部 SPI Flash | J-Flash/STM32CubeProgrammer + 自定义 external loader 直接写 W25Q128 | 不占应用代码 | ST-Link 官方 loader 只覆盖 QSPI 映射的板子,SPI1 挂的 Flash 要自己写 loader(裸机驱动+对齐 FAL 偏移),工作量大且易错 | ❌ 不值 |
| C. 先传进 littlefs 再搬运 | 文件传到 `/font.bin`,再拷入裸分区 | "文件"直观 | 仍需要一条传输通道(绕回 YMODEM),多一次 12MB 分区的中转和双倍等待 | ❌ 多此一举 |
| D. 自定义串口协议 + Python 上位机 | 自己定帧格式收发 | 完全可控 | 重复造 YMODEM 的轮子,还没它可靠(YMODEM 有重传/CRC) | ❌ |
| E. 蓝牙 SPP/OTA 通道传输 | 用蓝牙传字库 | 无线 | BT-STACK 目前跑 A2DP/AVRCP,再加 SPP 通道复杂度高;字库是一次性烧写,不值 | ❌ 留给未来 OTA 阶段考虑 |

### 6.1 主方案落地细节(font_update 命令)

前置:menuconfig 开启 `RT_USING_RYM`(Utilities → YMODEM,当前**未开启**,要先打开)。

```
msh> font_update
  → fal_partition_find("font")
  → rym 会话开始,提示用户在终端发起 YMODEM 发送
  → on_data 回调:攒满 4KB → fal_partition_erase + fal_partition_write(边收边写)
       (YMODEM 128/1024B 一包,FAL 擦除粒度 4KB,需做 4KB 聚合缓冲)
  → 传输结束:重读 Header,按 glyph_cnt 计算总长,校验 CRC32
  → 打印 "font OK: 6900 glyphs, crc pass" / 失败则报错并建议重传
  → 通知 font_app 重新 init(或提示重启)
```

细节注意:
- **先擦后写、4KB 对齐**:聚合缓冲必须静态分配(4KB),别放线程栈;
- **写入期间 GUI 仍在跑**:font_app 若已就绪会同时从该分区读!
  `font_update` 开始时先把 font_app 置为 not-ready(GUI 回退英文),写完再重新 init;
- **CRC32 兜底**:掉电/串口断线导致半截数据时,下次开机 `font_app_init` 的 CRC 校验
  会失败并拒绝启用,不会显示乱码——这就是 Header 里放 CRC 的原因。

### 6.2 验证手段

- `msh> font_info`:打印 Header(magic/字数/CRC 校验结果);
- `msh> font_test 晴天`:临时创建一个 label 显示参数文本,肉眼验证;
- FAL 自带 `fal probe font` + `fal read 0 16` 可裸查 Header 十六进制。

---

## 7. 自我 Review(风险清单与结论)

| # | 风险/疑点 | 分析 | 处置 |
|---|---|---|---|
| 1 | 静态 32B 点阵缓冲会不会被并发覆写? | LVGL 渲染单线程,dsc→bitmap 严格串行,一字一取 | 无风险,不加锁 |
| 2 | 曲名滚动时取字频率有多高? | LVGL label 滚动每帧重排,最坏每帧全串取字 | 先实测;慢则加 32 字 LRU 缓存(约 1KB),回调层加,GUI 无感 |
| 3 | 字库没烧/烧坏,界面会怎样? | init 校验 magic+CRC,失败则 get16() 返回 NULL | GUI 判空回退 Montserrat,只丢中文不崩溃 |
| 4 | dsc 回调记 index、bitmap 回调用,若 LVGL 乱序调用? | LVGL 8.3 渲染路径保证 dsc 成功后紧跟 bitmap;但 fallback 解析时可能对同字先后问两个字体 | bitmap 回调内不依赖"上一次 dsc 一定是本字体":上下文里同时存 letter+index,bitmap 入参虽只有 letter,可比对不符则重查一次(防御性,代价一次二分) |
| 5 | uint16 索引放得下所有字符? | GB2312 全部汉字在 BMP(≤0xFFFF) | 够用;若未来要 emoji 再升 uint32 索引(格式 version 字段留了后路) |
| 6 | 2MB font 分区只用 230KB,浪费? | 预留给 24px 大字号点阵(约 500KB)/2bpp 灰度版共存,Header 可扩展成多字号包 | 接受,分区不必改 |
| 7 | YMODEM 占用的是哪个串口? | msh 控制台串口复用(rym 就是这么设计的),传输期间控制台静默 | 可接受;注意 uart_send_pcm 若占用同串口需错开 |
| 8 | 烧写时 GUI 正在读同一分区 | §6.1 已处理:写前置 not-ready | 已闭环 |
| 9 | 显示效果 1bpp 锯齿 | 16px 1bpp 是 F407 级别的行业常规(如老款 MP3/家电) | 接受;升级路径明确(2bpp,只改生成脚本+bpp 字段) |

**结论**:方案 E + Unicode 定长点阵格式 + YMODEM 烧写,在本硬件约束下是
假设最少、依赖最少、每一环都可单独验证的路径(物理层 fal read → font_info →
font_test → GUI 集成,四步各自可测),符合奥卡姆剃刀。

---

## 8. 实施顺序(建议)

1. **前置**:menuconfig 开 `RT_USING_RYM`;完成 littlefs 挂载链(fal_init 时机也在此定);
2. `tools/font_gen.py` 生成 font.bin(PC 侧可先用 `font_info` 期望值自校验);
3. `font_app.c`:init + 两个回调 + `font_info`/`font_test` msh 命令;
4. `font_update` YMODEM 烧写命令,烧入真字库;
5. GUI 接入:曲名 Label 换 `font_app_get16()`,配合 AVRCP 元数据显示;
6. 实测滚动帧率,决定是否加 LRU/索引缓存(预留优化点,不提前做)。

其中 2/3 可并行,4 依赖 1,5 依赖 3+4。
