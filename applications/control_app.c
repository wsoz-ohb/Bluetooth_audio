/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 按键:
 *   单击     -> AVRCP 播放/暂停
 *   双击     -> AVRCP 下一首
 *   长按说话 -> PTT 采集(按住采,松手停)
 * 编码器:
 *   旋转     -> AVRCP 音量 +/-
 *
 * PTT 策略:
 *   进入采集前,若对端正在播放则发 AVRCP pause,并记账;
 *   本地关闭 A2DP media gate,切 CAPTURE,可选 uart3 导出 PCM;
 *   松手后停采集,仅当“因 PTT 暂停过”才自动 play 恢复。
 */
#include "control_app.h"

#include <rtdevice.h>

#include "drv_common.h"
#include "keyboard_driver.h"
#include "bt_avrcp_ct_app.h"
#include "bt_a2dp_sink_app.h"
#include "es8311_audio.h"
#include "es8311_driver.h"
#include "uart_send_pcm.h"

#define DBG_TAG "control_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ============ 按键配置 ============ */
#define CONTROL_KEY_SW_ID              1u
#define CONTROL_KEY_SW_NAME            "sw"
#define CONTROL_KEY_SW_PIN             GET_PIN(C, 9)

/* ============ 旋转编码器配置（GPIO 轮询 + AB 相状态机）============ */
#define CONTROL_ENCODER_CLK_PIN        GET_PIN(B, 6)   /* CLK (A 相) */
#define CONTROL_ENCODER_DT_PIN         GET_PIN(B, 7)   /* DT  (B 相) */
#define CONTROL_ENCODER_STEPS_PER_CMD  2
#define CONTROL_ENCODER_CMD_INTERVAL_MS 50

/* ============ 线程配置 ============ */
#define CONTROL_THREAD_STACK_SIZE      2048
#define CONTROL_THREAD_PRIORITY        19
#define CONTROL_THREAD_TICK            10
#define CONTROL_KEY_POLL_MS            10u
#define CONTROL_ENCODER_SAMPLE_MS      1u

/* PTT: 等对端 pause / 本地停流的最长时间 */
#define CONTROL_PTT_PAUSE_WAIT_MS      500u
#define CONTROL_PTT_PAUSE_POLL_MS      20u
/* 语音采集默认 MIC PGA,0dB 对 ASR 往往偏小 */
#define CONTROL_PTT_MIC_GAIN           ES8311_MIC_GAIN_24DB

typedef enum
{
    CONTROL_PTT_REQ_NONE = 0,
    CONTROL_PTT_REQ_START,
    CONTROL_PTT_REQ_STOP,
} control_ptt_req_t;

static keyboard_control_t g_keyboard_ctrl;
static rt_thread_t g_control_thread;

static rt_int8_t g_encoder_accumulator = 0;
static rt_int8_t g_encoder_pending_cmds = 0;
static uint8_t g_encoder_last_state = 0;
static rt_tick_t g_encoder_last_cmd_tick = 0;

/* PTT 状态: 请求由按键回调置位,统一在 control 线程执行,避免在回调里长时间阻塞。 */
static volatile control_ptt_req_t g_ptt_req = CONTROL_PTT_REQ_NONE;
static volatile rt_bool_t g_ptt_want_hold = RT_FALSE; /* 按住期间为真,松手为假 */
static rt_bool_t g_ptt_capturing = RT_FALSE;
static rt_bool_t g_ptt_resume_play = RT_FALSE;        /* 仅当因 PTT 暂停过对端才恢复 */
static rt_bool_t g_ptt_media_gate_closed = RT_FALSE;

static uint8_t control_read_pin(uint8_t pin)
{
    return (uint8_t)(rt_pin_read((rt_base_t)pin) ? 1u : 0u);
}

static uint32_t control_get_tick_ms(void)
{
    return (uint32_t)rt_tick_get_millisecond();
}

rt_bool_t control_app_is_capturing(void)
{
    return g_ptt_capturing;
}

