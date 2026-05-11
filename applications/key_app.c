/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "key_app.h"

#include <rtdevice.h>

#include "bt_a2dp_sink_app.h"
#include "drv_common.h"
#include "es8311_audio.h"
#include "keyboard_driver.h"
#include "uart_send_pcm.h"
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
#define KEY_APP_EVT_TOGGLE_CAPTURE     (1u << 0)

static keyboard_control_t key_app_keyboard;
static rt_thread_t key_app_thread;
static struct rt_event key_app_event;
static rt_bool_t key_app_event_inited = RT_FALSE;
static rt_bool_t key_app_capture_wait_suspend = RT_FALSE;

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

    if (evt == KB_EVT_DOUBLE_CLICK)
    {
        (void)rt_event_send(&key_app_event, KEY_APP_EVT_TOGGLE_CAPTURE);
    }
}

static void key_app_start_capture_now(void)
{
    if (es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_CAPTURE) != RT_EOK)
    {
        LOG_E("switch audio to capture failed");
        return;
    }

    if (uart_send_pcm_start() != RT_EOK)
    {
        LOG_E("uart_send_pcm_start failed");
        (void)es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_PLAYBACK);
        return;
    }

    LOG_I("PC9 double click: capture PCM enabled");
}

static void key_app_start_capture(void)
{
    bt_a2dp_sink_suspend_result_t suspend_result;

    if (key_app_capture_wait_suspend)
    {
        LOG_W("capture request is already waiting for A2DP suspend");
        return;
    }
    if (bt_avrcp_ct_pause() == RT_EOK)
    {
        rt_thread_mdelay(300);
    }

    suspend_result = bt_a2dp_sink_request_media_suspend();
    switch (suspend_result)
    {
    case BT_A2DP_SINK_SUSPEND_PENDING:
        key_app_capture_wait_suspend = RT_TRUE;
        LOG_I("capture request queued, wait A2DP stream suspend");
        return;

    case BT_A2DP_SINK_SUSPEND_FAILED:
        LOG_E("request A2DP stream suspend failed");
        return;

    case BT_A2DP_SINK_SUSPEND_NOT_NEEDED:
    default:
        break;
    }

    key_app_start_capture_now();
}

static void key_app_stop_capture(void)
{
    uart_send_pcm_stop();

    if (bt_a2dp_sink_resume_media_stream() != RT_EOK)
    {
        LOG_E("restore playback and request A2DP start failed");
        return;
    }
    bt_avrcp_ct_play();    //控制远端开始播放

    LOG_I("PC9 double click: playback mode restored");
}

static void key_app_toggle_capture(void)
{
    if (es8311_audio_get_run_mode() == ES8311_AUDIO_RUN_MODE_CAPTURE)
    {
        key_app_stop_capture();
    }
    else
    {
        key_app_start_capture();
    }
}

static void key_app_handle_events(void)
{
    rt_uint32_t events;

    if (rt_event_recv(&key_app_event,
                      KEY_APP_EVT_TOGGLE_CAPTURE,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      0,
                      &events) != RT_EOK)
    {
        return;
    }

    if ((events & KEY_APP_EVT_TOGGLE_CAPTURE) != 0u)
    {
        key_app_toggle_capture();
    }
}

static void key_app_poll_capture_wait_suspend(void)
{
    if (!key_app_capture_wait_suspend)
    {
        return;
    }

    if (!bt_a2dp_sink_is_stream_active())
    {
        key_app_capture_wait_suspend = RT_FALSE;
        key_app_start_capture_now();
        return;
    }

    if (!bt_a2dp_sink_is_suspend_in_progress())
    {
        key_app_capture_wait_suspend = RT_FALSE;
        LOG_E("A2DP stream is still active after suspend request");
    }
}

static void key_app_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        keyboard_poll(&key_app_keyboard, KEY_APP_POLL_MS);
        key_app_handle_events();
        key_app_poll_capture_wait_suspend();
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

    if (!key_app_event_inited)
    {
        if (rt_event_init(&key_app_event, "keyevt", RT_IPC_FLAG_FIFO) != RT_EOK)
        {
            LOG_E("key event init failed");
            return -RT_ERROR;
        }
        key_app_event_inited = RT_TRUE;
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

    LOG_I("key app init ok, pin=PC9, double_click=%ums", KB_DOUBLE_CLICK_MS);
    return RT_EOK;
}
