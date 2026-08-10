# Bluetooth Audio 当前架构说明

> 更新时间：2026-08-10
> 主控：STM32F407VG + RT-Thread 4.1.1  
> 蓝牙：外挂 ESP32-WROOM-32E（Controller）+ 工程内 `BT-STACK`（Host）  
> 当前定位：**蓝牙音箱 + PTT AI 对话**（A2DP/AI PCM 混音 + AVRCP + GUI）

本文只描述**已经在代码里落地**的架构与能力，不把“计划做”的事写成已完成。

---

## 1. 目标与范围

### 1.1 已完成的主目标

- 被手机 / PC 发现并连接（Bluetooth Classic）
- **A2DP Sink**：接收 SBC → 解码 PCM → `I2S2 + DMA + ES8311` 播放
- **AVRCP Controller / Target**：
  - 本地按键 / 编码器控制对端播放、暂停、切歌、音量
  - 拉取 Now Playing（歌名 / 歌手 / 时长 / 进度）
  - 绝对音量同步（支持时）
- **LVGL 播放页 GUI**（320×240 横屏）
  - Welcome 启动页 → Main 播放页
  - 歌名 / 歌手中文显示、进度条、矢量旋转圆盘、状态文案
- **板载 W25Q128**：SFUD + FAL 分区；`font` 分区存放 ZBFT 点阵字库
- 开机提示音与 Welcome 淡出同步
- 长按 PTT 采集 PCM，经 `uart3` 与香橙派完成 AI 对话
- A2DP 背景音与 AI 回复统一进入 `audio_mixer`，AI 回复期间自动 Duck

### 1.2 明确未做 / 未产品化

| 项 | 状态 |
|---|---|
| HFP / HSP 通话 | 未接 |
| BLE 业务 | 配置关闭 |
| A2DP SBC fragmentation 重组 | 未实现（分片包直接丢） |
| littlefs 挂载与业务读写 | 包已引入、分区已规划，业务未产品化 |
| OTA（fw_a / fw_b） | 仅分区预留 |
| 触摸屏播控 | 未做（播控走按键/编码器） |
| SPI DMA 刷屏 | 未做（draw buf 在 CCM，DMA 不可见） |
| AI UART 帧协议 | 当前仍为 44.1kHz mono s16le 裸流，以空闲超时判断回复结束 |
| 全双工回声消除 | 未做；PTT 采集期间播放与采集保持互斥 |

---

## 2. 总体分层

```text
手机 / PC
    |  Classic: A2DP (SBC) + AVRCP
    v
+------------------------------------------------------------------+
| STM32F407 + RT-Thread                                            |
|                                                                  |
|  applications/                                                   |
|    main.c                 启动编排                               |
|    bt_app.c               BT Host + profile 注册                 |
|    bt_a2dp_sink_app.c     A2DP Sink 事件 / media 转发            |
|    bt_a2dp_audio.c        RTP/SBC 解码 → PCM                     |
|    audio_mixer.c          A2DP/AI 双音源 ring、Duck 与饱和混音   |
|    bt_avrcp_ct_app.c      AVRCP CT/TG、元数据、音量              |
|    es8311_audio.c         Codec、采集会话与 I2S DMA 输出         |
|    control_app.c          按键 + 旋转编码器 → AVRCP              |
|    lcd_app / mylvgl_app   ST7789 + LVGL 显示端口                 |
|    gui_manager / welcome / main   界面管理与播放页               |
|    sfud_app / font_app / font_update   Flash / 字库              |
|                                                                  |
|  mycomponents/BT-STACK/   Host 栈 + RT-Thread port + ESP32 chipset|
|  mycomponents/es8311/     Codec 寄存器驱动                       |
|  mycomponents/LCD/        ST7789 面板驱动                        |
|  mycomponents/keyboard/   按键状态机                             |
|  packages/LVGL-v8.3.11    GUI                                    |
|  packages/littlefs-v2.5.0 已引入，业务未挂载产品化               |
+------------------------------------------------------------------+
    | HCI H4 UART (uart2, 921600, 流控)
    v
ESP32-WROOM-32E  (Bluetooth Controller)
    |
    | 播放: SBC→PCM→I2S2/DMA→ES8311 DAC
    | 显示: SPI1 → ST7789 + W25Q128（共总线，独立 CS）
```

---

## 3. 启动链

### 3.1 真正入口

- 用户态入口：`applications/main.c` 的 `main()`
- `cubemx/Src/main.c` 的 `__WEAK main` 会被覆盖
- CubeMX 主要提供：时钟、`hi2s2` 等句柄、MSP 初始化

