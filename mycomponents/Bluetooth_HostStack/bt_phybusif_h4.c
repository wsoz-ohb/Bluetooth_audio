/******************************************************************************
  * @file           bt_phybusif.c
  * @author         Yu-ZhongJun(124756828@qq.com)
  * @Taobao link    https://shop220811498.taobao.com/
  * @version        V0.0.1
  * @date           2020-4-16
  * @brief          bt phybusif source file
******************************************************************************/


#include "bt_phybusif_h4.h"
#include "bt_hci.h"
#include "bt_common.h"
#include <rtthread.h>
#include <rtdevice.h>
#include "drivers/serial.h"
#include <board.h>

#define HW_ERR_OK           0
#define HW_HAL_EXCU_ERR     1

struct phybusif_cb uart_if;

#define BT_RX_BUF_SIZE (4 * 1024)
#define BT_TX_BUF_SIZE 1024
#define BT_RX_TMP_BUF_SIZE 256

static struct rt_ringbuffer bt_ring_buf;
static rt_uint8_t bt_rx_buf[BT_RX_BUF_SIZE];
static rt_uint8_t bt_tx_buf[BT_TX_BUF_SIZE];
static rt_uint8_t bt_rx_tmp_buf[BT_RX_TMP_BUF_SIZE];

static rt_device_t bt_uart_dev = RT_NULL;
static struct rt_semaphore bt_rx_sem;
static rt_bool_t bt_sem_inited = RT_FALSE;
static rt_thread_t bt_rx_thread = RT_NULL;
static volatile rt_bool_t bt_cb_ready = RT_FALSE;

static rt_err_t bt_uart_rx_indicate(rt_device_t dev, rt_size_t size)
{
    (void)dev;

    if (size == 0)
    {
        return RT_EOK;
    }

    if (bt_sem_inited)
    {
        rt_sem_release(&bt_rx_sem);
    }

    return RT_EOK;
}

static void bt_h4_rx_thread(void *parameter)
{
    (void)parameter;

    while (1)
    {
        rt_sem_take(&bt_rx_sem, RT_WAITING_FOREVER);

        if (bt_uart_dev == RT_NULL)
        {
            continue;
        }

        while (1)
        {
            rt_size_t len = rt_device_read(bt_uart_dev, 0, bt_rx_tmp_buf, sizeof(bt_rx_tmp_buf));
            if (len == 0)
            {
                break;
            }
            rt_size_t wrote = rt_ringbuffer_put(&bt_ring_buf, bt_rx_tmp_buf, (rt_uint16_t)len);
            if (wrote < len)
            {
                BT_TRANSPORT_TRACE_WARNING("H4 ringbuffer overflow, drop %d bytes\n", (int)(len - wrote));
            }
        }

        if (!bt_cb_ready)
        {
            continue;
        }

        while (rt_ringbuffer_data_len(&bt_ring_buf) > 0)
        {
            err_t err = phybusif_input(&uart_if);
            if (err != BT_ERR_OK)
            {
                break;
            }
        }
    }
}

static rt_err_t bt_uart_configure(rt_uint32_t baud_rate)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    cfg.baud_rate = baud_rate;
    cfg.flowcontrol = RT_SERIAL_FLOWCONTROL_CTSRTS;

    return rt_device_control(bt_uart_dev, RT_DEVICE_CTRL_CONFIG, &cfg);
}

uint8_t* bt_get_tx_buffer()
{
    return bt_tx_buf;
}

