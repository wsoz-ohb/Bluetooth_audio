/******************************************************************************
  * @file           bt_hci.h
  * @author         Yu-ZhongJun(124756828@qq.com)
  * @Taobao link    https://shop220811498.taobao.com/
  * @version        V0.0.1
  * @date           2020-4-15
  * @brief          bt hci header file
******************************************************************************/

#ifndef BT_HCI_H_H_H
#define BT_HCI_H_H_H

#include "bt_common.h"  //通用文件 
#include "bt_pbuf.h"    //缓冲区文件
#include "bt_vendor_manager.h" //供应商管理文件

//包头长度定义
#define HCI_EVT_HDR_LEN 2
#define HCI_ACL_HDR_LEN 4
#define HCI_SCO_HDR_LEN 3
#define HCI_CMD_HDR_LEN 3


/*-----OPCODE定义---- 24-273 */
/* Group occupies high 6 bits of the HCI command rest is opcode itself */
#define HCI_OGF(p)  (uint8_t)((0xFC00 & (p)) >> 10)
#define HCI_OCF(p)  ( 0x3FF & (p))  //低10位

/*
**  Definitions for HCI groups
*/
#define HCI_GRP_LINK_CTL_CMDS       (0x01 << 10)            /* 0x0400 */    //链路控制命令
#define HCI_GRP_LINK_PLY_CMDS        (0x02 << 10)            /* 0x0800 */   //链路策略命令
#define HCI_GRP_HOST_CONT_BB_CMDS (0x03 << 10)            /* 0x0C00 */      //控制器和基带命令
#define HCI_GRP_INFO_PARA    (0x04 << 10)            /* 0x1000 */           //信息参数命令
#define HCI_GRP_STATUS_PARA           (0x05 << 10)            /* 0x1400 */  //状态参数命令
#define HCI_GRP_TEST_CMDS            (0x06 << 10)            /* 0x1800 */   //测试命令
#define HCI_GRP_BLE_CMDS                (0x08 << 10)                        //低功耗控制器命令
#define HCI_GRP_VENDOR_SPECIFIC         (0x3F << 10)            /* 0xFC00 */    //供应商特定命令

/* Following opcode is used only in command complete event for flow control */
#define HCI_OP_NONE                0x0000

/* Commands of HCI_GRP_LINK_CTL_CMDS group */
#define HCI_OP_INQUIRY                     (0x0001 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_INQUIRY_CANCEL              (0x0002 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_PERIODIC_INQUIRY_MODE       (0x0003 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_EXIT_PERIODIC_INQUIRY_MODE  (0x0004 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CREATE_CONNECTION           (0x0005 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_DISCONNECT                  (0x0006 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_ADD_SCO_CONNECTION          (0x0007 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CREATE_CONNECTION_CANCEL    (0x0008 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_ACCEPT_CONNECTION_REQUEST   (0x0009 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_REJECT_CONNECTION_REQUEST   (0x000A | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_LINK_KEY_REQUEST_REPLY      (0x000B | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_LINK_KEY_REQUEST_NEG_REPLY  (0x000C | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_PIN_CODE_REQUEST_REPLY      (0x000D | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_PIN_CODE_REQUEST_NEG_REPLY  (0x000E | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CHANGE_CONN_PACKET_TYPE     (0x000F | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_AUTHENTICATION_REQUESTED    (0x0011 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_SET_CONN_ENCRYPTION         (0x0013 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CHANGE_CONN_LINK_KEY        (0x0015 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_MASTER_LINK_KEY             (0x0017 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_RMT_NAME_REQUEST            (0x0019 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_RMT_NAME_REQUEST_CANCEL     (0x001A | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_READ_RMT_FEATURES           (0x001B | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_READ_RMT_EXT_FEATURES       (0x001C | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_READ_RMT_VERSION_INFO       (0x001D | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_READ_RMT_CLOCK_OFFSET       (0x001F | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_READ_LMP_HANDLE             (0x0020 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_SETUP_ESCO_CONNECTION       (0x0028 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_ACCEPT_ESCO_CONNECTION      (0x0029 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_REJECT_ESCO_CONNECTION      (0x002A | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_IO_CAPABILITY_RESPONSE      (0x002B | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_USER_CONF_REQUEST_REPLY     (0x002C | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_USER_CONF_VALUE_NEG_REPLY   (0x002D | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_USER_PASSKEY_REQ_REPLY      (0x002E | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_USER_PASSKEY_REQ_NEG_REPLY  (0x002F | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_REM_OOB_DATA_REQ_REPLY      (0x0030 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_REM_OOB_DATA_REQ_NEG_REPLY  (0x0033 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_IO_CAP_REQ_NEG_REPLY        (0x0034 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CREATE_PHYSICAL_LINK        (0x0035 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_ACCEPT_PHYSICAL_LINK        (0x0036 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_DISCONNECT_PHYSICAL_LINK    (0x0037 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_CREATE_LOGICAL_LINK         (0x0038 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_ACCEPT_LOGICAL_LINK         (0x0039 | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_DISCONNECT_LOGICAL_LINK     (0x003A | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_LOGICAL_LINK_CANCEL         (0x003B | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_FLOW_SPEC_MODIFY            (0x003C | HCI_GRP_LINK_CTL_CMDS)
#define HCI_OP_LINK_CTRL_CMDS_FIRST        HCI_OP_INQUIRY
#define HCI_OP_LINK_CTRL_CMDS_LAST         HCI_OP_FLOW_SPEC_MODIFY


/* Commands of HCI_GRP_LINK_PLY_CMDS */
#define HCI_OP_HOLD_MODE                   (0x0001 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_SNIFF_MODE                  (0x0003 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_EXIT_SNIFF_MODE             (0x0004 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_PARK_MODE                   (0x0005 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_EXIT_PARK_MODE              (0x0006 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_QOS_SETUP                   (0x0007 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_ROLE_DISCOVERY              (0x0009 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_SWITCH_ROLE                 (0x000B | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_READ_POLICY_SETTINGS        (0x000C | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_WRITE_POLICY_SETTINGS       (0x000D | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_READ_DEF_POLICY_SETTINGS    (0x000E | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_WRITE_DEF_POLICY_SETTINGS   (0x000F | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_FLOW_SPECIFICATION          (0x0010 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_SNIFF_SUB_RATE              (0x0011 | HCI_GRP_LINK_PLY_CMDS)
#define HCI_OP_LINK_POLICY_CMDS_FIRST      HCI_OP_HOLD_MODE
#define HCI_OP_LINK_POLICY_CMDS_LAST       HCI_OP_SNIFF_SUB_RATE



