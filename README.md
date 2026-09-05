# Bluetooth Audio

> 主控：STM32F407VG + RT-Thread 4.1.1  
> 蓝牙控制器：ESP32-WROOM-32E  
> 音频编解码器：ES8311  
> 显示：ST7789 320×240  
> 可选 AI 协处理器：Orange Pi 5

这是一个基于 RT-Thread 的 Bluetooth Classic 音箱 MCU 固件工程。STM32 运行 BTstack Host，ESP32 仅作为蓝牙控制器，通过 HCI UART 与 STM32 通信；音频由 STM32 解码后交给 ES8311 播放，而不是由 ESP32 直接播放。

本仓库实现了蓝牙播放、AVRCP 播控、LVGL 播放界面、片外 Flash 字库，以及 PTT（按住说话）串口音频和 SPP OTA 的 MCU 侧接入。**下载本仓库不等于获得完整 AI 音箱部署包或完整 OTA 系统。**

## 仓库范围与外部依赖

| 内容 | 本仓库提供 | 需要另行准备 |
| --- | --- | --- |
| MCU 应用 | RT-Thread、板级驱动、音频/蓝牙/GUI 应用及 Studio 工程配置 | 对应硬件、ARM GCC 工具链与烧录器 |
| 蓝牙控制器 | STM32 侧 HCI H4 传输与 ESP32 适配代码 | ESP32 上支持 Classic Bluetooth、HCI H4 和 RTS/CTS 的匹配控制器固件；不能用普通 AT 固件替代 |
| AI 语音 | 麦克风 PCM 上行、回复 PCM 接收和播放 | Orange Pi 端服务、模型、运行环境及串口收发实现 |
| OTA | SPP 接收协议、镜像校验、FAL/BCB 适配和试运行确认 | 兼容的独立 Bootloader，以及按协议发送固件的上位机 |
| 中文字库 | 字库生成与 YMODEM 烧录工具 | 合法授权的字体文件、生成后单独烧录的字库 |

仓库内文件均使用相对链接；外部配套工程未在这里提供下载地址，不依赖开发者个人电脑的目录。

## 功能概览

- **A2DP Sink**：手机或电脑发送 SBC，BT-STACK 解码为 44.1 kHz PCM，经 Mixer、I2S2 DMA 和 ES8311 播放。
- **AVRCP Controller/Target**：按键和编码器控制播放、暂停、下一首和音量；读取歌名、歌手、时长、进度与播放状态。上一首接口已存在，但当前没有绑定实体按键。
- **LVGL 播放页**：Welcome 启动页、中文歌名/歌手、进度条、状态文案和旋转唱片动画。
- **PTT AI 音频链路**：长按 PC9 采集 ES8311 麦克风 PCM，经 USART3 发送给香橙派；松开后 USART3 接收回复裸 PCM，进入 Mixer 的语音通道播放。背景蓝牙音乐在回复期间自动 Duck。
- **W25Q128 Flash**：SFUD + FAL；`font` 分区运行时读取 ZBFT 中文字库，`filesystem` 分区挂载 littlefs 保存录音文件。
- **SPP OTA 应用侧**：通过 Classic RFCOMM SPP 将固件写入备用槽，读回校验 CRC32 并记录升级状态；安装、试运行和回滚依赖配套 Bootloader。
- **启动健康确认**：应用初始化成功后延时确认试运行镜像；异常复位不会立即丢失回滚机会。

## 系统架构

```text
手机 / PC
  ├─ A2DP (SBC) ─┐
  └─ AVRCP       │
                 v
        ESP32 Controller
        HCI H4 / uart2 / 921600 / RTS-CTS
                 |
                 v
STM32F407 + RT-Thread + BT-STACK Host
  ├─ SBC 解码 ──> audio_mixer ──> I2S2 DMA ──> ES8311 ──> 喇叭
  ├─ AVRCP 元数据 ──> LVGL / ST7789
  ├─ PTT 采集 ──> uart3 / 2Mbps ──> Orange Pi 5
  ├─ uart3 回复 PCM ──> audio_mixer VOICE ──> ES8311
  └─ SPP OTA ──> fw_a/fw_b + BCB ──> 独立 Bootloader 安装
```