/******************************************************************************
 * func name   : hw_uart_bt_init
 * para        : baud_rate(IN)  --> Baud rate of uart1
 * return      : hw_uart_bt_init result
 * description : Initialization of USART2.PA0->CTS PA1->RTS PA2->TX PA3->RX
******************************************************************************/
uint8_t hw_uart_bt_init(uint32_t baud_rate)
{
    rt_err_t err;

    bt_uart_dev = rt_device_find("uart2");
    if (bt_uart_dev == RT_NULL)
    {
        BT_TRANSPORT_TRACE_ERROR("uart2 device not found\n");
        return HW_HAL_EXCU_ERR;
    }

    err = rt_device_open(bt_uart_dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (err != RT_EOK)
    {
        BT_TRANSPORT_TRACE_ERROR("uart2 open failed: %d\n", err);
        return HW_HAL_EXCU_ERR;
    }

    /* CTS(PA0) + RTS(PA1): AF7 for USART2 hardware flow control */
    {
        GPIO_InitTypeDef gpio = {0};
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gpio);
    }

    err = bt_uart_configure(baud_rate);
    if (err != RT_EOK)
    {
        BT_TRANSPORT_TRACE_ERROR("uart2 configure failed: %d\n", err);
        return HW_HAL_EXCU_ERR;
    }

    rt_device_set_rx_indicate(bt_uart_dev, bt_uart_rx_indicate);

    if (!bt_sem_inited)
    {
        rt_sem_init(&bt_rx_sem, "h4rx", 0, RT_IPC_FLAG_FIFO);
        bt_sem_inited = RT_TRUE;
    }

    if (bt_rx_thread == RT_NULL)
    {
        bt_rx_thread = rt_thread_create("h4_rx", bt_h4_rx_thread, RT_NULL, 2048, 12, 10);
        if (bt_rx_thread == RT_NULL)
        {
            BT_TRANSPORT_TRACE_ERROR("h4 rx thread create failed\n");
            return HW_HAL_EXCU_ERR;
        }
        rt_thread_startup(bt_rx_thread);
    }

    return HW_ERR_OK;

}


/******************************************************************************
 * func name   : uart_bt_send
 * para        : buf(IN)  --> Buffer to send
                 len(IN)  --> The buffer length to be sent
 * return      : NULL
 * description : The send function of USART2
******************************************************************************/
void uart_bt_send(uint8_t *buf,uint16_t len)
{
    if (bt_uart_dev == RT_NULL || buf == RT_NULL || len == 0)
    {
        return;
    }

    (void)rt_device_write(bt_uart_dev, 0, buf, len);
}

/******************************************************************************
 * func name   : bt_uart_test
 * para        : NULL
 * return      : NULL
 * description : Bluetooth HCI reset command over H4 transport test
******************************************************************************/
void bt_uart_test()
{
    uint8_t hci_reset[4] = {0x01,0x03,0x0c,0x00};
    uart_bt_send(hci_reset,4);
}



err_t phybusif_reset(struct phybusif_cb *cb)
{
    bt_cb_ready = RT_FALSE;

    /* Init new ctrl block */
    /* Alloc new bt_pbuf_t. bt will handle dealloc */
    if((cb->p = bt_pbuf_alloc(BT_PBUF_RAW, PBUF_POOL_BUFSIZE, BT_PBUF_POOL)) == NULL)
    {
        BT_TRANSPORT_TRACE_ERROR("ERROR:file[%s],function[%s],line[%d] bt_pbuf_alloc fail\n",__FILE__,__FUNCTION__,__LINE__);
        return BT_ERR_MEM; /* Could not allocate memory for bt_pbuf_t */
    }
    cb->q = cb->p; /* Make p the pointer to the head of the bt_pbuf_t chain and q to the tail */

    cb->state = W4_PACKET_TYPE;
    bt_cb_ready = RT_TRUE;
    return BT_ERR_OK;
}

void phybusif_open(uint32_t baud_rate)
{
    rt_ringbuffer_init(&bt_ring_buf, bt_rx_buf, BT_RX_BUF_SIZE);
    hw_uart_bt_init(baud_rate);
}

void phybusif_reopen(uint32_t baud_rate)
{
    rt_ringbuffer_reset(&bt_ring_buf);
    bt_cb_ready = RT_FALSE;

    if (bt_uart_dev != RT_NULL)
    {
        rt_device_set_rx_indicate(bt_uart_dev, RT_NULL);
        rt_device_close(bt_uart_dev);
        bt_uart_dev = RT_NULL;
    }

    if (hw_uart_bt_init(baud_rate) != HW_ERR_OK)
    {
        BT_TRANSPORT_TRACE_ERROR("uart2 reopen failed at baud %u\n", (unsigned int)baud_rate);
        return;
    }

    bt_cb_ready = RT_TRUE;
}

void phybusif_close()
{
    rt_ringbuffer_reset(&bt_ring_buf);

    if (bt_uart_dev != RT_NULL)
    {
        rt_device_set_rx_indicate(bt_uart_dev, RT_NULL);
        rt_device_close(bt_uart_dev);
        bt_uart_dev = RT_NULL;
    }
}



