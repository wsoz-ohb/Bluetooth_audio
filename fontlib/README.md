# fontlib - 汉字点阵字库生成

本目录存放字库生成工具及产物,配合 `docs/lvgl_chinese_font_design.md` 的方案使用。

## 文件

| 文件 | 说明 |
|---|---|
| `font_gen.py` | 生成脚本(依赖 Pillow,`pip install pillow`) |
| `font.bin` | 生成的字库,烧写到 W25Q128 的 `font` 分区(偏移 0) |
| `font_send.py` | YMODEM 烧录上位机(tkinter UI,内置协议实现) |
| `FontFlasher.exe` | font_send.py 打包的单文件 exe,双击即用 |

## 当前 font.bin 参数

- 字体:黑体 SimHei,16×16 px,1bpp
- 字符集:ASCII 95 + GB2312 全部(汉字 6763 + 符号区),共 **7540 字**
- 体积:**256376 字节(250.4 KB)**
- CRC32:**0x52840A35**(烧写后 `font_info` 命令应回显一致)

## 烧录字库(用 FontFlasher.exe)

1. 关掉占用 COM 口的终端软件(XShell/MobaXterm 等),串口必须独占;
2. 双击 `FontFlasher.exe` -> 选串口 -> 点"烧录字库"(文件默认已填 font.bin);
3. 等进度条走完(~27 秒),日志窗看到 `font OK: 7540 glyphs 16x16, crc 0x52840A35 pass` 即成功;
4. 任何时候可用 msh 命令 `font_info` 复验分区内字库完整性。

## 常用命令

```bash
# 重新生成(默认黑体 16px)
python font_gen.py

# 预览某几个字的点阵效果(不生成文件)
python font_gen.py --preview 晴天ABC

# 换字体/字号(如微软雅黑轻量版、思源黑体)
python font_gen.py --font C:/Windows/Fonts/msyhl.ttc --size 16 --out font_msyh.bin

# 重新打包 exe(改了 font_send.py 后才需要)
env -u TCL_LIBRARY -u TK_LIBRARY python -m PyInstaller --onefile --windowed \
  --name FontFlasher --add-data "<python目录>/tcl/tk8.6;_tcl_data/tk8.6" font_send.py
```

## 格式(ZBFT v1,小端)

```
Header 16B : "ZBFT" | ver u8 | width u8 | height u8 | bpp u8 | glyph_cnt u32 | crc32 u32
索引表     : glyph_cnt × u16 Unicode 码点,升序(供 MCU 二分查找)
点阵区     : glyph_cnt × 32B,每行 2B,MSB 在左;与索引表一一对应
```

crc32 覆盖索引表+点阵区(不含 Header),MCU 端 `font_app_init` 据此校验完整性。

## 注意

- SimHei 是 Windows 自带字体,仅限个人学习使用;若产品化发布,
  请换成开源可商用字体重新生成(思源黑体 SourceHanSansSC / 文泉驿微米黑)。
- 换字体只是改 `--font` 参数重跑,bin 格式与 MCU 端代码完全不变。

## 踩坑记录:字库烧录后 bad magic(2026-07-26)

**现象**:YMODEM 烧字库,传输字节计数 256376 精确无误(零重传、每包 CRC16 全 ACK),
但落盘后 `font_info` 报 `verify FAIL: bad magic`,分区开头读回的是上一任 OTA demo
残留的旧数据 `OTA0 A5 A5 A5 A5...`,从未被改写。

**排查过程**(详见 `font_update.c` 文件头注释):
1. 换 MobaXterm 和自写 Python sender 两个独立实现,结果一致 -> 排除上位机;
2. received 字节数精确命中 -> 排除接收代码;
3. font_update 持有 SPI 总线锁、屏幕冻结 -> 排除 SPI1 总线竞争;
4. `fal read 0 16` 连读四遍完全一致 -> 读稳定,排除 20MHz 信号完整性;
5. `fal erase` / `fal write` 报 success 但内容纹丝不动 -> 写入被芯片静默丢弃。

**真凶**:W25Q128 状态寄存器 = `0x3C`,BP 位(TB/BP3..BP0)全置位 = 整片 16MB 写保护。
W25Q 系列对受保护区域的 PROGRAM/ERASE 命令**静默丢弃、不报错**,读完全正常——
所以 SFUD/FAL 全程回 success,但一个比特都没写进去。

**解决**:
- 临时手动:`sf probe w25q` -> `sf status`(看到 0x3C) -> `sf status 0 00`(清零);
- 已固化:`sfud_app_init()` 开机自动检测 BP 位非零则清零(见 `sfud_app.c`),换板/换芯片也不会再踩。

**教训**:SPI Flash "读正常 + 擦写报 success + 内容不变" 的三连,第一反应应是查状态
寄存器写保护位,而不是先怀疑上位机协议或信号完整性。