/* Commands of HCI_GRP_HOST_CONT_BB_CMDS */
#define HCI_OP_SET_EVENT_MASK              (0x0001 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_RESET                       (0x0003 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SET_EVENT_FILTER            (0x0005 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_FLUSH                       (0x0008 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PIN_TYPE               (0x0009 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PIN_TYPE              (0x000A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_CREATE_NEW_UNIT_KEY         (0x000B | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_STORED_LINK_KEY        (0x000D | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_STORED_LINK_KEY       (0x0011 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_DELETE_STORED_LINK_KEY      (0x0012 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_CHANGE_LOCAL_NAME           (0x0013 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_LOCAL_NAME             (0x0014 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_CONN_ACCEPT_TOUT       (0x0015 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_CONN_ACCEPT_TOUT      (0x0016 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PAGE_TOUT              (0x0017 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PAGE_TOUT             (0x0018 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_SCAN_ENABLE            (0x0019 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_SCAN_ENABLE           (0x001A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PAGESCAN_CFG           (0x001B | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PAGESCAN_CFG          (0x001C | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_INQUIRYSCAN_CFG        (0x001D | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_INQUIRYSCAN_CFG       (0x001E | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_AUTHENTICATION_ENABLE  (0x001F | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_AUTHENTICATION_ENABLE (0x0020 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_ENCRYPTION_MODE        (0x0021 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_ENCRYPTION_MODE       (0x0022 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_CLASS_OF_DEVICE        (0x0023 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_CLASS_OF_DEVICE       (0x0024 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_VOICE_SETTINGS         (0x0025 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_VOICE_SETTINGS        (0x0026 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_AUTO_FLUSH_TOUT        (0x0027 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_AUTO_FLUSH_TOUT       (0x0028 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_NUM_BCAST_REXMITS      (0x0029 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_NUM_BCAST_REXMITS     (0x002A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_HOLD_MODE_ACTIVITY     (0x002B | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_HOLD_MODE_ACTIVITY    (0x002C | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_TRANSMIT_POWER_LEVEL   (0x002D | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_SCO_FLOW_CTRL_ENABLE   (0x002E | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_SCO_FLOW_CTRL_ENABLE  (0x002F | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SET_HC_TO_HOST_FLOW_CTRL    (0x0031 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_HOST_BUFFER_SIZE            (0x0033 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_HOST_NUM_PACKETS_DONE       (0x0035 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_LINK_SUPER_TOUT        (0x0036 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_LINK_SUPER_TOUT       (0x0037 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_NUM_SUPPORTED_IAC      (0x0038 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_CURRENT_IAC_LAP        (0x0039 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_CURRENT_IAC_LAP       (0x003A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PAGESCAN_PERIOD_MODE   (0x003B | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PAGESCAN_PERIOD_MODE  (0x003C | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PAGESCAN_MODE          (0x003D | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PAGESCAN_MODE         (0x003E | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SET_AFH_CHANNELS            (0x003F | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_INQSCAN_TYPE           (0x0042 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_INQSCAN_TYPE          (0x0043 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_INQUIRY_MODE           (0x0044 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_INQUIRY_MODE          (0x0045 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_PAGESCAN_TYPE          (0x0046 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_PAGESCAN_TYPE         (0x0047 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_AFH_ASSESSMENT_MODE    (0x0048 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_AFH_ASSESSMENT_MODE   (0x0049 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_EXT_INQ_RESPONSE       (0x0051 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_EXT_INQ_RESPONSE      (0x0052 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_REFRESH_ENCRYPTION_KEY      (0x0053 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_SIMPLE_PAIRING_MODE    (0x0055 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_SIMPLE_PAIRING_MODE   (0x0056 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_LOCAL_OOB_DATA         (0x0057 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_INQ_TX_POWER_LEVEL     (0x0058 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_INQ_TX_POWER_LEVEL    (0x0059 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_ERRONEOUS_DATA_RPT     (0x005A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_ERRONEOUS_DATA_RPT    (0x005B | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_ENHANCED_FLUSH              (0x005F | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SEND_KEYPRESS_NOTIF         (0x0060 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_LOGICAL_LINK_ACCEPT_TIMEOUT  (0x0061 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT (0x0062 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SET_EVENT_MASK_PAGE_2             (0x0063 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_LOCATION_DATA                (0x0064 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_LOCATION_DATA               (0x0065 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_FLOW_CONTROL_MODE            (0x0066 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_FLOW_CONTROL_MODE           (0x0067 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_READ_BE_FLUSH_TOUT                (0x0069 | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_BE_FLUSH_TOUT               (0x006A | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_SHORT_RANGE_MODE                  (0x006B | HCI_GRP_HOST_CONT_BB_CMDS) /* 802.11 only */
#define HCI_OP_READ_LE_SUPPORT 						(0x006C | HCI_GRP_HOST_CONT_BB_CMDS)
#define HCI_OP_WRITE_LE_SUPPORT 						(0x006D | HCI_GRP_HOST_CONT_BB_CMDS)

#define HCI_OP_CONT_BASEBAND_CMDS_FIRST    HCI_OP_SET_EVENT_MASK
#define HCI_OP_CONT_BASEBAND_CMDS_LAST     HCI_OP_WRITE_LE_SUPPORT


/* Commands of HCI_GRP_INFO_PARA group */
#define HCI_OP_READ_LOCAL_VERSION_INFO     (0x0001 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_LOCAL_SUPPORTED_CMDS   (0x0002 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_LOCAL_FEATURES         (0x0003 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_LOCAL_EXT_FEATURES     (0x0004 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_BUFFER_SIZE            (0x0005 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_COUNTRY_CODE           (0x0007 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_BD_ADDR                (0x0009 | HCI_GRP_INFO_PARA)
#define HCI_OP_READ_DATA_BLOCK_SIZE        (0x000A | HCI_GRP_INFO_PARA)
#define HCI_OP_INFORMATIONAL_CMDS_FIRST    HCI_OP_READ_LOCAL_VERSION_INFO
#define HCI_OP_INFORMATIONAL_CMDS_LAST     HCI_OP_READ_BD_ADDR


/* Commands of HCI_GRP_STATUS_PARA group */
#define HCI_OP_READ_FAILED_CONTACT_COUNT   (0x0001 | HCI_GRP_STATUS_PARA)
#define HCI_OP_RESET_FAILED_CONTACT_COUNT  (0x0002 | HCI_GRP_STATUS_PARA)
#define HCI_OP_GET_LINK_QUALITY            (0x0003 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_RSSI                   (0x0005 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_AFH_CH_MAP             (0x0006 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_CLOCK                  (0x0007 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_ENCR_KEY_SIZE          (0x0008 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_LOCAL_AMP_INFO         (0x0009 | HCI_GRP_STATUS_PARA)
#define HCI_OP_READ_LOCAL_AMP_ASSOC        (0x000A | HCI_GRP_STATUS_PARA)
#define HCI_OP_WRITE_REMOTE_AMP_ASSOC      (0x000B | HCI_GRP_STATUS_PARA)
#define HCI_OP_STATUS_PARAMS_CMDS_FIRST    HCI_OP_READ_FAILED_CONTACT_COUNT
#define HCI_OP_STATUS_PARAMS_CMDS_LAST     HCI_OP_WRITE_REMOTE_AMP_ASSOC


/* Commands of HCI_GRP_TEST_CMDS group */
#define HCI_OP_READ_LOOPBACK_MODE          (0x0001 | HCI_GRP_TEST_CMDS)
#define HCI_OP_WRITE_LOOPBACK_MODE         (0x0002 | HCI_GRP_TEST_CMDS)
#define HCI_OP_ENABLE_DEV_UNDER_TEST_MODE  (0x0003 | HCI_GRP_TEST_CMDS)
#define HCI_OP_WRITE_SIMP_PAIR_DEBUG_MODE  (0x0004 | HCI_GRP_TEST_CMDS)
#define HCI_OP_ENABLE_AMP_RCVR_REPORTS     (0x0007 | HCI_GRP_TEST_CMDS)
#define HCI_OP_AMP_TEST_END                (0x0008 | HCI_GRP_TEST_CMDS)
#define HCI_OP_AMP_TEST                    (0x0009 | HCI_GRP_TEST_CMDS)
#define HCI_OP_TESTING_CMDS_FIRST          HCI_OP_READ_LOOPBACK_MODE
#define HCI_OP_TESTING_CMDS_LAST           HCI_OP_AMP_TEST

/* Commands of BLE Controller setup and configuration */
#define HCI_OP_BLE_SET_EVENT_MASK          (0x0001 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_BUFFER_SIZE        (0x0002 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_LOCAL_SPT_FEAT     (0x0003 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_LOCAL_SPT_FEAT    (0x0004 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_RANDOM_ADDR       (0x0005 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_ADV_PARAMS        (0x0006 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_ADV_CHNL_TX_POWER  (0x0007 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_ADV_DATA          (0x0008 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_SCAN_RSP_DATA     (0x0009 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_ADV_ENABLE        (0x000A | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_SCAN_PARAMS       (0x000B | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_WRITE_SCAN_ENABLE       (0x000C | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_CREATE_LL_CONN          (0x000D | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_CREATE_CONN_CANCEL      (0x000E | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_WHITE_LIST_SIZE    (0x000F | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_CLEAR_WHITE_LIST        (0x0010 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_ADD_WHITE_LIST          (0x0011 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_REMOVE_WHITE_LIST       (0x0012 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_UPD_LL_CONN_PARAMS      (0x0013 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_SET_HOST_CHNL_CLASS     (0x0014 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_CHNL_MAP           (0x0015 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_REMOTE_FEAT        (0x0016 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_ENCRYPT                 (0x0017 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_RAND                    (0x0018 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_START_ENC               (0x0019 | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_LTK_REQ_REPLY           (0x001A | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_LTK_REQ_NEG_REPLY       (0x001B | HCI_GRP_BLE_CMDS)
#define HCI_OP_BLE_READ_SUPPORTED_STATES   (0x001C | HCI_GRP_BLE_CMDS)

/*--------------------*/

/*------单独将OCF以及OGF值列出来------- 275-454*/
/* Opcode Group Field (OGF) values */
#define HCI_NOP_OGF 0x00
#define HCI_LINK_CONTROL 0x01   /* Link Control Commands */
#define HCI_LINK_POLICY 0x02    /* Link Policy Commands */
#define HCI_HOST_C_N_BB 0x03    /* Host Controller & Baseband Commands */
#define HCI_INFO_PARAM 0x04     /* Informational Parameters */
#define HCI_STATUS_PARAM 0x05   /* Status Parameters */
#define HCI_TESTING 0x06        /* Testing Commands */
#define HCI_LE	0x08 /* BLE cmd */
#define HCI_VENDOR_OGF 0x3F /* vendor Commands */

