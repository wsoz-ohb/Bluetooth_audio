# Bluetooth Audio 当前架构说明

## 1. 目标与范围

当前工程的目标是：

- STM32F407 作为主控运行 RT-Thread。
- 外挂 ESP32-WROOM-32E 作为蓝牙 Controller，通过 HCI H4 UART 接入 Host。
- Host 协议栈统一使用工程内置的 `BT-STACK`。
- 当前主功能聚焦为 `Bluetooth Classic A2DP Sink`：
  - 被手机 / PC 发现并连接
  - 接收 SBC 音频
  - 解码为 PCM
  - 通过 `I2S2 + DMA + ES8311` 播放
- 在本地按键触发后，切换到采集模式：
  - 从 `ES8311 + I2S2 Rx` 采集 PCM
  - 经 `uart3` 导出原始 PCM 数据

当前文档只描述已经接通并在代码里落地的架构，不讨论尚未接入的 AVRCP、HFP、BLE 业务、文件系统或更复杂的音频路由。

## 2. 当前真实总体分层

```text
手机 / PC
    |
    |  Bluetooth Classic
    |  A2DP / AVDTP / SDP
    v
+--------------------------------------+
| STM32F407 + RT-Thread                |
|                                      |
|  applications/                       |
|    main.c                            |
|    bt_app.c                          |
|    bt_a2dp_sink_app.c                |
|    bt_a2dp_audio.c                   |
|    es8311_audio.c                    |
|    key_app.c                         |
|    uart_send_pcm.c                   |
|                                      |
|  mycomponents/BT-STACK/              |
|    core/src/bt_host.c                |
|    port/btstack_port.c               |
|    port/btstack_run_loop_embedded.c  |
|    port/btstack_uart_block_embedded.c|
+--------------------------------------+
    |
    | HCI H4 UART (`uart2`)
    v
+--------------------------------------+
| ESP32-WROOM-32E                      |
| Bluetooth Controller                 |
+--------------------------------------+
    |
    | SBC -> PCM -> I2S2 / DMA
    v
+--------------------------------------+
| ES8311 Codec                         |
| Playback / Capture Analog Frontend   |
+--------------------------------------+
    |
    +--> 播放输出
    |
    +--> 采集 PCM -> uart3 导出
```

## 3. 启动链与构建入口

### 3.1 实际启动入口

当前工程真正的启动入口不是 `cubemx/Src/main.c`，而是 RT-Thread 用户态入口：

- `drivers/board.c`
  - `rt_hw_board_init()`
  - 在 `hw_board_init()` 之后继续执行 `rt_components_board_init()`
- `drivers/drv_common.c`
  - `hw_board_init()`
  - 初始化时钟、SysTick、Pin、USART 等 BSP
- `drivers/drv_clk.c`
  - `clk_init()` 内部调用 CubeMX 生成的 `SystemClock_Config()`
- `applications/main.c`
  - 当前业务入口 `main()`

也就是说：

- `cubemx/Src/main.c` 里的 `__WEAK int main(void)` 会被 `applications/main.c` 覆盖。
- CubeMX 在当前工程里的主要作用是：
  - 提供 `SystemClock_Config()`
  - 提供 `hi2s2` 等 HAL 句柄定义
  - 提供 `HAL_I2S_MspInit()` / `HAL_UART_MspInit()` 等底层 MSP 初始化

### 3.2 应用启动顺序

`applications/main.c` 当前启动顺序如下：

```text
main()
  -> es8311_audio_init()
  -> bt__init()
  -> key_app_init()
  -> while (1) { rt_thread_mdelay(10); }
```

这里的含义是：

- 先把统一音频会话层准备好
- 再启动蓝牙 Host 和 A2DP Sink
- 最后拉起按键线程，允许切换采集模式

## 4. 当前模块职责

### 4.1 应用编排层