### 3.2 应用启动顺序（与代码一致）

```text
main()
  → sfud_app_init()              // W25Q128：GPIO/JEDEC/SFUD/清写保护
  → es8311_audio_init()          // Codec + I2S 会话层
  → boot_prompt_play_once()      // 同步阻塞提示音
  → mylvgl_notify_boot_prompt_done()  // 通知 Welcome 可淡出
  → bt__init()
       → btstack_port_init()
       → bt_a2dp_sink_service_init()
       → bt_avrcp_ct_service_init()
       → btstack_port_start_thread()
  → control_app_init()           // 按键 + 编码器线程
  → while (1) rt_thread_mdelay(10)
```

### 3.3 LVGL 启动（组件自动拉起，不在 main 里手写）

```text
LVGL 线程
  → lv_port_disp_init()          // lcd_app_init + draw buf + flush
  → lv_user_gui_init()
       → font_app_init()         // FAL "font" 分区，索引表进 RAM
       → gui_manager_init()
            → Welcome
            → (提示音完成 + 打字动画结束) → Main 播放页
```

---

## 4. 模块职责

### 4.1 蓝牙

| 文件 | 职责 |
|---|---|
| `bt_app.c` | Host 初始化编排；只注册 A2DP Sink + AVRCP |
| `bt_a2dp_sink_app.c` | SEP / SDP / stream 事件；media 包交给解码层；suspend/resume 查询 |
| `bt_a2dp_audio.c` | RTP + SBC 解码；写 Mixer 背景音源；满缓冲回压丢包 |
| `bt_avrcp_ct_app.c` | CT 控制 + TG 侧绝对音量；Now Playing 缓存；状态机防重入 |

AVRCP 对外只读接口（GUI 用）：

- `bt_avrcp_ct_get_title()` / `get_artist()`
- `bt_avrcp_ct_get_song_length_ms()` / `get_song_position_ms()`
- `bt_avrcp_ct_get_link_state()` / `get_playback_state()`
- `bt_avrcp_ct_is_absolute_volume_active()`

控制接口：`play / pause / next / previous / volume_up / volume_down`。

### 4.2 音频会话

`audio_mixer.c` 是统一播放入口：

- 背景音源：A2DP stereo PCM
- 语音音源：香橙派 AI mono PCM
- 单路时 1.0 原样直通；双路时 AI 保持 1.0，背景音在约 20ms 内降到 0.2
- AI PCM 暂时断流或结束后，背景音约 250ms 平滑恢复，最终输出使用 int16 饱和保护

`es8311_audio.c` 负责硬件会话：

- 模式：`IDLE` / `PLAYBACK` / `CAPTURE`（互斥）
- Mixer 背景 ring 放在 **CCM**；采集 ring 和 AI ring 放在主 RAM
- I2S DMA 双缓冲必须在 **主 RAM**
- 本地音量 0~127，与 AVRCP Absolute Volume 对齐
- `boot_prompt_play_once()`：开机提示音

### 4.3 本地控制

`control_app.c` 取代旧的“按键切采集”主路径：

| 输入 | 动作 |
|---|---|
| PC9 单击 | PLAYING→pause；否则 play |
| PC9 双击 | next |
| 编码器 PB6/PB7 | volume up / down（优先绝对音量） |

### 4.4 显示与 GUI

| 文件 | 职责 |
|---|---|
| `lcd_app.c` | ST7789，SPI1，CS=`PC4`，DC=`PE4`，RST=`PE5`，横屏 320×240，总线约 42 MHz |
| `mylvgl_app.c` | LVGL disp port；draw buf **36 行** 放 CCM；flush 时 RGB565 字节交换 + `LCD_ShowPicture` |
| `gui_manager.c` | Welcome ↔ Main 切换；转发 boot prompt 完成事件 |
| `gui_welcome.c` | 打字机欢迎页，等提示音后淡出 |
| `gui_main.c` | 播放页：矢量圆盘 + 歌名/歌手 + 进度/时间 + 状态；约 400 ms 刷新；圆盘约 24 s/圈 |

### 4.5 Flash / 字库

| 文件 | 职责 |
|---|---|
| `sfud_app.c` | 共总线 SPI 扫卡、JEDEC、SFUD probe、开机清 BP 写保护 |
| `fal_cfg.h` | 分区表（见下） |
| `font_app.c` | LVGL 自定义字体：索引表 RAM 缓存 + 单字 32B 点阵按需读 |
| `font_update.c` | `font_update` / `font_info`：YMODEM 烧字库 + CRC 校验 |
| `fontlib/` | PC 侧生成 `font.bin`、FontFlasher |