/* Opcode Command Field (OCF) values */
#define HCI_NOP_OCF 0x00
/* Link control commands */
#define HCI_INQUIRY 0x01
#define HCI_INQUIRY_CANCEL 0x02
#define HCI_PERIODIC_INQUIRY 0x03
#define HCI_EXIT_PERIODIC_INQUIRY 0x04
#define HCI_CREATE_CONNECTION 0x05
#define HCI_DISCONNECT 0x06
#define HCI_CREATE_CON_CANCEL 0x08
#define HCI_ACCEPT_CONNECTION_REQUEST 0x09
#define HCI_REJECT_CONNECTION_REQUEST 0x0A
#define HCI_LINKKEY_REQ_REPLY 0x0B
#define HCI_LINKKEY_REQ_NEG_REPLY 0x0C
#define HCI_PIN_CODE_REQ_REP 0x0D
#define HCI_PIN_CODE_REQ_NEG_REP 0x0E
#define HCI_CHANGE_CON_PTYPE 0x0F
#define HCI_AUTH_REQ 0x11
#define HCI_SET_CONN_ENCRYPT 0x13
#define HCI_CHANGE_CON_LINKKEY 0x15
#define HCI_MASTER_LINKKEY 0x17
#define HCI_REMOTE_NAME_REQ 0x19
#define HCI_REMOTE_NAME_REQ_CANCEL 0x1A
#define HCI_READ_REMOTE_SUPPORT_FEATURE 0x1B
#define HCI_READ_REMOTE_EXR_FEATURE 0x1C
#define HCI_READ_REMOTE_VER_INFO 0x1D
#define HCI_READ_CLK_OFFSET 0x1F
#define HCI_READ_LMP_HANDLE 0x20
#define HCI_CONNECT_SCO_REQ 0x28
#define HCI_ACCEP_SCO_REQ 0x29
#define HCI_REJECT_SCO_REQ 0x2A
#define HCI_IOCAP_REQ_REPLY 0x2B
#define HCI_USER_CONFIRM_REQ_REPLY 0x2C
#define HCI_USER_CONFIRM_REQ_NEG_REPLY 0x2D
#define HCI_USER_PASSKEY_REQ_REPLY 0x2E
#define HCI_USER_PASSKEY_REQ_NEG_REPLY 0x2F
#define HCI_REMOTE_OOB_REQ_REPLY 0x30
#define HCI_REMOTE_OOB_REQ_NEG_REPLY 0x33
#define HCI_IOCAP_REQ_NEG_REPLY 0x34

/* Link Policy commands */
#define HCI_HOLD_MODE 0x01
#define HCI_SNIFF_MODE 0x03
#define HCI_EXIT_SNIFF_MODE 0x04
#define HCI_PARK_MODE 0x05
#define HCI_EXIT_PARK_MODE 0x06
#define HCI_QOS_SETUP 0x07
#define HCI_ROLE_DISCOVERY 0x09
#define HCI_SWITCH_ROLE 0x0B
#define HCI_R_LINK_POLICY 0x0C
#define HCI_W_LINK_POLICY 0x0D
#define HCI_R_DEFAULT_LINK_POLICY 0x0E
#define HCI_W_DEFAULT_LINK_POLICY 0x0F
#define HCI_FLOW_SPEC 0x10
#define HCI_SNIFF_SUBRATING 0x11

/* Host-Controller and Baseband Commands */
#define HCI_SET_EVENT_MASK 0x01
#define HCI_RESET 0x03
#define HCI_SET_EVENT_FILTER 0x05
#define HCI_FLUSH 0x08
#define HCI_READ_PINCODE_TYPE 0x09
#define HCI_WRITE_PINCODE_TYPE 0x0A
#define HCI_READ_STORED_LINK_KEY 0x0D
#define HCI_WRITE_STORED_LINK_KEY 0x11
#define HCI_DELETE_STORED_LINK_KEY 0x12
#define HCI_WRITE_LOCAL_NAME 0x13
#define HCI_READ_LOCAL_NAME 0x14
#define HCI_READ_CONN_ACCEPT_TIMEOUT 0x15
#define HCI_WRITE_CONN_ACCEPT_TIMEOUT 0x16
#define HCI_READ_PAGE_TIMEOUT 0x17
#define HCI_WRITE_PAGE_TIMEOUT 0x18
#define HCI_READ_SCAN_ENABLE 0x19
#define HCI_WRITE_SCAN_ENABLE 0x1A
#define HCI_READ_PAGE_SCAN_ACTIVITY 0x1B
#define HCI_WRITE_PAGE_SCAN_ACTIVITY 0x1C
#define HCI_READ_INQ_SCAN_ACTIVITY 0x1D
#define HCI_WRITE_INQ_SCAN_ACTIVITY 0x1E
#define HCI_READ_AUTH_ENABLE 0x1F
#define HCI_WRITE_AUTH_ENABLE 0x20
#define HCI_READ_COD 0x23
#define HCI_WRITE_COD 0x24
#define HCI_READ_VOICE_SETTING 0x25
#define HCI_WRITE_VOICE_SETTING 0x26
#define HCI_READ_AUTO_FLUSH_TIMEOUT 0x27
#define HCI_WRITE_FLUSHTO 0x28
#define HCI_READ_NUM_BROADCAST_RETRA 0x29
#define HCI_WRITE_NUM_BROADCAST_RETRA 0x2A
#define HCI_READ_HOLD_MODE_ACTIVITY 0x2B
#define HCI_WRITE_HOLD_MODE_ACTIVITY 0x2C
#define HCI_READ_TRANSMIT_POWER_LEVEL 0x2D
#define HCI_READ_SCO_FLOW_CONTROL_ENABLE 0x2E
#define HCI_WRITE_SCO_FLOW_CONTROL_ENABLE 0x2F
#define HCI_SET_HC_TO_H_FC 0x31
#define HCI_HOST_BUFF_SIZE 0x33
#define HCI_HOST_NUM_COMPL 0x35
#define HCI_READ_LINK_SUPERVISION_TIMEOUT 0x36
#define HCI_WRITE_LINK_SUPERVISION_TIMEOUT 0x37
#define HCI_READ_NUM_SUPPORT_IAC 0x38
#define HCI_READ_CURRENT_IAC 0x39
#define HCI_WRITE_CURRENT_IAC 0x3A
#define HCI_SET_AFG_HOST_CHANNEL_CLASSIFICATION 0x3F
#define HCI_READ_INQUIRY_SCAN_TYPE 0x42
#define HCI_WRITE_INQUIRY_SCAN_TYPE 0x43
#define HCI_READ_INQUIRY_MODE 0x44
#define HCI_WRITE_INQUIRY_MODE 0x45
#define HCI_READ_PAGE_SCAN_MODE 0x46
#define HCI_WRITE_PAGE_SCAN_MODE 0x47
#define HCI_READ_EIR 0x51
#define HCI_WRITE_EIR 0x52
#define HCI_WRITE_SSP_MODE 0x56
#define HCI_READ_LE_SUPPORT 0x6C
#define HCI_WRITE_LE_SUPPORT 0x6D

/* Informational Parameters */
#define HCI_READ_LOCAL_VER_INFO 0x01
#define HCI_READ_LOCAL_SUPPORT_CMD 0x02
#define HCI_RAED_LOCAL_SUPPORT_FEATURE 0x03
#define HCI_READ_LOCAL_EXTEND_FEATURE 0x04
#define HCI_READ_BUFFER_SIZE 0x05
#define HCI_READ_BD_ADDR 0x09
#define HCI_READ_DATA_BLOCK_SIZE  0x0A

/* Status Parameters */
#define HCI_READ_FAILED_CONTACT_COUNTER 0x01
#define HCI_RESET_FAILED_CONTACT_COUNTER 0x02
#define HCI_READ_LINK_QUALITY 0x03
#define HCI_READ_RSSI 0x05
#define HCI_READ_AFH_CHANNEL_MAP 0x06
#define HCI_READ_CLOCK 0x07
#define HCI_READ_ENC_KEY_SIZE 0x08
#define HCI_READ_LOCAL_AMP_INFO 0x09
#define HCI_READ_LOCAL_AMP_ASSOC 0x0A
#define HCI_WRITE_LOCAL_AMP_ASSOC 0x0B

/* Testing commands */
#define HCI_READ_LOOPBACK_MODE 0x01
#define HCI_WRITE_LOOPBACK_MODE 0x02
#define HCI_ENABLE_DUT_MODE 0x03
#define HCI_WRITE_SP_DEBUG_MODE 0x04
#define HCI_ENABLE_AMP_REV_REPORT 0x07
#define HCI_AMP_TEST_END 0x08
#define HCI_AMP_TEST 0x09
#define HCI_WRITE_S_CONN_TEST_MODE 0x0A


