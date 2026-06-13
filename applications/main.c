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
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>
#include "bt_app.h"
#include "es8311_audio.h"
#include <control_app.h>
#include "mylvgl_app.h"

int main(void)
{
    if (es8311_audio_init() != RT_EOK)
    {
        LOG_E("es8311_audio_init failed");
    }
    boot_prompt_play_once();

    if (bt__init() != RT_EOK)
    {
        LOG_E("bt__init failed");
    }

    if (control_app_init() != RT_EOK)
    {
           LOG_E("control_app_init failed");
    }

    lv_user_gui_init();
    while (1)
    {
        rt_thread_mdelay(10);
    }

    return RT_EOK;
}
