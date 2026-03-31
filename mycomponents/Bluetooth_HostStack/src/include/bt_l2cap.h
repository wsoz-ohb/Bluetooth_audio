/******************************************************************************
  * @file           bt_l2cap.h
  * @author         Yu-ZhongJun(124756828@qq.com)
  * @Taobao link    https://shop220811498.taobao.com/
  * @version        V0.0.1
  * @date           2020-4-16
  * @brief          bt l2cap header file
******************************************************************************/

#ifndef BT_L2CAP_H_H_H
#define BT_L2CAP_H_H_H

#include "bt_common.h"
#include "bt_hci.h"
#include "bt_pbuf.h"

/* PSM上层协议标识 */
/* Protocol and service multiplexor */
#define SDP_PSM 0x0001
#define RFCOMM_PSM 0x0003
#define TCS_BIN_PSM 0x0005
#define TCS_BIN_CORDLESS_PSM 0x0007
#define BNEP_PSM 0x000F
#define HID_CONTROL_PSM 0x0011
#define HID_INTERRUPT_PSM 0x0013
#define UPNP_PSM 0x0015
#define AVCTP_PSM 0x0017
#define AVDTP_PSM 0x0019
#define AVCTP_BROWSING_PSM 0x001B
#define UDI_C_PLANE 0x001D
#define ATT_PSM 0x001F
#define DSP_3_PSM 0x0021
#define LE_PSM_IPSP_PSM 0x0023
#define OTS_PSM 0x0025
#define EATT_PSM 0x0027

/*L2CAP包头*/
/* Packet header lengths */
#define L2CAP_HDR_LEN 4 //基础头部长度
#define L2CAP_SIGHDR_LEN 4  //C-frame里面每条Signaling Command自己的头部
#define L2CAP_CFGOPTHDR_LEN 2   //Configure配置option配置选项的头部

/*---控制帧中Signaling Command中data字段大小---*/
/* Signals sizes */
#define L2CAP_CONN_REQ_SIZE 4       //连接请求
#define L2CAP_CONN_RSP_SIZE 8       //连接响应
#define L2CAP_CFG_RSP_SIZE 6        //配置响应
#define L2CAP_INFO_MTU_RSP_SIZE 8   //MTU响应
#define L2CAP_INFO_EXFEATURE_RSP_SIZE 8     //扩展特性响应
#define L2CAP_INFO_FIXED_CHNL_RSP_SIZE 12       //固定通道响应
#define L2CAP_DISCONN_RSP_SIZE 4        //断开连接响应

#define L2CAP_CFG_REQ_SIZE 4        //配置请求

#define L2CAP_DISCONN_REQ_SIZE 4    //断开连接请求
#define L2CAP_CMD_REJ_SIZE 2        //命令拒绝响应

/*---Signaling Command的code---*/
/* Signal codes */
#define L2CAP_CMD_REJ 0x01
#define L2CAP_CONN_REQ 0x02
#define L2CAP_CONN_RSP 0x03
#define L2CAP_CFG_REQ 0x04
#define L2CAP_CFG_RSP 0x05
#define L2CAP_DISCONN_REQ 0x06
#define L2CAP_DISCONN_RSP 0x07
#define L2CAP_ECHO_REQ 0x08
#define L2CAP_ECHO_RSP 0x09
#define L2CAP_INFO_REQ 0x0A
#define L2CAP_INFO_RSP 0x0B
#define L2CAP_CREATE_CHANNEL_REQ 0x0C
#define L2CAP_CREATE_CHANNEL_RSP 0x0D
#define L2CAP_MOVE_CHANNEL_REQ 0x0E
#define L2CAP_MOVE_CHANNEL_RSP 0x0F
#define L2CAP_MOVE_CHANNEL_CONFIRMATION_REQ 0x10
#define L2CAP_MOVE_CHANNEL_CONFIRMATION_RSP 0x11
#define L2CAP_CONN_PARAM_UPDATE_REQ 0x12
#define L2CAP_CONN_PARAM_UPDATE_RSP 0x13
#define L2CAP_LE_CREDIT_BASED_CONN_REQ 0x14
#define L2CAP_LE_CREDIT_BASED_CONN_RSP 0x15
#define L2CAP_FLOW_CONTROL_CREDIT_IND 0x16
#define L2CAP_CREDIT_BASED_CONN_REQ 0x17
#define L2CAP_CREDIT_BASED_CONN_RSP 0x18
#define L2CAP_CREDIT_BASED_RECONFIGURE_REQ 0x19
#define L2CAP_CREDIT_BASED_RECONFIGURE_RSP 0x1A