/* LE commands */
#define HCI_LE_SET_EVT_MASK 0x01
#define HCI_LE_READ_BUF_SIZE 0x02
#define HCI_LE_READ_LOCAL_SUPPORT_FEATURES 0x03
#define HCI_LE_SET_RANDOM_ADDRESS 0x05
#define HCI_LE_SET_ADV_PARAM 0x06
#define HCI_LE_SET_ADV_DATA 0x08
#define HCI_LE_SET_ADV_ENABLE 0x0a
#define HCI_LE_SET_SCAN_PARAM 0x0b
#define HCI_LE_SET_SCAN 0x0c
#define HCI_LE_CREATE_CON 0x0d
#define HCI_LE_LTK_REQ_REPLY 0x1a
#define HCI_LE_LTK_REQ_NEG_REPLAY 0x1b
#define HCI_LE_READ_LOCAL_P256_PUBLIC_KEY 0x25
#define HCI_LE_GENERATE_DHKEY 0x26
#define HCI_LE_ADD_DEV_RESOLVING_LIST 0x27
#define HCI_LE_RM_DEV_RESOLVING_LIST 0x28
#define HCI_LE_CLEAR_RESOLVING_LIST 0x29
#define HCI_LE_READ_RESOLV_SIZE 0x2a
#define HCI_LE_READ_PEER_RESOLV_ADDR 0x2b
#define HCI_LE_READ_LOCAL_RESOLV_ADDR 0x2c
#define HCI_LE_SET_ADDR_RESOLUTION_ENABLE 0x2d

/*--------------------*/


/*-----事件码EVENT CODE----- 460-513 */
/* Possible event codes */
#define HCI_INQUIRY_COMPLETE 0x01
#define HCI_INQUIRY_RESULT 0x02
#define HCI_CONNECTION_COMPLETE 0x03
#define HCI_CONNECTION_REQUEST 0x04
#define HCI_DISCONNECTION_COMPLETE 0x05
#define HCI_AUTH_COMPLETE 0x06
#define HCI_GET_REMOTE_NAME_COMPLETE 0x07
#define HCI_ENCRYPTION_CHANGE 0x08
#define HCI_CHANGE_CONN_LINKKEY_COMPLETE 0x09
#define HCI_MASTER_LINKKEY_COMPLETE 0x0A
#define HCI_READ_REMOTE_SUPPORT_FEATURE_COMPLETE 0x0B
#define HCI_READ_REMOTE_VERSION_INFO_COMPLETE 0x0C
#define HCI_QOS_SETUP_COMPLETE 0x0D
#define HCI_COMMAND_COMPLETE 0x0E
#define HCI_COMMAND_STATUS 0x0F
#define HCI_HARDWARE_ERROR 0x10
#define HCI_FLUSH_OCCURRED 0x11
#define HCI_ROLE_CHANGE 0x12
#define HCI_NBR_OF_COMPLETED_PACKETS 0x13
#define HCI_MODE_CHANGE 0x14
#define HCI_RETURN_LINKKEY 0x15
#define HCI_PIN_CODE_REQUEST 0x16
#define HCI_LINK_KEY_REQUEST 0x17
#define HCI_LINK_KEY_NOTIFICATION 0x18
#define HCI_LOOPBACK 0x19
#define HCI_DATA_BUFFER_OVERFLOW 0x1A
#define HCI_MAX_SLOTS_CHANGE 0x1B
#define HCI_READ_CLK_OFFSET_COMPLETE 0x1C
#define HCI_CONN_PACK_TYPE_CHANGE 0x1D
#define HCI_QOS_VIOLATION 0x1E
#define HCI_PS_REPETITION_MODE_CHANGE 0x20
#define HCI_FLOW_SPEC_COMPLETE 0x21
#define HCI_INQ_RESULT_RSSI 0x22
#define HCI_READ_REMOTE_EXT_FEATURE_COMPLETE 0x23
#define HCI_SCO_CONNECTION_COMPLETE 0x2C
#define HCI_SCO_CONN_CHANGE 0x2D
#define HCI_EVT_SNIFF_SUBRATING 0x2E
#define HCI_EXT_INQ_RESULT 0x2F
#define HCI_ENC_KEY_REFRESH_COMPLETE 0x30
#define HCI_IO_CAP_REQ 0x31
#define HCI_IO_CAP_RESPONSE 0x32
#define HCI_USER_CONF_REQ 0x33
#define HCI_USER_PASSKEY_REQ 0x34
#define HCI_REMOTE_OOB_DATA_REQ 0x35
#define HCI_SP_COMPLETE 0x36
#define HCI_LINK_SUPERVISION_TIMEOUT_CHANGE 0x38
#define HCI_EFLUSH_COMPLETE 0x39
#define HCI_USER_PASSKEY_NOTIFICATION 0x3B
#define HCI_KEYPRESS_NOTIFICATION 0x3C
#define HCI_REMOTE_HOST_SUPPORT_FEATURE_NOTIFICATION 0x3D
#define HCI_LE_META 0x3E
#define HCI_VENDOR_SPEC 0xFF
/*--------------------*/


/*-----错误码ERROR CODE----- 514-559 */
/* Success code */
#define HCI_SUCCESS 0x00
/* Possible error codes */
#define HCI_UNKNOWN_HCI_COMMAND 0x01
#define HCI_NO_CONNECTION 0x02
#define HCI_HW_FAILURE 0x03
#define HCI_PAGE_TIMEOUT 0x04
#define HCI_AUTHENTICATION_FAILURE 0x05
#define HCI_KEY_MISSING 0x06
#define HCI_MEMORY_FULL 0x07
#define HCI_CONN_TIMEOUT 0x08
#define HCI_MAX_NUMBER_OF_CONNECTIONS 0x09
#define HCI_MAX_NUMBER_OF_SCO_CONNECTIONS_TO_DEVICE 0x0A
#define HCI_ACL_CONNECTION_EXISTS 0x0B
#define HCI_COMMAND_DISSALLOWED 0x0C
#define HCI_HOST_REJECTED_DUE_TO_LIMITED_RESOURCES 0x0D
#define HCI_HOST_REJECTED_DUE_TO_SECURITY_REASONS 0x0E
#define HCI_HOST_REJECTED_DUE_TO_REMOTE_DEVICE_ONLY_PERSONAL_SERVICE 0x0F
#define HCI_HOST_TIMEOUT 0x10
#define HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE 0x11
#define HCI_INVALID_HCI_COMMAND_PARAMETERS 0x12
#define HCI_OTHER_END_TERMINATED_CONN_USER_ENDED 0x13
#define HCI_OTHER_END_TERMINATED_CONN_LOW_RESOURCES 0x14
#define HCI_OTHER_END_TERMINATED_CONN_ABOUT_TO_POWER_OFF 0x15
#define HCI_CONN_TERMINATED_BY_LOCAL_HOST 0x16
#define HCI_REPETED_ATTEMPTS 0x17
#define HCI_PAIRING_NOT_ALLOWED 0x18
#define HCI_UNKNOWN_LMP_PDU 0x19
#define HCI_UNSUPPORTED_REMOTE_FEATURE 0x1A
#define HCI_SCO_OFFSET_REJECTED 0x1B
#define HCI_SCO_INTERVAL_REJECTED 0x1C
#define HCI_SCO_AIR_MODE_REJECTED 0x1D
#define HCI_INVALID_LMP_PARAMETERS 0x1E
#define HCI_UNSPECIFIED_ERROR 0x1F
#define HCI_UNSUPPORTED_LMP_PARAMETER_VALUE 0x20
#define HCI_ROLE_CHANGE_NOT_ALLOWED 0x21
#define HCI_LMP_RESPONSE_TIMEOUT 0x22
#define HCI_LMP_ERROR_TRANSACTION_COLLISION 0x23
#define HCI_LMP_PDU_NOT_ALLOWED 0x24
#define HCI_ENCRYPTION_MODE_NOT_ACCEPTABLE 0x25
#define HCI_UNIT_KEY_USED 0x26
#define HCI_QOS_NOT_SUPPORTED 0x27
#define HCI_INSTANT_PASSED 0x28
#define HCI_PAIRING_UNIT_KEY_NOT_SUPPORTED 0x29

/*---固定长度参数定义---*/
/* Specification specific parameters */
#define HCI_BD_ADDR_LEN 6           //蓝牙地址长度
#define HCI_LMP_FEATURES_LEN 8      //LMP功能长度,每一位代表一种功能特性是否支持
#define HCI_LINK_KEY_LEN 16         //链接密钥长度，密钥是配对成功后双方协商出来的，用于后续加密和认证
#define HCI_LMP_FEAT_LEN 8          //LMP功能长度

