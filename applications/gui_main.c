/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       music player main screen (title/artist/progress/disc)
 */
#include "gui_main.h"
#include "bt_avrcp_ct_app.h"
#include "bt_a2dp_sink_app.h"
#include "control_app.h"
#include "font_app.h"

#include <rtthread.h>
#include <string.h>

#define DBG_TAG "gui_main"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

/* 配色沿用 welcome / 旧 main 的深蓝体系 */
#define GUI_MAIN_BG_COLOR              0x101A33
#define GUI_MAIN_DISC_COLOR            0x1C2A4A
#define GUI_MAIN_DISC_RING_COLOR       0x35527E
#define GUI_MAIN_DISC_DOT_COLOR        0x8FC3FF
#define GUI_MAIN_DISC_HUB_COLOR        0x0B1224
#define GUI_MAIN_TITLE_COLOR           0xFFFFFF
#define GUI_MAIN_ARTIST_COLOR          0xB9C3D9
#define GUI_MAIN_HINT_COLOR            0x7E8AA6
#define GUI_MAIN_BAR_BG_COLOR          0x24324F
#define GUI_MAIN_BAR_IND_COLOR         0x8FC3FF
#define GUI_MAIN_TIME_COLOR            0x9AA6BF

#define GUI_MAIN_REFRESH_PERIOD_MS     400u
#define GUI_MAIN_DISC_SIZE             88
#define GUI_MAIN_DISC_INNER_SIZE       28
#define GUI_MAIN_DISC_DOT_SIZE         8
/* 一圈约 24s; 用 36 步×10° 而不是逐度插值,脏区刷新约 1.5Hz,更省 SPI */
#define GUI_MAIN_DISC_ROTATE_MS        24000u
#define GUI_MAIN_DISC_STEPS            36
#define GUI_MAIN_DISC_STEP_DEG         10
#define GUI_MAIN_BAR_RANGE             1000
#define GUI_MAIN_TEXT_CMP_MAX          64

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *disc;           /* 圆盘底座(不转) */
    lv_obj_t *disc_dot;       /* 边缘高亮点,绕圆心转 */
    lv_obj_t *title_label;
    lv_obj_t *artist_label;
    lv_obj_t *time_cur_label;
    lv_obj_t *time_total_label;
    lv_obj_t *progress_bar;
    lv_obj_t *status_label;
    lv_timer_t *refresh_timer;
    rt_bool_t disc_spinning;
    int32_t disc_angle_deg;   /* 当前角度 0~359 */
    char last_title[GUI_MAIN_TEXT_CMP_MAX];
    char last_artist[GUI_MAIN_TEXT_CMP_MAX];
    char last_status[GUI_MAIN_TEXT_CMP_MAX];
    uint32_t last_pos_ms;
    uint32_t last_len_ms;
} gui_main_ctx_t;

static gui_main_ctx_t s_main;

/* 避免引入完整 libm: 用 LVGL 自带三角函数(输入为整度) */
static void gui_main_place_disc_dot(int32_t angle_deg)
{
    lv_coord_t radius;
    lv_coord_t cx;
    lv_coord_t cy;
    lv_coord_t x;
    lv_coord_t y;
    int16_t angle;

    if (s_main.disc_dot == RT_NULL)
    {
        return;
    }

    /* 点中心所在圆周半径: 圆盘半径 - 边距 - 点半径 */
    radius = (GUI_MAIN_DISC_SIZE / 2) - 8 - (GUI_MAIN_DISC_DOT_SIZE / 2);
    cx = GUI_MAIN_DISC_SIZE / 2;
    cy = GUI_MAIN_DISC_SIZE / 2;

    /* lv_trigo_*: 入参为整度,内部会归一化到 0..359; 0 度在右侧。
     * 从顶部起步更自然,所以减 90 度。 */
    angle = (int16_t)(angle_deg - 90);
    x = cx + (lv_coord_t)((lv_trigo_cos(angle) * radius) / LV_TRIGO_SIN_MAX)
        - (GUI_MAIN_DISC_DOT_SIZE / 2);
    y = cy + (lv_coord_t)((lv_trigo_sin(angle) * radius) / LV_TRIGO_SIN_MAX)
        - (GUI_MAIN_DISC_DOT_SIZE / 2);

    lv_obj_set_pos(s_main.disc_dot, x, y);
}

