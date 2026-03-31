/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     wsoz       the first version
 */

#include "keyboard_driver.h"

static uint8_t key_pool_buf[KEYBOARD_POOL_SIZE];
static mpool_t key_pool;

typedef struct
{
    uint8_t raw_last;
    uint8_t stable;
    uint8_t long_sent;
    uint8_t click_count;
    uint32_t debounce_ms;
    uint32_t press_ms;
    uint32_t repeat_ms;
    uint32_t click_wait_ms;
} kb_key_runtime_t;

typedef struct
{
    const keyboard_que_t *node;
    kb_event_t evt;
} kb_pending_evt_t;

static kb_key_runtime_t key_rt[KB_MAX_KEYS];

static int kb_hw_equal(uint8_t backend_mode, const keyboard_hw_ref_t *a, const keyboard_hw_ref_t *b)
{
    if (a == NULL || b == NULL)
    {
        return 0;
    }

    switch (backend_mode)
    {
    case KB_BACKEND_GPIO:
        return (a->gpio_pin == b->gpio_pin);
    case KB_BACKEND_MATRIX:
        return (a->matrix.row == b->matrix.row) && (a->matrix.col == b->matrix.col);
    case KB_BACKEND_CUSTOM:
    default:
        return (a->hw_code == b->hw_code);
    }
}

static void kb_emit_event(keyboard_control_t *ctl, const keyboard_que_t *node, kb_event_t evt)
{
    if (ctl == NULL || node == NULL)
    {
        return;
    }
    if (ctl->keyboard_cb.on_event != NULL)
    {
        ctl->keyboard_cb.on_event(node->keyname, node->key_id, evt, ctl->keyboard_cb.user);
    }
}

static uint8_t kb_read_raw(const keyboard_control_t *ctl, const keyboard_que_t *node, uint8_t index, const uint8_t *snapshot)
{
    if (ctl == NULL || node == NULL)
    {
        return 0u;
    }

    switch (ctl->backend_mode)
    {
    case KB_BACKEND_GPIO:
        if (ctl->keyboard_ops.read_pin == NULL)
        {
            return 0u;
        }
        return (uint8_t)((ctl->keyboard_ops.read_pin(node->hw.gpio_pin) == KB_GPIO_ACTIVE_LEVEL) ? 1u : 0u);

    case KB_BACKEND_MATRIX:
        if (ctl->keyboard_ops.matrix_select_row == NULL ||
            ctl->keyboard_ops.matrix_read_col == NULL ||
            ctl->keyboard_ops.matrix_unselect_row == NULL)
        {
            return 0u;
        }
        ctl->keyboard_ops.matrix_select_row(node->hw.matrix.row);
        {
            uint8_t level = (uint8_t)ctl->keyboard_ops.matrix_read_col(node->hw.matrix.col);
            ctl->keyboard_ops.matrix_unselect_row(node->hw.matrix.row);
            return (uint8_t)((level == KB_MATRIX_ACTIVE_LEVEL) ? 1u : 0u);
        }

    case KB_BACKEND_CUSTOM:
    default:
        if (snapshot == NULL || index >= KB_MAX_KEYS)
        {
            return 0u;
        }
        return (uint8_t)(snapshot[index] ? 1u : 0u);
    }
}


int keyboard_init(keyboard_control_t *ctl, const keyboard_ops_t *ops, const keyboard_cb_t *cb)
{
    uint16_t stride;
    uint16_t count;

    if (ctl == NULL || ops == NULL)
    {
        return KB_ERR_PARAM;
    }

#if (KB_BACKEND_MODE == KB_BACKEND_GPIO)
    if (ops->read_pin == NULL)
    {
        return KB_ERR_BACKEND;
    }
#elif (KB_BACKEND_MODE == KB_BACKEND_MATRIX)
    if (ops->matrix_select_row == NULL || ops->matrix_read_col == NULL || ops->matrix_unselect_row == NULL)
    {
        return KB_ERR_BACKEND;
    }
#endif

    stride = MPOOL_ALIGN_UP((uint16_t)(sizeof(keyboard_que_t) + sizeof(mpool_node_t)));
    count = (uint16_t)(KEYBOARD_POOL_SIZE / stride);

    if (count == 0u)
    {
        return KB_ERR_POOL_CFG;
    }
    if (count > KB_MAX_KEYS)
    {
        count = KB_MAX_KEYS;
    }

    mpool_init(&key_pool, key_pool_buf, (uint16_t)sizeof(keyboard_que_t), count);

    ctl->backend_mode = (uint8_t)KB_BACKEND_MODE;
    ctl->keyboard_ops = *ops;
    ctl->keyboard_cb.on_event = (cb != NULL) ? cb->on_event : NULL;
    ctl->keyboard_cb.user = (cb != NULL) ? cb->user : NULL;
    ctl->head = NULL;
    ctl->key_num = 0;
    ctl->keyboard_pool = &key_pool;
    memset(key_rt, 0, sizeof(key_rt));

    return KB_OK;
}