- `applications/main.c`
  - 负责整个应用级启动顺序
  - 不承载具体协议和音频逻辑

- `applications/bt_app.c`
  - 蓝牙应用总编排入口
  - 负责按顺序完成：
    - `btstack_port_init(NULL)`
    - `bt_a2dp_sink_service_init()`
    - `btstack_port_start_thread()`
  - 当前只注册 A2DP Sink，不直接操作 PCM 缓冲

### 4.2 BT-STACK 平台接入层

- `mycomponents/BT-STACK/core/src/bt_host.c`
  - 对 BTstack 原生接口做一层工程内封装
  - 负责：
    - `hci_init()`
    - `l2cap_init()`
    - `sdp_init()`
    - `rfcomm_init()`
    - 应用本地设备名 / discoverable / connectable 配置

- `mycomponents/BT-STACK/port/btstack_port.c`
  - RT-Thread 端口主入口
  - 负责：
    - 初始化 run loop
    - 初始化 TLV
    - 绑定 UART H4 传输层
    - 绑定 ESP32 chipset driver
    - 创建 `btstack` 专用线程
    - 在 run loop 线程上下文里投递 `bt_host_start()` 完成控制器上电

- `mycomponents/BT-STACK/port/btstack_run_loop_embedded.c`
  - BTstack 在 RT-Thread 下的事件循环实现
  - 通过信号量唤醒 run loop

- `mycomponents/BT-STACK/port/btstack_uart_block_embedded.c`
  - BTstack 到 RT-Thread UART 设备的 H4 适配层
  - 当前默认使用：
    - 设备名：`uart2`
    - 波特率：`921600`
    - 硬件流控：开启
  - 内部有：
    - RX ring buffer
    - 一个专用 UART RX 线程
    - 从中断回调转发到 BTstack run loop 的上下文切换

- `mycomponents/BT-STACK/port/btstack_chipset_esp32.c`
  - ESP32 Controller 的 chipset 适配层
  - 负责把当前外挂控制器接入 BTstack 的 chipset 抽象

### 4.3 A2DP 服务层

- `applications/bt_a2dp_sink_app.c`
  - 注册 A2DP Sink 服务
  - 创建本地 SBC Sink SEP
  - 注册 SDP 记录
  - 处理 A2DP META 事件：
    - signaling connection established / released
    - SBC configuration
    - stream established / started / suspended / stopped / released
  - 在 `STREAM_STARTED` 时确保本地播放链路已经 arm
  - 收到媒体包后，把包转交给 `bt_a2dp_audio_process_media_packet()`

### 4.4 A2DP 解码层

- `applications/bt_a2dp_audio.c`
  - 负责 RTP 头解析
  - 负责 SBC 载荷解析
  - 负责调用 BTstack 自带 SBC decoder 解码为 PCM
  - 解码后的 PCM 直接写入 `es8311_audio` 提供的统一播放接口

当前这层的几个重要约束：

- 只处理最常见的非分片 SBC 包
- 尚未实现 SBC fragmentation 重组
- 如果远端发送分片包，当前直接丢弃
- 当播放缓冲达到回压阈值时，会主动丢弃媒体包避免进一步堆积

### 4.5 统一音频会话层

- `applications/es8311_audio.c`
  - 这是当前音频主链路的核心
  - 统一管理：
    - 播放模式
    - 采集模式
    - `I2S2 + DMA`
    - 播放 / 采集 ring buffer
    - ES8311 状态切换
    - DMA half/full 回调

它不是单纯 codec 驱动包装，而是一个“音频会话层”：

- 上层播放只管写 PCM：
  - `es8311_audio_write_playback_checked()`
- 上层采集只管读 PCM：
  - `es8311_audio_read_capture()`

当前 run mode 定义为：

- `IDLE`
- `PLAYBACK`
- `CAPTURE`

播放与采集当前是互斥关系：

