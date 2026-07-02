/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_GUI_MANAGER_H_
#define APPLICATIONS_GUI_MANAGER_H_

typedef enum
{
    GUI_SCREEN_WELCOME = 0,
    GUI_SCREEN_MAIN,
} gui_screen_id_t;

void gui_manager_init(void);
void gui_manager_show_screen(gui_screen_id_t screen_id);
void mylvgl_notify_boot_prompt_done(void);

#endif /* APPLICATIONS_GUI_MANAGER_H_ */