static void gui_main_format_ms(uint32_t ms, char *buf, rt_size_t buf_len)
{
    uint32_t total_sec;
    uint32_t min;
    uint32_t sec;

    if ((buf == RT_NULL) || (buf_len < 8u))
    {
        return;
    }

    total_sec = ms / 1000u;
    min = total_sec / 60u;
    sec = total_sec % 60u;
    rt_snprintf(buf, buf_len, "%u:%02u", (unsigned int)min, (unsigned int)sec);
}

static const lv_font_t *gui_main_get_font(void)
{
    const lv_font_t *font;

    font = font_app_get16();
    if (font != RT_NULL)
    {
        return font;
    }
    return &lv_font_montserrat_14;
}

static void gui_main_disc_angle_cb(void *obj, int32_t step)
{
    int32_t angle;

    LV_UNUSED(obj);

    /* step 为 0..36 的步进序号,换算成 10° 一格的角度 */
    angle = (step % GUI_MAIN_DISC_STEPS) * GUI_MAIN_DISC_STEP_DEG;
    if (angle < 0)
    {
        angle += 360;
    }
    if (angle == s_main.disc_angle_deg)
    {
        return;
    }
    s_main.disc_angle_deg = angle;
    gui_main_place_disc_dot(s_main.disc_angle_deg);
}

static void gui_main_disc_stop(void)
{
    if (s_main.disc == RT_NULL)
    {
        return;
    }

    lv_anim_del(s_main.disc, gui_main_disc_angle_cb);
    s_main.disc_spinning = RT_FALSE;
}

static void gui_main_disc_start(void)
{
    lv_anim_t a;
    int32_t start_step;

    if (s_main.disc == RT_NULL)
    {
        return;
    }

    if (s_main.disc_spinning)
    {
        return;
    }

    start_step = (s_main.disc_angle_deg / GUI_MAIN_DISC_STEP_DEG) % GUI_MAIN_DISC_STEPS;

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_main.disc);
    lv_anim_set_exec_cb(&a, gui_main_disc_angle_cb);
    /* 只走 36 步,中间大量重复 step 会被 angle 去重直接 return */
    lv_anim_set_values(&a, start_step, start_step + GUI_MAIN_DISC_STEPS);
    lv_anim_set_time(&a, GUI_MAIN_DISC_ROTATE_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
    s_main.disc_spinning = RT_TRUE;
}

static void gui_main_set_label_if_changed(lv_obj_t *label,
                                          char *cache,
                                          rt_size_t cache_size,
                                          const char *text)
{
    if ((label == RT_NULL) || (text == RT_NULL) || (cache == RT_NULL) || (cache_size == 0u))
    {
        return;
    }

    if (rt_strncmp(cache, text, cache_size) == 0)
    {
        return;
    }

    rt_strncpy(cache, text, cache_size - 1u);
    cache[cache_size - 1u] = '\0';
    lv_label_set_text(label, text);
}

static rt_bool_t gui_main_ascii_case_equal(const char *lhs, const char *rhs)
{
    char lhs_ch;
    char rhs_ch;

    if ((lhs == RT_NULL) || (rhs == RT_NULL))
    {
        return RT_FALSE;
    }

    while ((*lhs != '\0') && (*rhs != '\0'))
    {
        lhs_ch = *lhs++;
        rhs_ch = *rhs++;
        if ((lhs_ch >= 'A') && (lhs_ch <= 'Z'))
        {
            lhs_ch = (char)(lhs_ch + ('a' - 'A'));
        }
        if ((rhs_ch >= 'A') && (rhs_ch <= 'Z'))
        {
            rhs_ch = (char)(rhs_ch + ('a' - 'A'));
        }
        if (lhs_ch != rhs_ch)
        {
            return RT_FALSE;
        }
    }

    return (rt_bool_t)((*lhs == '\0') && (*rhs == '\0'));
}

