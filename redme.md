# Bluetooth Audio 当前架构说明

## 1. 目标与范围

当前工程的目标是：

- STM32F407 作为主控运行 RT-Thread 应用层。
- 外挂 ESP32-WROOM-32E 作为蓝牙控制器。
- Host 协议栈统一使用 `BT-STACK`，不再使用旧的 `Bluetooth_HostStack`。
- 当前主功能聚焦为 `A2DP Sink`：被手机/PC 发现、建立连接、接收 SBC 音频、解码成 PCM，并通过 I2S 送到 WM8978 播放。

当前这份文档只描述已经接通并可工作的主链路，不讨论后续还没接入的 AVRCP、HFP、BLE、录音等功能。

## 2. 总体分层

```text
手机 / PC
    |
    |  Bluetooth Classic
    |  A2DP / AVDTP / SDP
    v
+-------------------------------+
| STM32F407 + RT-Thread         |
|                               |
|  applications/                |
|    main.c                     |
|    bt_app.c                   |
|    bt_a2dp_sink_app.c         |
|    bt_a2dp_audio.c            |
|    bt_i2s_player.c            |
|                               |
|  mycomponents/BT-STACK/port/  |
|    btstack_port.c             |
|    btstack_run_loop_embedded.c|
|    btstack_uart_block_embedded|
+-------------------------------+
    |
    | HCI H4 UART
    v
+-------------------------------+
| ESP32-WROOM-32E               |
| 蓝牙控制器 Controller         |
+-------------------------------+
    |
    | PCM -> I2S
    v
+-------------------------------+
| WM8978 Codec                  |
| DAC / 喇叭 / 耳机输出         |
+-------------------------------+
```

## 3. 当前模块职责

### 3.1 应用入口层

- `applications/main.c`
  - 系统启动后直接调用 `bt__init()`。
  - 当前已经去掉手工 shell 启动蓝牙的流程，上电就进入蓝牙音频链路初始化。

- `applications/bt_app.c`
  - 蓝牙应用总编排入口。
  - 负责按顺序完成：
    - `btstack_port_init()`
    - `bt_i2s_player_init()`
    - `bt_a2dp_sink_service_init()`
    - `btstack_port_start_thread()`
  - 这里不直接处理媒体包，只做系统级初始化和模块串联。

### 3.2 BT-STACK 端口层

- `mycomponents/BT-STACK/port/btstack_port.c`
  - 把 BT-STACK 适配到 RT-Thread。
  - 负责：
    - 初始化 run loop
    - 初始化 TLV
    - 绑定 UART 传输层
    - 绑定 ESP32 chipset driver
    - 创建 `btstack` 专用线程
    - 在 run loop 线程上下文里执行 `bt_host_start()` 完成控制器上电

- `mycomponents/BT-STACK/port/btstack_run_loop_embedded.c`
  - BT-STACK 在 RT-Thread 下的事件循环实现。

- `mycomponents/BT-STACK/port/btstack_uart_block_embedded.c`
  - BT-STACK 到底层 UART/H4 的阻塞式传输适配。

- `mycomponents/BT-STACK/port/btstack_chipset_esp32.c`
  - 外挂 ESP32 控制器的 chipset 适配层。

### 3.3 A2DP 协议服务层

- `applications/bt_a2dp_sink_app.c`
  - 注册 A2DP Sink 服务。
  - 创建本地 SBC Sink SEP。
  - 注册 SDP 记录，确保手机/PC 可以把本机识别为音频接收端。
  - 处理 A2DP META 事件：
    - 信令连接建立
    - SBC 参数协商
    - Stream 建立
    - Stream 开始 / 暂停 / 停止 / 释放
  - 收到媒体包后，把包转交给 `bt_a2dp_audio_process_media_packet()`。

### 3.4 音频解码层

- `applications/bt_a2dp_audio.c`
  - 负责 RTP/SBC 载荷解析。
  - 负责调用 BT-STACK 自带 SBC decoder，把 SBC 解码成 PCM。
  - 解码出 PCM 后，通过回调把 PCM 继续交给 `bt_i2s_player`。