- 进入 `CAPTURE` 前会停掉播放
- 回到 `PLAYBACK` 前会停掉采集

### 4.6 Codec 控制层

- `mycomponents/es8311/es8311_driver.c`
- `mycomponents/es8311/es8311_driver.h`

职责：

- 通过 `i2c1` 访问 ES8311 寄存器
- 初始化 codec
- 配置采样率 / word length / I2S 格式
- 启停播放路径
- 启停录音路径
- 配置模拟 MIC PGA 增益

当前这层只放开已经核实过的采样率：

- `44.1 kHz`
- `48 kHz`

### 4.7 按键与采集导出层

- `applications/key_app.c`
  - 使用 `mycomponents/keyboard`
  - 轮询 `PC9`
  - 当前业务动作是：
    - `PC9` 双击
    - 在播放模式和采集模式之间切换

- `applications/uart_send_pcm.c`
  - 在采集模式下创建独立线程
  - 从 `es8311_audio_read_capture()` 读取 PCM
  - 原样写到 `uart3`
  - 当前导出配置：
    - 设备名：`uart3`
    - 波特率：`2000000`

## 5. 当前硬件资源分工

当前板级资源在代码里的分工如下：

- `uart1`
  - RT-Thread 控制台

- `uart2`
  - 蓝牙 HCI H4 链路
  - 连接外挂 ESP32 Controller

- `uart3`
  - 采集 PCM 导出

- `i2c1`
  - ES8311 控制面
  - 软件 I2C，SCL=`PC11`，SDA=`PC12`

- `I2S2`
  - ES8311 数字音频接口
  - 既承担播放，也承担采集

- `PC9`
  - 本地按键输入
  - 当前用于双击切换采集模式

## 6. 启动流程

当前启动流程如下：

```text
RT-Thread startup
  -> rt_hw_board_init()
      -> hw_board_init()
          -> clk_init()
              -> SystemClock_Config()
          -> rt_hw_pin_init()
          -> rt_hw_usart_init()
      -> rt_components_board_init()
          -> rt_hw_i2c_init()
  -> applications/main.c : main()
      -> es8311_audio_init()
          -> es8311_init()
          -> es8311_audio_i2s_reconfigure(44100)
      -> bt__init()
          -> btstack_port_init()
              -> bt_host_stack_init()
              -> bt_host_protocol_init()
              -> bt_host_apply_device_config()
          -> bt_a2dp_sink_service_init()
              -> bt_a2dp_audio_init()
              -> a2dp_sink_init()
              -> 创建 SBC Sink SEP
              -> 注册 SDP record
          -> btstack_port_start_thread()
              -> 创建 btstack 线程
              -> btstack_run_loop_execute()
              -> bt_host_start()
                  -> HCI_POWER_ON
      -> key_app_init()
          -> 创建 key_app 轮询线程
```

当 `BTSTACK_EVENT_STATE == HCI_STATE_WORKING` 时，说明：

- Host 栈已经起来
- Controller 已上电
- 本机可以被远端发现并连接

## 7. 数据流

### 7.1 蓝牙播放链

```text
手机 / PC
  -> A2DP media packet
  -> bt_app_a2dp_sink_media_handler()
  -> bt_a2dp_audio_process_media_packet()
      -> RTP/SBC 解析
      -> SBC decoder
      -> es8311_audio_write_playback_checked()
  -> playback ring buffer
  -> 达到启动阈值后启动 HAL_I2S_Transmit_DMA()
  -> DMA half/full callback 持续补 TX buffer
  -> I2S2
  -> ES8311 playback path
  -> 模拟输出
```

### 7.2 本地采集链

```text
PC9 双击
  -> key_app_toggle_capture()
  -> es8311_audio_set_run_mode(CAPTURE)
  -> es8311_audio_start_capture()
      -> HAL_I2SEx_TransmitReceive_DMA()
      -> es8311_start_record()
  -> DMA half/full callback
      -> 从 I2S Rx buffer 提取一个有效 slot
      -> 写入 capture ring buffer
  -> uart_send_pcm 线程
      -> es8311_audio_read_capture()
      -> rt_device_write(uart3)
```

