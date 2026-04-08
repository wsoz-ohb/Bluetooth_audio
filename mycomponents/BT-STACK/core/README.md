# BTstack Host 协议栈提取说明

更完整的当前工程移植说明见：

- [../README.md](../README.md)
- [../USAGE.md](../USAGE.md)

这个目录是从当前仓库的 `src/` 中提取出来的一份“便于移植”的 Host 协议栈副本，原始源码没有被移动。

当前版本已经进一步裁剪为只保留 `H4` 传输相关内容。

## 目录结构

- `core/inc`
  公共头文件，对应原始 `src/*.h`
- `core/src`
  公共源文件，对应原始 `src/*.c`
- `core/classic/inc/classic`
  经典蓝牙头文件，对应原始 `src/classic/*.h`
- `core/classic/src`
  经典蓝牙源文件，对应原始 `src/classic/*.c`
- `core/ble/inc/ble`
  BLE 头文件，对应原始 `src/ble/*.h`
- `core/ble/inc/ble/gatt-service`
  BLE GATT Service 头文件
- `core/ble/src`
  BLE 源文件，对应原始 `src/ble/*.c`
- `core/ble/src/gatt-service`
  BLE GATT Service 源文件
- `core/config/bt_config.h`
  统一配置入口，只改这一个文件即可控制协议、名字、波特率和大部分容量参数
- `core/config/btstack_config.h`
  BTstack 原生配置映射层，自动从 `bt_config.h` 取值，通常不需要直接改
- `core/inc/bt_host.h`
  H4 统一初始化封装头文件
- `core/src/bt_host.c`
  H4 统一初始化封装实现

## 建议的头文件搜索路径

- `core/inc`
- `core/config`
- `core/classic/inc`
- `core/ble/inc`

这样可以继续使用原始包含方式，例如：

- `#include "hci.h"`
- `#include "classic/rfcomm.h"`
- `#include "ble/att_server.h"`
- `#include "ble/gatt-service/battery_service_server.h"`

## 我这次有意保留的原则

- 只提取公共层、Classic、BLE，没把 `le-audio` 和 `mesh` 一起搬进来。
- 只保留 HCI H4 传输，已经去掉 H5、USB、EM9304 SPI 相关文件。
- 原始 `src/` 不改，避免影响你后续继续参考官方目录。
- 提取版里的 `btstack.h` 已经去掉了对 `le-audio`、`mesh` 的聚合引用，避免头文件断链。

## 现在怎么统一配置

以后优先改这个文件：

- `core/config/bt_config.h`

你最常会改的几个项目：

- 开关 Classic：
  `#define BT_CFG_ENABLE_CLASSIC 1`
- 开关 BLE：
  `#define BT_CFG_ENABLE_BLE 1`
- 改 H4 初始波特率：
  `#define BT_CFG_UART_BAUDRATE_INIT 115200`
- 改 H4 工作波特率：
  `#define BT_CFG_UART_BAUDRATE_MAIN 921600`
- 改 UART 流控：
  `#define BT_CFG_UART_FLOWCONTROL BT_CFG_UART_FLOWCONTROL_ON`
- 改 Classic 设备名：
  `#define BT_CFG_LOCAL_NAME "My BT Device 00:00:00:00:00:00"`
- 改 BLE 广播名：
  `#define BT_CFG_BLE_ADV_NAME "My BLE Device"`

## 协议怎么开

这里分两层，不要混在一起：

- 第一层是“编译开关”，在 `bt_config.h` 里改：
  `BT_CFG_ENABLE_CLASSIC`、`BT_CFG_ENABLE_BLE`
- 第二层是“基础协议初始化”，由 `bt_host.c` 统一做：
  `BT_CFG_CLASSIC_ENABLE_SDP`
  `BT_CFG_CLASSIC_ENABLE_RFCOMM`
  `BT_CFG_BLE_ENABLE_SM`

常见对应关系：

- `SPP`
  需要 `BT_CFG_ENABLE_CLASSIC=1`、`BT_CFG_CLASSIC_ENABLE_SDP=1`、`BT_CFG_CLASSIC_ENABLE_RFCOMM=1`