static rt_bool_t gui_main_meta_is_unavailable(const char *text)
{
    if ((text == RT_NULL) || (text[0] == '\0'))
    {
        return RT_TRUE;
    }

    return (rt_bool_t)(gui_main_ascii_case_equal(text, "Not Provided") ||
                       gui_main_ascii_case_equal(text, "Not Provide") ||
                       gui_main_ascii_case_equal(text, "Unknown"));
}

static const char *gui_main_status_text(bt_avrcp_ct_link_state_t link_state,
                                        bt_avrcp_ct_playback_state_t playback_state)
{
    /* PTT 长按采集优先显示 */
    if (control_app_is_capturing())
    {
        return "正在说话...";
    }

    if (link_state != BT_AVRCP_CT_LINK_STATE_CONNECTED)
    {
        return "未连接蓝牙";
    }

    switch (playback_state)
    {
    case BT_AVRCP_CT_PLAYBACK_STATE_PLAYING:
        if (bt_a2dp_sink_is_stream_active())
        {
            return "正在播放";
        }
        return "播放中";
    case BT_AVRCP_CT_PLAYBACK_STATE_PAUSED:
        return "已暂停";
    case BT_AVRCP_CT_PLAYBACK_STATE_STOPPED:
        return "已停止";
    default:
        return "已连接,等待播放";
    }
}

static void gui_main_refresh(lv_timer_t *timer)
{
    bt_avrcp_ct_link_state_t link_state;
    bt_avrcp_ct_playback_state_t playback_state;
    const char *title;
    const char *artist;
    const char *status;
    uint32_t pos_ms;
    uint32_t len_ms;
    char time_buf[8];
    int32_t bar_value;
    rt_bool_t should_spin;

    LV_UNUSED(timer);

    link_state = bt_avrcp_ct_get_link_state();
    playback_state = bt_avrcp_ct_get_playback_state();
    title = bt_avrcp_ct_get_title();
    artist = bt_avrcp_ct_get_artist();
    pos_ms = bt_avrcp_ct_get_song_position_ms();
    len_ms = bt_avrcp_ct_get_song_length_ms();
    status = gui_main_status_text(link_state, playback_state);

    /*
     * 暂停后再播时,部分手机会先推一帧 position=0,随后才是真实进度,
     * 进度条会闪回起点。若总时长仍有效且本地已有明显进度,则忽略这帧 0。
     * 真换歌时 AVRCP 会先把 length 清 0,不会误伤。
     */
    if ((pos_ms == 0u) &&
        (len_ms != 0u) &&
        (s_main.last_len_ms != 0u) &&
        (s_main.last_pos_ms > 1000u))
    {
        pos_ms = s_main.last_pos_ms;
    }

    if (gui_main_meta_is_unavailable(title))
    {
        if (link_state != BT_AVRCP_CT_LINK_STATE_CONNECTED)
        {
            title = "等待连接";
        }
        else
        {
            title = "未知歌曲";
        }
    }

    if (gui_main_meta_is_unavailable(artist))
    {
        if (link_state != BT_AVRCP_CT_LINK_STATE_CONNECTED)
        {
            artist = "请用手机连接本设备";
        }
        else
        {
            artist = "未知歌手";
        }
    }

    gui_main_set_label_if_changed(s_main.title_label,
                                  s_main.last_title,
                                  sizeof(s_main.last_title),
                                  title);
    gui_main_set_label_if_changed(s_main.artist_label,
                                  s_main.last_artist,
                                  sizeof(s_main.last_artist),
                                  artist);
    gui_main_set_label_if_changed(s_main.status_label,
                                  s_main.last_status,
                                  sizeof(s_main.last_status),
                                  status);

    if ((pos_ms != s_main.last_pos_ms) || (len_ms != s_main.last_len_ms))
    {
        s_main.last_pos_ms = pos_ms;
        s_main.last_len_ms = len_ms;

        if (s_main.time_cur_label != RT_NULL)
        {
            gui_main_format_ms(pos_ms, time_buf, sizeof(time_buf));
            lv_label_set_text(s_main.time_cur_label, time_buf);
        }

        if (s_main.time_total_label != RT_NULL)
        {
            if (len_ms == 0u)
            {
                lv_label_set_text(s_main.time_total_label, "--:--");
            }
            else
            {
                gui_main_format_ms(len_ms, time_buf, sizeof(time_buf));
                lv_label_set_text(s_main.time_total_label, time_buf);
            }
        }

        if (s_main.progress_bar != RT_NULL)
        {
            if (len_ms == 0u)
            {
                bar_value = 0;
            }
            else if (pos_ms >= len_ms)
            {
                bar_value = GUI_MAIN_BAR_RANGE;
            }
            else
            {
                bar_value = (int32_t)((pos_ms * (uint32_t)GUI_MAIN_BAR_RANGE) / len_ms);
            }
            lv_bar_set_value(s_main.progress_bar, bar_value, LV_ANIM_OFF);
        }
    }

    should_spin = (rt_bool_t)(playback_state == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING);
    if (should_spin)
    {
        gui_main_disc_start();
    }
    else
    {
        gui_main_disc_stop();
    }
}