/*-----命令长度定义-----*/
/* Command packet length (including ACL header)*/
#define HCI_INQUIRY_PLEN 8
#define HCI_CANCEL_INQUIRY_PLEN 3
#define HCI_PERIDIC_INQUIRY 12
#define HCI_CANCEL_PERIDIC_INQYIRY 3
#define HCI_CREATE_CONN_PLEN 16
#define HCI_DISCONN_PLEN 6
#define HCI_ACCEPT_CON_REQ_PLEN 10
#define HCI_REJECT_CONN_REQ_PLEN 10
#define HCI_LINK_KEY_REQ_REPLAY_PLEN 25
#define HCI_LINK_KEY_REQ_NG_REPLAY_PLEN 9
#define HCI_PIN_CODE_REQ_REP_PLEN 26
#define HCI_PIN_CODE_REQ_NEG_REP_PLEN 9
#define HCI_GET_REMOTE_NAME_PLEN 13
#define HCI_CANCEL_GET_REMOTE_NAME_PLEN 9
#define HCI_GET_REMOTE_FEATURE_PLEN 5
#define HCI_CONNECT_SCO_REQ_PLEN 20
#define HCI_ACCEPT_SCO_REQ_PLEN 24
#define HCI_REJECT_SCO_REQ_PLEN 10
#define HCI_IOCAP_REQ_REPLY_PLEN 12
#define HCI_USER_CONFIRM_REQ_REPLY_PLEN 9
#define HCI_USER_CONFIRM_REQ_NEG_REPLY_PLEN 9
#define HCI_USER_PASSKEY_REQ_REPLY_PLEN 13
#define HCI_USER_PASSKEY_REQ_NEG_REPLY_PLEN 9
#define HCI_IOCAP_REQ_NEG_REPLY_PLEN 10
#define HCI_SET_CONN_ENCRYPT_PLEN 6
#define HCI_WRITE_STORED_LINK_KEY_PLEN 26
#define HCI_CHANGE_LOCAL_NAME_PLEN 251
#define HCI_READ_LOCAL_NAME_PLEN 3
#define HCI_SET_EV_MASK_PLEN 11
#define HCI_HOLD_PLEN 9
#define HCI_SNIFF_PLEN 13
#define HCI_EXIT_SNIFF_PLEN 5
#define HCI_ROLE_DISCOVERY_PLEN 5
#define HCI_SWITCH_ROLE_PLEN 10
#define HCI_R_LINK_POLICY_PLEN 5
#define HCI_W_LINK_POLICY_PLEN 7
#define HCI_R_DEFAULT_LINK_POLICY_PLEN 3
#define HCI_W_DEFAULT_LINK_POLICY_PLEN 5
#define HCI_SET_EVENT_MASK_PLEN 11
#define HCI_RESET_PLEN 3
#define HCI_SET_EV_FILTER_PLEN 5
#define HCI_W_PAGE_TIMEOUT_PLEN 5
#define HCI_W_SCAN_EN_PLEN 4
#define HCI_R_COD_PLEN 3
#define HCI_W_COD_PLEN 6
#define HCI_R_VOICE_SETTING_PLEN 3
#define HCI_W_VOICE_SETTING_PLEN 5
#define HCI_W_FLUSHTO_PLEN 6
#define HCI_SET_HC_TO_H_FC_PLEN 4
#define HCI_W_INQUIRY_MODE_LEN 4
#define HCI_WRITE_EIR_PLEN 244
#define HCI_WRITE_SSP_MODE_PLEN 4
#define HCI_WRITE_LE_SUPPORT_PLEN 5
#define HCI_H_BUF_SIZE_PLEN 6
#define HCI_H_NUM_COMPL_PLEN 8
#define HCI_R_LOCOL_VER_INFO_PLEN 3
#define HCI_R_LOCAL_SUPPORT_CMD_PLEN 3
#define HCI_R_BUF_SIZE_PLEN 3
#define HCI_R_BD_ADDR_PLEN 3
#define HCI_READ_RSSI_PLEN 5
#define HCI_ENABLE_DUT_PLEN 3
#define HCI_SET_LE_EVENT_MASK_PLEN 11
#define HCI_LE_R_BUF_SIZE_PLEN 3
#define HCI_LE_R_LOCAL_SUPPORT_FEATURES_PLEN 3
#define HCI_LE_SET_RANDOM_ADDR_PLEN 9
#define HCI_SET_LE_SCAN_PLEN 5
#define HCI_SET_LE_SCAN_PARAM_PLEN 10
#define HCI_SET_LE_ADV_PARAM_PLEN 18
#define HCI_SET_LE_ADV_DATA_PLEN 35
#define HCI_SET_LE_ADV_ENABLE_PLEN 4
#define HCI_LTK_REQ_REPLY_PLEN 21
#define HCI_LTK_REQ_NEG_REPLAY_PLEN 5
#define HCI_READ_LOCAL_P256_PUBLIC_KEY_PLEN 3
#define HCI_GENERATE_DHKEY_PLEN 67
#define HCI_ADD_DEV_RESOLV_LIST_PLEN 42
#define HCI_RM_DEV_RESOLV_LIST_PLEN 10
#define HCI_CLEAR_RESOLV_LIST_PLEN 3
#define HCI_READ_RESOLV_LIST_PLEN 3
#define HCI_READ_PEER_RESOLV_ADDR_PLEN 10
#define HCI_READ_LOCAL_RESOLV_ADDR_PLEN 10
#define HCI_SET_ADDR_RESOLUTION_ENABLE_PLEN 4
#define HCI_WRITE_IAC_LAP_HDR_PLEN 3

/*----过滤某些设备的查询结果或者连接请求上报----*/
/* Set Event Filter params */
#define HCI_SET_EV_FILTER_CLEAR 0       //过滤类型  清除所有过滤条件
#define HCI_SET_EV_FILTER_INQUIRY 1     //询查结果过滤
#define HCI_SET_EV_FILTER_CONNECTION 2      //连接请求过滤

#define HCI_SET_EV_FILTER_ALLDEV 0     //过滤条件     不过滤所有设备
#define HCI_SET_EV_FILTER_COD 1         //根据设备类过滤
#define HCI_SET_EV_FILTER_BDADDR 2      //根据蓝牙地址过滤

#define HCI_SET_EV_FILTER_AUTOACC_OFF 1     //自动接受连接请求的设置（仅connection类型用） 关闭
#define HCI_SET_EV_FILTER_AUTOACC_NOROLESW 2    //不进行角色切换自动接受连接请求
#define HCI_SET_EV_FILTER_AUTOACC_ROLESW 3      //进行角色切换自动接受连接请求

/*---配置可发现性和可连接性参数---*/
/* Write Scan Enable params */
#define HCI_SCAN_EN_INQUIRY 1       //开启查询扫描，可发现性
#define HCI_SCAN_EN_PAGE 2          //开启页扫描，可连接性

/*---主机到控制器流控快捷控制宏---*/
#define hci_num_cmd(pcb) ((pcb)->numcmd)
#define hci_num_acl(pcb) ((pcb)->controller_num_acl)
#define hci_maxsize(pcb) ((pcb)->acl_maxsize)

/*---蓝牙的交互能力描述---*/    //便于配对时进行配对方法选择
#define IO_CAP_DISPLAY_ONLY 0x00
#define IO_CAP_DISPLAY_YES_NO 0x01
#define IO_CAP_KEYBOARD_ONLY 0x02
#define IO_CAP_NOINPUT_OUTPUT 0x03

/*---查询模式描述符---*/
#define INQUIRY_MODE_STANDARD 0
#define INQUIRY_MODE_RSSI 1
#define INQUIRY_MODE_EIR 2

/*---查询接入码---*/        //用于Inquiry命令的访问码参数
#define INQUIRY_GIAC 0x9e8b33
#define INQUIRY_LIAC 0x9e8b00
//GIAC（General）：日常用的就是这个，扫描周围所有蓝牙设备
//LIAC（Limited）：只扫描那些临时开启可发现的设备（比如用户刚按下配对键的设备）


/*---安全简单配对认证请求---*/
// Numeric comparison with automatic accept allowed.
#define SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_NO_BONDING 0x00

// Use IO Capabilities to deter- mine authentication procedure
#define SSP_IO_AUTHREQ_MITM_PROTECTION_REQUIRED_NO_BONDING 0x01

// Numeric compar- ison with automatic accept allowed.
#define SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING 0x02

// Use IO Capabilities to determine authentication procedure
#define SSP_IO_AUTHREQ_MITM_PROTECTION_REQUIRED_DEDICATED_BONDING 0x03

// Numeric Compari- son with automatic accept allowed.
#define SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING 0x04

