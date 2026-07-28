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
#include "gui_manager.h"
#include "sfud_app.h"
#include "fs_app.h"

int main(void)
{
    if (sfud_app_init() != RT_EOK)
    {
        LOG_E("sfud_app_init failed");
    }

    /* Flash 就绪后挂 littlefs；失败不阻断音箱主链，只是 PTT 无法落盘 */
    if (fs_app_init() != RT_EOK)
    {
        LOG_E("fs_app_init failed (PTT file record disabled)");
    }

    if (es8311_audio_init() != RT_EOK)
    {
        LOG_E("es8311_audio_init failed");
    }
    boot_prompt_play_once();
    /* 提示音是同步阻塞播放的，返回即代表播放完成。
     * 通知 LVGL 线程可以淡出欢迎界面、切换到主界面。 */
    mylvgl_notify_boot_prompt_done();

    if (bt__init() != RT_EOK)
    {
        LOG_E("bt__init failed");
    }

    if (control_app_init() != RT_EOK)
    {
           LOG_E("control_app_init failed");
    }

    while (1)
    {
        rt_thread_mdelay(10);
    }

    return RT_EOK;
}