### 7.3 采集 slot 处理说明

当前采集不是直接输出双声道，而是：

- 先读取 I2S Rx 的左右 slot
- 统计每个 slot 的：
  - `min`
  - `max`
  - `saturated`
  - `zero`
- 自动挑选一个相对有效的 slot
- 锁定该 slot
- 最终输出单声道 PCM

这说明当前采集链路更偏向“稳定拿到一条可用音频”，而不是完整双声道录音。

## 8. 当前关键配置

### 8.1 蓝牙配置

当前 `bt_config.h` 里的有效策略为：

- `BT_CFG_ENABLE_CLASSIC = 1`
- `BT_CFG_ENABLE_BLE = 0`
- `BT_CFG_LOCAL_NAME = "WSOZ"`
- `BT_CFG_UART_DEVICE_NAME = "uart2"`
- `BT_CFG_UART_BAUDRATE_INIT = 921600`
- `BT_CFG_UART_FLOWCONTROL = ON`
- `BT_CFG_CLASSIC_DISCOVERABLE = 1`
- `BT_CFG_CLASSIC_CONNECTABLE = 1`
- `BT_CFG_MAX_NR_AVDTP_STREAM_ENDPOINTS = 1`
- `BT_CFG_MAX_NR_AVDTP_CONNECTIONS = 1`

### 8.2 A2DP / SBC 能力

当前本机对外声明的 SBC 能力偏向最常见组合：

- 采样率：44.1 kHz / 48 kHz
- 通道：Stereo / Joint Stereo
- Block length：4 / 8 / 12 / 16
- Subbands：4 / 8
- Allocation：Loudness / SNR
- Bitpool：2 ~ 53

当前默认偏好配置是：

- 44.1 kHz
- Joint Stereo
- 16 blocks
- 8 subbands
- Loudness
- bitpool `2 ~ 53`

### 8.3 `es8311_audio.c` 播放 / 采集侧关键参数

当前统一音频会话层的关键参数如下：

- `ES8311_AUDIO_DMA_HALF_FRAMES = 512`
- `ES8311_AUDIO_DMA_BUFFER_FRAMES = 1024`
- `ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES = 8192`
- `ES8311_AUDIO_CAPTURE_BUFFER_FRAMES = 4096`
- `ES8311_AUDIO_PLAYBACK_START_THRESHOLD_FRAMES = 6144`
- 播放输出通道数：`2`
- 采集输出通道数：`1`

这些参数的目标是：

- 播放优先保证连续性
- 采集优先保证“能稳定拿到数据”

### 8.4 I2S / ES8311 侧

当前 I2S / codec 关键约束如下：

- `I2S2`
- `I2S_MODE_MASTER_TX`
- `I2S_FULLDUPLEXMODE_ENABLE`
- `I2S_STANDARD_PHILIPS`
- `I2S_DATAFORMAT_16B`
- `MCLKOutput = ENABLE`

ES8311 当前默认配置特征：

- 默认采样率：`44.1 kHz`
- 默认 `bits_per_sample = 16`
- 默认 `use_mclk = 1`
- 默认 `dac_source = LEFT`

这里有一个很重要的现实约束：

- ES8311 当前是按“单 DAC source 选一路 slot”在工作
- 上层并没有做更复杂的立体声 downmix / 路由抽象

## 9. 当前已经完成的能力

当前工程已经完成并打通的能力：