// Use IO capabilities to determine authentication procedure.
#define SSP_IO_AUTHREQ_MITM_PROTECTION_REQUIRED_GENERAL_BONDING 0x05
/*
  维度1：是否需要 MITM 保护（中间人攻击防护）
  ┌──────────────┬───────────────────────────────────────────────────────┐
  │     选项     │                         含义                          │
  ├──────────────┼───────────────────────────────────────────────────────┤
  │ NOT_REQUIRED │ 不需要，允许 Just Works（自动配对，安全性低）         │
  ├──────────────┼───────────────────────────────────────────────────────┤
  │ REQUIRED     │ 需要，强制走有用户交互的配对方式（数字比对/密码输入） │
  └──────────────┴───────────────────────────────────────────────────────┘
  维度2：绑定类型（Bonding）
  ┌───────────────────┬────────────────────────────────┐
  │       选项        │              含义              │
  ├───────────────────┼────────────────────────────────┤
  │ NO_BONDING        │ 不保存配对信息，下次还得重新配 │
  ├───────────────────┼────────────────────────────────┤
  │ DEDICATED_BONDING │ 专门为了绑定而配对，配完就断开 │
  ├───────────────────┼────────────────────────────────┤
  │ GENERAL_BONDING   │ 绑定后保持连接继续用（最常见） │
  └───────────────────┴────────────────────────────────┘
  组合表：
  ┌──────┬────────┬──────────┬────────────────────┐
  │  值  │  MITM  │ Bonding  │      典型场景      │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x00 │ 不要求 │ 不绑定   │ 临时连一下         │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x01 │ 要求   │ 不绑定   │ 安全临时连接       │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x02 │ 不要求 │ 专用绑定 │ 快速配对存储       │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x03 │ 要求   │ 专用绑定 │ 安全配对存储       │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x04 │ 不要求 │ 通用绑定 │ 耳机自动配对       │
  ├──────┼────────┼──────────┼────────────────────┤
  │ 0x05 │ 要求   │ 通用绑定 │ 手机配对（最常用） │
  └──────┴────────┴──────────┴────────────────────┘
  一般设备用 0x05，既要安全（MITM保护）又要保存配对信息（通用绑定）。
*/


/*---蓝牙协议规范版本号---*/    //用于读取本地版本信息命令返回参数以及获取远程版本信息命令返回参数便于协商
#define HCI_PROTO_VERSION     0x01      /* Version for BT spec 1.1          */
#define HCI_PROTO_VERSION_1_2 0x02      /* Version for BT spec 1.2          */
#define HCI_PROTO_VERSION_2_0 0x03      /* Version for BT spec 2.0          */
#define HCI_PROTO_VERSION_2_1 0x04      /* Version for BT spec 2.1 [Lisbon] */
#define HCI_PROTO_VERSION_3_0 0x05      /* Version for BT spec 3.0          */
#define HCI_PROTO_VERSION_4_0 0x06      /* Version for BT spec 4.0          */
#define HCI_PROTO_VERSION_4_1 0x07      /* Version for BT spec 4.1          */
#define HCI_PROTO_VERSION_4_2 0x08      /* Version for BT spec 4.2          */
#define HCI_PROTO_VERSION_5_0 0x09      /* Version for BT spec 5.0          */
#define HCI_PROTO_VERSION_5_1 0x0a      /* Version for BT spec 5.1          */
#define HCI_PROTO_VERSION_5_2 0x0b      /* Version for BT spec 5.2          */
#define HCI_PROTO_VERSION_5_3 0x0c      /* Version for BT spec 5.3          */

/*---BLE相关的子事件码---*/
/* LE META SUBEVENT */
#define HCI_SUBEVENT_LE_CONN_COMPLETE                0x01
#define HCI_SUBEVENT_LE_ADV_REPORT                 0x02
#define HCI_SUBEVENT_LE_CONNE_UPDATE_COMPLETE         0x03
#define HCI_SUBEVENT_LE_READ_REMOTE_USED_FEATURES_COMPLETE 0x04
#define HCI_SUBEVENT_LE_LONG_TERM_KEY_REQUEST              0x05
#define HCI_SUBEVENT_LE_REMOTE_CONN_PARAM_REQUEST 0x06
#define HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE 0x07
#define HCI_SUBEVENT_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE 0x08
#define HCI_SUBEVENT_LE_GENERATE_DHKEY_COMPLETE            0x09
#define HCI_SUBEVENT_LE_ENHANCED_CONN_COMPLETE       0x0A
#define HCI_SUBEVENT_LE_DIRECT_AD_REPORT          0x0B

#pragma pack (1)    //告诉编译器按1字节对齐，取消字节对齐带来的空洞

typedef struct
{
	uint16_t opcode;
	uint8_t len;
}hci_cmd_hdr_t;     //命令头部结构体

typedef struct 
{
    uint8_t code; /* Event code */
    uint8_t len;  /* Parameter total length */
}hci_evt_hdr_t;     //事件头部结构体

typedef struct 
{
    uint16_t conhdl_pb_bc; /* Connection handle, packet boundary and broadcast flag flag */
    uint16_t len; /* length of data */
} hci_acl_hdr_t;        //ACL数据包头部结构体

#pragma pack () 

/*
  - #pragma pack(1) — 开启1字节对齐（紧凑排列，无填充）
  - #pragma pack() — 恢复默认对齐（通常是4字节或8字节，取决于平台/编译器）
  配对使用，防止编译器在结构体中插入填充字节，导致结构体大小与预期不符。之后恢复默认对齐方式
*/

/* BR/EDR的查询结果 */
typedef struct _hci_inq_res_t
{
    struct _hci_inq_res_t *next; /* For the linked list */

    struct bd_addr_t bdaddr; /* Bluetooth address of a device found in an inquiry */
    uint8_t cod[3]; /* Class of the remote device */
    uint8_t psrm; /* Page scan repetition mode */
    uint8_t psm; /* Page scan mode */       //目前未使用已经被废弃了有两个字段Page_Scan_Period_Mode和Page_Scan_Mode，现为保留位，此处只存
    uint16_t co; /* Clock offset */
    int8_t rssi;        //HCI_Inquiry_Result_with_RSSI命令获取，RSSI上报模式下获取
    uint8_t remote_name[HCI_REMOTE_NAME_LEN];   //HCI_Extended_Inquiry_Result命令获取，EIR模式下获取
}hci_inq_res_t;


/* BLE的查询结构体*/
typedef struct 
{
    uint8_t addr_type;
    struct bd_addr_t bdaddr; /* Bluetooth address of a device found in an inquiry */
    int8_t rssi;
    uint8_t adv_type;
    uint8_t adv_size;
    uint8_t adv_data[HCI_LE_ADV_MAX_SIZE];
}hci_le_inq_res_t;


/**
 * Connection State
 */
typedef enum
{
    SEND_CREATE_CONNECTION = 0,
    SEND_CANCEL_CONNECTION,
    RECEIVED_CONNECTION_REQUEST,
    ACCEPTED_CONNECTION_REQUEST,
    REJECTED_CONNECTION_REQUEST,
    OPEN,
    SEND_DISCONNECT,
    RECEIVED_DISCONNECTION_COMPLETE
} CONNECTION_STATE;

/* HCI LINK状态机
  ┌─────────────────────────────────┬─────┬──────────────────────────┐
  │              状态               │ 值  │           含义            │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ SEND_CREATE_CONNECTION          │ 0   │ 本机主动发起了连接请求     │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ SEND_CANCEL_CONNECTION          │ 1   │ 本机取消了正在进行的连接   │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ RECEIVED_CONNECTION_REQUEST     │ 2   │ 收到了对方的连接请求       │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ ACCEPTED_CONNECTION_REQUEST     │ 3   │ 已接受对方的连接请求       │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ REJECTED_CONNECTION_REQUEST     │ 4   │ 已拒绝对方的连接请求       │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ OPEN                            │ 5   │ 连接已建立，正常工作中     │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ SEND_DISCONNECT                 │ 6   │ 本机发起了断开请求         │
  ├─────────────────────────────────┼─────┼──────────────────────────┤
  │ RECEIVED_DISCONNECTION_COMPLETE │ 7   │ 断开完成                 │
  └─────────────────────────────────┴─────┴──────────────────────────┘
*/

/* HCI role */
#define HCI_ROLE_MASTER 0   //主设备
#define HCI_ROLE_SLAVE  1   //从设备

//链路类型
#define HCI_LINK_TYPE_SCO 0
#define HCI_LINK_TYPE_ACL 1
#define HCI_LINK_TYPE_ESCO 2

#define HCI_ADDR_LE_PUBLIC       0x00 /* Public Device Address */
#define HCI_ADDR_LE_RANDOM       0x01 /* Random Device Address */
#define HCI_ADDR_LE_PUBLIC_ID    0x02 /* Public Identity Address (Corresponds to Resolved Private Address) */
#define HCI_ADDR_LE_RANDOM_ID    0x03 /* Random (Static) Identity Address (Corresponds to Resolved PrivateAddress) */

/* 连接链路结构体 */
typedef struct _hci_link_t
{
    struct _hci_link_t *next; /* For the linked list */

    struct bd_addr_t bdaddr; /* The remote peers Bluetooth address for this connection */
    uint32_t cod;

    uint8_t role; /* role: 0 - master, 1 - slave */
    uint16_t conhdl; /* Connection handle */
    uint16_t sco_connhdl;
    CONNECTION_STATE state;
#if HCI_FLOW_QUEUEING   //流控排队
    struct bt_pbuf_t *p;
    uint16_t len;
    uint8_t pb;
#endif
}hci_link_t;