/*---固定的信道标识符---*/
/* Permanent channel identifiers */
#define L2CAP_NULL_CID 0x0000   //无效
#define L2CAP_SIG_CID 0x0001    //BR/EDR L2CAP信号信道
#define L2CAP_CONNLESS_CID 0x0002   //无连接信道
#define L2CAP_AMP_MANAGER_CID 0x0003    //AMP管理信道，用于高速传输

#define L2CAP_ATT_CID 0x0004    //BLE ATT信道
#define L2CAP_L2_SIG_CID 0x0005 //BLE L2CAP信号信道
#define L2CAP_SM_CID 0x0006     //BLE SM信道
#define L2CAP_BREDR_SM_CID 0x0007       //BR/EDR SM信道，基本不用

/*---动态分配上层协议信道---*/
/* Channel identifiers values */    
#define L2CAP_MIN_CID 0x0040
#define L2CAP_MAX_CID 0xFFFF

/*---Signaling Command的type标识符*/
/* Configuration types */
#define L2CAP_CFG_MTU 0x01  //MTU配置
#define L2CAP_FLUSHTO 0x02  //刷新超时配置

#define L2CAP_QOS 0x03      //QoS配置
#define L2CAP_CFG_RETRANSMISSION_FLOW_CONTROL 0x04  //重传和流控配置
#define L2CAP_CFG_FCS 0x05      //帧校验序列配置
#define L2CAP_CFG_EXT_FLOW 0x06     
#define L2CAP_CFG_EXT_WINDOW_SIZE 0x07

/*---Information Request/Response中的Info Type字段---*/
/* Info type */
#define L2CAP_CONLESS_MTU 0x01
#define L2CAP_EXFEATURE_SUPPORT 0x02
#define L2CAP_FIXED_CHNL_SUPPORT 0x03

/*---配置请求中option_data字段长度---*/
/* Configuration types length */
#define L2CAP_MTU_LEN 2
#define L2CAP_FLUSHTO_LEN 2
#define L2CAP_QOS_LEN 22
#define L2CAP_RETRA_FLOW_CTL_LEN 9
#define L2CAP_FCS_LEN 1
#define L2CAP_EXT_FLOW_SPEC_LEN 16
#define L2CAP_EXT_WINDOWS_SIZE 2

/*---配置响应中RESULT结果参数----*/
/* Configuration response types */
#define L2CAP_CFG_SUCCESS 0x0000
#define L2CAP_CFG_UNACCEPT 0x0001
#define L2CAP_CFG_REJ 0x0002
#define L2CAP_CFG_UNKNOWN 0x0003
#define L2CAP_CFG_PENDING 0x0004
#define L2CAP_CFG_FLOW_SPEC_REJ 0x0005
#define L2CAP_CFG_TIMEOUT 0xEEEE

/*---QOS服务类型---*/
/* QoS types */
#define L2CAP_QOS_NO_TRAFFIC 0x00
#define L2CAP_QOS_BEST_EFFORT 0x01
#define L2CAP_QOS_GUARANTEED 0x02

/*---命令拒绝原因---*/
/* Command reject reasons */
#define L2CAP_CMD_NOT_UNDERSTOOD 0x0000
#define L2CAP_MTU_EXCEEDED 0x0001
#define L2CAP_INVALID_CID 0x0002

/*---连接响应结果---*/
/* Connection response results */
#define L2CAP_CONN_SUCCESS 0x0000
#define L2CAP_CONN_PND 0x0001
#define L2CAP_CONN_REF_PSM 0x0002
#define L2CAP_CONN_REF_SEC 0x0003
#define L2CAP_CONN_REF_RES 0x0004
#define L2CAP_CONN_CFG_TO 0x0005 /* Implementation specific result */
#define L2CAP_CONN_REF_CID 0x0006
#define L2CAP_CONN_REF_HAS_CID 0x0007