### 启动顺序

[applications/main.c](applications/main.c) 是应用入口，CubeMX 提供时钟、MSP 和外设句柄。应用初始化顺序为：

```text
sfud_app_init
  -> fs_app_init（littlefs 挂载，失败不阻断音箱主链）
  -> es8311_audio_init / audio_mixer_init
  -> boot_prompt_play_once
  -> bt__init（A2DP Sink、AVRCP、SPP）
  -> boot_ota_init
  -> control_app_init
  -> 延时 3 秒确认试运行镜像
  -> 主循环每 10 ms 调用 boot_ota_poll
```

LVGL 线程由组件自动启动，创建显示端口、字库索引和 Welcome/Main 页面。试运行确认以核心初始化函数返回成功和延时为条件，不代表已完成蓝牙连接、实际出声或 AI 往返测试。

## OTA 说明

### 分区与地址

片外 W25Q128（16 MiB）分区如下，以 [fal_cfg.h](applications/fal_cfg.h) 为准：

| 分区 | 偏移 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| `font` | `0x000000` | 2 MiB | ZBFT 中文字库 |
| `fw_a` | `0x200000` | 2 MiB | OTA 下载槽 A |
| `fw_b` | `0x400000` | 1 MiB | OTA 下载槽 B / 回退副本 |
| `filesystem` | `0x500000` | 11 MiB | littlefs |

BCB 位于 STM32 片内 Flash `0x0800C000`，大小 16 KiB。应用链接地址是 `0x08010000`，应用最大镜像大小为 960 KiB；独立 Bootloader 必须使用同一地址、镜像头和 BCB 定义。

### SPP OTA 流程

1. 手机或 PC 通过 Classic SPP 连接服务 `WSOZ SPP`（RFCOMM channel 1）。
2. 发送 `55 AA FF EE 55 55` 开始会话。
3. 按 Easy Bootloader APP 协议发送带长度、校验和和尾标记的数据帧；固件写入当前确认槽之外的备用槽。
4. 发送结束帧（包含大端 `version` 和 `build_date`）；应用重新校验 payload CRC32、镜像头和目标地址，追加 BCB `UPDATE_READY`。
5. 设备复位后由 Bootloader 安装并进入 `TRIAL`；应用核心服务稳定 3 秒后调用确认，失败则按最大尝试次数回滚。

版本查询命令：

```text
查询版本：55 AA FF DD 55 55
查询日期：55 AA FF CC 55 55
```

以上命令按十六进制字节发送，不是发送字符串。开始、数据及结束阶段的 ACK 为 `55 AA FF FE 55 55`；发送端应等待 ACK，避免连续灌入数据导致 SPP 接收缓冲溢出。完整帧格式以 [easy_bootloader_app.c](mycomponents/easy_bootloader_app/easy_bootloader_app.c) 为准，不能直接把 bin 文件作为无协议字节流发送。

版本和日期通过当前 SPP 连接返回，其值来自 BCB 指向的确认/试运行槽镜像头；缺少有效 BCB 或镜像头时，不保证能返回有效版本信息。

本仓库只包含 [easy_bootloader_app](mycomponents/easy_bootloader_app/) 应用侧代码，不包含独立 Bootloader 工程。集成前必须对齐 [boot_config_app.h](mycomponents/easy_bootloader_app/boot_config_app.h)、[boot_image.h](mycomponents/easy_bootloader_app/boot_image.h) 和 [boot_control.h](mycomponents/easy_bootloader_app/boot_control.h) 中的地址、大小、镜像格式及启动控制块（BCB）定义。这里的 A/B 是片外下载/备份槽，应用仍在同一个片内地址运行，不是两个片内执行分区。

## PTT 与香橙派

### MCU 端

