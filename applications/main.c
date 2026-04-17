/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-03-28     RT-Thread    first version
 */

#include <rtthread.h>
#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
#include "bt_app.h"
#include "bt_i2s_player.h"

int main(void)
{
    if (bt_i2s_player_init() != RT_EOK)
    {
        LOG_E("bt_i2s_player_init failed");
    }

    if (bt__init() != RT_EOK)
    {
        LOG_E("bt__init failed");
    }

    while (1)
    {
        rt_thread_mdelay(10);
    }

    return RT_EOK;
}
