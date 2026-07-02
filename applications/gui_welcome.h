/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_GUI_WELCOME_H_
#define APPLICATIONS_GUI_WELCOME_H_

#include <lvgl.h>

typedef void (*gui_welcome_ready_cb_t)(void);

lv_obj_t *gui_welcome_create(gui_welcome_ready_cb_t ready_cb);
void gui_welcome_notify_boot_prompt_done(void);
void gui_welcome_prepare_for_unload(void);

#endif /* APPLICATIONS_GUI_WELCOME_H_ */