static void control_ptt_wait_playback_quiet(void)
{
    uint32_t waited;

    for (waited = 0u; waited < CONTROL_PTT_PAUSE_WAIT_MS; waited += CONTROL_PTT_PAUSE_POLL_MS)
    {
        bt_avrcp_ct_playback_state_t pb;
        rt_bool_t stream_active;
        rt_bool_t local_playing;

        pb = bt_avrcp_ct_get_playback_state();
        stream_active = bt_a2dp_sink_is_stream_active();
        local_playing = es8311_audio_is_playback_running();

        if ((pb != BT_AVRCP_CT_PLAYBACK_STATE_PLAYING) &&
            !stream_active &&
            !local_playing)
        {
            return;
        }

        /* 对端已 pause 但本地还在播尾巴,主动停本地,避免占 I2S */
        if ((pb != BT_AVRCP_CT_PLAYBACK_STATE_PLAYING) && local_playing)
        {
            es8311_audio_stop_playback();
            es8311_audio_flush_playback();
        }

        rt_thread_mdelay(CONTROL_PTT_PAUSE_POLL_MS);
    }

    /* 超时仍强制停本地,保证能进采集 */
    if (es8311_audio_is_playback_running())
    {
        LOG_W("PTT: pause wait timeout, force stop local playback");
        es8311_audio_stop_playback();
        es8311_audio_flush_playback();
    }
}

static rt_err_t control_ptt_start(void)
{
    bt_avrcp_ct_playback_state_t pb;
    rt_bool_t was_playing;
    rt_bool_t pcm_export_started;
    rt_err_t err;

    if (g_ptt_capturing)
    {
        return RT_EOK;
    }

    /* 松手发生在 start 排队期间: 直接取消 */
    if (!g_ptt_want_hold)
    {
        LOG_I("PTT: start canceled (already released)");
        return RT_EOK;
    }

    pb = bt_avrcp_ct_get_playback_state();
    was_playing = (rt_bool_t)(
        (bt_avrcp_ct_is_connected() && (pb == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING)) ||
        bt_a2dp_sink_is_stream_active() ||
        es8311_audio_is_playback_running());

    g_ptt_resume_play = RT_FALSE;

    if (was_playing && bt_avrcp_ct_is_connected() &&
        (pb == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING))
    {
        if (bt_avrcp_ct_pause() == RT_EOK)
        {
            g_ptt_resume_play = RT_TRUE;
            LOG_I("PTT: pause remote for talk");
        }
        else
        {
            LOG_W("PTT: avrcp pause failed, still try local capture");
        }
    }
    else if (was_playing)
    {
        /* 本地在播但 AVRCP 未连/状态未知: 只停本地,不记账自动 play */
        LOG_I("PTT: stop local playback only (no avrcp resume)");
    }

    control_ptt_wait_playback_quiet();

    if (!g_ptt_want_hold)
    {
        LOG_I("PTT: released during pause wait, abort start");
        if (g_ptt_resume_play)
        {
            (void)bt_avrcp_ct_play();
            g_ptt_resume_play = RT_FALSE;
        }
        return RT_EOK;
    }

    /* 关闭本地 media gate,防止采集期间 STREAM 事件又把播放 arm 起来 */
    if (bt_a2dp_sink_set_local_media_enabled(RT_FALSE) == RT_EOK)
    {
        g_ptt_media_gate_closed = RT_TRUE;
    }

    (void)es8311_set_mic_gain(CONTROL_PTT_MIC_GAIN);
    es8311_audio_flush_capture();

    pcm_export_started = RT_FALSE;
    err = uart_send_pcm_start();
    if (err != RT_EOK)
    {
        LOG_E("PTT: pcm export prepare failed: %d", err);
        if (g_ptt_media_gate_closed)
        {
            (void)bt_a2dp_sink_set_local_media_enabled(RT_TRUE);
            g_ptt_media_gate_closed = RT_FALSE;
        }
        if (g_ptt_resume_play)
        {
            (void)bt_avrcp_ct_play();
            g_ptt_resume_play = RT_FALSE;
        }
        return err;
    }
    pcm_export_started = RT_TRUE;

    if (!g_ptt_want_hold)
    {
        LOG_I("PTT: released during pcm export prepare, abort start");
        uart_send_pcm_stop();
        if (g_ptt_media_gate_closed)
        {
            (void)bt_a2dp_sink_set_local_media_enabled(RT_TRUE);
            g_ptt_media_gate_closed = RT_FALSE;
        }
        if (g_ptt_resume_play)
        {
            (void)bt_avrcp_ct_play();
            g_ptt_resume_play = RT_FALSE;
        }
        return RT_EOK;
    }

    err = es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_CAPTURE);
    if (err != RT_EOK)
    {
        LOG_E("PTT: enter capture failed: %d", err);
        if (pcm_export_started)
        {
            uart_send_pcm_stop();
        }
        if (g_ptt_media_gate_closed)
        {
            (void)bt_a2dp_sink_set_local_media_enabled(RT_TRUE);
            g_ptt_media_gate_closed = RT_FALSE;
        }
        if (g_ptt_resume_play)
        {
            (void)bt_avrcp_ct_play();
            g_ptt_resume_play = RT_FALSE;
        }
        return err;
    }

    g_ptt_capturing = RT_TRUE;
    LOG_I("PTT: capturing (hold to talk), resume_play=%d, mic_gain=%udB",
          g_ptt_resume_play,
          (unsigned)CONTROL_PTT_MIC_GAIN * 6u);
    return RT_EOK;
}

