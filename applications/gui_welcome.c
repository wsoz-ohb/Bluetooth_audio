/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui_welcome.h"

#include <rtthread.h>

#define DBG_TAG "gui_welcome"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define WELCOME_TITLE_TEXT             "Welcome"
#define WELCOME_SUBTITLE_TEXT          "ZhiBan Smart Companion"
#define WELCOME_STATUS_TEXT            "Starting..."
#define WELCOME_TITLE_PERIOD_MS        70u
#define WELCOME_SUBTITLE_PERIOD_MS     80u
#define WELCOME_WATCH_PERIOD_MS        50u
#define WELCOME_FALLBACK_MS            6000u
#define WELCOME_EXIT_WAIT_MS           350u
#define WELCOME_EXIT_ANIM_TIME_MS      160u
#define WELCOME_BG_COLOR               0x101A33
#define WELCOME_TITLE_COLOR            0xFFFFFF
#define WELCOME_SUBTITLE_COLOR         0xC6D0E3
#define WELCOME_STATUS_COLOR           0x7E8AA6
#define WELCOME_CURSOR_COLOR           0x8FC3FF

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *content;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *status;
    lv_obj_t *cursor;
    lv_timer_t *type_timer;
    lv_timer_t *watch_timer;
    gui_welcome_ready_cb_t ready_cb;
    uint16_t title_pos;
    uint16_t subtitle_pos;
    uint32_t elapsed_ms;
    uint32_t type_done_wait_ms;
    rt_bool_t type_done;
    rt_bool_t exit_started;
} gui_welcome_ctx_t;

static gui_welcome_ctx_t s_welcome;
static volatile rt_bool_t s_boot_prompt_done = RT_FALSE;

static void gui_welcome_cursor_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void gui_welcome_content_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void gui_welcome_exit_ready_cb(lv_anim_t *anim)
{
    LV_UNUSED(anim);

    if (s_welcome.ready_cb != RT_NULL)
    {
        s_welcome.ready_cb();
    }
}

static void gui_welcome_update_cursor(lv_obj_t *anchor)
{
    if (s_welcome.cursor == RT_NULL || anchor == RT_NULL)
    {
        return;
    }

    lv_obj_update_layout(anchor);
    if (s_welcome.content != RT_NULL)
    {
        lv_obj_update_layout(s_welcome.content);
    }
    lv_obj_align_to(s_welcome.cursor, anchor, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
}

static void gui_welcome_refresh_status(void)
{
    if (s_welcome.status == RT_NULL)
    {
        return;
    }

    if (s_welcome.exit_started)
    {
        lv_label_set_text(s_welcome.status, "");
        return;
    }

    if (s_boot_prompt_done && s_welcome.type_done)
    {
        lv_label_set_text(s_welcome.status, "Ready");
        return;
    }

    if (s_boot_prompt_done)
    {
        lv_label_set_text(s_welcome.status, "Preparing interface...");
        return;
    }

    lv_label_set_text(s_welcome.status, WELCOME_STATUS_TEXT);
}

static void gui_welcome_finish_typing(void)
{
    s_welcome.type_done = RT_TRUE;
    s_welcome.type_done_wait_ms = 0;
    gui_welcome_update_cursor(s_welcome.subtitle);
    gui_welcome_refresh_status();

    if (s_welcome.cursor != RT_NULL)
    {
        lv_anim_del(s_welcome.cursor, gui_welcome_cursor_opa_cb);
        lv_obj_add_flag(s_welcome.cursor, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_welcome.type_timer != RT_NULL)
    {
        lv_timer_del(s_welcome.type_timer);
        s_welcome.type_timer = RT_NULL;
    }
}

static void gui_welcome_type_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_welcome.title_pos < (uint16_t)sizeof(WELCOME_TITLE_TEXT) - 1u)
    {
        char buf[sizeof(WELCOME_TITLE_TEXT)];

        s_welcome.title_pos++;
        rt_memcpy(buf, WELCOME_TITLE_TEXT, s_welcome.title_pos);
        buf[s_welcome.title_pos] = '\0';
        lv_label_set_text(s_welcome.title, buf);
        lv_obj_clear_flag(s_welcome.cursor, LV_OBJ_FLAG_HIDDEN);
        gui_welcome_update_cursor(s_welcome.title);
        return;
    }

    if (s_welcome.subtitle_pos == 0u && s_welcome.type_timer != RT_NULL)
    {
        lv_timer_set_period(s_welcome.type_timer, WELCOME_SUBTITLE_PERIOD_MS);
    }

    if (s_welcome.subtitle_pos < (uint16_t)sizeof(WELCOME_SUBTITLE_TEXT) - 1u)
    {
        char buf[sizeof(WELCOME_SUBTITLE_TEXT)];

        s_welcome.subtitle_pos++;
        rt_memcpy(buf, WELCOME_SUBTITLE_TEXT, s_welcome.subtitle_pos);
        buf[s_welcome.subtitle_pos] = '\0';
        lv_label_set_text(s_welcome.subtitle, buf);
        gui_welcome_update_cursor(s_welcome.subtitle);
        return;
    }

    gui_welcome_finish_typing();
}