int keyboard_register_key(const keyboard_key_cfg_t *cfg, keyboard_control_t *ctl)
{
    keyboard_que_t *node;
    keyboard_que_t *tail;

    if (ctl == NULL || cfg == NULL || cfg->keyname == NULL || ctl->keyboard_pool == NULL)
    {
        return KB_ERR_PARAM;
    }

    if (ctl->backend_mode == KB_BACKEND_MATRIX)
    {
        if (cfg->hw.matrix.row >= KB_MATRIX_MAX_ROW || cfg->hw.matrix.col >= KB_MATRIX_MAX_COL)
        {
            return KB_ERR_RANGE;
        }
    }

    if (ctl->keyboard_ops.lock != NULL)
    {
        ctl->keyboard_ops.lock();
    }

    tail = ctl->head;
    while (tail != NULL)
    {
        if (tail->key_id == cfg->key_id || kb_hw_equal(ctl->backend_mode, &tail->hw, &cfg->hw))
        {
            if (ctl->keyboard_ops.unlock != NULL)
            {
                ctl->keyboard_ops.unlock();
            }
            return KB_ERR_DUPLICATE;
        }
        if (tail->next == NULL)
        {
            break;
        }
        tail = tail->next;
    }

    if (ctl->key_num >= KB_MAX_KEYS)
    {
        if (ctl->keyboard_ops.unlock != NULL)
        {
            ctl->keyboard_ops.unlock();
        }
        return KB_ERR_FULL;
    }

    node = (keyboard_que_t *)mpool_alloc(ctl->keyboard_pool);
    if (node == NULL)
    {
        if (ctl->keyboard_ops.unlock != NULL)
        {
            ctl->keyboard_ops.unlock();
        }
        return KB_ERR_NOMEM;
    }

    node->keyname = cfg->keyname;
    node->key_id = cfg->key_id;
    node->hw = cfg->hw;
    node->next = NULL;

    if (tail == NULL)
    {
        ctl->head = node;
    }
    else
    {
        tail->next = node;
    }
    ctl->key_num++;

    if (ctl->keyboard_ops.unlock != NULL)
    {
        ctl->keyboard_ops.unlock();
    }
    return KB_OK;
}

int keyboard_register_gpio(uint8_t pin, const char *key_name, uint16_t key_id, keyboard_control_t *ctl)
{
    keyboard_key_cfg_t cfg;

    cfg.keyname = key_name;
    cfg.key_id = key_id;
    cfg.hw.gpio_pin = pin;

    return keyboard_register_key(&cfg, ctl);
}

int keyboard_register_matrix(uint8_t row, uint8_t col, const char *key_name, uint16_t key_id, keyboard_control_t *ctl)
{
    keyboard_key_cfg_t cfg;

    cfg.keyname = key_name;
    cfg.key_id = key_id;
    cfg.hw.matrix.row = row;
    cfg.hw.matrix.col = col;

    return keyboard_register_key(&cfg, ctl);
}