static void control_ptt_stop(void)
{
    rt_bool_t need_resume;

    if (!g_ptt_capturing)
    {
        /* start 还没完成就松手: 清请求即可,start 里会看到 want_hold=0 */
        g_ptt_resume_play = RT_FALSE;
        return;
    }

    need_resume = g_ptt_resume_play;
    g_ptt_resume_play = RT_FALSE;
    g_ptt_capturing = RT_FALSE;

    es8311_audio_stop_capture();
    uart_send_pcm_stop();
    es8311_audio_flush_capture();

    /* 回到 idle,等对端 play / STREAM_STARTED 再 arm 播放 */
    (void)es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_IDLE);

    if (g_ptt_media_gate_closed)
    {
        (void)bt_a2dp_sink_set_local_media_enabled(RT_TRUE);
        g_ptt_media_gate_closed = RT_FALSE;
    }

    if (need_resume && bt_avrcp_ct_is_connected())
    {
        if (bt_avrcp_ct_play() == RT_EOK)
        {
            LOG_I("PTT: resume remote play");
        }
        else
        {
            LOG_W("PTT: avrcp play resume failed");
        }
    }
    else
    {
        LOG_I("PTT: capture stopped (no auto resume)");
    }
}

static void control_ptt_poll(void)
{
    control_ptt_req_t req;

    req = g_ptt_req;
    if (req == CONTROL_PTT_REQ_NONE)
    {
        return;
    }
    g_ptt_req = CONTROL_PTT_REQ_NONE;

    if (req == CONTROL_PTT_REQ_START)
    {
        (void)control_ptt_start();
        /* 若 start 过程中已松手,补一次 stop 语义(start 内部多数已处理) */
        if (g_ptt_capturing && !g_ptt_want_hold)
        {
            control_ptt_stop();
        }
    }
    else if (req == CONTROL_PTT_REQ_STOP)
    {
        control_ptt_stop();
    }
}

