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
#include "bt_i2s_player.h"
#include "btstack_port.h"
#include "bt_a2dp_sink_app.h"

#define DBG_TAG "bt_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static int bt_profiles_init(void)
{
    int err;

    /*
     * 当前应用目标已经收敛为“蓝牙音频接收并本地播放”，
     * 因此本地音频输出链路初始化失败时，直接终止后续 profile 注册。
     */
    err = bt_i2s_player_init();
    if (err != RT_EOK)
    {
        LOG_E("bt_i2s_player_init failed: %d", err);
        return err;
    }

    return bt_a2dp_sink_service_init();
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

    /* 第二步注册当前需要的 profile 服务。 */
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
