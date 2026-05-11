#ifndef BT_CONFIG_H
#define BT_CONFIG_H

#include <stddef.h>
#include "bluetooth.h"

// Keep UART config values local here to avoid the include cycle:
// bt_config.h -> btstack_uart.h -> btstack_config.h -> bt_config.h
#define BT_CFG_UART_PARITY_OFF                    0
#define BT_CFG_UART_PARITY_EVEN                   1
#define BT_CFG_UART_PARITY_ODD                    2

#define BT_CFG_UART_FLOWCONTROL_ON                1
#define BT_CFG_UART_FLOWCONTROL_OFF               0

// ---------------------------------------------------------------------------
// 平台能力
// 这些宏会被 core/config/btstack_config.h 转成 BTstack 原生配置
// ---------------------------------------------------------------------------
#define BT_CFG_HAVE_ASSERT                         0
#define BT_CFG_HAVE_BTSTACK_STDIN                  0
#define BT_CFG_HAVE_EMBEDDED_TIME_MS               1
#define BT_CFG_HAVE_MALLOC                         0
#define BT_CFG_HAVE_POSIX_FILE_IO                  0
#define BT_CFG_HAVE_POSIX_TIME                     0

// ---------------------------------------------------------------------------
// 总开关
// 至少开启一种：Classic / BLE
// ---------------------------------------------------------------------------
#define BT_CFG_ENABLE_CLASSIC                      1
#define BT_CFG_ENABLE_BLE                          0

// ---------------------------------------------------------------------------
// BLE 编译能力
// ---------------------------------------------------------------------------
#define BT_CFG_ENABLE_LE_CENTRAL                   1
#define BT_CFG_ENABLE_LE_PERIPHERAL                1
#define BT_CFG_ENABLE_LE_SECURE_CONNECTIONS        1
#define BT_CFG_ENABLE_LE_L2CAP_CREDIT              1
#define BT_CFG_ENABLE_MICRO_ECC_FOR_LE_SC          1
#define BT_CFG_ENABLE_SOFTWARE_AES128              1

// ---------------------------------------------------------------------------
// Classic 可选能力
// ---------------------------------------------------------------------------
#define BT_CFG_ENABLE_HFP_WBS                      0
#define BT_CFG_ENABLE_L2CAP_ERTM                   0
#define BT_CFG_ENABLE_SCO_OVER_HCI                 0
#define BT_CFG_MAX_NR_AVRCP_CONNECTIONS            2
#define BT_CFG_MAX_NR_AVRCP_BROWSING_CONNECTIONS   0
// ---------------------------------------------------------------------------
// 基础协议初始化开关
// 这里只处理“协议层”初始化，不直接代替具体 profile 的业务注册
// ---------------------------------------------------------------------------
#define BT_CFG_CLASSIC_ENABLE_SDP                  1
#define BT_CFG_CLASSIC_ENABLE_RFCOMM               1
#define BT_CFG_BLE_ENABLE_SM                       1

// ---------------------------------------------------------------------------
// H4 UART 配置
// baudrate_init: 控制器上电后的初始波特率
// baudrate_main: 初始化完成后的工作波特率；如果芯片不支持切波特率，设成和 init 一样
// device_name  : 透传给你的 UART 驱动，含义由你的平台实现决定，可为 NULL
// ---------------------------------------------------------------------------
#define BT_CFG_UART_BAUDRATE_INIT                  921600
#define BT_CFG_UART_BAUDRATE_MAIN                  921600
#define BT_CFG_UART_FLOWCONTROL                    BT_CFG_UART_FLOWCONTROL_ON
#define BT_CFG_UART_PARITY                         BT_CFG_UART_PARITY_OFF
#define BT_CFG_UART_DEVICE_NAME                    "uart2"