static lv_obj_t *gui_main_create_disc(lv_obj_t *parent)
{
    lv_obj_t *disc;
    lv_obj_t *ring;
    lv_obj_t *hub;

    /* 圆盘底座: 静态矢量圆,不靠整层 transform,省离屏缓冲 */
    disc = lv_obj_create(parent);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, GUI_MAIN_DISC_SIZE, GUI_MAIN_DISC_SIZE);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(GUI_MAIN_DISC_COLOR), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 2, 0);
    lv_obj_set_style_border_color(disc, lv_color_hex(GUI_MAIN_DISC_RING_COLOR), 0);
    lv_obj_set_style_border_opa(disc, LV_OPA_COVER, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    /* 子对象用绝对坐标摆点,关闭布局干扰 */
    lv_obj_add_flag(disc, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* 内环装饰 */
    ring = lv_obj_create(disc);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, GUI_MAIN_DISC_SIZE - 18, GUI_MAIN_DISC_SIZE - 18);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(GUI_MAIN_DISC_RING_COLOR), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_60, 0);
    lv_obj_center(ring);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    /* 中心轴 */
    hub = lv_obj_create(disc);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, GUI_MAIN_DISC_INNER_SIZE, GUI_MAIN_DISC_INNER_SIZE);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, lv_color_hex(GUI_MAIN_DISC_HUB_COLOR), 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 2, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(GUI_MAIN_DISC_DOT_COLOR), 0);
    lv_obj_center(hub);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);

    /* 边缘高亮点: 绕圆心转,表示“碟片在转” */
    s_main.disc_dot = lv_obj_create(disc);
    lv_obj_remove_style_all(s_main.disc_dot);
    lv_obj_set_size(s_main.disc_dot, GUI_MAIN_DISC_DOT_SIZE, GUI_MAIN_DISC_DOT_SIZE);
    lv_obj_set_style_radius(s_main.disc_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_main.disc_dot, lv_color_hex(GUI_MAIN_DISC_DOT_COLOR), 0);
    lv_obj_set_style_bg_opa(s_main.disc_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_main.disc_dot, LV_OBJ_FLAG_SCROLLABLE);
    s_main.disc_angle_deg = 0;
    gui_main_place_disc_dot(0);

    return disc;
}