static void gui_welcome_start_exit(void)
{
    lv_anim_t anim;
    lv_coord_t current_y;

    if (s_welcome.exit_started)
    {
        return;
    }

    s_welcome.exit_started = RT_TRUE;
    gui_welcome_refresh_status();

    if (s_welcome.watch_timer != RT_NULL)
    {
        lv_timer_del(s_welcome.watch_timer);
        s_welcome.watch_timer = RT_NULL;
    }

    if (s_welcome.type_timer != RT_NULL)
    {
        lv_timer_del(s_welcome.type_timer);
        s_welcome.type_timer = RT_NULL;
    }

    if (s_welcome.cursor != RT_NULL)
    {
        lv_anim_del(s_welcome.cursor, gui_welcome_cursor_opa_cb);
        lv_obj_set_style_bg_opa(s_welcome.cursor, LV_OPA_40, 0);
    }

    current_y = lv_obj_get_y(s_welcome.content);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_welcome.content);
    lv_anim_set_exec_cb(&anim, gui_welcome_content_y_cb);
    lv_anim_set_values(&anim, current_y, current_y - 10);
    lv_anim_set_time(&anim, WELCOME_EXIT_ANIM_TIME_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&anim, gui_welcome_exit_ready_cb);
    lv_anim_start(&anim);
}

static void gui_welcome_watch_timer_cb(lv_timer_t *timer)
{
    rt_bool_t ready;
    rt_bool_t timeout;

    LV_UNUSED(timer);

    if (s_welcome.exit_started)
    {
        return;
    }

    s_welcome.elapsed_ms += WELCOME_WATCH_PERIOD_MS;
    timeout = (rt_bool_t)(s_welcome.elapsed_ms >= WELCOME_FALLBACK_MS);

    if (s_welcome.type_done)
    {
        s_welcome.type_done_wait_ms += WELCOME_WATCH_PERIOD_MS;
    }

    gui_welcome_refresh_status();

    ready = (rt_bool_t)(s_boot_prompt_done && s_welcome.type_done &&
                        s_welcome.type_done_wait_ms >= WELCOME_EXIT_WAIT_MS);

    if (ready || timeout)
    {
        gui_welcome_start_exit();
    }
}