void keyboard_poll(keyboard_control_t *ctl, uint32_t dt_ms)
{
    keyboard_que_t *node;
    uint8_t custom_snapshot[KB_MAX_KEYS] = {0};
    kb_pending_evt_t pending_evt[KB_MAX_KEYS * 4u];
    uint16_t evt_num = 0u;
    uint16_t idx = 0u;

    if (ctl == NULL || dt_ms == 0u)
    {
        return;
    }

    if (ctl->backend_mode == KB_BACKEND_CUSTOM)
    {
        if (ctl->keyboard_ops.scan_snapshot == NULL)
        {
            return;
        }
        if (ctl->keyboard_ops.scan_snapshot(custom_snapshot, ctl->key_num) != 0)
        {
            return;
        }
    }

    node = ctl->head;
    while (node != NULL && idx < ctl->key_num && idx < KB_MAX_KEYS)
    {
        kb_key_runtime_t *rt = &key_rt[idx];
        uint8_t raw = kb_read_raw(ctl, node, (uint8_t)idx, custom_snapshot);

        if (raw != rt->raw_last)
        {
            rt->raw_last = raw;
            rt->debounce_ms = 0u;
        }
        else
        {
            if (rt->debounce_ms < KB_DEBOUNCE_MS)
            {
                rt->debounce_ms += dt_ms;
            }
        }

        if (rt->debounce_ms >= KB_DEBOUNCE_MS && rt->stable != rt->raw_last)
        {
            rt->stable = rt->raw_last;
            if (rt->stable != 0u)
            {
                rt->press_ms = 0u;
                rt->repeat_ms = 0u;
                rt->long_sent = 0u;

                if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                {
                    pending_evt[evt_num].node = node;
                    pending_evt[evt_num].evt = KB_EVT_PRESS;
                    evt_num++;
                }
            }
            else
            {
                if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                {
                    pending_evt[evt_num].node = node;
                    pending_evt[evt_num].evt = KB_EVT_RELEASE;
                    evt_num++;
                }

                if (rt->long_sent != 0u)
                {
                    if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                    {
                        pending_evt[evt_num].node = node;
                        pending_evt[evt_num].evt = KB_EVT_LONGPRESS_RELEASE;
                        evt_num++;
                    }
                    rt->click_count = 0u;
                    rt->click_wait_ms = 0u;
                }
                else
                {
                    if (rt->click_count == 0u)
                    {
                        rt->click_count = 1u;
                        rt->click_wait_ms = 0u;
                    }
                    else if (rt->click_count == 1u && rt->click_wait_ms <= KB_DOUBLE_CLICK_MS)
                    {
                        if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                        {
                            pending_evt[evt_num].node = node;
                            pending_evt[evt_num].evt = KB_EVT_DOUBLE_CLICK;
                            evt_num++;
                        }
                        rt->click_count = 0u;
                        rt->click_wait_ms = 0u;
                    }
                    else
                    {
                        rt->click_count = 1u;
                        rt->click_wait_ms = 0u;
                    }
                }

                rt->press_ms = 0u;
                rt->repeat_ms = 0u;
                rt->long_sent = 0u;
            }
        }

        if (rt->stable != 0u)
        {
            rt->press_ms += dt_ms;

            if (rt->long_sent == 0u && rt->press_ms >= KB_LONGPRESS_MS)
            {
                rt->long_sent = 1u;
                if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                {
                    pending_evt[evt_num].node = node;
                    pending_evt[evt_num].evt = KB_EVT_LONGPRESS;
                    evt_num++;
                }
            }

            if (rt->press_ms >= KB_REPEAT_START_MS)
            {
                rt->repeat_ms += dt_ms;
                if (rt->repeat_ms >= KB_REPEAT_PERIOD_MS)
                {
                    rt->repeat_ms = 0u;
                    if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                    {
                        pending_evt[evt_num].node = node;
                        pending_evt[evt_num].evt = KB_EVT_REPEAT;
                        evt_num++;
                    }
                }
            }
        }
        else
        {
            if (rt->click_count == 1u)
            {
                rt->click_wait_ms += dt_ms;
                if (rt->click_wait_ms >= KB_DOUBLE_CLICK_MS)
                {
                    if (evt_num < (uint16_t)(KB_MAX_KEYS * 4u))
                    {
                        pending_evt[evt_num].node = node;
                        pending_evt[evt_num].evt = KB_EVT_CLICK;
                        evt_num++;
                    }
                    rt->click_count = 0u;
                    rt->click_wait_ms = 0u;
                }
            }
        }

        node = node->next;
        idx++;
    }

    for (idx = 0u; idx < evt_num; idx++)
    {
        kb_emit_event(ctl, pending_evt[idx].node, pending_evt[idx].evt);
    }
}