static void control_key_event_cb(const char *keyname, uint16_t key_id, kb_event_t evt, void *user)
{
    RT_UNUSED(keyname);
    RT_UNUSED(user);

    if (key_id != CONTROL_KEY_SW_ID)
    {
        return;
    }

    /* 长按说话: 按住开始,松手结束 */
    if (evt == KB_EVT_LONGPRESS)
    {
        g_ptt_want_hold = RT_TRUE;
        g_ptt_req = CONTROL_PTT_REQ_START;
        return;
    }

    if (evt == KB_EVT_LONGPRESS_RELEASE)
    {
        g_ptt_want_hold = RT_FALSE;
        g_ptt_req = CONTROL_PTT_REQ_STOP;
        return;
    }

    /* 采集中忽略单击/双击,避免松手沿边误触播控 */
    if (g_ptt_capturing || g_ptt_want_hold)
    {
        return;
    }

    if (evt == KB_EVT_CLICK)
    {
        switch (bt_avrcp_ct_get_playback_state())
        {
        case BT_AVRCP_CT_PLAYBACK_STATE_PLAYING:
            bt_avrcp_ct_pause();
            break;

        case BT_AVRCP_CT_PLAYBACK_STATE_PAUSED:
        case BT_AVRCP_CT_PLAYBACK_STATE_STOPPED:
        case BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN:
        default:
            bt_avrcp_ct_play();
            break;
        }
        return;
    }

    if (evt == KB_EVT_DOUBLE_CLICK)
    {
        bt_avrcp_ct_next();
    }
}

static uint8_t control_encoder_read_state(void)
{
    uint8_t clk;
    uint8_t dt;

    clk = (uint8_t)(rt_pin_read(CONTROL_ENCODER_CLK_PIN) == PIN_HIGH ? 1u : 0u);
    dt = (uint8_t)(rt_pin_read(CONTROL_ENCODER_DT_PIN) == PIN_HIGH ? 1u : 0u);

    return (uint8_t)((clk << 1) | dt);
}

static rt_int8_t control_encoder_decode_transition(uint8_t last_state, uint8_t current_state)
{
    static const rt_int8_t transition_table[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0,
    };

    return transition_table[((last_state & 0x03u) << 2) | (current_state & 0x03u)];
}

static rt_err_t control_encoder_init(void)
{
    rt_pin_mode(CONTROL_ENCODER_CLK_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(CONTROL_ENCODER_DT_PIN, PIN_MODE_INPUT_PULLUP);

    g_encoder_accumulator = 0;
    g_encoder_pending_cmds = 0;
    g_encoder_last_state = control_encoder_read_state();
    g_encoder_last_cmd_tick = 0;

    LOG_I("rotary encoder init ok (polling mode), CLK=PB6, DT=PB7, steps_per_cmd=%d",
          CONTROL_ENCODER_STEPS_PER_CMD);
    return RT_EOK;
}

static void control_encoder_sample(void)
{
    uint8_t current_state;
    rt_int8_t delta;

    current_state = control_encoder_read_state();
    if (current_state == g_encoder_last_state)
    {
        return;
    }

    delta = control_encoder_decode_transition(g_encoder_last_state, current_state);
    g_encoder_last_state = current_state;

    if (delta == 0)
    {
        g_encoder_accumulator = 0;
        return;
    }

    if (((g_encoder_accumulator > 0) && (delta < 0)) ||
        ((g_encoder_accumulator < 0) && (delta > 0)))
    {
        g_encoder_accumulator = delta;
    }
    else
    {
        g_encoder_accumulator += delta;
    }

    if (g_encoder_accumulator >= CONTROL_ENCODER_STEPS_PER_CMD)
    {
        if (g_encoder_pending_cmds < 4)
        {
            g_encoder_pending_cmds++;
        }
        g_encoder_accumulator = 0;
    }
    else if (g_encoder_accumulator <= -CONTROL_ENCODER_STEPS_PER_CMD)
    {
        if (g_encoder_pending_cmds > -4)
        {
            g_encoder_pending_cmds--;
        }
        g_encoder_accumulator = 0;
    }
}

static void control_encoder_poll(void)
{
    rt_tick_t current_tick;

    if (g_encoder_pending_cmds == 0)
    {
        return;
    }

    /* 采集说话时不调音量,避免误触 */
    if (g_ptt_capturing || g_ptt_want_hold)
    {
        g_encoder_pending_cmds = 0;
        return;
    }

    if (!bt_avrcp_ct_is_connected())
    {
        g_encoder_pending_cmds = 0;
        return;
    }

    current_tick = rt_tick_get();
    if ((g_encoder_last_cmd_tick != 0) &&
        (current_tick - g_encoder_last_cmd_tick < rt_tick_from_millisecond(CONTROL_ENCODER_CMD_INTERVAL_MS)))
    {
        return;
    }

    if (g_encoder_pending_cmds > 0)
    {
        if (bt_avrcp_ct_volume_up() == RT_EOK)
        {
            g_encoder_pending_cmds--;
            LOG_D("encoder CW -> volume up (%s), pending=%d",
                  bt_avrcp_ct_is_absolute_volume_active() ? "absolute" : "relative",
                  g_encoder_pending_cmds);
        }
        g_encoder_last_cmd_tick = current_tick;
    }
    else
    {
        if (bt_avrcp_ct_volume_down() == RT_EOK)
        {
            g_encoder_pending_cmds++;
            LOG_D("encoder CCW -> volume down (%s), pending=%d",
                  bt_avrcp_ct_is_absolute_volume_active() ? "absolute" : "relative",
                  g_encoder_pending_cmds);
        }
        g_encoder_last_cmd_tick = current_tick;
    }
}

static void control_thread_entry(void *parameter)
{
    uint32_t key_poll_elapsed_ms = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        control_encoder_sample();
        control_encoder_poll();
        control_ptt_poll();

        key_poll_elapsed_ms += CONTROL_ENCODER_SAMPLE_MS;
        if (key_poll_elapsed_ms >= CONTROL_KEY_POLL_MS)
        {
            keyboard_poll(&g_keyboard_ctrl, key_poll_elapsed_ms);
            key_poll_elapsed_ms = 0;
        }

        rt_thread_mdelay(CONTROL_ENCODER_SAMPLE_MS);
    }
}

