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

#define DBG_TAG "bt_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

rt_err_t bt__init(void)
{
    int err;

    // 第一步先把 BTstack 基础栈和本地设备配置准备好.
    // 如果后面要加 A2DP/HID/SPP 这类 profile，推荐放在 btstack_port_init() 之后、
    // btstack_port_start_thread() 之前完成初始化和服务注册.
    err = btstack_port_init(NULL);
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_init failed: %d", err);
        return RT_ERROR;
    }

    // 第二步启动 BTstack 专用线程，并在该线程的 run loop 上下文里执行控制器上电.
    err = btstack_port_start_thread();
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_start_thread failed: %d", err);
        return RT_ERROR;
    }

    LOG_I("BT-STACK init ok");
    return RT_EOK;
}