当前这里的状态是：

- 已支持最常见的非分片 SBC 包。
- 还没有实现 SBC fragmentation 重组。
- 如果远端发送分片包，当前实现会直接丢弃该包。

这是一条当前必须保留在脑子里的限制，因为它会直接影响播放连续性。

### 3.5 PCM 播放层

- `applications/bt_i2s_player.c`
  - 是当前音频输出主链路的核心。
  - 负责：
    - 接收解码出来的 PCM
    - 写入软件 ring buffer
    - 在合适阈值达到后启动 I2S DMA
    - 在 DMA half/full callback 里持续从 ring buffer 补数据
    - 根据协商采样率重配 I2S 和 WM8978

当前已经去掉 `wm_tone` 等手工测试入口，保留的就是正式播放链路。

### 3.6 Codec 驱动层

- `mycomponents/wm9878/wm9878_driver.c`
- `mycomponents/wm9878/wm9878_driver.h`

职责：

- 控制 WM8978 寄存器
- 初始化数字音频接口
- 初始化播放路径
- 控制喇叭/耳机输出音量
- 启停 DAC 播放路径

## 4. 启动流程

当前启动流程如下：

```text
main.c
  -> bt__init()
      -> btstack_port_init()
          -> 初始化 BT-STACK run loop / UART / chipset / host 参数
      -> bt_i2s_player_init()
          -> wm8978_init()
          -> bt_i2s_player_i2s_reconfigure(44100)
          -> wm8978_start_playback()
          -> 注册 PCM callback
      -> bt_a2dp_sink_service_init()
          -> bt_a2dp_audio_init()
          -> a2dp_sink_init()
          -> 创建 SBC Sink SEP
          -> 注册 SDP record
      -> btstack_port_start_thread()
          -> 创建 btstack 线程
          -> btstack_run_loop_execute()
          -> bt_host_start()
              -> 控制器上电
              -> HCI_STATE_WORKING
```

当 `BTSTACK_EVENT_STATE == HCI_STATE_WORKING` 时，说明：

- Host 栈已经起来了
- 控制器已经上电
- 本机可以被远端发现并连接

## 5. 连接与播放数据流

### 5.1 连接建立流

```text
手机/PC 搜索设备
  -> SDP 查询
  -> 识别本机为 A2DP Sink
  -> 建立 AVDTP Signaling
  -> 协商 SBC 参数
  -> 建立 Media Stream
  -> Stream Started
```

对应代码主入口：

- `bt_a2dp_sink_service_init()`
- `bt_app_handle_a2dp_meta_event()`

### 5.2 音频数据流

```text
A2DP media packet
  -> bt_app_a2dp_sink_media_handler()
  -> bt_a2dp_audio_process_media_packet()
      -> RTP 头解析
      -> SBC 载荷解析
      -> SBC decoder
      -> PCM callback
  -> bt_i2s_player_pcm_callback()
      -> PCM 写入 ring buffer
      -> 达到阈值后启动 DMA
  -> HAL_I2S_Transmit_DMA()
  -> DMA half/full callback 补数
  -> SPI2 / I2S
  -> WM8978 DAC
  -> 喇叭输出
```

## 6. 当前关键配置

### 6.1 A2DP / SBC 侧

当前本机对外声明的 SBC 能力偏向最常见组合：

- 采样率：44.1 kHz / 48 kHz
- 通道：Stereo / Joint Stereo
- Block length：4 / 8 / 12 / 16
- Subbands：4 / 8
- Allocation：Loudness / SNR
- Bitpool：2 ~ 53

当前偏好配置是：

- 44.1 kHz
- Joint Stereo
- 16 blocks
- 8 subbands
- Loudness
- bitpool 2~53

### 6.2 I2S 播放侧

当前 `bt_i2s_player.c` 关键参数如下：