- `HFP / A2DP / AVRCP`
  先要 `BT_CFG_ENABLE_CLASSIC=1`，一般也会依赖 `SDP`
- `BLE GATT Server`
  需要 `BT_CFG_ENABLE_BLE=1`，通常再开 `BT_CFG_BLE_ENABLE_SM=1`

注意：

- `bt_config.h` 负责“能不能编进来”和“基础层是否初始化”。
- 具体 profile 仍然要在你的应用里继续调用对应 `xxx_init()` / `xxx_register_service()`。
- 比如 `SPP` 还要自己调用 `rfcomm_register_service()`、`spp_create_sdp_record()`。
- 比如 `ATT Server` 还要提供 `profile_data`，再调用 `bt_host_ble_init_att_server()`。

## 最小启动顺序

下面这套顺序是给 H4 版用的：

```c
#include "bt_host.h"

static void app_bt_init(void){
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

    bt_host_stack_init(btstack_uart_block_embedded_instance(), chipset_driver);
    bt_host_protocol_init();

    // 如果你有 BLE GATT Server
    bt_host_ble_init_att_server(profile_data, att_read_callback, att_write_callback);

    // 如果你有 Classic profile
    // rfcomm_register_service(...);
    // spp_create_sdp_record(...);
    // sdp_register_service(...);

    bt_host_apply_device_config();
    bt_host_ble_setup_advertising();
    bt_host_start();
}
```

如果你只做 Classic：

- 可以不调用 `bt_host_ble_init_att_server()`
- 可以不调用 `bt_host_ble_setup_advertising()`

如果你只做 BLE：

- 把 `BT_CFG_ENABLE_CLASSIC` 设成 `0`
- Classic 相关的 `rfcomm/sdp` 初始化和 profile 注册都不要调

## 波特率和名字分别影响什么

- `BT_CFG_UART_BAUDRATE_INIT`
  控制器刚上电时主机用这个波特率和它通信
- `BT_CFG_UART_BAUDRATE_MAIN`
  芯片初始化完成后切到这个工作波特率；如果你的芯片或固件不支持切换，就把它设成和 init 一样
- `BT_CFG_LOCAL_NAME`
  主要给 Classic 本地名、EIR 使用
- `BT_CFG_BLE_ADV_NAME`
  只给 BLE 广播和扫描响应使用，和 Classic 名字是分开的
- `BT_CFG_UART_DEVICE_NAME`
  只是透传给 UART 驱动的标识，具体含义由你平台里的 UART 实现决定

## 你移植时还需要自己补的部分

- 平台相关实现：`run loop`、`TLV/NVM`、`UART` 底层、时钟/时间接口
- H4 底层实现：`btstack_uart.h` / `btstack_uart_block.h` 对应的 UART 驱动
- 控制器相关适配：`chipset/` 下对应厂商初始化和 patch 下载逻辑
- 具体工程构建脚本：Makefile / CMake / IDE 工程

参考目录：

- `platform/embedded`
- `platform/freertos`
- `port/stm32-f4discovery-cc256x`
- `port/posix-h4`

## 额外第三方依赖提醒

下面这些不是所有场景都需要，但如果你启用了对应功能，编译时要把它们的头文件路径加进去：

- `3rd-party/micro-ecc`
  BLE Secure Connections
- `3rd-party/rijndael`
  软件 AES
- `3rd-party/bluedroid/decoder/include`
  SBC / mSBC 解码
- `3rd-party/bluedroid/encoder/include`
  SBC / mSBC 编码
- `3rd-party/md5`
  PBAP Client
- `3rd-party/yxml`
  PBAP Client XML 解析

## 一个简单的裁剪思路

- 只做 BLE：
  保留 `core/inc`、`core/src`、`core/ble`
  去掉 `ENABLE_CLASSIC`
- 只做 Classic：
  保留 `core/inc`、`core/src`、`core/classic`
  去掉 `ENABLE_BLE`
- 双模：
  三部分都保留



