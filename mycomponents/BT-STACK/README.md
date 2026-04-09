# BT-STACK 移植手册

这份 `BT-STACK` 是从 BTstack 源码中提取并整理出来的一份当前工程可直接使用的 Host 协议栈组件，目标是给当前 `RT-Thread + STM32 + 外部蓝牙控制器(H4 UART)` 工程提供一套清晰、可裁剪、可继续扩展的 Host 层。

更偏“怎么用”的说明见：

- [USAGE.md](./USAGE.md)
- [core/README.md](./core/README.md)

## 这次移植做了什么

当前组件已经完成了下面这些工作：

- 把 BTstack 的公共层、Classic、BLE 代码拆成了独立目录，方便单独移植和维护。
- 只保留了当前工程需要的 `H4 UART` 传输路径，去掉了 H5、USB、EM9304 SPI 等无关传输层。
- 增加了统一配置入口 [core/config/bt_config.h](./core/config/bt_config.h)，把常用开关、名字、波特率、容量参数集中到一个文件。
- 增加了配置映射层 [core/config/btstack_config.h](./core/config/btstack_config.h)，把项目配置翻译成 BTstack 原生宏。
- 增加了基础 Host 封装 [core/inc/bt_host.h](./core/inc/bt_host.h) 和 [core/src/bt_host.c](./core/src/bt_host.c)，统一做基础协议初始化和基础设备配置。
- 增加了 RT-Thread 端口封装 [port/btstack_port.c](./port/btstack_port.c)，统一处理 run loop、TLV、chipset、线程启动和上电流程。
- 对 `core/classic/src` 和 `core/ble/src` 做了源码级条件编译门控，现在只改 `bt_config.h` 就能切 `Classic-only / BLE-only / Dual Mode`。
- 补齐了 Classic 的显式 `connectable/discoverable` 控制，避免 L2CAP 在“还没注册具体 profile”时自动关掉基础连接能力。
- 补齐了 BLE 广播辅助函数的接线；如果后面打开 BLE，广播参数可以从配置直接生效。
- 整理了当前需要的 `3rd-party` 依赖目录，并把测试目录从工程构建里排除。

## 当前组件的边界

这份移植版是“Host 协议栈组件”，不是“完整业务应用”。

已经具备：

- `HCI`
- `L2CAP`
- `SDP`
- `RFCOMM`
- `SM`
- `A2DP / AVDTP / AVRCP / HID / SPP / HFP` 等 profile 的源码和公开 API

但默认没有替你完成：

- 某个具体 profile 的业务初始化
- SDP 服务记录注册
- A2DP codec 能力填写
- HID report descriptor / callback
- BLE ATT 数据库内容
- 音频编解码和音频数据通路

也就是说，这个组件现在已经是“可用的 Host 协议栈底座”，但你要做哪个 profile，仍然要在应用层把该 profile 接起来。

## 当前目录结构

- `3rd-party`
  第三方依赖。当前工程实际会用到的主要是 `micro-ecc`、`rijndael`、`bluedroid`、`md5`、`yxml`、`lc3-google` 等目录。
- `core/inc`
  BTstack 公共头文件。
- `core/src`
  BTstack 公共源文件。
- `core/classic/inc/classic`
  Classic profile 头文件。
- `core/classic/src`
  Classic profile 源文件。
- `core/ble/inc/ble`
  BLE 协议头文件。
- `core/ble/inc/ble/gatt-service`
  BLE GATT Service 头文件。
- `core/ble/src`
  BLE 协议源文件。
- `core/ble/src/gatt-service`
  BLE GATT Service 源文件。
- `core/config/bt_config.h`
  当前工程统一配置入口。以后优先改这个文件。
- `core/config/btstack_config.h`
  BTstack 原生宏映射层。通常不直接改。
- `core/inc/bt_host.h`
  当前工程的基础 Host 初始化封装头文件。
- `core/src/bt_host.c`
  当前工程的基础 Host 初始化封装实现。
- `port/btstack_port.c`
  当前工程的 RT-Thread 端口入口，负责基础栈初始化、线程启动和上电。
- `port/btstack_run_loop_embedded.c`
  当前工程使用的 run loop 实现。

## 当前工程里的启动路径

当前工程不是在应用层手工一条条调用 `hci_init()`、`l2cap_init()`、`sdp_init()`，而是已经整理成一条固定启动链路。

### 启动流转图

```text
[applications/main.c]
main()
    -> bt__init()

[applications/bt_app.c]
bt__init()
    -> btstack_port_init(NULL)
       这里只做基础栈准备，还没有真正 HCI 上电
       这里之后最适合注册 A2DP / HID / SPP / ATT DB / SDP record
    -> btstack_port_start_thread()
       创建 BTstack 专用线程
       把 btstack_port_power_on() 投递到 run loop 线程执行

[port/btstack_port.c]
btstack_port_init()
    -> btstack_run_loop_init(btstack_run_loop_embedded_get_instance())
       注册当前 RT-Thread 平台的 run loop
    -> btstack_tlv_set_instance(btstack_tlv_none_init_instance(), NULL)
       注册 TLV 存储后端，当前是 none，不做持久化
    -> bt_host_stack_init(...)
       接好 H4 UART / chipset / HCI 传输层
    -> bt_host_protocol_init()
       初始化 L2CAP / SDP / RFCOMM / SM 等基础协议
    -> bt_host_apply_device_config()
       应用本地名字、Class of Device、可发现/可连接、SSP IO 能力等配置

btstack_port_start_thread()
    -> rt_thread_create(..., btstack_port_thread_entry, ...)
    -> rt_thread_startup(...)
    -> btstack_run_loop_execute_on_main_thread(&btstack_port_power_on_registration)

btstack_port_thread_entry()
    -> btstack_run_loop_execute()
       开始跑 BTstack 的事件循环

btstack_port_power_on()
    -> bt_host_start()

[core/src/bt_host.c]
bt_host_start()
    -> hci_power_control(HCI_POWER_ON)
       这里才是真正开始让控制器和 Host 状态机工作
```

