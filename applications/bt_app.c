/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-02     wsoz       the first version
 */
#include "bt_app.h"
#include "bt_common.h"
#include <rtthread.h>
#include <rtdevice.h>
#define DBG_TAG "bt_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static bt_app_common_cb_t g_common_cb;
static bt_app_a2dp_cb_t g_a2dp_cb;
bt_app_cb_t bt_app_cb = {0};
rt_thread_t timer_polling_thread = RT_NULL;
static rt_mutex_t g_bt_stack_lock = RT_NULL;
static struct bd_addr_t g_remote_addr = {0}; // 远程设备地址


static rt_err_t bt_stack_lock_take(void)
{
    if (g_bt_stack_lock == RT_NULL)
    {
        return RT_ERROR;
    }
    return rt_mutex_take(g_bt_stack_lock, RT_WAITING_FOREVER);
}

static void bt_stack_lock_release(void)
{
    if (g_bt_stack_lock != RT_NULL)
    {
        rt_mutex_release(g_bt_stack_lock);
    }
}

//com cb
static void bt_init_result(uint8_t status,uint16_t profile_mask)
{
    LOG_I("bt init status:%d,mask:%d\r\n",status,profile_mask);
    if (status==BT_INIT_FAIL)
    {
        LOG_E("bt init fail,try again\r\n");
        if (bt_stack_lock_take() == RT_EOK)
        {
            bt_start(&bt_app_cb);    //启动蓝牙协议栈
            bt_stack_lock_release();
        }
    }
}

static void bt_inquiry_status(uint8_t status)
{
    switch (status)
    {
        case BT_INQUIRY_START:
            LOG_I("bt inquiry start\r\n");
            break;

        case BT_INQUIRY_COMPLETE:
            LOG_I("bt inquiry complete\r\n");
            break;
    }
}

static void bt_inquiry_result(struct bd_addr_t *address,uint8_t dev_type,uint8_t *name)
{
    LOG_I("bt inquiry result:addr:%02x:%02x:%02x:%02x:%02x:%02x,dev_type:%d,name:%s\r\n",
        address->addr[5],address->addr[4],address->addr[3],address->addr[2],address->addr[1],address->addr[0],
        dev_type,name);
}


static void bt_hardware_error_app(uint8_t reason)
{
    LOG_E("BT hardware error, reason=0x%02X", reason);

    /*
     * HCI Hardware Error event 的 reason 是控制器厂商自定义码，
     */
    if (bt_stack_lock_take() == RT_EOK)
    {
        bt_stop();
        bt_stack_lock_release();
    }
    LOG_I("bt stoped\r\n");
    memset(&g_remote_addr, 0, sizeof(g_remote_addr));

    /* 建议：这里仅置标志，在工作线程里执行 bt_stop()/bt_start() 做重启恢复 */
}

//a2dp cb
static void bt_a2dp_signal_connect(struct bd_addr_t *remote_addr,uint8_t status)
{

}

void bt_a2dp_signal_disconnect(struct bd_addr_t *remote_addr,uint8_t status)
{

}

void bt_a2dp_stream_connect(struct bd_addr_t *remote_addr,uint8_t status)
{

}

void bt_a2dp_stream_disconnect(struct bd_addr_t *remote_addr,uint8_t status)
{

}

void bt_a2dp_start(struct bd_addr_t *remote_addr,uint8_t value)
{

}

void bt_a2dp_relase(struct bd_addr_t *remote_addr,uint8_t value)
{

}

void bt_a2dp_suspend(struct bd_addr_t *remote_addr,uint8_t value)
{

}

void bt_a2dp_abort(struct bd_addr_t *remote_addr,uint8_t value)
{

}

static void bt_timer(void *parameter)
{
    rt_tick_t last_1s = rt_tick_get();
    (void)parameter;

    while(1)
    {
        if (bt_stack_lock_take() == RT_EOK)
        {
            utimer_polling();   //维护线程

            if (rt_tick_get() - last_1s >= RT_TICK_PER_SECOND)
            {
                last_1s += RT_TICK_PER_SECOND;
                l2cap_tmr();
                rfcomm_tmr();
            }

            bt_stack_lock_release();
        }

        rt_thread_mdelay(1);
    }
}

rt_err_t bt__init(void)
{
    g_bt_stack_lock = rt_mutex_create("bt_lock", RT_IPC_FLAG_FIFO);
    if (g_bt_stack_lock == RT_NULL)
    {
        return RT_ERROR;
    }

    timer_polling_thread = rt_thread_create("BT_TIMER", bt_timer, RT_NULL, 2048, 11, 10);
    if (timer_polling_thread == RT_NULL)
    {
        rt_mutex_delete(g_bt_stack_lock);
        g_bt_stack_lock = RT_NULL;
        return RT_ERROR;
    }

    rt_thread_startup(timer_polling_thread);

    g_common_cb.bt_hardware_error = bt_hardware_error_app;
    g_common_cb.bt_init_result = bt_init_result;
    g_common_cb.bt_inquiry_status = bt_inquiry_status;
    g_common_cb.bt_inquiry_result = bt_inquiry_result;

    g_a2dp_cb.bt_a2dp_abort=&bt_a2dp_abort;
    g_a2dp_cb.bt_a2dp_relase=&bt_a2dp_relase;
    g_a2dp_cb.bt_a2dp_signal_connect=&bt_a2dp_signal_connect;
    g_a2dp_cb.bt_a2dp_signal_disconnect=&bt_a2dp_signal_disconnect;
    g_a2dp_cb.bt_a2dp_start=&bt_a2dp_start;
    g_a2dp_cb.bt_a2dp_stream_connect=&bt_a2dp_stream_connect;
    g_a2dp_cb.bt_a2dp_stream_disconnect=&bt_a2dp_stream_disconnect;
    g_a2dp_cb.bt_a2dp_suspend=&bt_a2dp_suspend;

    bt_app_cb.app_common_cb = &g_common_cb;
    bt_app_cb.app_a2dp_cb = &g_a2dp_cb;


    if (bt_stack_lock_take() != RT_EOK)
    {
        return RT_ERROR;
    }
    bt_start(&bt_app_cb);    //启动蓝牙协议栈
    bt_stack_lock_release();
    LOG_I("bt init ok\r\n");
    return RT_EOK;
}