/*---Information Response中L2CAP_EXFEATURE_SUPPORT(0x02)返回的扩展特性位掩码，支持的L2CAP特性*/
/* Extended features mask bits
*/
#define L2CAP_EXTFEA_FC             0x00000001    /* Flow Control Mode   (Not Supported)    */
#define L2CAP_EXTFEA_RTRANS         0x00000002    /* Retransmission Mode (Not Supported)    */
#define L2CAP_EXTFEA_QOS            0x00000004
#define L2CAP_EXTFEA_ENH_RETRANS    0x00000008    /* Enhanced retransmission mode           */
#define L2CAP_EXTFEA_STREAM_MODE    0x00000010    /* Streaming Mode                         */
#define L2CAP_EXTFEA_NO_CRC         0x00000020    /* Optional FCS (if set No FCS desired)   */
#define L2CAP_EXTFEA_EXT_FLOW_SPEC  0x00000040    /* Extended flow spec                     */
#define L2CAP_EXTFEA_FIXED_CHNLS    0x00000080    /* Fixed channels                         */
#define L2CAP_EXTFEA_EXT_WINDOW     0x00000100    /* Extended Window Size                   */
#define L2CAP_EXTFEA_UCD_RECEPTION  0x00000200    /* Unicast Connectionless Data Reception  */
#define L2CAP_EXTFEA_ENH_CREDIT_BASE_FC 0x00000400 /* Enhanced Credit Based Flow Control Mode */
#define L2CAP_EXTFEA_SUPPORTED_MASK (L2CAP_EXTFEA_ENH_RETRANS  | L2CAP_EXTFEA_NO_CRC | L2CAP_EXTFEA_FIXED_CHNLS |L2CAP_EXTFEA_FIXED_CHNLS | L2CAP_EXTFEA_UCD_RECEPTION)

/*---L2CAP工作模式---*/
/* L2CAP mode */
#define L2CAP_MODE_BASIC	0x00
#define L2CAP_MODE_RETRANS	0x01
#define L2CAP_MODE_FLOWCTL	0x02
#define L2CAP_MODE_ERTM		0x03
#define L2CAP_MODE_STREAMING	0x04

/*---Echo测试命令result结果---*/
/* Echo response results */
#define L2CAP_ECHO_RCVD 0x00
#define L2CAP_ECHO_TO 0x01

/*---信息响应结果---*/
/* Info request results */
#define L2CAP_INFO_REQ_SUCCESS 0x0

/*---L2CAP分片状态（HCI ACL包头中PB Flag）---*/
/* L2CAP segmentation */
#define L2CAP_ACL_START 0x02
#define L2CAP_ACL_CONT 0x01

/*---L2CAP默认参数MTU FLASHTIME---*/
/* L2CAP config default parameters */
#define L2CAP_CFG_DEFAULT_INMTU 672 /* Two Baseband DH5 packets (2*341=682) minus the Baseband ACL 
				       headers (2*2=4) and L2CAP header (6) */
#define L2CAP_CFG_DEFAULT_OUTFLUSHTO 0xFFFF

//协议栈内部状态码
/* L2CAP configuration parameter masks */
#define L2CAP_CFG_IR 0x01   //本地作为连接发起方
#define L2CAP_CFG_IN_SUCCESS 0x02   //对端配置响应成功
#define L2CAP_CFG_OUT_SUCCESS 0x04  //本地配置输出成功
#define L2CAP_CFG_OUT_REQ 0x08      //本地发送配置请求

#pragma pack (1) 

typedef struct 
{
    uint16_t len;
    uint16_t cid;
}l2cap_hdr_t;   //L2CAP基础头部

typedef struct 
{
    uint8_t code;
    uint8_t id;
    uint16_t len;
}l2cap_sig_hdr_t;   //L2CAP的signaling Command头部

typedef struct 
{
    uint8_t type;
    uint8_t len;
}l2cap_cfgopt_hdr_t;    //L2CAP配置选项头部
#pragma pack () 


enum l2cap_state_e
{
    L2CAP_CLOSED, L2CAP_LISTEN, W4_L2CAP_CONNECT_RSP, W4_L2CA_CONNECT_RSP, L2CAP_CONFIG,
    L2CAP_OPEN, W4_L2CAP_DISCONNECT_RSP, W4_L2CA_DISCONNECT_RSP
};  //L2CAP内部状态码

typedef struct _l2cap_acl_link_t
{
    struct _l2cap_acl_link_t *next;
    struct bd_addr_t bdaddr;
}l2cap_acl_link_t;  //ACL链路结构体


/* This structure is used to represent L2CAP signals. */
typedef struct _l2cap_sig_t
{
    struct _l2cap_sig_t *next;    /* for the linked list, used when putting signals
				on a queue */
    struct bt_pbuf_t *p;          /* buffer containing data + L2CAP header */
    uint16_t sigid; /* Identification */
    uint16_t ertx; /* extended response timeout expired */  //对端回Pending响应的超时
    uint8_t rtx; /* response timeout expired */ //发出请求后等待响应的超时
    uint8_t nrtx; /* number of retransmissions */   //最大重传次数
}l2cap_sig_t;   //C帧数据Signaling Command控制结构体