- 按键 PC9 长按进入采集，松开结束；采集期间关闭本地 A2DP media gate。仅在进入 PTT 时成功暂停过对端播放的情况下，松开后才请求自动恢复播放。
- USART3（PD8 TX / PB11 RX）使用 2,000,000 baud、8N1、无流控。
- 上行：44.1 kHz、单声道、signed int16 little-endian 裸 PCM，无 WAV 头。
- 下行：同格式回复 PCM；按 16-bit 边界拼接，空闲约 1 秒停止语音源。
- 回复 PCM 经 `AUDIO_MIXER_SOURCE_VOICE` 播放，当前默认额外衰减到约 0.125 倍，避免盖住背景音乐。
- littlefs 可选记录 `/pcm/last.pcm` 和 `/pcm/last.txt`，当前默认关闭录音落盘。这两个路径是 MCU 文件系统路径，不是开发者电脑路径；相关开关为 [uart_send_pcm.c](applications/uart_send_pcm.c) 中的 `UART_SEND_PCM_FILE_RECORD_ENABLE`。

### Orange Pi 端

本仓库不包含 Orange Pi 端源码、模型或部署脚本，因此这里只描述对接职责，不把外部服务的部署状态当成本仓库已验证的功能。配套方案的处理链路为：

```text
MCU PCM -> 串口接收 -> Sherpa-ONNX ASR
        -> Qwen3-4B RKLLM OpenAI API -> Piper TTS
        -> SoXR 重采样到 44.1 kHz -> 串口发送回复 PCM -> MCU 播放
```

ASR、LLM 和 TTS 的选型不属于 MCU 串口协议；可以替换为其他实现，只要满足上述 PCM 格式和时序要求。串口设备名、服务安装目录、模型路径和 API 地址由外部部署自行配置，不应照搬某一台机器的设备编号。

外部服务需要接收上行音频、判断本轮采集结束、完成推理，并在 MCU 松开按键进入回复接收状态后发送裸 PCM。只生成回复文件并不会让设备自动出声。当前协议仍是裸 PCM + 空闲超时，没有 START/DATA/END、序号或 CRC；完整 AI 往返效果需要与实际外部服务联调验证。

## 硬件资源

| 资源 | 用途 |
| --- | --- |
| `uart1` | 控制台、msh、YMODEM 字库升级 |
| `uart2` | ESP32 HCI H4，921600，硬件流控 |
| `uart3` | PTT 上行与 AI 回复下行，2 Mbps，无流控 |
| `I2C1`（软件） | ES8311，SCL=PC11，SDA=PC12 |
| `I2S2 + DMA` | ES8311 播放/采集 |
| `SPI1` | ST7789 CS=PC4；W25Q128 CS=PA4；SCK/MISO/MOSI=PA5/PA6/PA7 |
| `PC9` | PTT / 播控按键 |
| `PB6/PB7` | 旋转编码器 |
| CCM 64 KiB | Mixer 背景 ring、UART 线程栈、LVGL draw buffer；不可被 DMA 访问 |

## 构建与烧录

### 推荐：RT-Thread Studio

1. 下载仓库后，在 RT-Thread Studio 中导入已有工程，选择包含 `.project` 和 `.cproject` 的仓库根目录。
2. 在工程属性中选择本机安装的 ARM GCC 工具链，核对目标为 STM32F407VG / Cortex-M4，检查工具链和调试器配置是否残留不可用路径。
3. 保留工程现有源码排除规则、头文件搜索路径及宏定义，执行构建。链接脚本为 [link.lds](linkscripts/STM32F407VG/link.lds)。
4. 在构建输出目录查看 ELF 以及启用转换后生成的 bin/hex，确认没有编译或链接错误，再进行烧录。

### SCons 现状

仓库保留了 [SConstruct](SConstruct)、[SConscript](SConscript) 和 [rtconfig.py](rtconfig.py)，目标名为 `rt-thread.elf`。但当前 `rtconfig.py` 的编译/汇编参数为空，`applications/` 和 `mycomponents/` 也没有对应的 `SConscript`，因此**不能把 `scons -j4` 视为已具备的完整构建方式**。需要命令行构建时，应先补齐源码收集、启动文件和编译链接参数，并与 Studio 配置核对。`RTT_EXEC_PATH` 仅用于指定本机工具链可执行文件目录，不能替代这些配置。

### 首次烧录与使用