void phybusif_output(struct bt_pbuf_t *p, uint16_t len,uint8_t packet_type)
{
    bt_pbuf_header(p, 1);
    ((uint8_t *)p->payload)[0] = packet_type;

#if 0
    switch(packet_type)
    {
    case PHYBUSIF_PACKET_TYPE_CMD
    {
        ((uint8_t *)p->payload)[3] = ;
        break;
    }
    case PHYBUSIF_PACKET_TYPE_ACL_DATA
    {
        break;
    }
    case PHYBUSIF_PACKET_TYPE_SCO_DATA
    {
        break;
    }
    case PHYBUSIF_PACKET_TYPE_EVT
    {
        break;
    }
    default:
    {
        BT_TRANSPORT_TRACE_ERROR("ERROR:file[%s],function[%s],line[%d] packet_type is unvalid\n",__FILE__,__FUNCTION__,__LINE__);
        break;
    }
    }
#endif

    uint8_t *tx_buffer = bt_get_tx_buffer();
    bt_pbuf_copy_partial(p, tx_buffer, p->tot_len, 0);

    BT_TRANSPORT_TRACE_DEBUG("BT TX LEN:totol len:%d, len:%d\n",p->tot_len,len);
    bt_hex_dump(tx_buffer,len+1);

    uart_bt_send(tx_buffer,len+1);
}



err_t phybusif_input(struct phybusif_cb *cb)
{

    while(rt_ringbuffer_data_len(&bt_ring_buf) > 0)
    {
        //printf("state %d\n",cb->state);

        switch(cb->state)
        {
        case W4_PACKET_TYPE:
        {
            uint8_t packet_type = 0;

            rt_ringbuffer_get(&bt_ring_buf,&packet_type,1);
            //printf("+++++++++++++packet_type 0x%x\n",packet_type);
            switch(packet_type)
            {
            case PHYBUSIF_PACKET_TYPE_EVT:
            {
                cb->state = W4_EVENT_HDR;
                break;
            }
            case PHYBUSIF_PACKET_TYPE_ACL_DATA:
            {
                cb->state = W4_ACL_HDR;
                break;
            }
            default:
                break;
            }
            break;
        }
        case W4_EVENT_HDR:
        {
            if(rt_ringbuffer_data_len(&bt_ring_buf) < HCI_EVT_HDR_LEN)
            {
                return BT_ERR_BUF;
            }

            rt_ringbuffer_get(&bt_ring_buf,(uint8_t *)cb->p->payload,HCI_EVT_HDR_LEN);
            cb->evhdr = cb->p->payload;
            if(cb->evhdr->len > PBUF_POOL_BUFSIZE)
            {
                bt_pbuf_free(cb->p);
                BT_TRANSPORT_TRACE_WARNING("W4_EVENT_HDR len too large: %d\n", cb->evhdr->len);
                phybusif_reset(cb);
                return BT_ERR_BUF;
            }
            cb->state = W4_EVENT_PARAM;
            break;
        }
        case W4_ACL_HDR:
        {
            if(rt_ringbuffer_data_len(&bt_ring_buf) < HCI_ACL_HDR_LEN)
            {
                return BT_ERR_BUF;
            }

            rt_ringbuffer_get(&bt_ring_buf,(uint8_t *)cb->p->payload,HCI_ACL_HDR_LEN);
            cb->aclhdr = cb->p->payload;
            if(cb->aclhdr->len > PBUF_POOL_BUFSIZE)
            {
                bt_pbuf_free(cb->p);
                BT_TRANSPORT_TRACE_WARNING("W4_ACL_HDR len too large: %d\n", cb->aclhdr->len);
                phybusif_reset(cb);
                return BT_ERR_BUF;
            }
            cb->state = W4_ACL_DATA;
            break;
        }
        case W4_EVENT_PARAM:
        {
            if(rt_ringbuffer_data_len(&bt_ring_buf) < cb->evhdr->len)
            {
                return BT_ERR_BUF;
            }
            rt_ringbuffer_get(&bt_ring_buf,(uint8_t *)cb->p->payload+HCI_EVT_HDR_LEN,cb->evhdr->len);
            hci_event_input(cb->p);
            bt_pbuf_free(cb->p);
            phybusif_reset(cb);
            break;
        }
        case W4_ACL_DATA:
        {
            if(rt_ringbuffer_data_len(&bt_ring_buf) < cb->aclhdr->len)
            {
                return BT_ERR_BUF;
            }
            rt_ringbuffer_get(&bt_ring_buf,(uint8_t *)cb->p->payload+HCI_ACL_HDR_LEN,cb->aclhdr->len);
            hci_acl_input(cb->p);
            bt_pbuf_free(cb->p);
            phybusif_reset(cb);
            break;
        }
        default:
            break;
        }

    }
    return BT_ERR_OK;
}