static void gui_welcome_build_screen(void)
{
    lv_obj_t *scr;
    lv_obj_t *content;

    scr = lv_obj_create(RT_NULL);
    s_welcome.screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(WELCOME_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    content = lv_obj_create(scr);
    s_welcome.content = content;
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 240, 120);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, -2);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_welcome.title = lv_label_create(content);
    lv_label_set_text(s_welcome.title, "");
    lv_obj_set_style_text_color(s_welcome.title, lv_color_hex(WELCOME_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(s_welcome.title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(s_welcome.title, 3, 0);
    lv_obj_align(s_welcome.title, LV_ALIGN_CENTER, 0, -18);

    s_welcome.cursor = lv_obj_create(content);
    lv_obj_remove_style_all(s_welcome.cursor);
    lv_obj_add_flag(s_welcome.cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_welcome.cursor, 2, 16);
    lv_obj_set_style_bg_color(s_welcome.cursor, lv_color_hex(WELCOME_CURSOR_COLOR), 0);
    lv_obj_set_style_bg_opa(s_welcome.cursor, LV_OPA_70, 0);

    {
        lv_anim_t cursor_anim;

        lv_anim_init(&cursor_anim);
        lv_anim_set_var(&cursor_anim, s_welcome.cursor);
        lv_anim_set_exec_cb(&cursor_anim, gui_welcome_cursor_opa_cb);
        lv_anim_set_values(&cursor_anim, LV_OPA_70, LV_OPA_20);
        lv_anim_set_time(&cursor_anim, 720);
        lv_anim_set_playback_time(&cursor_anim, 720);
        lv_anim_set_repeat_count(&cursor_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&cursor_anim);
    }

    s_welcome.subtitle = lv_label_create(content);
    lv_label_set_text(s_welcome.subtitle, "");
    lv_obj_set_style_text_align(s_welcome.subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_welcome.subtitle, lv_color_hex(WELCOME_SUBTITLE_COLOR), 0);
    lv_obj_set_style_text_font(s_welcome.subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align(s_welcome.subtitle, LV_ALIGN_CENTER, 0, 16);

    s_welcome.status = lv_label_create(content);
    lv_label_set_text(s_welcome.status, WELCOME_STATUS_TEXT);
    lv_obj_set_style_text_color(s_welcome.status, lv_color_hex(WELCOME_STATUS_COLOR), 0);
    lv_obj_set_style_text_font(s_welcome.status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_welcome.status, LV_ALIGN_CENTER, 0, 48);

    s_welcome.type_timer = lv_timer_create(gui_welcome_type_timer_cb,
                                           WELCOME_TITLE_PERIOD_MS, RT_NULL);
    s_welcome.watch_timer = lv_timer_create(gui_welcome_watch_timer_cb,
                                            WELCOME_WATCH_PERIOD_MS, RT_NULL);
}

lv_obj_t *gui_welcome_create(gui_welcome_ready_cb_t ready_cb)
{
    gui_welcome_prepare_for_unload();
    s_boot_prompt_done = RT_FALSE;
    rt_memset(&s_welcome, 0, sizeof(s_welcome));
    s_welcome.ready_cb = ready_cb;

    gui_welcome_build_screen();
    gui_welcome_refresh_status();
    LOG_I("welcome screen built");
    return s_welcome.screen;
}

void gui_welcome_notify_boot_prompt_done(void)
{
    s_boot_prompt_done = RT_TRUE;
    LOG_I("boot prompt done notified");
}

void gui_welcome_prepare_for_unload(void)
{
    if (s_welcome.type_timer != RT_NULL)
    {
        lv_timer_del(s_welcome.type_timer);
        s_welcome.type_timer = RT_NULL;
    }

    if (s_welcome.watch_timer != RT_NULL)
    {
        lv_timer_del(s_welcome.watch_timer);
        s_welcome.watch_timer = RT_NULL;
    }

    if (s_welcome.cursor != RT_NULL)
    {
        lv_anim_del(s_welcome.cursor, gui_welcome_cursor_opa_cb);
    }

    if (s_welcome.content != RT_NULL)
    {
        lv_anim_del(s_welcome.content, gui_welcome_content_y_cb);
    }

    s_welcome.exit_started = RT_TRUE;
}