typedef struct 
{
    uint16_t inmtu; /* Maximum transmission unit this channel can accept */    //本地MTU
    uint16_t outmtu; /* Maximum transmission unit that can be sent on this channel */      //对端MTU
    uint16_t influshto; /* In flush timeout */  //对端发送数据时，本地接收缓存区满，对端等待本地接收缓存区空闲的超时
    uint16_t outflushto; /* Out flush timeout */    //本地发送数据时，对端接收缓存区满，本地等待对端接收缓存区空闲的超时

    struct bt_pbuf_t *opt; /* Any received non-hint unknown option(s) or option(s) with unacceptable parameters
		       will be temporarily stored here */   //命令不接受时，保存对端发送的配置选项

    uint8_t cfgto; /* Configuration timeout */  //配置超时计数器
    uint8_t l2capcfg; /* Bit 1 indicates if we are the initiator of this connection
		  * Bit 2 indicates if a successful configuration response has been received
		  * Bit 3 indicates if a successful configuration response has been sent
		  * Bit 4 indicates if an initial configuration request has been sent
		  */        //配置状态阶段码
}l2cap_cfg_t;   //L2CAP配置参数结构体

/*---回调函数定义---*/
struct _l2cap_pcb_t;
typedef err_t (* l2ca_connect_ind_cb)(void *arg, struct _l2cap_pcb_t *pcb, err_t err);      //收到对端连接请求时的回调函数
typedef err_t (* l2ca_disconnect_ind_cb)(void *arg, struct _l2cap_pcb_t *pcb, err_t err);   //收到对端断开连接请求时的回调函数
typedef err_t (* l2ca_connect_cfm_cb)(void *arg, struct _l2cap_pcb_t *pcb, uint16_t result, uint16_t status);   //连接请求响应时的回调函数
typedef err_t (* l2ca_timeout_ind_cb)(void *arg, struct _l2cap_pcb_t *newpcb, err_t err);   //连接/配置超时回调函数
typedef err_t (* l2ca_recv_cb)(void *arg, struct _l2cap_pcb_t *pcb, struct bt_pbuf_t *p, err_t err);    //收到数据时的回调函数（B-frame）
typedef err_t (* l2ca_disconnect_cfm_cb)(void *arg, struct _l2cap_pcb_t *pcb);  //断开连接响应时的回调函数
typedef err_t (* l2ca_ping_cb)(void *arg, struct _l2cap_pcb_t *pcb, uint8_t result);    //收到Echo响应时的回调函数，ping对端


/*-------重要------*/
/*---L2CAP整体控制结构体---*/
typedef struct _l2cap_pcb_t
{
    struct _l2cap_pcb_t *next; /* For the linked list */

	uint8_t conn_role;      //连接角色，0为从机，1为主机
	
    enum l2cap_state_e state; /* L2CAP state */     //L2CAP内部状态码

    void *callback_arg;     //回调函数参数

    uint16_t scid; /* Source CID */     //本地CID
    uint16_t dcid; /* Destination CID */    //对端CID

    uint16_t psm; /* Protocol/Service Multiplexer */        //上层协议标识符
	uint16_t fixed_cid;     //固定CID，0表示动态分配CID，非0表示固定CID
	
    uint16_t ursp_id; /* Signal id to respond to */ //信号标识符IDdefined
    uint8_t encrypt; /* encryption mode */      //链路是否加密

    l2cap_sig_t *unrsp_sigs;  /* List of sent but unresponded signals */        //已发送但未响应的信号链表

    struct bd_addr_t remote_bdaddr;     //对端蓝牙地址

    l2cap_cfg_t cfg; /* Configuration parameters */     //参数

    uint8_t mode;       //L2CAP工作模式（本协议栈只实现了基本模式）
    /* Upper layer to L2CAP confirmation functions */

    /* Function to be called when a connection has been set up */
	l2ca_connect_cfm_cb l2ca_connect_cfm;
    /* Function to be called when a connection has been closed */
	l2ca_disconnect_cfm_cb l2ca_disconnect_cfm;
    /* Function to be called when a echo reply has been received */
	l2ca_ping_cb l2ca_ping;

    /* L2CAP to upper layer indication functions */

    /* Function to be called when a connection indication event occurs */
	l2ca_connect_ind_cb l2ca_connect_ind;
    /* Function to be called when a disconnection indication event occurs */
	l2ca_disconnect_ind_cb l2ca_disconnect_ind;
    /* Function to be called when a timeout indication event occurs */
    l2ca_timeout_ind_cb l2ca_timeout_ind;
    /* Function to be called when a L2CAP connection receives data */
	l2ca_recv_cb l2ca_recv;
}l2cap_pcb_t;