lv_obj_t *gui_main_create(void)
{
    lv_obj_t *scr;
    lv_obj_t *progress_row;
    const lv_font_t *font;

    if (s_main.refresh_timer != RT_NULL)
    {
        lv_timer_del(s_main.refresh_timer);
        s_main.refresh_timer = RT_NULL;
    }
    if (s_main.disc != RT_NULL)
    {
        gui_main_disc_stop();
    }
    rt_memset(&s_main, 0, sizeof(s_main));

    font = gui_main_get_font();

    scr = lv_obj_create(RT_NULL);
    s_main.screen = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(GUI_MAIN_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 圆盘 ---- */
    s_main.disc = gui_main_create_disc(scr);
    lv_obj_align(s_main.disc, LV_ALIGN_TOP_MID, 0, 12);

    /* ---- 歌名 ---- */
    s_main.title_label = lv_label_create(scr);
    lv_label_set_text(s_main.title_label, "等待连接");
    lv_obj_set_width(s_main.title_label, 280);
    lv_obj_set_style_text_align(s_main.title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_main.title_label, lv_color_hex(GUI_MAIN_TITLE_COLOR), 0);
    lv_obj_set_style_text_font(s_main.title_label, font, 0);
    lv_label_set_long_mode(s_main.title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_clear_flag(s_main.title_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_main.title_label, LV_ALIGN_TOP_MID, 0, 110);

    /* ---- 歌手 ---- */
    s_main.artist_label = lv_label_create(scr);
    lv_label_set_text(s_main.artist_label, "请用手机连接本设备");
    lv_obj_set_width(s_main.artist_label, 280);
    lv_obj_set_style_text_align(s_main.artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_main.artist_label, lv_color_hex(GUI_MAIN_ARTIST_COLOR), 0);
    lv_obj_set_style_text_font(s_main.artist_label, font, 0);
    lv_label_set_long_mode(s_main.artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_clear_flag(s_main.artist_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_main.artist_label, LV_ALIGN_TOP_MID, 0, 132);

    /* ---- 进度行: 当前时间 | bar | 总时长 ---- */
    progress_row = lv_obj_create(scr);
    lv_obj_remove_style_all(progress_row);
    lv_obj_set_size(progress_row, 292, 22);
    lv_obj_align(progress_row, LV_ALIGN_TOP_MID, 0, 164);
    lv_obj_clear_flag(progress_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(progress_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(progress_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(progress_row, 6, 0);

    s_main.time_cur_label = lv_label_create(progress_row);
    lv_label_set_text(s_main.time_cur_label, "0:00");
    lv_obj_set_style_text_color(s_main.time_cur_label, lv_color_hex(GUI_MAIN_TIME_COLOR), 0);
    lv_obj_set_style_text_font(s_main.time_cur_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_main.time_cur_label, 40);

    s_main.progress_bar = lv_bar_create(progress_row);
    lv_obj_set_size(s_main.progress_bar, 196, 6);
    lv_bar_set_range(s_main.progress_bar, 0, GUI_MAIN_BAR_RANGE);
    lv_bar_set_value(s_main.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_main.progress_bar, lv_color_hex(GUI_MAIN_BAR_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_main.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_main.progress_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_main.progress_bar, lv_color_hex(GUI_MAIN_BAR_IND_COLOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_main.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_main.progress_bar, 3, LV_PART_INDICATOR);

    s_main.time_total_label = lv_label_create(progress_row);
    lv_label_set_text(s_main.time_total_label, "--:--");
    lv_obj_set_style_text_color(s_main.time_total_label, lv_color_hex(GUI_MAIN_TIME_COLOR), 0);
    lv_obj_set_style_text_font(s_main.time_total_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_main.time_total_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_main.time_total_label, 40);

    /* ---- 底部状态 ---- */
    s_main.status_label = lv_label_create(scr);
    lv_label_set_text(s_main.status_label, "未连接蓝牙");
    lv_obj_set_width(s_main.status_label, 280);
    lv_obj_set_style_text_align(s_main.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_main.status_label, lv_color_hex(GUI_MAIN_HINT_COLOR), 0);
    lv_obj_set_style_text_font(s_main.status_label, font, 0);
    lv_obj_align(s_main.status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_main.refresh_timer = lv_timer_create(gui_main_refresh,
                                           GUI_MAIN_REFRESH_PERIOD_MS,
                                           RT_NULL);
    gui_main_refresh(RT_NULL);

    LOG_I("player main screen built");
    return scr;
}
