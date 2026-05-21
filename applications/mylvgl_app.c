/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-05-15     26410       the first version
 */
#include "mylvgl_app.h"
#include "lcd_app.h"

#include <lvgl.h>
#include <lcd.h>

#define DBG_TAG "mylvgl_app"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define MYLVGL_DRAW_BUF_LINES  20

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_draw_buf_1[320 * MYLVGL_DRAW_BUF_LINES];
static lv_disp_drv_t s_disp_drv;
static rt_bool_t s_disp_inited = RT_FALSE;

static void mylvgl_rgb565_swap(rt_uint8_t *buf, rt_size_t pixel_count)
{
    rt_size_t i;

    for (i = 0; i < pixel_count; i++)
    {
        rt_uint8_t tmp = buf[2 * i];
        buf[2 * i] = buf[2 * i + 1];
        buf[2 * i + 1] = tmp;
    }
}

static void mylvgl_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    rt_uint16_t width;
    rt_uint16_t height;
    rt_size_t pixel_count;

    if (!LCD_IsReady())
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    width = (rt_uint16_t)(area->x2 - area->x1 + 1);
    height = (rt_uint16_t)(area->y2 - area->y1 + 1);
    pixel_count = (rt_size_t)width * height;

#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
    mylvgl_rgb565_swap((rt_uint8_t *)color_p, pixel_count);
#endif

    LCD_ShowPicture((u16)area->x1, (u16)area->y1, width, height, (const u8 *)color_p);
    lv_disp_flush_ready(disp_drv);
}

void lv_port_disp_init(void)
{
    rt_err_t err;
    rt_uint16_t width;
    rt_uint16_t height;
    rt_size_t buf_pixels;

    if (s_disp_inited)
    {
        return;
    }

    err = lcd_app_init();
    if (err != RT_EOK)
    {
        LOG_E("lcd_app_init failed: %d", err);
        return;
    }

    width = LCD_GetWidth();
    height = LCD_GetHeight();
    if (width == 0 || height == 0)
    {
        LOG_E("invalid lcd geometry: %d x %d", width, height);
        return;
    }

    buf_pixels = (rt_size_t)width * MYLVGL_DRAW_BUF_LINES;
    lv_disp_draw_buf_init(&s_draw_buf, s_draw_buf_1, RT_NULL, buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = width;
    s_disp_drv.ver_res = height;
    s_disp_drv.flush_cb = mylvgl_disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    s_disp_inited = RT_TRUE;
    LOG_I("lvgl display init ok, res=%dx%d, buf_lines=%d", width, height, MYLVGL_DRAW_BUF_LINES);
}

void lv_port_indev_init(void)
{

}


void lv_user_gui_init(void)
{
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *bar;

    if (lv_disp_get_default() == RT_NULL)
    {
        LOG_E("lvgl display is not registered yet");
        return;
    }

    scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x09111F), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x132238), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    title = lv_label_create(scr);
    lv_label_set_text(title, "Bluetooth Audio");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    label = lv_label_create(scr);
    lv_label_set_text(label, "ST7789  LVGL  Static Demo");
    lv_obj_set_style_text_color(label, lv_palette_lighten(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_align_to(label, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    card = lv_obj_create(scr);
    lv_obj_set_size(card, 212, 148);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 14);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C2541), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(card);
    lv_label_set_text(label, "LVGL demo ready");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    label = lv_label_create(card);
    lv_label_set_text(label, "LCD : ST7789 SPI1\nAudio : ES8311\nMode : A2DP Sink");
    lv_obj_set_style_text_color(label, lv_palette_lighten(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 30);

    bar = lv_bar_create(card);
    lv_obj_set_size(bar, 160, 10);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 72, LV_ANIM_OFF);

    label = lv_label_create(card);
    lv_label_set_text(label, "Volume 72%");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);

    LOG_I("static lvgl demo started");
}