FAL 分区（W25Q128 16MB）：

```text
font        0 ~ 2MB     汉字点阵（ZBFT），运行时直读
fw_a        2 ~ 4MB     OTA 下载区（预留）
fw_b        4 ~ 5MB     回退备份（预留）
filesystem  5 ~ 16MB    littlefs 预留（业务未挂载产品化）
```

字库：SimHei 16×16 1bpp，ASCII + 全 GB2312，约 7540 字 / 250 KB。细节见 `fontlib/README.md` 与 `docs/lvgl_chinese_font_design.md`。

---

## 5. 硬件资源分工

| 资源 | 用途 |
|---|---|
| `uart1` | 控制台 / msh / YMODEM 烧字库 |
| `uart2` | 蓝牙 HCI H4（921600 + 流控）→ ESP32 |
| `uart3` | PTT PCM 上行与 AI 回复 PCM 下行（2Mbps，半双工切换） |
| `i2c1`（软） | ES8311 控制，SCL=`PC11`，SDA=`PC12` |
| `I2S2` + DMA | ES8311 数字音频（播放 / 全双工采集能力仍在） |
| `SPI1` | **共总线**：ST7789 CS=`PC4` + W25Q128 CS=`PA4`；SCK/MISO/MOSI=`PA5/6/7` |
| `PC9` | 按键 SW |
| `PB6/PB7` | 旋转编码器 CLK/DT |
| CCM 64KB | Mixer 背景 ring + UART 线程栈 + LVGL draw buf（**不可 DMA**） |

---

## 6. 数据流

### 6.1 蓝牙播放

```text
手机
  → A2DP media
  → bt_a2dp_sink media handler
  → bt_a2dp_audio_process_media_packet()
       RTP/SBC 解析 → SBC decode
  → audio_mixer_write(BACKGROUND)
  → background ring (CCM)
  → audio_mixer_render_stereo()
  → 过启动阈值后 I2S TX DMA
  → ES8311 DAC → 喇叭
```

### 6.2 PTT AI 对话

```text
长按 PTT
  → 暂停 A2DP、关闭本地 media gate
  → ES8311 CAPTURE → uart3 TX → 香橙派
松开 PTT
  → 退出 CAPTURE、恢复 A2DP、uart3 切 RX
  → AI mono PCM → audio_mixer_write(VOICE)
  → 与 BACKGROUND Duck 混音 → ES8311 DAC
```

### 6.3 控制与元数据

```text
按键/编码器
  → control_app
  → bt_avrcp_ct_play/pause/next/volume_*
  → 对端手机播放器

对端通知 / GetElementAttributes
  → bt_avrcp_ct 缓存 title/artist/pos/len/playback
  → gui_main 400ms timer 差分刷新 UI
```

### 6.4 显示刷新

```text
LVGL 脏区渲染 → CCM draw buf
  → RGB565 swap（若未开 LV_COLOR_16_SWAP）
  → LCD_ShowPicture（CPU SPI，无 DMA）
  → ST7789
```

中文：`lv_label` UTF-8 → Unicode → `font_app` 二分索引 → FAL 读 32B 点阵。

---

## 7. 关键配置摘要

### 7.1 蓝牙（`bt_config.h`）

- 本地名：`WSOZ`
- Classic 可发现 / 可连接
- BLE 关闭
- HCI：`uart2`，921600，流控开
- AVDTP：1 connection / 1 SEP

### 7.2 SBC 能力（常见组合）

- 44.1 kHz；Stereo / Joint Stereo
- block 4~16；subbands 4/8；Loudness/SNR；bitpool 2~53
- 固定 44.1 kHz，与采集及 AI PCM 保持一致，不引入重采样

### 7.3 音频缓冲

- DMA half：512 frames；DMA buffer：1024 frames
- Mixer background ring：8192 frames（立体声，CCM）
- Mixer voice ring：4096 frames（单声道，主 RAM）
- Capture ring：8192 frames（单声道有效 slot，主 RAM，约 186ms）
- 背景音启动阈值：6144 frames；AI 单独播放启动阈值：1024 frames
- 默认采样率：44.1 kHz

### 7.4 GUI / 总线

- 逻辑分辨率：320×240（`LCD_ROTATION_90`）
- LVGL draw buf：36 行 ≈ 23 KB CCM
- LCD / Flash SPI 工作时钟目标：42 MHz（不稳可降回 30/20）
- 圆盘动画：约 24 s/圈，36 步 × 10°，降低脏区刷新

---

## 8. 已完成能力清单

