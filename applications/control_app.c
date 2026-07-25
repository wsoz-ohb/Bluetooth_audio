#include <control_app.h>
/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtdevice.h>

#include "drv_common.h"
#include "keyboard_driver.h"
#include "bt_avrcp_ct_app.h"

#define DBG_TAG "control_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

// ============ 按键配置 ============
#define CONTROL_KEY_SW_ID              1u
#define CONTROL_KEY_SW_NAME            "sw"
#define CONTROL_KEY_SW_PIN             GET_PIN(C, 9)

// ============ 旋转编码器配置（GPIO轮询 + AB相状态机）============
#define CONTROL_ENCODER_CLK_PIN        GET_PIN(B, 6)   // CLK (A相)
#define CONTROL_ENCODER_DT_PIN         GET_PIN(B, 7)   // DT  (B相)
#define CONTROL_ENCODER_STEPS_PER_CMD  2               // 一个完整编码器档位通常包含4个合法跳变
#define CONTROL_ENCODER_CMD_INTERVAL_MS 50             // AVRCP音量命令最小间隔

// ============ 线程配置 ============
#define CONTROL_THREAD_STACK_SIZE      2048
#define CONTROL_THREAD_PRIORITY        19
#define CONTROL_THREAD_TICK            10
#define CONTROL_KEY_POLL_MS            10u
#define CONTROL_ENCODER_SAMPLE_MS      1u

// ============ 按键模块变量 ============
static keyboard_control_t g_keyboard_ctrl;
static rt_thread_t g_control_thread;

// ============ 旋转编码器模块变量（GPIO轮询 + AB相状态机）============
static rt_int8_t g_encoder_accumulator = 0;             // AB相合法跳变累计
static rt_int8_t g_encoder_pending_cmds = 0;            // 待发送音量命令，正数音量+，负数音量-
static uint8_t g_encoder_last_state = 0;                // 上一次AB相状态
static rt_tick_t g_encoder_last_cmd_tick = 0;           // 上次音量命令发送时间

static uint8_t control_read_pin(uint8_t pin)
{
    return (uint8_t)(rt_pin_read((rt_base_t)pin) ? 1u : 0u);
}

static uint32_t control_get_tick_ms(void)
{
    return (uint32_t)rt_tick_get_millisecond();
}

static void control_key_event_cb(const char *keyname, uint16_t key_id, kb_event_t evt, void *user)
{
    RT_UNUSED(keyname);
    RT_UNUSED(user);

    if (key_id != CONTROL_KEY_SW_ID)
    {
        return;
    }


    if (evt == KB_EVT_CLICK) //单击暂停/继续
    {
        /* PLAYING -> pause；PAUSED/STOPPED/UNKNOWN -> play。
         * 之前 PAUSED/UNKNOWN 落入 default 会直接丢弃，导致只能暂停一次。 */
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
    }

    if (evt == KB_EVT_DOUBLE_CLICK) //双击下一首
    {
        bt_avrcp_ct_next();
    }

}

// ============ 旋转编码器模块（GPIO轮询 + AB相状态机）============

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

/**
 * @brief 初始化旋转编码器（GPIO轮询模式）
 * @return RT_EOK 成功，-RT_ERROR 失败
 */
static rt_err_t control_encoder_init(void)
{
    /* 配置CLK引脚为上拉输入 */
    rt_pin_mode(CONTROL_ENCODER_CLK_PIN, PIN_MODE_INPUT_PULLUP);

    /* 配置DT引脚为上拉输入 */
    rt_pin_mode(CONTROL_ENCODER_DT_PIN, PIN_MODE_INPUT_PULLUP);

    g_encoder_accumulator = 0;
    g_encoder_pending_cmds = 0;
    g_encoder_last_state = control_encoder_read_state();
    g_encoder_last_cmd_tick = 0;

    LOG_I("rotary encoder init ok (polling mode), CLK=PB6, DT=PB7, steps_per_cmd=%d",
          CONTROL_ENCODER_STEPS_PER_CMD);
    return RT_EOK;
}

/**
 * @brief 采样旋转编码器AB相，累计合法方向步进
 */
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

/**
 * @brief 发送旋转编码器累计出的音量命令
 */
static void control_encoder_poll(void)
{
    rt_tick_t current_tick;

    if (g_encoder_pending_cmds == 0)
    {
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

// ============ 统一轮询线程 ============

static void control_thread_entry(void *parameter)
{
    uint32_t key_poll_elapsed_ms = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        control_encoder_sample();
        control_encoder_poll();

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

    // ============ 初始化按键模块 ============
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

    LOG_I("key sw init ok, pin=PC9");

    // ============ 初始化旋转编码器模块 ============
    control_encoder_init();  /* 编码器初始化失败不影响整体启动 */

    // ============ 创建统一控制线程 ============
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