//总体HCI状态
typedef enum hci_init_status
{
    BLUETOOTH_INITING = 0,
    BLUETOOTH_WORKING = 1,
} hci_init_status_e;
//厂商相关初始化状态
typedef enum hci_vendor_init_status
{
    VENDOR_NOT_INIT = 0,
    VENDOR_INITED = 1,
} hci_vendor_init_status_e;


/*----回调函数-----*/
typedef err_t (* cmd_complete_fun_cb)(void *arg, uint16_t opcode, uint8_t result);
typedef err_t (* pin_req_fun_cb)(void *arg, struct bd_addr_t *bdaddr);
typedef err_t (* bt_working_fun_cb)(void *arg);
typedef err_t (* sco_conn_req_fun_cb)(void *arg, struct bd_addr_t *bdaddr);
typedef err_t (*sco_conn_complete_fun_cb)(void *arg, uint8_t status,struct bd_addr_t *bdaddr);
typedef err_t (*sco_disconn_complete_fun_cb)(void *arg, uint8_t status,struct bd_addr_t *bdaddr);
typedef err_t (*inq_result_fun_cb)(hci_inq_res_t *inqres);
typedef err_t (* inq_complete_fun_cb)(uint16_t result);
typedef err_t (*le_inq_result_fun_cb)(hci_le_inq_res_t *le_inqres);
typedef err_t (*le_inq_complete_fun_cb)(uint16_t result);
typedef err_t (*name_req_complete_fun_cb)(struct bd_addr_t *bdaddr,uint8_t * name);
typedef err_t (* rbd_complete_fun_cb)(void *arg, struct bd_addr_t *bdaddr);
typedef err_t (* link_key_not_fun_cb)(void *arg, struct bd_addr_t *bdaddr, uint8_t *key,uint8_t key_type);
typedef err_t (*link_key_req_fun_cb)(void *arg,struct bd_addr_t *bdaddr);
typedef err_t (* wlp_complete_fun_cb)(void *arg, struct bd_addr_t *bdaddr);
typedef err_t (* conn_complete_fun_cb)(void *arg, struct bd_addr_t *bdaddr);
typedef err_t (* hardware_error_fun_cb)(uint8_t reson);
typedef err_t (* ltk_request_fun_cb)(struct bd_addr_t *bdaddr,uint8_t *random,uint16_t ediv);
typedef err_t (* enc_change_fun_cb)(struct bd_addr_t *bdaddr,uint8_t enc);
typedef err_t (* local_p256_public_key_fun_cb)(uint8_t *public_key);
typedef err_t (* dhkey_complete_fun_cb)(uint8_t dhkey[32]);


//重要
/*-----HCI全局控制块-----*/
typedef struct
{
    void *callback_arg;

    /* hci cmd timer */
    uint32_t timer;
    /* init status */
    hci_init_status_e init_status;  //总体初始化状态
    hci_vendor_init_status_e vendor_init_status;    //厂商相关初始化状态
    chip_mgr_t *chip_mgr;       //蓝牙芯片厂商驱动接口
    struct bd_addr_t local_bd_addr;     //本地蓝牙地址
    uint8_t ssp_enable;     //是否启用安全简单配对SSP
    uint8_t ssp_io_cap;     //IO能力描述符    
    uint32_t class_of_device;   //COD
    const uint8_t *local_name;  //本地蓝牙名称
    uint8_t *pincode;       //本地PIN码
	uint8_t hci_version;    //HCI版本
    
    /*主机到控制器流控*/
    /* Host to host controller flow control */
    uint8_t numcmd; /* Number of command packets that the host controller (Bluetooth module)can buffer */
    uint16_t acl_maxsize; /* Maximum length of the data portion of each HCI ACL data packet that the Host Controller is able to accept */
	uint16_t controler_max_acl;
    uint16_t controller_num_acl; /* Number of ACL packets that the Bluetooth module can buffer */

    /* Host controller to host flow control */
    uint8_t flow; /* Indicates if host controller to host flow control is on */
    uint16_t host_num_acl; /* Number of ACL packets that we (the host) can buffer */

    hci_inq_res_t *ires; /* Results of an inquiry */

    uint8_t le_inq_w2_stop;
	pin_req_fun_cb pin_req; //PIN码配对请求回调函数
    bt_working_fun_cb bt_working;   //蓝牙初始化完成回调函数
    sco_conn_req_fun_cb sco_conn_req;   //Sco连接请求回调函数
    sco_conn_complete_fun_cb sco_conn_complete; //Sco连接完成回调函数
    sco_disconn_complete_fun_cb sco_disconn_complete;   //Sco断开连接完成回调函数
	inq_result_fun_cb inq_result;   //设备搜索结果回调函数
	inq_complete_fun_cb inq_complete;   //设备搜索完成回调函数
	le_inq_result_fun_cb le_inq_result;   //LE设备搜索结果回调函数
	le_inq_complete_fun_cb le_inq_complete;    //LE设备搜索完成回调函数
	name_req_complete_fun_cb name_req_complete;   //设备名称请求完成回调函数
	rbd_complete_fun_cb rbd_complete;   //读取远程设备信息完成回调函数
	link_key_not_fun_cb link_key_not;    //远程设备发送的LINK_KEY通知回调函数
	link_key_req_fun_cb link_key_req;   //远程设备发送的LINK_KEY请求回调函数
	wlp_complete_fun_cb wlp_complete;   //写入远程设备链接密钥完成回调函数
	conn_complete_fun_cb conn_complete;  //连接完成回调函数
	cmd_complete_fun_cb cmd_complete;    //命令完成回调函数
	hardware_error_fun_cb hardware_error;   //硬件错误回调函数
	ltk_request_fun_cb ltk_req;    //ltk请求回调函数
	enc_change_fun_cb enc_change;   //加密状态改变回调函数
	local_p256_public_key_fun_cb public_key;    //本地P256公钥完成回调函数
	dhkey_complete_fun_cb dhkey_complete;   //DHKey计算完成回调函数
}hci_pcb_t;


/*-------------------- common api ----------------------------------------*/
/*  Functions for interfacing with HCI */
err_t hci_init(void);   //蓝牙初始化
void hci_deinit(void);  //蓝牙反初始化
/*---回调函数注册---*/
void hci_register_cmd_complete(cmd_complete_fun_cb cmd_complete);
void hci_register_pin_req(pin_req_fun_cb pin_req);
void hci_register_bt_working(bt_working_fun_cb bt_working);
void hci_register_sco_req(sco_conn_req_fun_cb sco_conn_req);
void hci_register_sco_conn_complete(sco_conn_complete_fun_cb sco_conn_complete);
void hci_register_sco_disconn_complete(sco_disconn_complete_fun_cb sco_disconn_complete);
void hci_register_link_key_req(link_key_req_fun_cb link_key_req);
void hci_register_link_key_not(link_key_not_fun_cb link_key_not);
void hci_register_write_policy_complete(wlp_complete_fun_cb wlp_complete);
void hci_register_connection_complete(conn_complete_fun_cb conn_complete);
void hci_register_hardware_error(hardware_error_fun_cb hardware_error);
void hci_register_ltk_req(ltk_request_fun_cb ltk_req);
void hci_register_enc_change(enc_change_fun_cb enc_change);
void hci_register_public_key(local_p256_public_key_fun_cb public_key);
void hci_register_dhkey_complete(dhkey_complete_fun_cb dhkey_complete);


uint8_t hci_get_version(void);  //获取HCI版本
uint8_t *hci_get_local_addr(void);  //获取本地蓝牙地址
err_t hci_acl_write(struct bd_addr_t *bdaddr, struct bt_pbuf_t *p, uint16_t len, uint8_t pb);   //发送ACL数据包
uint8_t hci_is_connected(struct bd_addr_t *bdaddr); //判断是否已连接
uint16_t hci_pdu_maxsize(void); //获取HCI ACL数据包最大负载长度
void hci_acl_input(struct bt_pbuf_t *p);    //接收ACL数据包
void hci_event_input(struct bt_pbuf_t *p);  //接收事件包

/*
  函数: hci_init()
  原因: 整个蓝牙初始化的入口，能看到初始化顺序
  ────────────────────────────────────────
  函数: hci_event_input()
  原因: 事件解析和分发的核心，能看懂事件怎么触发回调
  ────────────────────────────────────────
  函数: hci_acl_write() / hci_acl_input()
  原因: 了解数据怎么在HCI层和L2CAP层之间传递
  可以快速过的
  函数: hci_register_xxx() 系列
  原因: 就是把函数指针存到结构体里，看一个就懂
  ────────────────────────────────────────
  核心思路

  上层 (L2CAP)
      │
      ├── hci_acl_write()   → 上层数据往下发
      │
      ├── hci_acl_input()   → 下层数据往上送给L2CAP
      │
      └── hci_event_input() → 事件解析后触发回调
      │
  下层 (Controller)
*/