- RT-Thread 启动链与 BSP 正常工作
- `uart1 / uart2 / uart3 / i2c1 / I2S2` 已按当前业务接入
- BT-STACK Host 在 RT-Thread 下正常运行
- 外挂 ESP32 Controller 能正常上电并配合 Host 工作
- 本机可作为 A2DP Sink 被手机 / PC 发现
- 可建立 A2DP 连接并协商 SBC 参数
- 可接收 SBC 媒体包并解码为 PCM
- 可通过 `I2S2 + DMA + ES8311` 播放音频
- 可通过本地按键切换到采集模式
- 可把采集 PCM 经 `uart3` 导出

## 10. 当前已知限制

当前架构不是“全功能蓝牙音频系统”，而是“以 A2DP 播放为主、附带基础采集导出的可工作版本”。已知限制如下：

- 只实现了 A2DP Sink 主链路
- BLE 业务当前关闭，虽然底层封装预留了接口
- 没有接 AVRCP 控制层
- 没有接 HFP / HSP
- `bt_a2dp_audio.c` 尚未实现 SBC fragmentation 重组
- 播放与采集当前是互斥关系，不能并行
- 采集输出当前是单声道，不是完整双声道
- 采集链路当前只是“选一个有效 slot 输出”，没有更完整的声道抽象
- ES8311 当前默认只使用单一 DAC source，没有更高层播放路由策略
- 没有做更完整的静音 / standby / 自动省电策略

其中最值得优先继续完善的一点仍然是：

- `A2DP SBC fragmentation` 处理

因为这会直接影响不同手机 / PC 组合下的播放连续性。

## 11. 后续推荐扩展方向

推荐按这个顺序往下做：

1. 补齐 `A2DP SBC fragmentation` 重组
2. 继续稳定 `SBC -> PCM -> playback ring -> DMA` 的连续性
3. 梳理 ES8311 的播放 / 录音静音与 standby 状态机
4. 把采集链路补成更明确的导出协议或至少补齐格式说明
5. 如果确实需要录音质量，再决定是否做双声道采集或更明确的 slot 选择策略
6. 加 AVRCP，至少先接播放 / 暂停 / 音量同步
7. 如果后续真的需要 BLE，再把 BLE 初始化和业务配置接回当前架构

## 12. 关键文件索引

应用层：

- `applications/main.c`
- `applications/bt_app.c`
- `applications/bt_a2dp_sink_app.c`
- `applications/bt_a2dp_audio.c`
- `applications/es8311_audio.c`
- `applications/key_app.c`
- `applications/uart_send_pcm.c`

BT-STACK 封装与端口层：

- `mycomponents/BT-STACK/core/src/bt_host.c`
- `mycomponents/BT-STACK/core/config/bt_config.h`
- `mycomponents/BT-STACK/port/btstack_port.c`
- `mycomponents/BT-STACK/port/btstack_run_loop_embedded.c`
- `mycomponents/BT-STACK/port/btstack_uart_block_embedded.c`
- `mycomponents/BT-STACK/port/btstack_chipset_esp32.c`

Codec 与输入组件：

- `mycomponents/es8311/es8311_driver.c`
- `mycomponents/es8311/es8311_driver.h`
- `mycomponents/keyboard/inc/keyboard_driver.h`
- `mycomponents/keyboard/src/keyboard_driver.c`

BSP 与时钟 / 外设初始化：

- `drivers/board.c`
- `drivers/board.h`
- `drivers/drv_common.c`
- `drivers/drv_clk.c`
- `drivers/drv_usart.c`
- `drivers/drv_soft_i2c.c`
- `cubemx/Src/main.c`
- `cubemx/Src/stm32f4xx_hal_msp.c`

## 13. 一句话总结

当前工程已经形成两条真实可工作的链路：

- `BT-STACK Host -> A2DP Sink -> SBC 解码 -> ES8311 音频会话层 -> I2S2 DMA -> ES8311 播放`
- `PC9 双击 -> CAPTURE mode -> I2S2 Rx DMA -> 单声道 PCM -> uart3 导出`

后续工作重点不再是“把链路接通”，而是继续做一致性、稳定性和文档收敛。
