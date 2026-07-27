/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       LVGL custom font backed by W25Q128 font partition
 *
 * 作用: 把 FAL "font" 分区里的 ZBFT 点阵字库包装成 lv_font_t,
 *       供 LVGL 按需逐字读取点阵显示中文。详见 docs/lvgl_chinese_font_design.md。
 */
#ifndef APPLICATIONS_FONT_APP_H_
#define APPLICATIONS_FONT_APP_H_

#include <rtthread.h>
#include <lvgl.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* 校验并加载字库(读头部 + 缓存索引表到 RAM)。
 * 失败时 font_app_get16() 返回 NULL,GUI 应回退到 Montserrat。
 * 可重复调用,已就绪则直接返回。 */
rt_err_t font_app_init(void);

/* 取 16px 中文字体指针。未就绪返回 NULL。
 * 内部会在首次调用时惰性 init,但建议在 lv_user_gui_init() 里显式调一次。 */
const lv_font_t *font_app_get16(void);

rt_bool_t font_app_is_ready(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_FONT_APP_H_ */
