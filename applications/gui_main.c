/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui_main.h"
#include "bt_avrcp_ct_app.h"
#include "bt_a2dp_sink_app.h"

#include <rtthread.h>

#define DBG_TAG "gui_main"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define GUI_MAIN_BG_COLOR              0x101A33
#define GUI_MAIN_PANEL_COLOR           0x17213B
#define GUI_MAIN_PANEL_BORDER_COLOR    0x35527E
#define GUI_MAIN_ACCENT_COLOR          0x8FC3FF
#define GUI_MAIN_MUTED_TEXT_COLOR      0xB9C3D9
#define GUI_MAIN_HINT_TEXT_COLOR       0x7E8AA6
#define GUI_MAIN_REFRESH_PERIOD_MS     250u

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *link_value;
    lv_obj_t *stream_value;
    lv_obj_t *playback_value;
    lv_obj_t *op_value;
    lv_obj_t *hint_value;
    lv_timer_t *refresh_timer;
} gui_main_ctx_t;

static gui_main_ctx_t s_main;

static const char *gui_main_stream_state_text(void)
{
    if (bt_a2dp_sink_is_suspend_in_progress())
    {
        return "Suspending";
    }

    if (bt_a2dp_sink_is_stream_active())
    {
        return "Streaming";
    }

    return "Idle";
}

static const char *gui_main_hint_text(bt_avrcp_ct_link_state_t link_state,
                                      bt_avrcp_ct_playback_state_t playback_state)
{
    if (link_state != BT_AVRCP_CT_LINK_STATE_CONNECTED)
    {
        return "Waiting for Bluetooth connection";
    }

    if (playback_state == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING)
    {
        return "PC9 single: play/pause   double: next";
    }

    return "Rotate encoder to send volume commands";
}

static void gui_main_refresh(lv_timer_t *timer)
{
    bt_avrcp_ct_link_state_t link_state;
    bt_avrcp_ct_playback_state_t playback_state;
    bt_avrcp_ct_op_state_t op_state;

    LV_UNUSED(timer);

    link_state = bt_avrcp_ct_get_link_state();
    playback_state = bt_avrcp_ct_get_playback_state();
    op_state = bt_avrcp_ct_get_op_state();

    if (s_main.link_value != RT_NULL)
    {
        lv_label_set_text(s_main.link_value,
                          bt_avrcp_ct_link_state_name(link_state));
    }

    if (s_main.stream_value != RT_NULL)
    {
        lv_label_set_text(s_main.stream_value, gui_main_stream_state_text());
    }

    if (s_main.playback_value != RT_NULL)
    {
        lv_label_set_text(s_main.playback_value,
                          bt_avrcp_ct_playback_state_name(playback_state));
    }

    if (s_main.op_value != RT_NULL)
    {
        lv_label_set_text(s_main.op_value,
                          bt_avrcp_ct_op_state_name(op_state));
    }

    if (s_main.hint_value != RT_NULL)
    {
        lv_label_set_text(s_main.hint_value,
                          gui_main_hint_text(link_state, playback_state));
    }
}

static lv_obj_t *gui_main_create_row(lv_obj_t *parent,
                                     const char *title,
                                     lv_obj_t **value_label)
{
    lv_obj_t *row;
    lv_obj_t *title_label;
    lv_obj_t *value;

    row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 196, 24);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(GUI_MAIN_HINT_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 0, 0);

    value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_hex(GUI_MAIN_MUTED_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);

    *value_label = value;
    return row;
}

lv_obj_t *gui_main_create(void)
{
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *panel;
    lv_obj_t *row;

    if (s_main.refresh_timer != RT_NULL)
    {
        lv_timer_del(s_main.refresh_timer);
        s_main.refresh_timer = RT_NULL;
    }
    rt_memset(&s_main, 0, sizeof(s_main));

    scr = lv_obj_create(RT_NULL);
    s_main.screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(GUI_MAIN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(scr);
    lv_label_set_text(title, "Bluetooth Audio");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Device runtime overview");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(GUI_MAIN_HINT_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 224, 144);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 18);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(GUI_MAIN_PANEL_BORDER_COLOR), 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(GUI_MAIN_PANEL_COLOR), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(panel, 16, 0);
    lv_obj_set_style_pad_bottom(panel, 16, 0);
    lv_obj_set_style_pad_left(panel, 14, 0);
    lv_obj_set_style_pad_right(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    row = gui_main_create_row(panel, "Link", &s_main.link_value);
    lv_obj_set_style_text_color(s_main.link_value, lv_color_hex(GUI_MAIN_ACCENT_COLOR), 0);
    LV_UNUSED(row);

    row = gui_main_create_row(panel, "Stream", &s_main.stream_value);
    LV_UNUSED(row);

    row = gui_main_create_row(panel, "Playback", &s_main.playback_value);
    LV_UNUSED(row);

    row = gui_main_create_row(panel, "Operation", &s_main.op_value);
    LV_UNUSED(row);

    s_main.hint_value = lv_label_create(scr);
    lv_label_set_text(s_main.hint_value, "Waiting for Bluetooth connection");
    lv_obj_set_width(s_main.hint_value, 250);
    lv_obj_set_style_text_align(s_main.hint_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_main.hint_value, lv_color_hex(GUI_MAIN_HINT_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(s_main.hint_value, &lv_font_montserrat_14, 0);
    lv_obj_align(s_main.hint_value, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_main.refresh_timer = lv_timer_create(gui_main_refresh,
                                           GUI_MAIN_REFRESH_PERIOD_MS,
                                           RT_NULL);
    gui_main_refresh(RT_NULL);

    LOG_I("main screen built");
    return scr;
}
