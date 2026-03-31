/******************************************************************************
  * @file           bt_hid_device.h
  * @author         Yu-ZhongJun(124756828@qq.com)
  * @Taobao link    https://shop220811498.taobao.com/
  * @version        V0.0.1
  * @date           2020-12-06
  * @brief          bt hid device role header file
******************************************************************************/

#ifndef BT_HID_DEVICE_H_H_H
#define BT_HID_DEVICE_H_H_H

#include "bt_common.h"

#if PROFILE_HID_ENABLE

#include "bt_l2cap.h"
#include "bt_sdp.h"

/*---HID封包消息类型---*/
typedef enum {
    HID_MSG_TYPE_HANDSHAKE = 0x00,
    HID_MSG_TYPE_HID_CONTROL = 0x01,
    HID_MSG_TYPE_GET_REPORT = 0x04,
    HID_MSG_TYPE_SET_REPORT = 0x05,
    HID_MSG_TYPE_GET_PROTOCOL = 0x06,
    HID_MSG_TYPE_SET_PROTOCOL = 0x07,
    HID_MSG_TYPE_GET_IDLE = 0x08,
    HID_MSG_TYPE_SET_IDLE = 0x09,
    HID_MSG_TYPE_DATA = 0x0a,
} hid_message_type_e;

/*---Handshake具体参数---*/
typedef enum {
    HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL = 0x00,
    HID_HANDSHAKE_PARAM_TYPE_NOT_READY, 
    HID_HANDSHAKE_PARAM_TYPE_ERR_INVALID_REPORT_ID,
    HID_HANDSHAKE_PARAM_TYPE_ERR_UNSUPPORTED_REQUEST,
    HID_HANDSHAKE_PARAM_TYPE_ERR_INVALID_PARAMETER,
    HID_HANDSHAKE_PARAM_TYPE_ERR_UNKNOWN = 0x0E
} hid_handshake_param_type_e;


/*---HID描述符TAG符---*/
typedef enum {
    HID_DATA_TYPE_OTHER = 0x00,
    HID_DATA_TYPE_INPUT = 0x01,
    HID_DATA_TYPE_OUTPUT = 0x02,
    HID_DATA_TYPE_FEATURE = 0x03,
} hid_data_type_para_e;

typedef enum {
    HID_PROTOCOL_MODE_BOOT = 0x00,
    HID_PROTOCOL_MODE_REPORT = 0x01,
} hid_protocol_mode_e;


/*---HID设备控制块结构体---*/
struct hid_device_pcb_t
{
    struct hid_device_pcb_t *next; /* For the linked list */
    l2cap_pcb_t *hid_device_control_l2cappcb; /* The L2CAP connection */
    l2cap_pcb_t *hid_device_interrupt_l2cappcb;
	struct bd_addr_t remote_bdaddr;
    uint8_t protocol_mode;
    uint8_t idle_rate;
    uint8_t led_output;
};

/*---HID设备回调函数---*/
typedef struct
{
    void (*hid_connect_set_up)(struct bd_addr_t *remote_addr,uint8_t status);
    void (*hid_connect_realease)(struct bd_addr_t *remote_addr,uint8_t status);
    void (*hid_interrupt_data_ind)(struct bd_addr_t *remote_addr,uint8_t *data,uint16_t data_len);
} hid_cbs_t;



err_t hid_device_init(hid_cbs_t *cb);   //初始化HID设备角色
err_t hid_device_sdp_init(uint8_t *descriptor,uint16_t descriptor_len);     //初始化HID设备SDP记录
err_t hid_device_interupt_report(struct bd_addr_t *remote_addr,uint8_t *report,uint8_t report_size);    //发送HID中断报告


#endif
#endif