// ---------------------------------------------------------------------------
// 设备信息
// BT_CFG_LOCAL_NAME: 主要给 Classic 本地名 / EIR 使用
// BT_CFG_BLE_ADV_NAME: BLE 广播名，独立于 Classic 本地名
// BT_CFG_CLASS_OF_DEVICE: 设为 0 表示不主动配置 COD
// ---------------------------------------------------------------------------
#define BT_CFG_LOCAL_NAME                          "WSOZ"
#define BT_CFG_BLE_ADV_NAME                        "WSOZ"
#define BT_CFG_CLASS_OF_DEVICE                     0x240414u

// ---------------------------------------------------------------------------
// Classic 可发现 / 可连接 / 配对参数
// ---------------------------------------------------------------------------
#define BT_CFG_CLASSIC_DISCOVERABLE                1
#define BT_CFG_CLASSIC_CONNECTABLE                 1
#define BT_CFG_CLASSIC_SSP_IO_CAPABILITY           SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT
#define BT_CFG_CLASSIC_SSP_AUTHREQ                 SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING

// ---------------------------------------------------------------------------
// BLE 安全参数
// ---------------------------------------------------------------------------
#define BT_CFG_BLE_SM_IO_CAPABILITY                IO_CAPABILITY_NO_INPUT_NO_OUTPUT
#define BT_CFG_BLE_SM_AUTH_REQ                     (SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION)

// ---------------------------------------------------------------------------
// BLE 广播参数
// 默认使用 Flags + Local Name 的最小广播内容
// 如果名字过长，会自动截短广播包并把完整名字放到 Scan Response
// ---------------------------------------------------------------------------
#define BT_CFG_BLE_ADV_ENABLE                      1
#define BT_CFG_BLE_ADV_FLAGS                       0x06
#define BT_CFG_BLE_ADV_INTERVAL_MIN                0x0030
#define BT_CFG_BLE_ADV_INTERVAL_MAX                0x0030
#define BT_CFG_BLE_ADV_TYPE                        0x00
#define BT_CFG_BLE_ADV_CHANNEL_MAP                 0x07
#define BT_CFG_BLE_ADV_FILTER_POLICY               0x00

// ---------------------------------------------------------------------------
// 日志开关
// ---------------------------------------------------------------------------
#define BT_CFG_ENABLE_LOG_ERROR                    1
#define BT_CFG_ENABLE_LOG_INFO                     1
#define BT_CFG_ENABLE_PRINTF_HEXDUMP               0

// ---------------------------------------------------------------------------
// 资源池配置
// ---------------------------------------------------------------------------
#define BT_CFG_HCI_ACL_PAYLOAD_SIZE                (1691 + 4)

#define BT_CFG_MAX_NR_HCI_CONNECTIONS              2
#define BT_CFG_MAX_NR_L2CAP_CHANNELS               6
#define BT_CFG_MAX_NR_L2CAP_SERVICES               4

#define BT_CFG_MAX_NR_RFCOMM_MULTIPLEXERS          1
#define BT_CFG_MAX_NR_RFCOMM_SERVICES              1
#define BT_CFG_MAX_NR_RFCOMM_CHANNELS              1
#define BT_CFG_MAX_NR_SERVICE_RECORD_ITEMS         8
#define BT_CFG_MAX_NR_AVDTP_STREAM_ENDPOINTS      1
#define BT_CFG_MAX_NR_AVDTP_CONNECTIONS           1

#define BT_CFG_MAX_NR_GATT_CLIENTS                 1
#define BT_CFG_MAX_NR_SM_LOOKUP_ENTRIES            3
#define BT_CFG_MAX_NR_WHITELIST_ENTRIES            1

#define BT_CFG_NVM_NUM_DEVICE_DB_ENTRIES           16
#define BT_CFG_NVM_NUM_LINK_KEYS                   16

#define BT_CFG_MAX_ATT_DB_SIZE                     512

#if !BT_CFG_ENABLE_CLASSIC && !BT_CFG_ENABLE_BLE
#error "bt_config.h: BT_CFG_ENABLE_CLASSIC 和 BT_CFG_ENABLE_BLE 不能同时关闭"
#endif

#endif // BT_CONFIG_H





