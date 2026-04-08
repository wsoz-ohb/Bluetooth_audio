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

    err = btstack_port_init(NULL);
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_init failed: %d", err);
        return RT_ERROR;
    }

    err = btstack_port_start_thread();
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_start_thread failed: %d", err);
        return RT_ERROR;
    }

    LOG_I("BT-STACK init ok");
    return RT_EOK;
}