- `BT_I2S_PLAYER_DMA_HALF_FRAMES = 512`
- `BT_I2S_PLAYER_RING_BUFFER_FRAMES = 8192`
- `BT_I2S_PLAYER_START_THRESHOLD_FRAMES = 6 * DMA buffer`
- 数据格式：`I2S_DATAFORMAT_16B`
- 模式：`I2S_MODE_MASTER_TX`
- 标准：`I2S_STANDARD_PHILIPS`
- `MCLKOutput = ENABLE`

这些参数的目标是优先保证播放连续性，而不是最低延迟。

### 6.3 WM8978 播放侧

当前关键播放参数如下：

- 默认采样率：44.1 kHz
- 默认喇叭音量：`0x28`
- `WM8978_SPK_USE_BTL = 0`
- `WM8978_ENABLE_SPKBOOST = 0`

这套配置是最近为降低底噪、啸叫和模拟放大量做过收敛后的结果。

## 7. 当前已经完成的能力

当前工程已经完成并打通的能力：

- BT-STACK Host 在 RT-Thread 下正常运行
- 外挂 ESP32 控制器能够正常上电和配合 Host 工作
- 本机可以作为 A2DP Sink 被手机/PC 发现
- 可以建立 A2DP 连接并协商 SBC 配置
- 可以接收 SBC 媒体包
- 可以把 SBC 解码成 PCM
- 可以把 PCM 通过 I2S + DMA 送到 WM8978
- 可以通过 WM8978 走喇叭输出播放音频

## 8. 当前已知限制

当前架构不是“全功能蓝牙音频系统”，而是“能工作的第一条主链路”。已知限制如下：

- 只实现了 A2DP Sink 主链路
- 当前没有接 AVRCP 控制层
- 当前没有接 HFP/HSP
- 当前没有接录音链路
- 当前没有接 BLE 业务逻辑
- `bt_a2dp_audio.c` 尚未实现 SBC fragmentation 重组
- 当前播放输出在 `bt_i2s_player_init()` 中固定先走喇叭路径
- 还没有做“静音自动 mute / 空闲自动关闭 codec 模拟级”
- 当前很多音质优化参数仍然是经验值，后续还需要继续听感调参

其中最值得优先继续完善的一点是：

- `A2DP SBC fragmentation` 处理

因为这会直接影响播放连续性和“卡一卡”的听感。

## 9. 后续推荐扩展方向

推荐按这个顺序往下做：

1. 补齐 `A2DP SBC fragmentation` 重组
2. 继续稳定 `PCM -> ring buffer -> DMA` 的播放连续性
3. 给 `WM8978` 增加更完整的 mute / standby 控制
4. 加 AVRCP，至少先接播放/暂停/音量同步
5. 把播放路由抽成可配置项，支持 speaker / headphone / both
6. 如果后续要做 BLE，再把 BLE 初始化与业务配置真正接回当前架构

## 10. 关键文件索引

应用层：

- `applications/main.c`
- `applications/bt_app.c`
- `applications/bt_a2dp_sink_app.c`
- `applications/bt_a2dp_audio.c`
- `applications/bt_i2s_player.c`

BT-STACK 端口层：

- `mycomponents/BT-STACK/port/btstack_port.c`
- `mycomponents/BT-STACK/port/btstack_run_loop_embedded.c`
- `mycomponents/BT-STACK/port/btstack_uart_block_embedded.c`
- `mycomponents/BT-STACK/port/btstack_chipset_esp32.c`

Codec 驱动层：

- `mycomponents/wm9878/wm9878_driver.c`
- `mycomponents/wm9878/wm9878_driver.h`

## 11. 一句话总结

当前工程已经形成了一条完整的主链路：

`BT-STACK Host -> A2DP Sink -> SBC 解码 -> PCM ring buffer -> I2S DMA -> WM8978 -> 喇叭播放`

后续的工作，不再是“从 0 到 1 接通”，而是围绕这条主链路继续做稳定性、音质和功能扩展。