/*-------------------- hci command ----------------------------------------*/
/* OGF = 0x01 LINK CONTROL COMMANDS */
err_t hci_inquiry(uint32_t lap, uint8_t inq_len, uint8_t num_resp,
                  inq_result_fun_cb inq_result,
                  inq_complete_fun_cb inq_complete);
err_t hci_cancel_inquiry(void);
err_t hci_periodic_inquiry(uint16_t min_periodic,uint16_t max_periodic,uint32_t lap, uint8_t inq_len, uint8_t num_resp,
                           inq_result_fun_cb inq_result,
                           inq_complete_fun_cb inq_complete);
err_t hci_cancel_periodic_inquiry(void);
err_t hci_connect_req(struct bd_addr_t *bdaddr, uint8_t allow_role_switch);
err_t hci_disconnect_acl(struct bd_addr_t *bdaddr, uint8_t reason);
err_t hci_disconnect_sco(struct bd_addr_t *bdaddr, uint8_t reason);
err_t hci_accept_connection_request(struct bd_addr_t *bdaddr, uint8_t role);
err_t hci_reject_connection_request(struct bd_addr_t *bdaddr, uint8_t reason);
err_t hci_link_key_request_reply(struct bd_addr_t *bdaddr, uint8_t *link_key);
err_t hci_link_key_request_negative_reply(struct bd_addr_t *bdaddr);
err_t hci_pin_code_request_reply(struct bd_addr_t *bdaddr, uint8_t pinlen, uint8_t *pincode);
err_t hci_pin_code_request_neg_reply(struct bd_addr_t *bdaddr);
err_t hci_get_remote_name(struct bd_addr_t *bdaddr,name_req_complete_fun_cb name_req_complete);
err_t hci_cancel_get_remote_name(struct bd_addr_t *bdaddr);
err_t hci_get_remote_feature(struct bd_addr_t *bdaddr);
err_t hci_connect_sco(struct bd_addr_t *bdaddr,uint32_t transmit_bandwidth,uint32_t receive_bandwidth,
                      uint16_t max_latency,uint16_t voice_setting,uint8_t retransmission_effort,uint16_t packet_type);
err_t hci_accept_sco_req(struct bd_addr_t *bdaddr,uint32_t transmit_bandwidth,uint32_t receive_bandwidth,
                         uint16_t max_latency,uint16_t voice_setting,uint8_t retransmission_effort,uint16_t packet_type);
err_t hci_reject_sco_req(struct bd_addr_t *bdaddr,uint8_t reason);
err_t hci_io_cap_req_replay(struct bd_addr_t *bdaddr,uint8_t io_cap,uint8_t oob,uint8_t auth_req);
err_t hci_user_confirm_req_reply(struct bd_addr_t *bdaddr);
err_t hci_user_confirm_req_neg_reply(struct bd_addr_t *bdaddr);
err_t hci_user_passkey_req_reply(struct bd_addr_t *bdaddr,uint32_t num_value);
err_t hci_user_passkey_req_neg_reply(struct bd_addr_t *bdaddr);
err_t hci_io_cap_req_neg_reply(struct bd_addr_t *bdaddr,uint8_t reason);
/* OGF = 0x02 LINK POLICY COMMANDS */
err_t hci_hold_mode(struct bd_addr_t *bdaddr, uint16_t max_interval, uint16_t min_interval);
err_t hci_sniff_mode(struct bd_addr_t *bdaddr, uint16_t max_interval, uint16_t min_interval,  uint16_t attempt,uint16_t timeout);
err_t hci_exit_sniff_mode(struct bd_addr_t *bdaddr);
err_t hci_role_discovery(struct bd_addr_t *bdaddr);
err_t hci_switch_role(struct bd_addr_t *bdaddr,uint8_t role);
err_t hci_read_link_policy(struct bd_addr_t *bdaddr);
err_t hci_write_link_policy(struct bd_addr_t *bdaddr, uint16_t link_policy);
err_t hci_read_default_link_policy(void);
err_t hci_write_default_link_policy(uint16_t link_policy);
/* OGF = 0x03 CONTROLLER & BASEBAND COMMANDS */
err_t hci_set_event_mask(uint32_t mask_lo,uint32_t mask_hi);
err_t hci_reset(void);
err_t hci_set_event_filter(uint8_t filter_type, uint8_t filter_cond_type, uint8_t* cond);
err_t hci_write_stored_link_key(struct bd_addr_t *bdaddr, uint8_t *key);
err_t hci_write_local_name(uint8_t *name, uint8_t len);
err_t hci_read_local_name(void);
err_t hci_write_page_timeout(uint16_t page_timeout);
err_t hci_write_scan_enable(uint8_t scan_enable);
err_t hci_write_cod(uint8_t *cod);
err_t hci_read_voice_setting(void);
err_t hci_write_voice_setting(uint16_t voice_setting);
err_t hci_write_flush_timeout(struct bd_addr_t *bdaddr, uint16_t flushto);
err_t hci_set_hc_to_h_fc(void);
err_t hci_host_buffer_size(void);
err_t hci_host_num_comp_packets(uint16_t conhdl, uint16_t num_complete);
err_t hci_write_current_iac_lap(uint8_t iac_num,uint32_t *iac);
err_t hci_write_inquiry_mode(uint8_t inquiry_mode);
err_t hci_write_eir(uint8_t *eir_data);
err_t hci_write_ssp_mode(uint8_t ssp_mode);
err_t hci_write_le_enable(uint8_t le_support,uint8_t simultaneous);
/* OGF = 0x04 INFORMATIONAL PARAMETERS */
err_t hci_read_local_version_info(void);
err_t hci_read_local_support_cmd(void);
err_t hci_read_buffer_size(void);
err_t hci_read_bd_addr(rbd_complete_fun_cb rbd_complete);
/* OGF = 0x05 STATUS PARAMETERS */
err_t hci_read_rssi(struct bd_addr_t *bdaddr);
/* OGF = 0x06 TESTING COMMANDS */
err_t hci_enable_dut_mode(void);
/* OGF = 0x08 LE CONTROLLER COMMANDS */
#if BT_BLE_ENABLE > 0
err_t hci_le_set_event_mask(uint8_t mask[8]);
err_t hci_le_read_buffer_size(void);
err_t hci_le_read_local_support_feature(void);
err_t hci_le_set_random_address(struct bd_addr_t *bdaddr);
err_t hci_le_set_scan_param(uint8_t scan_type,uint16_t scan_interval,uint16_t scan_window,uint8_t own_type,uint8_t scan_filter);
err_t hci_le_inquiry(uint8_t filter_duplicates,
					le_inq_result_fun_cb le_inq_result,
                     le_inq_complete_fun_cb le_inq_complete);
err_t hci_le_cancel_inquiry(void);
err_t hci_le_set_adv_param(uint16_t adv_int_min, uint16_t adv_int_max, uint8_t adv_type,
    uint8_t own_address_typ, uint8_t peer_address_type,struct bd_addr_t *peer_address, uint8_t channel_map, uint8_t filter_policy);
err_t hci_le_set_adv_data(uint8_t adv_len,uint8_t *adv_data);
err_t hci_le_set_adv_enable(uint8_t enable);
err_t hci_le_create_connection(void);

err_t hci_le_ltk_req_reply(struct bd_addr_t *bdaddr,uint8_t *ltk);
err_t hci_le_ltk_req_neg_replay(struct bd_addr_t *bdaddr);
err_t hci_le_read_p256_public_key(void);
err_t hci_le_generate_dhkey(uint8_t *remote_public_key);
err_t hci_le_add_dev_resolv_list(uint8_t peer_iat,uint8_t peer_ia[6],uint8_t peer_irk[16],uint8_t local_irk[16]);
err_t hci_le_rm_dev_resolv_list(uint8_t peer_iat,uint8_t peer_ia[6]);
err_t hci_le_clear_resolv_list(void);
err_t hci_le_read_resolv_list_size(void);
err_t hci_le_read_peer_resolv_addr(uint8_t peer_iat,uint8_t peer_ia[6]);
err_t hci_le_read_local_resolv_addr(uint8_t peer_iat,uint8_t peer_ia[6]);
err_t hci_le_set_addr_resolution_enable(uint8_t enable);

#endif



#endif
/*
  ┌─────────────────────────────────────┬─────────────────────┐
  │                命令                 │        特点         │
  ├─────────────────────────────────────┼─────────────────────┤
  │ HCI_Reset                           │ 最简单，无参数      │
  ├─────────────────────────────────────┼─────────────────────┤
  │ HCI_Write_Local_Name                │ 带字符串参数        │
  ├─────────────────────────────────────┼─────────────────────┤
  │ HCI_Create_Connection               │ 参数较多，有BD_ADDR │
  ├─────────────────────────────────────┼─────────────────────┤
  │ HCI_Write_Extended_Inquiry_Response │ 大数据块(240字节)   │
  └─────────────────────────────────────┴─────────────────────┘
*/
