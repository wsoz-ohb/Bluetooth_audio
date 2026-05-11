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
#include "btstack_port.h"
#include "bt_a2dp_sink_app.h"
#include "bt_avrcp_ct_app.h"

#define DBG_TAG "bt_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static int bt_profiles_init(void)
{
    // 这里只负责注册蓝牙 profile。
    // 当前工程只保留 ES8311 音频链路，具体音频启停在 A2DP 事件里控制。
    if(bt_a2dp_sink_service_init() != RT_EOK)
    {
        return -RT_ERROR;
    }
    //AVRCP注册
    if(bt_avrcp_ct_service_init() != RT_EOK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t bt__init(void)
{
    int err;
    static rt_bool_t bt_app_inited = RT_FALSE;

    if (bt_app_inited)
    {
        return RT_EOK;
    }

    /* 第一步先把 BTstack 基础栈和本地设备配置准备好。 */
    err = btstack_port_init(NULL);
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_init failed: %d", err);
        return RT_ERROR;
    }

    /* 第二步只注册当前需要的蓝牙 profile 服务。 */
    /* 到这里仍然不触碰具体音频后端。 */
    err = bt_profiles_init();
    if (err != RT_EOK)
    {
        LOG_E("bt_profiles_init failed: %d", err);
        return RT_ERROR;
    }

    /* 第三步启动 BTstack 专用线程，并在该线程上下文里执行控制器上电。 */
    err = btstack_port_start_thread();
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_start_thread failed: %d", err);
        return RT_ERROR;
    }

    bt_app_inited = RT_TRUE;
    LOG_I("BT-STACK init ok");
    return RT_EOK;
}