rt_err_t control_app_init(void)
{
    keyboard_ops_t ops;
    keyboard_cb_t cb;
    int kb_ret;

    if (g_control_thread != RT_NULL)
    {
        return RT_EOK;
    }

    rt_pin_mode(CONTROL_KEY_SW_PIN, PIN_MODE_INPUT_PULLUP);

    rt_memset(&ops, 0, sizeof(ops));
    ops.read_pin = control_read_pin;
    ops.get_tick_ms = control_get_tick_ms;

    cb.on_event = control_key_event_cb;
    cb.user = RT_NULL;

    kb_ret = keyboard_init(&g_keyboard_ctrl, &ops, &cb);
    if (kb_ret != KB_OK)
    {
        LOG_E("keyboard_init failed: %d", kb_ret);
        return -RT_ERROR;
    }

    kb_ret = keyboard_register_gpio((uint8_t)CONTROL_KEY_SW_PIN,
                                    CONTROL_KEY_SW_NAME,
                                    CONTROL_KEY_SW_ID,
                                    &g_keyboard_ctrl);
    if (kb_ret != KB_OK)
    {
        LOG_E("keyboard_register_gpio failed: %d", kb_ret);
        return -RT_ERROR;
    }

    LOG_I("key sw init ok, pin=PC9 (click=play/pause, double=next, long=PTT talk)");

    control_encoder_init();

    g_control_thread = rt_thread_create("control",
                                        control_thread_entry,
                                        RT_NULL,
                                        CONTROL_THREAD_STACK_SIZE,
                                        CONTROL_THREAD_PRIORITY,
                                        CONTROL_THREAD_TICK);
    if (g_control_thread == RT_NULL)
    {
        LOG_E("create control thread failed");
        return -RT_ENOMEM;
    }

    if (rt_thread_startup(g_control_thread) != RT_EOK)
    {
        g_control_thread = RT_NULL;
        LOG_E("startup control thread failed");
        return -RT_ERROR;
    }

    LOG_I("control app init ok");
    return RT_EOK;
}
