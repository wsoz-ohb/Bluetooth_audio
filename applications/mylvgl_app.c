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
#include "gui_manager.h"
#include "font_app.h"

#include <rtthread.h>
#include <lvgl.h>
#include <lcd.h>

#define DBG_TAG "mylvgl_app"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define MYLVGL_DRAW_BUF_LINES  20

static lv_disp_draw_buf_t s_draw_buf;
/* LCD 刷屏是 LCD_ShowPicture 纯 CPU 搬运(无 DMA),绘制缓冲放 CCM RAM */
static lv_color_t s_draw_buf_1[320 * MYLVGL_DRAW_BUF_LINES]
    __attribute__((section(".ccmbss.lvgl_draw")));
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
    if (lv_disp_get_default() == RT_NULL)
    {
        LOG_E("lvgl display is not registered yet");
        return;
    }

    /* 字库初始化放在 LVGL 线程内(此处即 LVGL 线程上下文):
     * 失败不阻断 GUI,font_app_get16() 会返回 NULL,GUI 回退 Montserrat。 */
    if (font_app_init() != RT_EOK)
    {
        LOG_W("font_app init failed, chinese text will fall back to english");
    }

    gui_manager_init();
}
