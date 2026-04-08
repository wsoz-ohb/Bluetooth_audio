#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

#include "bt_config.h"

// ---------------------------------------------------------------------------
// 平台能力
// ---------------------------------------------------------------------------
#if BT_CFG_HAVE_ASSERT
#define HAVE_ASSERT
#endif

#if BT_CFG_HAVE_BTSTACK_STDIN
#define HAVE_BTSTACK_STDIN
#endif

#if BT_CFG_HAVE_EMBEDDED_TIME_MS
#define HAVE_EMBEDDED_TIME_MS
#endif

#if BT_CFG_HAVE_MALLOC
#define HAVE_MALLOC
#endif

#if BT_CFG_HAVE_POSIX_FILE_IO
#define HAVE_POSIX_FILE_IO
#endif

#if BT_CFG_HAVE_POSIX_TIME
#define HAVE_POSIX_TIME
#endif

// ---------------------------------------------------------------------------
// 协议编译开关
// ---------------------------------------------------------------------------
#if BT_CFG_ENABLE_CLASSIC
#define ENABLE_CLASSIC
#define ENABLE_EXPLICIT_CONNECTABLE_MODE_CONTROL
#endif

#if BT_CFG_ENABLE_BLE
#define ENABLE_BLE
#endif

#if BT_CFG_ENABLE_BLE && BT_CFG_ENABLE_LE_CENTRAL
#define ENABLE_LE_CENTRAL
#endif

#if BT_CFG_ENABLE_BLE && BT_CFG_ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PERIPHERAL
#endif

#if BT_CFG_ENABLE_BLE && BT_CFG_ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_LE_SECURE_CONNECTIONS
#endif

#if BT_CFG_ENABLE_BLE && BT_CFG_ENABLE_LE_L2CAP_CREDIT
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#endif

#if BT_CFG_ENABLE_BLE && BT_CFG_ENABLE_MICRO_ECC_FOR_LE_SC
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#endif

#if BT_CFG_ENABLE_SOFTWARE_AES128
#define ENABLE_SOFTWARE_AES128
#endif

#if BT_CFG_ENABLE_HFP_WBS
#define ENABLE_HFP_WIDE_BAND_SPEECH
#endif

#if BT_CFG_ENABLE_L2CAP_ERTM
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#endif

#if BT_CFG_ENABLE_SCO_OVER_HCI
#define ENABLE_SCO_OVER_HCI
#endif

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
#if BT_CFG_ENABLE_LOG_ERROR
#define ENABLE_LOG_ERROR
#endif

#if BT_CFG_ENABLE_LOG_INFO
#define ENABLE_LOG_INFO
#endif

#if BT_CFG_ENABLE_PRINTF_HEXDUMP
#define ENABLE_PRINTF_HEXDUMP
#endif

// ---------------------------------------------------------------------------
// 资源池配置
// ---------------------------------------------------------------------------
#define HCI_ACL_PAYLOAD_SIZE                 BT_CFG_HCI_ACL_PAYLOAD_SIZE

#define MAX_NR_HCI_CONNECTIONS              BT_CFG_MAX_NR_HCI_CONNECTIONS
#define MAX_NR_L2CAP_CHANNELS               BT_CFG_MAX_NR_L2CAP_CHANNELS
#define MAX_NR_L2CAP_SERVICES               BT_CFG_MAX_NR_L2CAP_SERVICES

#define MAX_NR_RFCOMM_MULTIPLEXERS          BT_CFG_MAX_NR_RFCOMM_MULTIPLEXERS
#define MAX_NR_RFCOMM_SERVICES              BT_CFG_MAX_NR_RFCOMM_SERVICES
#define MAX_NR_RFCOMM_CHANNELS              BT_CFG_MAX_NR_RFCOMM_CHANNELS
#define MAX_NR_SERVICE_RECORD_ITEMS         BT_CFG_MAX_NR_SERVICE_RECORD_ITEMS

#define MAX_NR_GATT_CLIENTS                 BT_CFG_MAX_NR_GATT_CLIENTS
#define MAX_NR_SM_LOOKUP_ENTRIES            BT_CFG_MAX_NR_SM_LOOKUP_ENTRIES
#define MAX_NR_WHITELIST_ENTRIES            BT_CFG_MAX_NR_WHITELIST_ENTRIES

#define NVM_NUM_DEVICE_DB_ENTRIES           BT_CFG_NVM_NUM_DEVICE_DB_ENTRIES
#define NVM_NUM_LINK_KEYS                   BT_CFG_NVM_NUM_LINK_KEYS
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES BT_CFG_NVM_NUM_LINK_KEYS

#define MAX_ATT_DB_SIZE                     BT_CFG_MAX_ATT_DB_SIZE

#endif // BTSTACK_CONFIG_H
