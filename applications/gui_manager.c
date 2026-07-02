/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui_manager.h"
#include "gui_main.h"
#include "gui_welcome.h"

#include <rtthread.h>

#define DBG_TAG "gui_manager"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define GUI_SWITCH_ANIM_TIME_MS  0u

static lv_obj_t *s_current_screen = RT_NULL;
static gui_screen_id_t s_current_screen_id = GUI_SCREEN_WELCOME;
static rt_bool_t s_gui_inited = RT_FALSE;

static void gui_manager_on_welcome_ready(void)
{
    gui_manager_show_screen(GUI_SCREEN_MAIN);
}

static lv_obj_t *gui_manager_build_screen(gui_screen_id_t screen_id)
{
    switch (screen_id)
    {
    case GUI_SCREEN_WELCOME:
        return gui_welcome_create(gui_manager_on_welcome_ready);

    case GUI_SCREEN_MAIN:
        return gui_main_create();

    default:
        LOG_E("unsupported screen id: %d", screen_id);
        return RT_NULL;
    }
}

void gui_manager_init(void)
{
    if (s_gui_inited)
    {
        return;
    }

    s_gui_inited = RT_TRUE;
    s_current_screen = RT_NULL;
    gui_manager_show_screen(GUI_SCREEN_WELCOME);
}

void gui_manager_show_screen(gui_screen_id_t screen_id)
{
    lv_obj_t *next_screen;

    if (screen_id != GUI_SCREEN_WELCOME && s_current_screen_id == GUI_SCREEN_WELCOME)
    {
        gui_welcome_prepare_for_unload();
    }

    next_screen = gui_manager_build_screen(screen_id);
    if (next_screen == RT_NULL)
    {
        return;
    }

    if (s_current_screen == RT_NULL)
    {
        lv_scr_load(next_screen);
    }
    else
    {
        lv_scr_load_anim(next_screen, LV_SCR_LOAD_ANIM_NONE,
                         GUI_SWITCH_ANIM_TIME_MS, 0, true);
    }

    s_current_screen = next_screen;
    s_current_screen_id = screen_id;
    LOG_I("screen switched to %d", screen_id);
}

void mylvgl_notify_boot_prompt_done(void)
{
    gui_welcome_notify_boot_prompt_done();
}