//L2CAP重组数据段，封装后交给HCI拆成ACL分片发出
typedef struct _l2cap_seg_t
{
    struct _l2cap_seg_t *next;    //链表指针，多个段排队发送

    struct bd_addr_t bdaddr;      //目标设备蓝牙地址

    struct bt_pbuf_t *p;          //实际数据包（含L2CAP头部）
    uint16_t len;                 //L2CAP数据长度
    l2cap_hdr_t *l2caphdr;       //指向包中L2CAP头部（Length+CID）
    l2cap_pcb_t *pcb;            //所属的L2CAP连接控制块
}l2cap_seg_t;




//L2CAP监听控制块（精简版pcb），等待对端连接请求
typedef struct _l2cap_pcb_listen_t
{
    struct _l2cap_pcb_listen_t *next; //链表指针

    enum l2cap_state_e state;         //固定为L2CAP_LISTEN

    void *callback_arg;               //上层回调参数

    uint16_t psm;                     //监听的协议标识（如SDP=0x0001，RFCOMM=0x0003）
    l2ca_connect_ind_cb l2ca_connect_ind; //收到连接请求时的回调
}l2cap_pcb_listen_t;


#define l2cap_psm(pcb) ((pcb)->psm)


/* Functions for interfacing with L2CAP */
void l2cap_init(void); /* Must be called first to initialize L2CAP */
void l2cap_deinit(void);
void l2cap_tmr(void); /* Must be called every 1s */
l2cap_pcb_t *l2cap_new(void);
err_t l2cap_close(l2cap_pcb_t *pcb);
err_t l2cap_register_connect_ind(uint8_t psm,l2ca_connect_ind_cb l2ca_connect_ind);
void l2cap_register_disconnect_ind(l2cap_pcb_t *pcb,l2ca_disconnect_ind_cb l2ca_disconnect_ind);
void l2cap_register_timeout_ind(l2cap_pcb_t *pcb,l2ca_timeout_ind_cb l2ca_timeout_ind);
void l2cap_register_recv(l2cap_pcb_t *pcb,l2ca_recv_cb l2ca_recv);
err_t l2cap_fixed_channel_register_recv(uint16_t cid,
							l2ca_connect_ind_cb l2ca_connect_ind,
							l2ca_disconnect_ind_cb l2ca_disconnect_ind,
							l2ca_recv_cb l2ca_recv);
err_t l2cap_connect_req(l2cap_pcb_t *pcb, struct bd_addr_t *bdaddr, uint16_t psm, uint8_t role_switch,
                        l2ca_connect_cfm_cb l2ca_connect_cfm);
err_t l2cap_ertm_connect_req(l2cap_pcb_t *pcb, struct bd_addr_t *bdaddr, uint16_t psm, uint8_t role_switch,
                             l2ca_connect_cfm_cb l2ca_connect_cfm);
err_t l2cap_disconnect_req(l2cap_pcb_t *pcb,l2ca_disconnect_cfm_cb l2ca_disconnect_cfm);
err_t l2cap_datawrite(l2cap_pcb_t *pcb, struct bt_pbuf_t *p);
err_t l2cap_fixed_channel_datawrite(l2cap_pcb_t *pcb, struct bt_pbuf_t *p,uint16_t cid);
err_t l2cap_ping(struct bd_addr_t *bdaddr, l2cap_pcb_t *tpcb,l2ca_ping_cb l2ca_ping);
void l2cap_acl_input(struct bt_pbuf_t *p, struct bd_addr_t *bdaddr);
void lp_connect_cfm(struct bd_addr_t *bdaddr, uint8_t encrypt_mode, err_t err);
void lp_connect_ind(struct bd_addr_t *bdaddr);
void lp_disconnect_ind(struct bd_addr_t *bdaddr);
void le_connect_handler(struct bd_addr_t *bdaddr,uint8_t conn_role);



/*
  - l2cap_connect_req — 理解连接建立流程
  - l2cap_acl_input + l2cap_process_sig —
  理解收到数据/信令后怎么分发处理（这是核心）
  - l2cap_write — 理解数据发送和ACL分片
  其余的l2cap_config_req、l2cap_disconnect_req、l2cap_ping等看一个就知道套路了
*/

#endif