1. 为 ESP32 准备匹配的 HCI 控制器固件，按硬件资源表检查接线、波特率和硬件流控。
2. 先烧录兼容的独立 Bootloader，再烧录 MCU 应用。应用 bin 的写入起始地址为 `0x08010000`，不是 `0x08000000`；ELF/hex 使用其自带地址。缺少 Bootloader 时，不能假定应用上电即可启动。
3. 确认应用链接地址及启动后的 `SCB->VTOR` 均为 `0x08010000`，不要覆盖 BCB 区域。
4. 连接 `uart1` 控制台，检查 Flash、音频、蓝牙和控制线程启动日志；运行 `sfud_app_info`、`fs_app_info` 和 `font_info` 检查状态。
5. 按 [字库工具说明](fontlib/README.md) 生成并烧录 ZBFT 字库。仓库忽略 `*.bin`，首次下载后不要假定已有 `font.bin`；字体授权也需单独确认。
6. 手机或电脑搜索蓝牙设备 **`WSOZ`** 并连接，播放音乐，检查声音、歌曲信息与进度显示。名称由 [bt_config.h](mycomponents/BT-STACK/core/config/bt_config.h) 配置。
7. 基础播放正常后，再分别联调 PTT 外部服务和 SPP OTA。升级验证至少覆盖传输 ACK、重启、试运行确认、版本/日期查询和失败回滚。

当前实体操作以 [control_app.c](applications/control_app.c) 为准：

| 操作 | 行为 |
| --- | --- |
| 单击 PC9 | 播放 / 暂停 |
| 双击 PC9 | 下一首 |
| 长按 PC9，保持按住 | 进入 PTT 采集 |
| 长按后松开 PC9 | 停止采集，开启回复 PCM 接收 |
| 旋转 PB6/PB7 编码器 | 音量增减；采集期间忽略调音量 |

## 常用命令

| 命令 | 作用 |
| --- | --- |
| `sfud_app_info` / `sfud_app_jedec` / `sfud_app_hwcheck` | W25Q128 识别与硬件自检 |
| `fs_app_info` | littlefs 挂载状态和 `/pcm` 文件 |
| `font_update` | 通过 YMODEM 写入 `font` 分区 |
| `font_info` | 字库头信息与 CRC 校验 |
| `lcd_app_test` | ST7789 色条测试（会覆盖 LVGL 画面） |

字库也可使用 `fontlib/FontFlasher.exe` 烧录；烧录时必须独占 `uart1`。

## 目录索引

```text
applications/                         应用入口、蓝牙、音频、GUI、文件系统
mycomponents/BT-STACK/                BTstack Host、RT-Thread port、ESP32 chipset
mycomponents/easy_bootloader_app/     SPP OTA 应用侧协议与 FAL/BCB 适配
mycomponents/es8311/                  ES8311 驱动
mycomponents/LCD/                     ST7789 驱动
mycomponents/keyboard/                按键和编码器
packages/LVGL-v8.3.11/                LVGL GUI
packages/littlefs-v2.5.0/             littlefs
fontlib/                               字库生成与烧录工具
docs/lvgl_chinese_font_design.md      中文字体设计说明
linkscripts/STM32F407VG/link.lds      应用 Flash/RAM/CCM 布局
```

## 已知限制

1. A2DP SBC fragmentation 尚未重组，部分源端可能丢包或卡顿。
2. PTT 仍采用裸 PCM 和空闲超时，暂时没有序号、长度、CRC 和明确结束帧。
3. Orange Pi 服务和独立 Bootloader 不在本仓库内，完整 AI 对话与 OTA 回滚需要配套工程及实机验证。
4. 播放与采集是互斥会话，当前没有全双工回声消除。
5. LCD 与 W25Q128 共用 SPI1，刷屏无 DMA；42 MHz 不稳定时应降到 30/20 MHz。
6. HFP/HSP、BLE 业务和触摸屏播控未接入；littlefs 目前主要用于 PTT 录音验证。
7. AVRCP 元数据按 UTF-8 显示，源端编码异常时可能出现乱码。

## 进一步阅读

- [中文字体设计](docs/lvgl_chinese_font_design.md)
- [字库生成与烧录工具](fontlib/README.md)
- [录音文件提取工具](tools/README.md)：需先开启录音落盘，且独占控制台串口。
- [BTstack 组件说明](mycomponents/BT-STACK/README.md)