- [x] RT-Thread + CubeMX 时钟 / 外设 MSP
- [x] BT-STACK Host + ESP32 Controller HCI
- [x] A2DP Sink 连接、SBC 解码、ES8311 播放
- [x] AVRCP 播放/暂停/切歌/音量（相对 + 绝对）
- [x] Now Playing 元数据缓存与 GUI 展示
- [x] 按键 + 旋转编码器本地控制
- [x] ST7789 + LVGL Welcome / 播放主界面
- [x] W25Q128 SFUD 扫卡、写保护清除
- [x] FAL 分区；ZBFT 中文字库运行时渲染
- [x] YMODEM `font_update` / `font_info`
- [x] 开机提示音与 Welcome 同步退出
- [x] 进度条对“暂停再播瞬态 position=0”的防闪处理

---

## 9. 已知限制与坑

1. **SBC fragmentation 未实现** — 部分源端可能卡顿/丢音  
2. **播放与采集互斥** — 当前产品路径以播放为主  
3. **SPI 无 DMA 刷屏** — 卡顿主要受 SPI 带宽与脏区影响；draw buf 在 CCM 无法直接 DMA  
4. **LCD 与 Flash 共 SPI1** — 字库读与刷屏争用；探卡必须拉高 LCD CS  
5. **W25Q 写保护** — 出厂/旧 OTA 可能 BP 全置位，擦写静默失败；`sfud_app_init` 已自动清  
6. **个别手机元数据编码** — 按 UTF-8 显示；非 UTF-8 可能乱码/空白  
7. **42 MHz SPI** — 布线差时可能花屏/字库偶发读坏，需降频  
8. **littlefs / OTA** — 分区在，业务未接  
9. **`uart_send_pcm` / 采集切模式** — 代码仍在，主路径已改为 AVRCP 音箱控制  

字库度量注意：`base_line=0`、`ofs_y=0`，否则 16×16 全格点阵会被裁成“只剩上半”。

---

## 10. 常用调试命令

| 命令 | 作用 |
|---|---|
| `sfud_app_info` / `sfud_app_jedec` / `sfud_app_hwcheck` | Flash 识别与硬件自检 |
| `font_update` | YMODEM 接收 `font.bin` 到 `font` 分区 |
| `font_info` | 字库头 + CRC 校验 |
| `lcd_app_test` | ST7789 色条测试（会冲掉 LVGL 画面） |

烧字库也可用 `fontlib/FontFlasher.exe`（需独占串口）。

---

## 11. 关键文件索引

**应用层**

- `applications/main.c`
- `applications/bt_app.c` / `bt_a2dp_sink_app.*` / `bt_a2dp_audio.*` / `bt_avrcp_ct_app.*`
- `applications/es8311_audio.*` / `control_app.*`
- `applications/lcd_app.*` / `mylvgl_app.*`
- `applications/gui_manager.*` / `gui_welcome.*` / `gui_main.*`
- `applications/sfud_app.*` / `fal_cfg.h` / `font_app.*` / `font_update.c`

**组件与包**

- `mycomponents/BT-STACK/`（含 `bt_config.h`、port、ESP32 chipset）
- `mycomponents/es8311/` / `LCD/` / `keyboard/`
- `packages/LVGL-v8.3.11/` / `packages/littlefs-v2.5.0/`

**文档与工具**

- `fontlib/README.md` — 字库生成与烧录
- `docs/lvgl_chinese_font_design.md` — 中文字库设计说明
- `drivers/board.c` / `drv_*.c` / `cubemx/` — BSP 与 Cube 工程

---

## 12. 后续方向（音箱之后）

音箱主链路可视为 **MVP 已闭环**。建议优先级：

1. 为 AI UART 增加长度、结束标记和校验，替代裸流空闲超时
2. 补 SBC fragmentation，提升多机型稳定性  
3. 根据实机听感调校 Mixer Duck 比例和 attack/release
4. 若要更顺滑 GUI：主 RAM 双 draw buf + SPI DMA，或 LCD/Flash 分总线  
5. OTA 与 fw_a/fw_b 落地  
6. 需要时再开 HFP / BLE

---

## 13. 一句话总结

当前工程已形成 **蓝牙音箱 + PTT AI 对话 + 双音源混音** 主链：

`手机 → ESP32 Controller → BT-STACK A2DP/AVRCP → SBC 解码 → ES8311 播放`  
+ `按键/编码器本地控播`  
+ `LVGL 播放页 + W25Q 中文字库`
+ `PTT PCM ↔ 香橙派 AI，回复语音与 A2DP 背景音动态混音`
