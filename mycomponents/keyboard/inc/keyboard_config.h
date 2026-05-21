/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     wsoz       the first version
 */
#ifndef MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_CONFIG_H_
#define MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_CONFIG_H_

#include <stdint.h>

/* 内存池总大小（字节），用于按键节点分配 */
#define KEYBOARD_POOL_SIZE 512u

/* 最大按键数量（独立按键/矩阵按键都使用这个上限） */
#ifndef KB_MAX_KEYS
#define KB_MAX_KEYS 16u
#endif

/* 去抖/长按/连发的默认时间参数（ms） */
#ifndef KB_DEBOUNCE_MS
#define KB_DEBOUNCE_MS 20u
#endif

#ifndef KB_LONGPRESS_MS
#define KB_LONGPRESS_MS 800u
#endif

#ifndef KB_REPEAT_START_MS
#define KB_REPEAT_START_MS 500u
#endif

#ifndef KB_REPEAT_PERIOD_MS
#define KB_REPEAT_PERIOD_MS 80u
#endif

#ifndef KB_DOUBLE_CLICK_MS
#define KB_DOUBLE_CLICK_MS 250u
#endif

/*
 * 电平极性配置：
 * - GPIO/矩阵列输入，按下时是高电平(1)还是低电平(0)
 * - 矩阵行输出，选通电平是高(1)还是低(0)
 */
#ifndef KB_GPIO_ACTIVE_LEVEL
#define KB_GPIO_ACTIVE_LEVEL 1u
#endif

#ifndef KB_MATRIX_ACTIVE_LEVEL
#define KB_MATRIX_ACTIVE_LEVEL 1u
#endif

#ifndef KB_MATRIX_ROW_ACTIVE_LEVEL
#define KB_MATRIX_ROW_ACTIVE_LEVEL 1u
#endif

#define KB_MATRIX_ROW_IDLE_LEVEL ((KB_MATRIX_ROW_ACTIVE_LEVEL) ? 0u : 1u)

/*
 * 逻辑行列是否镜像（反相）：
 * 0: 不反相
 * 1: 反相（例如左右颠倒时把列反相打开）
 */
#ifndef KB_MATRIX_ROW_REVERSE
#define KB_MATRIX_ROW_REVERSE 0u
#endif

#ifndef KB_MATRIX_COL_REVERSE
#define KB_MATRIX_COL_REVERSE 1u
#endif

/* 采集后端模式 */
#define KB_BACKEND_GPIO   1u
#define KB_BACKEND_MATRIX 2u
#define KB_BACKEND_CUSTOM 3u

/* 当前工程使用独立 GPIO 按键，可在工程配置里覆写 */
#ifndef KB_BACKEND_MODE
#define KB_BACKEND_MODE KB_BACKEND_GPIO
#endif

/* 矩阵模式参数（仅矩阵后端使用） */
#ifndef KB_MATRIX_MAX_ROW
#define KB_MATRIX_MAX_ROW 8u
#endif

#ifndef KB_MATRIX_MAX_COL
#define KB_MATRIX_MAX_COL 8u
#endif

#if (KB_BACKEND_MODE != KB_BACKEND_GPIO) && \
    (KB_BACKEND_MODE != KB_BACKEND_MATRIX) && \
    (KB_BACKEND_MODE != KB_BACKEND_CUSTOM)
#error "KB_BACKEND_MODE must be KB_BACKEND_GPIO / KB_BACKEND_MATRIX / KB_BACKEND_CUSTOM"
#endif

#if (KB_GPIO_ACTIVE_LEVEL > 1u) || (KB_MATRIX_ACTIVE_LEVEL > 1u) || \
    (KB_MATRIX_ROW_ACTIVE_LEVEL > 1u) || (KB_MATRIX_ROW_REVERSE > 1u) || \
    (KB_MATRIX_COL_REVERSE > 1u)
#error "keyboard polarity/reverse config must be 0 or 1"
#endif



#endif /* MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_CONFIG_H_ */
