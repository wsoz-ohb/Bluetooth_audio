# Bluetooth Audio

> 更新时间：2026-08-18  
> 主控：STM32F407VG + RT-Thread 4.1.1  
> 蓝牙控制器：ESP32-WROOM-32E  
> 音频编解码器：ES8311  
> 显示：ST7789 320×240  
> AI 协处理器：Orange Pi 5（RK3588，部署资料位于 `E:\香橙派模型部署`）

这是一个以 RT-Thread 为基础的 Bluetooth Classic 音箱固件。工程已经完成蓝牙播放、AVRCP 播控、LVGL 播放界面、板载 Flash 字库、PTT 语音链路和 SPP OTA 应用侧接入。README 只记录当前代码中已经存在的实现；没有接入的协议或工具会明确标注。

## 功能概览

- **A2DP Sink**：手机或电脑发送 SBC，BT-STACK 解码为 44.1 kHz PCM，经 Mixer、I2S2 DMA 和 ES8311 播放。
- **AVRCP Controller/Target**：按键和编码器控制播放、暂停、上一首、下一首和音量；读取歌名、歌手、时长、进度与播放状态。
- **LVGL 播放页**：Welcome 启动页、中文歌名/歌手、进度条、状态文案和旋转唱片动画。
- **PTT AI 音频链路**：长按 PC9 采集 ES8311 麦克风 PCM，经 USART3 发送给香橙派；松开后 USART3 接收回复裸 PCM，进入 Mixer 的语音通道播放。背景蓝牙音乐在回复期间自动 Duck。
- **W25Q128 Flash**：SFUD + FAL；`font` 分区运行时读取 ZBFT 中文字库，`filesystem` 分区挂载 littlefs 保存录音文件。
- **SPP OTA**：Classic RFCOMM SPP 接收固件，校验 CRC32 后写入备用槽；配合 Easy Bootloader 完成安装、试运行、确认和回滚。
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

`applications/main.c` 是应用入口，CubeMX 只提供时钟、MSP 和外设句柄。实际顺序为：

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

LVGL 线程由组件自动启动，创建显示端口、字库索引和 Welcome/Main 页面。

## OTA 说明

### 分区与地址

片外 W25Q128（16 MiB）分区如下：

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

当前应用启动时会从 BCB 的确认/试运行槽读取镜像头，因此版本和日期不再默认显示为 0。成功安装后，串口应返回类似 `version:1` 和 `2026-08-18`。

本仓库包含 `mycomponents/easy_bootloader_app/` 的应用侧端口；独立 Bootloader 工程仍以 `D:\Code_Warehouse\easy_bootloader_project` 为参考/配套工程。烧录前必须确认两边的链接地址、分区、镜像格式完全一致。

## PTT 与香橙派

### MCU 端

- 按键 PC9 长按进入采集，松开结束；采集期间暂停 A2DP 并关闭本地 media gate。
- USART3（PD8 TX / PB11 RX）使用 2,000,000 baud、8N1、无流控。
- 上行：44.1 kHz、单声道、signed int16 little-endian 裸 PCM，无 WAV 头。
- 下行：同格式回复 PCM；按 16-bit 边界拼接，空闲约 1 秒停止语音源。
- 回复 PCM 经 `AUDIO_MIXER_SOURCE_VOICE` 播放，当前默认额外衰减到约 0.125 倍，避免盖住背景音乐。
- littlefs 可选记录 `/pcm/last.pcm` 和 `/pcm/last.txt`，当前默认关闭录音落盘。

### Orange Pi 端

配套源码和部署包在 Windows 目录 `E:\香橙派模型部署`，板端对应目录为 `/home/orangepi`。推荐阅读顺序：

1. `E:\香橙派模型部署\当前开发进度.md`
2. `E:\香橙派模型部署\voice-assistant\README.md`
3. `E:\香橙派模型部署\voice-assistant\SERIAL_PCM.md`
4. `E:\香橙派模型部署\voice-assistant\STARTUP.md`
5. `E:\香橙派模型部署\rkllm-openai-api\README.md`

处理链路为：

```text
MCU PCM -> voice-assistant.service -> Sherpa-ONNX ASR
        -> Qwen3-4B RKLLM OpenAI API -> Piper TTS
        -> SoXR 重采样到 44.1 kHz -> reply_44100.pcm
```

Orange Pi 5 基线为 Ubuntu 22.04.5 LTS、aarch64、8 GiB、RKLLM Runtime 1.3.0，模型为 `Qwen3-4B-rk3588-w8a8.rkllm`。API 默认监听 `127.0.0.1:8888`，由用户级 `rkllm-api.service` 管理；语音服务由 `voice-assistant.service` 管理，并等待稳定设备路径 `/dev/serial/by-id/usb-wch.cn_WCH-Link_00D88F06860A-if02`。

当前串口协议仍是裸 PCM + 空闲超时，不是带 START/DATA/END 的可靠帧协议。香橙派服务已经完成接收、ASR、LLM、TTS 和 44.1 kHz 回复文件生成；下行 PCM 的发送策略及双向帧协议仍应作为后续产品化工作单独完善。

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

工程是 RT-Thread Studio / SCons 工程，工具链配置在 `rtconfig.py`：`arm-none-eabi-gcc`、`cortex-m4`、链接脚本 `linkscripts/STM32F407VG/link.lds`。

使用 RT-Thread Studio 打开工程即可构建。命令行环境已安装对应 ARM GCC 时，也可以在工程根目录执行：

```powershell
scons -j4
```

输出目标为 `rt-thread.elf`（Studio 通常同时生成 bin/hex）。本项目不在 README 中固定某台电脑的工具链路径，构建前请确保 `arm-none-eabi-gcc` 已在 `PATH`，或设置 `RTT_EXEC_PATH`。

烧录顺序：先烧录与 `easy_bootloader_project` 匹配的独立 Bootloader，再烧录应用；首次 OTA 前确认应用向量表、`SCB->VTOR`、链接地址均为 `0x08010000`。升级验证至少检查启动日志、SPP ACK、版本查询和日期查询。

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
3. Orange Pi 端当前按轮次生成回复 PCM；要实现可靠双向产品链路，还需要增加下行传输和统一帧协议。
4. 播放与采集是互斥会话，当前没有全双工回声消除。
5. LCD 与 W25Q128 共用 SPI1，刷屏无 DMA；42 MHz 不稳定时应降到 30/20 MHz。
6. HFP/HSP、BLE 业务和触摸屏播控未接入；littlefs 目前主要用于 PTT 录音验证。
7. AVRCP 元数据按 UTF-8 显示，源端编码异常时可能出现乱码。

## 一句话总结

`手机 A2DP/AVRCP -> ESP32 HCI -> STM32 BT-STACK -> SBC/AVRCP -> Mixer/ES8311`，再结合 `PTT PCM <-> Orange Pi 本地 ASR/LLM/TTS`、LVGL 中文播放页和 SPP A/B OTA，构成当前 Bluetooth Audio 工程的完整主架构。
