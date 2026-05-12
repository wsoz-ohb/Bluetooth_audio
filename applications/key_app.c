/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key_app.h"

#include <rtdevice.h>

#include "drv_common.h"
#include "keyboard_driver.h"
#include "bt_avrcp_ct_app.h"

#define DBG_TAG "key_app"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define KEY_APP_RECORD_KEY_ID          1u
#define KEY_APP_RECORD_KEY_NAME        "record"
#define KEY_APP_RECORD_KEY_PIN         GET_PIN(C, 9)
#define KEY_APP_THREAD_STACK_SIZE      2048
#define KEY_APP_THREAD_PRIORITY        19
#define KEY_APP_THREAD_TICK            10
#define KEY_APP_POLL_MS                10u

static keyboard_control_t key_app_keyboard;
static rt_thread_t key_app_thread;
bt_avrcp_ct_playback_state_t paly_stat;

static uint8_t key_app_read_pin(uint8_t pin)
{
    return (uint8_t)(rt_pin_read((rt_base_t)pin) ? 1u : 0u);
}

static uint32_t key_app_get_tick_ms(void)
{
    return (uint32_t)rt_tick_get_millisecond();
}

static void key_app_event_cb(const char *keyname, uint16_t key_id, kb_event_t evt, void *user)
{
    RT_UNUSED(keyname);
    RT_UNUSED(user);

    if (key_id != KEY_APP_RECORD_KEY_ID)
    {
        return;
    }


    if (evt == KB_EVT_CLICK) //单击暂停/继续
    {
        // 先查询当前状态，如果正在播放则暂停，否则继续
        switch(bt_avrcp_ct_get_playback_state())
        {
            case BT_AVRCP_CT_PLAYBACK_STATE_STOPPED:
                bt_avrcp_ct_play();
                break;
            
            case BT_AVRCP_CT_PLAYBACK_STATE_PLAYING:
                bt_avrcp_ct_pause();
                break;

            default:
                break;
        }
    }

    if (evt == KB_EVT_DOUBLE_CLICK) //双击下一首
    {
        bt_avrcp_ct_next();
    }

}

static void key_app_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        keyboard_poll(&key_app_keyboard, KEY_APP_POLL_MS);
        rt_thread_mdelay(KEY_APP_POLL_MS);
    }
}

rt_err_t key_app_init(void)
{
    keyboard_ops_t ops;
    keyboard_cb_t cb;
    int kb_ret;

    if (key_app_thread != RT_NULL)
    {
        return RT_EOK;
    }

    rt_pin_mode(KEY_APP_RECORD_KEY_PIN, PIN_MODE_INPUT_PULLUP);

    rt_memset(&ops, 0, sizeof(ops));
    ops.read_pin = key_app_read_pin;
    ops.get_tick_ms = key_app_get_tick_ms;

    cb.on_event = key_app_event_cb;
    cb.user = RT_NULL;

    kb_ret = keyboard_init(&key_app_keyboard, &ops, &cb);
    if (kb_ret != KB_OK)
    {
        LOG_E("keyboard_init failed: %d", kb_ret);
        return -RT_ERROR;
    }

    kb_ret = keyboard_register_gpio((uint8_t)KEY_APP_RECORD_KEY_PIN,
                                    KEY_APP_RECORD_KEY_NAME,
                                    KEY_APP_RECORD_KEY_ID,
                                    &key_app_keyboard);
    if (kb_ret != KB_OK)
    {
        LOG_E("keyboard_register_gpio failed: %d", kb_ret);
        return -RT_ERROR;
    }

    key_app_thread = rt_thread_create("key_app",
                                      key_app_thread_entry,
                                      RT_NULL,
                                      KEY_APP_THREAD_STACK_SIZE,
                                      KEY_APP_THREAD_PRIORITY,
                                      KEY_APP_THREAD_TICK);
    if (key_app_thread == RT_NULL)
    {
        LOG_E("create key_app thread failed");
        return -RT_ENOMEM;
    }

    if (rt_thread_startup(key_app_thread) != RT_EOK)
    {
        key_app_thread = RT_NULL;
        LOG_E("startup key_app thread failed");
        return -RT_ERROR;
    }

    LOG_I("key app init ok, pin=PC9, double_click=%ums, capture control detached", KB_DOUBLE_CLICK_MS);
    return RT_EOK;
}