### 每个文件在做什么

- [applications/main.c](../applications/main.c)
  应用主入口。目前这里只负责调用 `bt__init()` 把蓝牙栈拉起来。
- [applications/bt_app.c](../applications/bt_app.c)
  当前工程的蓝牙应用入口。它负责组织启动顺序，也是后续插入 `A2DP / HID / SPP / BLE GATT` 初始化的推荐位置。
- [port/btstack_port.c](./port/btstack_port.c)
  端口胶水层。负责把 `run loop`、`TLV`、`UART/H4`、`chipset`、基础 Host 初始化、线程启动和上电流程串起来。
- [port/btstack_run_loop_embedded.c](./port/btstack_run_loop_embedded.c)
  当前平台的事件循环实现。BTstack 的定时器、回调和数据源轮询都依赖它在 RT-Thread 线程里运行。
- [core/src/bt_host.c](./core/src/bt_host.c)
  基础 Host 封装。这里负责 `HCI/L2CAP/SDP/RFCOMM/SM` 这类底层协议初始化，以及本地设备参数应用和最终 `HCI_POWER_ON`。

### 初学者最需要记住的一点

- `btstack_port_init()` 之后、`btstack_port_start_thread()` 之前，是注册具体 profile 的最好时机。

也就是说，后面你要加 `A2DP / SPP / HID / BLE GATT`，推荐都放在这个窗口里初始化和注册。

## 当前统一配置入口

以后优先改：

- [core/config/bt_config.h](./core/config/bt_config.h)

当前最常用的项目有：

- `BT_CFG_ENABLE_CLASSIC`
- `BT_CFG_ENABLE_BLE`
- `BT_CFG_CLASSIC_ENABLE_SDP`
- `BT_CFG_CLASSIC_ENABLE_RFCOMM`
- `BT_CFG_BLE_ENABLE_SM`
- `BT_CFG_UART_BAUDRATE_INIT`
- `BT_CFG_UART_BAUDRATE_MAIN`
- `BT_CFG_LOCAL_NAME`
- `BT_CFG_BLE_ADV_NAME`
- `BT_CFG_CLASSIC_DISCOVERABLE`
- `BT_CFG_CLASSIC_CONNECTABLE`

## 当前工程里已经接好的基础能力

如果按当前默认配置启动：

- Classic 打开
- BLE 关闭
- L2CAP 初始化
- SDP 初始化
- RFCOMM 初始化
- Classic 本地名配置
- Classic `connectable=1`
- Classic `discoverable=1`
- Classic SSP 配对参数生效

注意：

- 这里说的是“基础层已经接好”。
- 不代表 `A2DP / HID / SPP` 已经可以直接被系统当成完整业务设备使用。
- 具体 profile 仍然要在应用层调用对应 API。

## 头文件搜索路径建议

当前工程建议保留这些 include path：

- `mycomponents/BT-STACK/core/config`
- `mycomponents/BT-STACK/core/inc`
- `mycomponents/BT-STACK/core/classic/inc`
- `mycomponents/BT-STACK/core/ble/inc`
- `mycomponents/BT-STACK/port`

这样可以继续用 BTstack 原始包含方式：

```c
#include "hci.h"
#include "classic/a2dp_sink.h"
#include "classic/rfcomm.h"
#include "ble/att_server.h"
#include "ble/gatt-service/hids_device.h"
```

## 当前还需要你自己做的事情

后面你做业务时，通常还要自己补下面这些内容：

- 具体 profile 的 `init`
- 具体 profile 的 `packet handler`
- Classic profile 的 `SDP record`
- BLE profile 的 `ATT DB / GATT service`
- 音频/串口/输入设备等业务数据处理
- 需要持久化时的 TLV/NVM 实现

## 读文档顺序建议

如果你接下来要开始写业务，建议按这个顺序看：

1. [USAGE.md](./USAGE.md)
2. [core/config/bt_config.h](./core/config/bt_config.h)
3. [port/btstack_port.c](./port/btstack_port.c)
4. [applications/bt_app.c](../applications/bt_app.c)
5. 对应 profile 的头文件，例如：
   - [classic/a2dp_sink.h](./core/classic/inc/classic/a2dp_sink.h)
   - [classic/a2dp_source.h](./core/classic/inc/classic/a2dp_source.h)
   - [classic/hid_device.h](./core/classic/inc/classic/hid_device.h)
   - [classic/spp_server.h](./core/classic/inc/classic/spp_server.h)

