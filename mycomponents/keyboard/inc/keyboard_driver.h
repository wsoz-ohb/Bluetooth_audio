/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     wsoz         the first version
 */
#ifndef MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_DRIVER_H_
#define MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_DRIVER_H_

#include <stdint.h>
#include "keyboard_config.h"
#include "mypool.h"

/* 矩阵键盘位置 */
typedef struct
{
    uint8_t row;
    uint8_t col;
} keyboard_matrix_pos_t;


/* 硬件定位：独立 GPIO / 矩阵 row-col / 自定义编码 */
typedef union
{
    uint8_t gpio_pin;
    keyboard_matrix_pos_t matrix;
    uint16_t hw_code;
} keyboard_hw_ref_t;


/* 统一的按键注册描述 */
typedef struct
{
    const char *keyname;      /* 逻辑名称，如 "K_A" */
    uint16_t key_id;          /* 逻辑按键ID，业务层推荐用这个 */
    keyboard_hw_ref_t hw;     /* 硬件定位信息 */
} keyboard_key_cfg_t;


/* keyboard 操作集 */
typedef struct
{
    /* GPIO 后端：读取 pin 电平，返回 0/1 */
    uint8_t (*read_pin)(uint8_t pin);

    /* 矩阵后端：驱动行并读取列 */
    void (*matrix_select_row)(uint8_t row);
    uint8_t (*matrix_read_col)(uint8_t col);
    void (*matrix_unselect_row)(uint8_t row);

    /*
     * 自定义后端（复杂输入建议使用）：
     * 按“注册顺序”输出 key_count 个按键电平到 state_buf（每个元素取值0/1）
     * 返回 0 表示成功
     */
    int (*scan_snapshot)(uint8_t *state_buf, uint16_t key_count);

    /* 获取当前毫秒 tick（可选，不提供则可以依赖 poll 的 dt_ms） */
    uint32_t (*get_tick_ms)(void);

    /* 可选：多线程环境保护 */
    void (*lock)(void);
    void (*unlock)(void);
} keyboard_ops_t;


typedef enum
{
    KB_EVT_PRESS = 0,
    KB_EVT_RELEASE,

    KB_EVT_CLICK,

    KB_EVT_LONGPRESS,
    KB_EVT_LONGPRESS_RELEASE,

    KB_EVT_REPEAT,

    KB_EVT_DOUBLE_CLICK,
} kb_event_t;


/* keyboard 事件回调函数 */
typedef void (*keyboard_event_cb)(const char *keyname, uint16_t key_id, kb_event_t evt, void *user);


typedef struct
{
    keyboard_event_cb on_event;
    void *user;
} keyboard_cb_t;


/* keyboard 按键注册队列 */
typedef struct keyboard_que
{
    const char *keyname;
    uint16_t key_id;
    keyboard_hw_ref_t hw;
    struct keyboard_que *next;
} keyboard_que_t;


/* keyboard 控制结构体 */
typedef struct
{
    uint8_t backend_mode;      /* 取值: KB_BACKEND_GPIO / MATRIX / CUSTOM */
    keyboard_ops_t keyboard_ops;
    keyboard_cb_t keyboard_cb;
    keyboard_que_t *head;
    uint16_t key_num;
    mpool_t *keyboard_pool;
} keyboard_control_t;

/* 统一返回码 */
#define KB_OK              (0)
#define KB_ERR_PARAM       (-1) /* 参数非法/空指针 */
#define KB_ERR_BACKEND     (-2) /* 后端能力不满足 */
#define KB_ERR_POOL_CFG    (-3) /* 内存池配置不足 */
#define KB_ERR_RANGE       (-4) /* 参数越界（如矩阵row/col） */
#define KB_ERR_DUPLICATE   (-5) /* 重复注册（key_id或硬件位重复） */
#define KB_ERR_FULL        (-6) /* 注册数量达到上限 */
#define KB_ERR_NOMEM       (-7) /* 内存池分配失败 */

int keyboard_init(keyboard_control_t *ctl, const keyboard_ops_t *ops, const keyboard_cb_t *cb);


/* 通用注册接口 */
int keyboard_register_key(const keyboard_key_cfg_t *cfg, keyboard_control_t *ctl);


/* 便捷注册：独立 GPIO / 矩阵键盘 */
int keyboard_register_gpio(uint8_t pin, const char *key_name, uint16_t key_id, keyboard_control_t *ctl);
int keyboard_register_matrix(uint8_t row, uint8_t col, const char *key_name, uint16_t key_id, keyboard_control_t *ctl);


/* 周期驱动入口：建议在定时任务中调用 */
void keyboard_poll(keyboard_control_t *ctl, uint32_t dt_ms);


#endif /* MYCOMPONENTS_KEYBOARD_INC_KEYBOARD_DRIVER_H_ */
