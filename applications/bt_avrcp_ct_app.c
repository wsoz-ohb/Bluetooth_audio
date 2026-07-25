/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_avrcp_ct_app.h"
#include "es8311_audio.h"
#include "bt_config.h"
#include "btstack_event.h"
#include "btstack_run_loop.h"
#include "btstack_util.h"
#include "hci.h"
#include "hci_cmd.h"
#include "classic/avrcp_target.h"
#include "classic/avrcp_controller.h"
#include "classic/avrcp.h"
#include "classic/sdp_server.h"
#include <rthw.h>
#include <string.h>

#define DBG_TAG "bt_avrcp_ct"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_APP_AVRCP_CT_SDP_RECORD_HANDLE      0x00010002u
#define BT_APP_AVRCP_CT_SDP_RECORD_SIZE        200u
#define BT_APP_AVRCP_CT_SUPPORTED_FEATURES     (AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER | \
                                                AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER)
#define BT_APP_AVRCP_TG_SDP_RECORD_HANDLE      0x00010003u
#define BT_APP_AVRCP_TG_SDP_RECORD_SIZE        200u
/* 作为音箱 TG 声明 Category2，供手机做绝对音量同步。 */
#define BT_APP_AVRCP_TG_SUPPORTED_FEATURES     (AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER)
#define BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE     8u

typedef enum
{
    BT_AVRCP_CT_COMMAND_PLAY = 0,
    BT_AVRCP_CT_COMMAND_PAUSE,
    BT_AVRCP_CT_COMMAND_NEXT,
    BT_AVRCP_CT_COMMAND_PREVIOUS,
    BT_AVRCP_CT_COMMAND_VOLUME_UP,
    BT_AVRCP_CT_COMMAND_VOLUME_DOWN,
} bt_avrcp_ct_command_t;

typedef uint8_t (*bt_avrcp_ct_command_sender_t)(uint16_t avrcp_cid);

static uint8_t bt_app_avrcp_ct_sdp_record[BT_APP_AVRCP_CT_SDP_RECORD_SIZE];
static uint8_t bt_app_avrcp_tg_sdp_record[BT_APP_AVRCP_TG_SDP_RECORD_SIZE];
static uint16_t bt_avrcp_ct_cid = 0u;
static bt_avrcp_ct_link_state_t bt_avrcp_ct_link_state = BT_AVRCP_CT_LINK_STATE_DISCONNECTED;
static bt_avrcp_ct_playback_state_t bt_avrcp_ct_playback_state = BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN;
static bt_avrcp_ct_op_state_t bt_avrcp_ct_op_state = BT_AVRCP_CT_OP_STATE_IDLE;
static rt_bool_t bt_avrcp_ct_playback_status_notify_enabled = RT_FALSE;
/* 当前曲目总时长/进度缓存，供 pos 日志打印 “当前/总时长”。 */
static uint32_t bt_avrcp_ct_song_length_ms = 0u;
static uint32_t bt_avrcp_ct_song_position_ms = 0u;
static btstack_context_callback_registration_t bt_avrcp_ct_command_registration;
static bt_avrcp_ct_command_t bt_avrcp_ct_command_queue[BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE];
static uint8_t bt_avrcp_ct_command_read_index = 0u;
static uint8_t bt_avrcp_ct_command_write_index = 0u;
static uint8_t bt_avrcp_ct_command_count = 0u;
static rt_bool_t bt_avrcp_ct_command_callback_pending = RT_FALSE;

/* 绝对音量会话：默认 false，收到对端 SetAbsoluteVolume 后置 true。 */
static rt_bool_t bt_avrcp_ct_absolute_volume_active = RT_FALSE;
/* 编码器本地绝对音量步进（0~127），约 4% 一档。 */
#define BT_APP_AVRCP_ABS_VOLUME_STEP           5u
static int16_t bt_avrcp_ct_abs_volume_pending_delta = 0;
static rt_bool_t bt_avrcp_ct_abs_volume_callback_pending = RT_FALSE;
static btstack_context_callback_registration_t bt_avrcp_ct_abs_volume_registration;

const char * bt_avrcp_ct_link_state_name(bt_avrcp_ct_link_state_t state)
{
    switch (state)
    {
    case BT_AVRCP_CT_LINK_STATE_DISCONNECTED:
        return "disconnected";
    case BT_AVRCP_CT_LINK_STATE_CONNECTED:
        return "connected";
    default:
        return "unknown";
    }
}

const char * bt_avrcp_ct_playback_state_name(bt_avrcp_ct_playback_state_t state)
{
    switch (state)
    {
    case BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN:
        return "unknown";
    case BT_AVRCP_CT_PLAYBACK_STATE_STOPPED:
        return "stopped";
    case BT_AVRCP_CT_PLAYBACK_STATE_PLAYING:
        return "playing";
    case BT_AVRCP_CT_PLAYBACK_STATE_PAUSED:
        return "paused";
    default:
        return "invalid";
    }
}

const char * bt_avrcp_ct_op_state_name(bt_avrcp_ct_op_state_t state)
{
    switch (state)
    {
    case BT_AVRCP_CT_OP_STATE_IDLE:
        return "idle";
    case BT_AVRCP_CT_OP_STATE_WAIT_INITIAL_STATUS:
        return "wait_initial_status";
    case BT_AVRCP_CT_OP_STATE_WAIT_PLAY_ACK:
        return "wait_play_ack";
    case BT_AVRCP_CT_OP_STATE_WAIT_PAUSE_ACK:
        return "wait_pause_ack";
    default:
        return "invalid";
    }
}

static const char * bt_avrcp_ct_play_status_name(uint8_t play_status)
{
    switch (play_status)
    {
    case AVRCP_PLAYBACK_STATUS_STOPPED:
        return "stopped";
    case AVRCP_PLAYBACK_STATUS_PLAYING:
        return "playing";
    case AVRCP_PLAYBACK_STATUS_PAUSED:
        return "paused";
    case AVRCP_PLAYBACK_STATUS_FWD_SEEK:
        return "forward_seek";
    case AVRCP_PLAYBACK_STATUS_REV_SEEK:
        return "reverse_seek";
    case AVRCP_PLAYBACK_STATUS_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

static void bt_avrcp_ct_set_link_state(bt_avrcp_ct_link_state_t state)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_link_state = state;
    rt_hw_interrupt_enable(level);
}

static void bt_avrcp_ct_set_playback_state(bt_avrcp_ct_playback_state_t state)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_playback_state = state;
    rt_hw_interrupt_enable(level);
}

static void bt_avrcp_ct_set_op_state(bt_avrcp_ct_op_state_t state)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_op_state = state;
    rt_hw_interrupt_enable(level);
}

static void bt_avrcp_ct_set_playback_notify_enabled(rt_bool_t enabled)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_playback_status_notify_enabled = enabled ? RT_TRUE : RT_FALSE;
    rt_hw_interrupt_enable(level);
}

static void bt_avrcp_ct_set_absolute_volume_active(rt_bool_t active)
{
    rt_base_t level;
    rt_bool_t old_active;

    level = rt_hw_interrupt_disable();
    old_active = bt_avrcp_ct_absolute_volume_active;
    bt_avrcp_ct_absolute_volume_active = active ? RT_TRUE : RT_FALSE;
    rt_hw_interrupt_enable(level);

    if (old_active != bt_avrcp_ct_absolute_volume_active)
    {
        LOG_I("AVRCP absolute volume mode: %s",
              bt_avrcp_ct_absolute_volume_active ? "active" : "inactive");
    }
}

bt_avrcp_ct_link_state_t bt_avrcp_ct_get_link_state(void)
{
    rt_base_t level;
    bt_avrcp_ct_link_state_t state;

    level = rt_hw_interrupt_disable();
    state = bt_avrcp_ct_link_state;
    rt_hw_interrupt_enable(level);

    return state;
}

bt_avrcp_ct_playback_state_t bt_avrcp_ct_get_playback_state(void)
{
    rt_base_t level;
    bt_avrcp_ct_playback_state_t state;

    level = rt_hw_interrupt_disable();
    state = bt_avrcp_ct_playback_state;
    rt_hw_interrupt_enable(level);

    return state;
}

bt_avrcp_ct_op_state_t bt_avrcp_ct_get_op_state(void)
{
    rt_base_t level;
    bt_avrcp_ct_op_state_t state;

    level = rt_hw_interrupt_disable();
    state = bt_avrcp_ct_op_state;
    rt_hw_interrupt_enable(level);

    return state;
}

rt_bool_t bt_avrcp_ct_is_connected(void)
{
    return (rt_bool_t) (bt_avrcp_ct_get_link_state() == BT_AVRCP_CT_LINK_STATE_CONNECTED);
}

static bt_avrcp_ct_playback_state_t bt_avrcp_ct_map_play_status(uint8_t play_status)
{
    switch (play_status)
    {
    case AVRCP_PLAYBACK_STATUS_STOPPED:
        return BT_AVRCP_CT_PLAYBACK_STATE_STOPPED;
    case AVRCP_PLAYBACK_STATUS_PLAYING:
    case AVRCP_PLAYBACK_STATUS_FWD_SEEK:
    case AVRCP_PLAYBACK_STATUS_REV_SEEK:
        return BT_AVRCP_CT_PLAYBACK_STATE_PLAYING;
    case AVRCP_PLAYBACK_STATUS_PAUSED:
        return BT_AVRCP_CT_PLAYBACK_STATE_PAUSED;
    case AVRCP_PLAYBACK_STATUS_ERROR:
    default:
        return BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN;
    }
}

static void bt_avrcp_ct_reset_session_state(void)
{
    rt_base_t level;

    bt_avrcp_ct_set_link_state(BT_AVRCP_CT_LINK_STATE_DISCONNECTED);
    bt_avrcp_ct_set_playback_state(BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN);
    bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_IDLE);
    bt_avrcp_ct_set_playback_notify_enabled(RT_FALSE);
    bt_avrcp_ct_set_absolute_volume_active(RT_FALSE);
    bt_avrcp_ct_song_length_ms = 0u;
    bt_avrcp_ct_song_position_ms = 0u;

    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_abs_volume_pending_delta = 0;
    bt_avrcp_ct_abs_volume_callback_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);
}

static void bt_avrcp_ct_request_now_playing_info(const char * reason);

static void bt_avrcp_ct_update_playback_state(uint8_t play_status, const char * source)
{
    bt_avrcp_ct_playback_state_t old_state;
    bt_avrcp_ct_playback_state_t new_state;
    bt_avrcp_ct_op_state_t op_state;

    old_state = bt_avrcp_ct_get_playback_state();
    new_state = bt_avrcp_ct_map_play_status(play_status);
    bt_avrcp_ct_set_playback_state(new_state);

    if (old_state != new_state)
    {
        LOG_I("AVRCP cached playback state: %s -> %s, source=%s",
              bt_avrcp_ct_playback_state_name(old_state),
              bt_avrcp_ct_playback_state_name(new_state),
              source);

        /* 仅在“开始播放”时拉元数据：unknown/paused/stopped -> playing */
        if (new_state == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING)
        {
            bt_avrcp_ct_request_now_playing_info("start_playing");
        }
    }

    op_state = bt_avrcp_ct_get_op_state();
    if (new_state == BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN)
    {
        return;
    }

    if ((op_state == BT_AVRCP_CT_OP_STATE_WAIT_INITIAL_STATUS) ||
        ((op_state == BT_AVRCP_CT_OP_STATE_WAIT_PLAY_ACK) &&
         (new_state == BT_AVRCP_CT_PLAYBACK_STATE_PLAYING)) ||
        ((op_state == BT_AVRCP_CT_OP_STATE_WAIT_PAUSE_ACK) &&
         ((new_state == BT_AVRCP_CT_PLAYBACK_STATE_PAUSED) ||
          (new_state == BT_AVRCP_CT_PLAYBACK_STATE_STOPPED))))
    {
        bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_IDLE);
    }
}

/* 主动拉取 Now Playing：歌名/歌手/专辑/总时长等。
 * 仅在 start_playing / track_changed 两条路径调用；对端不支持时失败属正常。 */
static void bt_avrcp_ct_request_now_playing_info(const char * reason)
{
    uint8_t status;

    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        return;
    }

    status = avrcp_controller_get_now_playing_info(bt_avrcp_ct_cid);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_W("AVRCP get_now_playing_info failed, status=0x%02x, cid=0x%04x, reason=%s",
              status,
              bt_avrcp_ct_cid,
              (reason != RT_NULL) ? reason : "unknown");
        return;
    }

    LOG_I("AVRCP get_now_playing_info requested, cid=0x%04x, reason=%s",
          bt_avrcp_ct_cid,
          (reason != RT_NULL) ? reason : "unknown");
}

static void bt_avrcp_ct_sync_remote_playback_state(void)
{
    uint8_t status;

    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        return;
    }

    bt_avrcp_ct_set_playback_notify_enabled(RT_FALSE);  
    //注册播放状态变化通知
    status = avrcp_controller_enable_notification(bt_avrcp_ct_cid,
                                                  AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_W("AVRCP playback status notification request failed, status=0x%02x, cid=0x%04x",
              status,
              bt_avrcp_ct_cid);
    }
    else
    {
        LOG_I("AVRCP playback status notification requested, cid=0x%04x", bt_avrcp_ct_cid);
    }

    /* 注册播放进度变化通知；BTstack 内部 interval 固定约 1s，CHANGED 后会自动重新注册。 */
    status = avrcp_controller_enable_notification(bt_avrcp_ct_cid,
                                                  AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_W("AVRCP playback position notification request failed, status=0x%02x, cid=0x%04x",
              status,
              bt_avrcp_ct_cid);
    }
    else
    {
        LOG_I("AVRCP playback position notification requested, cid=0x%04x", bt_avrcp_ct_cid);
    }

    /* 曲目变化后再拉元数据；很多手机切歌只靠这条通知。 */
    status = avrcp_controller_enable_notification(bt_avrcp_ct_cid,
                                                  AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_W("AVRCP track changed notification request failed, status=0x%02x, cid=0x%04x",
              status,
              bt_avrcp_ct_cid);
    }
    else
    {
        LOG_I("AVRCP track changed notification requested, cid=0x%04x", bt_avrcp_ct_cid);
    }

    status = avrcp_controller_get_play_status(bt_avrcp_ct_cid); //主动查询播放状态
    if (status != ERROR_CODE_SUCCESS)
    {
        bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_IDLE);
        LOG_W("AVRCP get_play_status request failed, status=0x%02x, cid=0x%04x",
              status,
              bt_avrcp_ct_cid);
        return;
    }

    bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_WAIT_INITIAL_STATUS); //操作等待状态
    LOG_I("AVRCP get_play_status requested, cid=0x%04x", bt_avrcp_ct_cid);
}

static const char * bt_avrcp_ct_operation_name(uint8_t operation_id)
{
    switch (operation_id)
    {
    case AVRCP_OPERATION_ID_PLAY:
        return "play";
    case AVRCP_OPERATION_ID_PAUSE:
        return "pause";
    case AVRCP_OPERATION_ID_STOP:
        return "stop";
    case AVRCP_OPERATION_ID_FORWARD:
        return "forward";
    case AVRCP_OPERATION_ID_BACKWARD:
        return "backward";
    case AVRCP_OPERATION_ID_FAST_FORWARD:
        return "fast_forward";
    case AVRCP_OPERATION_ID_REWIND:
        return "rewind";
    case AVRCP_OPERATION_ID_VOLUME_UP:
        return "volume_up";
    case AVRCP_OPERATION_ID_VOLUME_DOWN:
        return "volume_down";
    case AVRCP_OPERATION_ID_MUTE:
        return "mute";
    default:
        return "other";
    }
}

static void bt_avrcp_ct_format_ms(uint32_t ms, char * buf, rt_size_t buf_len)
{
    uint32_t total_sec;
    uint32_t min;
    uint32_t sec;

    if ((buf == RT_NULL) || (buf_len < 8u))
    {
        return;
    }

    total_sec = ms / 1000u;
    min = total_sec / 60u;
    sec = total_sec % 60u;
    rt_snprintf(buf, buf_len, "%u:%02u", (unsigned int)min, (unsigned int)sec);
}

static void bt_avrcp_ct_set_song_length_ms(uint32_t length_ms)
{
    bt_avrcp_ct_song_length_ms = length_ms;
}

static void bt_avrcp_ct_set_song_position_ms(uint32_t position_ms)
{
    bt_avrcp_ct_song_position_ms = position_ms;
}

static void bt_avrcp_ct_log_playback_progress(uint32_t position_ms, uint8_t ctype, uint16_t cid)
{
    char pos_text[16];
    char len_text[16];
    uint32_t length_ms;
    unsigned int percent;

    length_ms = bt_avrcp_ct_song_length_ms;
    bt_avrcp_ct_set_song_position_ms(position_ms);
    bt_avrcp_ct_format_ms(position_ms, pos_text, sizeof(pos_text));

    if (length_ms == 0u)
    {
        LOG_I("AVRCP playback progress, cid=0x%04x, ctype=0x%02x, %s / unknown",
              cid,
              ctype,
              pos_text);
        return;
    }

    bt_avrcp_ct_format_ms(length_ms, len_text, sizeof(len_text));
    if (position_ms >= length_ms)
    {
        percent = 100u;
    }
    else
    {
        percent = (unsigned int)((position_ms * 100u) / length_ms);
    }

    LOG_I("AVRCP playback progress, cid=0x%04x, ctype=0x%02x, %s / %s (%u percent)",
          cid,
          ctype,
          pos_text,
          len_text,
          percent);
}

static void bt_avrcp_ct_log_text(const char * name, const uint8_t * value, uint8_t value_len)
{
    char text[64];
    uint8_t copy_len;

    if ((value == RT_NULL) || (value_len == 0u))
    {
        LOG_I("AVRCP now playing %s=", name);
        return;
    }

    copy_len = value_len;
    if (copy_len >= sizeof(text))
    {
        copy_len = sizeof(text) - 1u;
    }

    memcpy(text, value, copy_len);
    text[copy_len] = '\0';
    LOG_I("AVRCP now playing %s=%s", name, text);
}

static rt_err_t bt_avrcp_ct_sdp_register_service(void)
{
    uint8_t status;

    avrcp_controller_create_sdp_record(bt_app_avrcp_ct_sdp_record,
                                       BT_APP_AVRCP_CT_SDP_RECORD_HANDLE,
                                       BT_APP_AVRCP_CT_SUPPORTED_FEATURES,
                                       "WSOZ AVRCP Controller",
                                       BT_CFG_LOCAL_NAME);

    status = sdp_register_service(bt_app_avrcp_ct_sdp_record);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("AVRCP CT sdp_register_service failed: 0x%02x", status);
        return -RT_ERROR;
    }

    LOG_I("AVRCP CT SDP service registered, handle=0x%08x, features=0x%04x",
          BT_APP_AVRCP_CT_SDP_RECORD_HANDLE,
          BT_APP_AVRCP_CT_SUPPORTED_FEATURES);

    /* Target SDP：让手机把我们当可设绝对音量的音箱。 */
    avrcp_target_create_sdp_record(bt_app_avrcp_tg_sdp_record,
                                   BT_APP_AVRCP_TG_SDP_RECORD_HANDLE,
                                   BT_APP_AVRCP_TG_SUPPORTED_FEATURES,
                                   "WSOZ AVRCP Target",
                                   BT_CFG_LOCAL_NAME);

    status = sdp_register_service(bt_app_avrcp_tg_sdp_record);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("AVRCP TG sdp_register_service failed: 0x%02x", status);
        return -RT_ERROR;
    }

    LOG_I("AVRCP TG SDP service registered, handle=0x%08x, features=0x%04x",
          BT_APP_AVRCP_TG_SDP_RECORD_HANDLE,
          BT_APP_AVRCP_TG_SUPPORTED_FEATURES);
    return RT_EOK;
}

static const char * bt_avrcp_ct_command_name(bt_avrcp_ct_command_t command)
{
    switch (command)
    {
    case BT_AVRCP_CT_COMMAND_PLAY:
        return "play";
    case BT_AVRCP_CT_COMMAND_PAUSE:
        return "pause";
    case BT_AVRCP_CT_COMMAND_NEXT:
        return "next";
    case BT_AVRCP_CT_COMMAND_PREVIOUS:
        return "previous";
    case BT_AVRCP_CT_COMMAND_VOLUME_UP:
        return "volume_up";
    case BT_AVRCP_CT_COMMAND_VOLUME_DOWN:
        return "volume_down";
    default:
        return "unknown";
    }
}

static bt_avrcp_ct_command_sender_t bt_avrcp_ct_command_sender(bt_avrcp_ct_command_t command)
{
    switch (command)
    {
    case BT_AVRCP_CT_COMMAND_PLAY:
        return avrcp_controller_play;
    case BT_AVRCP_CT_COMMAND_PAUSE:
        return avrcp_controller_pause;
    case BT_AVRCP_CT_COMMAND_NEXT:
        return avrcp_controller_forward;
    case BT_AVRCP_CT_COMMAND_PREVIOUS:
        return avrcp_controller_backward;
    case BT_AVRCP_CT_COMMAND_VOLUME_UP:
        return avrcp_controller_volume_up;
    case BT_AVRCP_CT_COMMAND_VOLUME_DOWN:
        return avrcp_controller_volume_down;
    default:
        return RT_NULL;
    }
}

static rt_err_t bt_avrcp_ct_send_command_now(bt_avrcp_ct_command_t command)
{
    const char * name;
    bt_avrcp_ct_command_sender_t sender;
    uint8_t status;

    name = bt_avrcp_ct_command_name(command);
    sender = bt_avrcp_ct_command_sender(command);
    if (sender == RT_NULL)
    {
        LOG_E("AVRCP CT %s failed, sender is null", name);
        return -RT_ERROR;
    }

    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        LOG_W("AVRCP CT %s ignored, not connected", name);
        return -RT_ERROR;
    }

    status = sender(bt_avrcp_ct_cid);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("AVRCP CT %s failed, status=0x%02x, cid=0x%04x",
              name,
              status,
              bt_avrcp_ct_cid);
        return -RT_ERROR;
    }

    if (command == BT_AVRCP_CT_COMMAND_PLAY)
    {
        bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_WAIT_PLAY_ACK);
    }
    else if (command == BT_AVRCP_CT_COMMAND_PAUSE)
    {
        bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_WAIT_PAUSE_ACK);
    }

    LOG_I("AVRCP CT %s requested, cid=0x%04x", name, bt_avrcp_ct_cid);
    return RT_EOK;
}

static void bt_avrcp_ct_run_queued_commands(void * context)
{
    RT_UNUSED(context);

    while (1)
    {
        rt_base_t level;
        bt_avrcp_ct_command_t command;

        level = rt_hw_interrupt_disable();
        if (bt_avrcp_ct_command_count == 0u)
        {
            bt_avrcp_ct_command_callback_pending = RT_FALSE;
            rt_hw_interrupt_enable(level);
            break;
        }

        command = bt_avrcp_ct_command_queue[bt_avrcp_ct_command_read_index];
        bt_avrcp_ct_command_read_index++;
        if (bt_avrcp_ct_command_read_index >= BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE)
        {
            bt_avrcp_ct_command_read_index = 0u;
        }
        bt_avrcp_ct_command_count--;
        rt_hw_interrupt_enable(level);

        (void)bt_avrcp_ct_send_command_now(command);
    }
}

static rt_err_t bt_avrcp_ct_post_command(bt_avrcp_ct_command_t command)
{
    rt_bool_t schedule_callback;
    rt_base_t level;
    const char * name;

    name = bt_avrcp_ct_command_name(command);
    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        LOG_W("AVRCP CT %s ignored, not connected", name);
        return -RT_ERROR;
    }

    schedule_callback = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (bt_avrcp_ct_command_count >= BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE)
    {
        rt_hw_interrupt_enable(level);
        LOG_E("AVRCP CT %s ignored, command queue full", name);
        return -RT_ERROR;
    }

    bt_avrcp_ct_command_queue[bt_avrcp_ct_command_write_index] = command;
    bt_avrcp_ct_command_write_index++;
    if (bt_avrcp_ct_command_write_index >= BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE)
    {
        bt_avrcp_ct_command_write_index = 0u;
    }
    bt_avrcp_ct_command_count++;

    if (!bt_avrcp_ct_command_callback_pending)
    {
        bt_avrcp_ct_command_callback_pending = RT_TRUE;
        bt_avrcp_ct_command_registration.item = NULL;
        bt_avrcp_ct_command_registration.callback = bt_avrcp_ct_run_queued_commands;
        bt_avrcp_ct_command_registration.context = NULL;
        schedule_callback = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (schedule_callback)
    {
        btstack_run_loop_execute_on_main_thread(&bt_avrcp_ct_command_registration);
    }

    LOG_I("AVRCP CT %s queued", name);
    return RT_EOK;
}

rt_err_t bt_avrcp_ct_connect(const bd_addr_t remote_addr)
{
    uint16_t avrcp_cid = 0u;
    uint8_t status;

    if (remote_addr == RT_NULL)
    {
        LOG_E("AVRCP CT connect failed, remote addr is null");
        return -RT_ERROR;
    }

    if (bt_avrcp_ct_is_connected())
    {
        LOG_I("AVRCP CT already connected, cid=0x%04x", bt_avrcp_ct_cid);
        return RT_EOK;
    }

    status = avrcp_connect(remote_addr, &avrcp_cid);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("AVRCP CT connect request failed, status=0x%02x, remote=%s",
              status,
              bd_addr_to_str(remote_addr));
        return -RT_ERROR;
    }

    LOG_I("AVRCP CT connect requested, remote=%s, pending_cid=0x%04x",
          bd_addr_to_str(remote_addr),
          avrcp_cid);
    return RT_EOK;
}


static void bt_avrcp_ct_apply_absolute_volume(uint16_t avrcp_cid, uint8_t volume)
{
    volume = (uint8_t)(volume & 0x7Fu);

    /* 对端开始用绝对音量后，旋钮改走本地增益路径。 */
    bt_avrcp_ct_set_absolute_volume_active(RT_TRUE);

    /* 手机 SetAbsoluteVolume 时，BTstack 已写入 target_absolute_volume；
     * 这里只负责把 0~127 映射到本地 ES8311 增益。 */
    if (es8311_audio_set_volume(volume) != RT_EOK)
    {
        LOG_W("apply absolute volume failed, cid=0x%04x, volume=%u/127", avrcp_cid, volume);
        return;
    }

    LOG_I("absolute volume applied, cid=0x%04x, volume=%u/127 (%u percent)",
          avrcp_cid,
          volume,
          (unsigned int)((volume * 100u) / 127u));
}

/* 本地旋钮调整绝对音量：改本地增益，并通知手机音量条。 */
static void bt_avrcp_ct_apply_local_absolute_volume_delta(int16_t delta)
{
    int32_t volume;
    uint8_t new_volume;
    uint8_t status;

    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        LOG_W("local absolute volume ignored, not connected");
        return;
    }

    if (delta == 0)
    {
        return;
    }

    volume = (int32_t)es8311_audio_get_volume() + (int32_t)delta;
    if (volume < 0)
    {
        volume = 0;
    }
    else if (volume > 127)
    {
        volume = 127;
    }
    new_volume = (uint8_t)volume;

    if (es8311_audio_set_volume(new_volume) != RT_EOK)
    {
        LOG_W("local absolute volume set failed, volume=%u/127", new_volume);
        return;
    }

    status = avrcp_target_volume_changed(bt_avrcp_ct_cid, new_volume);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_W("notify absolute volume failed, status=0x%02x, cid=0x%04x, volume=%u",
              status,
              bt_avrcp_ct_cid,
              new_volume);
    }

    LOG_I("local absolute volume %s, volume=%u/127 (%u percent), notify_status=0x%02x",
          (delta > 0) ? "up" : "down",
          new_volume,
          (unsigned int)((new_volume * 100u) / 127u),
          status);
}

static void bt_avrcp_ct_run_abs_volume_delta(void * context)
{
    int16_t delta;
    rt_base_t level;

    RT_UNUSED(context);

    level = rt_hw_interrupt_disable();
    delta = bt_avrcp_ct_abs_volume_pending_delta;
    bt_avrcp_ct_abs_volume_pending_delta = 0;
    bt_avrcp_ct_abs_volume_callback_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);

    if (delta == 0)
    {
        return;
    }

    bt_avrcp_ct_apply_local_absolute_volume_delta(delta);
}

static rt_err_t bt_avrcp_ct_post_abs_volume_delta(int16_t delta)
{
    rt_bool_t schedule_callback;
    rt_base_t level;

    if (!bt_avrcp_ct_is_connected() || (bt_avrcp_ct_cid == 0u))
    {
        LOG_W("local absolute volume ignored, not connected");
        return -RT_ERROR;
    }

    if (delta == 0)
    {
        return RT_EOK;
    }

    schedule_callback = RT_FALSE;
    level = rt_hw_interrupt_disable();
    bt_avrcp_ct_abs_volume_pending_delta =
        (int16_t)(bt_avrcp_ct_abs_volume_pending_delta + delta);
    /* 防止连拧把 pending 顶得过大。 */
    if (bt_avrcp_ct_abs_volume_pending_delta > 127)
    {
        bt_avrcp_ct_abs_volume_pending_delta = 127;
    }
    else if (bt_avrcp_ct_abs_volume_pending_delta < -127)
    {
        bt_avrcp_ct_abs_volume_pending_delta = -127;
    }

    if (!bt_avrcp_ct_abs_volume_callback_pending)
    {
        bt_avrcp_ct_abs_volume_callback_pending = RT_TRUE;
        bt_avrcp_ct_abs_volume_registration.item = NULL;
        bt_avrcp_ct_abs_volume_registration.callback = bt_avrcp_ct_run_abs_volume_delta;
        bt_avrcp_ct_abs_volume_registration.context = NULL;
        schedule_callback = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (schedule_callback)
    {
        btstack_run_loop_execute_on_main_thread(&bt_avrcp_ct_abs_volume_registration);
    }

    return RT_EOK;
}

void btstack_event_avrcp_controller_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if(packet_type != HCI_EVENT_PACKET){
        return;
    }

    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META)
    {
        return;
    }

    unsigned int event = hci_event_avrcp_meta_get_subevent_code(packet);
    switch (event) 
    {
    case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:  // AVRCP 连接建立
    {
        bd_addr_t remote_addr;
        uint8_t status;

        //状态打印
        status = avrcp_subevent_connection_established_get_status(packet);
        avrcp_subevent_connection_established_get_bd_addr(packet, remote_addr);
        if (status != ERROR_CODE_SUCCESS)
        {
            LOG_E("AVRCP CT connect failed, status=0x%02x, remote=%s",
                  status,
                  bd_addr_to_str(remote_addr));
            break;
        }
        //记录CID，记录本地状态机
        bt_avrcp_ct_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        bt_avrcp_ct_set_link_state(BT_AVRCP_CT_LINK_STATE_CONNECTED);
        bt_avrcp_ct_set_playback_state(BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN);
        bt_avrcp_ct_set_op_state(BT_AVRCP_CT_OP_STATE_IDLE);
        bt_avrcp_ct_set_playback_notify_enabled(RT_FALSE);
        /* 连接初期先按相对音量；对端真正 SetAbsoluteVolume 后再切绝对模式。 */
        bt_avrcp_ct_set_absolute_volume_active(RT_FALSE);
        LOG_I("AVRCP CT connected, remote=%s, cid=0x%04x, handle=0x%04x",
              bd_addr_to_str(remote_addr),
              bt_avrcp_ct_cid,
              avrcp_subevent_connection_established_get_con_handle(packet));
        /* 声明 TG 支持 VOLUME_CHANGED，否则手机 GetCapabilities 看不到绝对音量能力。 */
        if (avrcp_target_support_event(bt_avrcp_ct_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED) != ERROR_CODE_SUCCESS)
        {
            LOG_W("AVRCP TG support VOLUME_CHANGED failed, cid=0x%04x", bt_avrcp_ct_cid);
        }
        else
        {
            LOG_I("AVRCP TG VOLUME_CHANGED supported, cid=0x%04x", bt_avrcp_ct_cid);
        }
        /* 仅写入 TG 上下文初始绝对音量，不主动通知对端。 */
        if (avrcp_target_adjust_absolute_volume(bt_avrcp_ct_cid, es8311_audio_get_volume()) != ERROR_CODE_SUCCESS)
        {
            LOG_W("AVRCP TG seed absolute volume failed, cid=0x%04x, volume=%u",
                  bt_avrcp_ct_cid,
                  es8311_audio_get_volume());
        }
        else
        {
            LOG_I("AVRCP TG absolute volume seeded, cid=0x%04x, volume=%u/127",
                  bt_avrcp_ct_cid,
                  es8311_audio_get_volume());
        }
        bt_avrcp_ct_sync_remote_playback_state();   //同步远程播放状态
        break;
    }

    case AVRCP_SUBEVENT_CONNECTION_RELEASED:  // AVRCP 连接断开
    {
        uint16_t cid;

        cid = avrcp_subevent_connection_released_get_avrcp_cid(packet);
        LOG_I("AVRCP CT released, cid=0x%04x", cid);
        if (bt_avrcp_ct_cid == cid)
        {
            bt_avrcp_ct_cid = 0u;
            bt_avrcp_ct_reset_session_state();
        }
        break;
    }

    case AVRCP_SUBEVENT_OPERATION_START:  // 操作开始(按键按下等)
    {
        uint8_t operation_id;

        operation_id = avrcp_subevent_operation_start_get_operation_id(packet);
        LOG_I("AVRCP operation start, cid=0x%04x, ctype=0x%02x, operation=%s(0x%02x)",
              avrcp_subevent_operation_start_get_avrcp_cid(packet),
              avrcp_subevent_operation_start_get_command_type(packet),
              bt_avrcp_ct_operation_name(operation_id),
              operation_id);
        break;
    }

    case AVRCP_SUBEVENT_OPERATION_COMPLETE:  // 操作完成应答
    {
        uint8_t operation_id;

        operation_id = avrcp_subevent_operation_complete_get_operation_id(packet);
        LOG_I("AVRCP operation complete, cid=0x%04x, ctype=0x%02x, opcode=0x%02x, pdu=0x%02x, operation=%s(0x%02x), status=0x%02x",
              avrcp_subevent_operation_complete_get_avrcp_cid(packet),
              avrcp_subevent_operation_complete_get_command_type(packet),
              avrcp_subevent_operation_complete_get_command_opcode(packet),
              avrcp_subevent_operation_complete_get_pdu_id(packet),
              bt_avrcp_ct_operation_name(operation_id),
              operation_id,
              avrcp_subevent_operation_complete_get_status(packet));
        break;
    }

    case AVRCP_SUBEVENT_OPERATION:  // Target 侧按键操作事件
    {
        uint8_t operation_id;

        operation_id = avrcp_subevent_operation_get_operation_id(packet);
        LOG_I("AVRCP target operation, cid=0x%04x, operation=%s(0x%02x), pressed=%u, operands_len=%u",
              avrcp_subevent_operation_get_avrcp_cid(packet),
              bt_avrcp_ct_operation_name(operation_id),
              operation_id,
              avrcp_subevent_operation_get_button_pressed(packet),
              avrcp_subevent_operation_get_operands_length(packet));
        break;
    }

    case AVRCP_SUBEVENT_PLAY_STATUS:  // 播放状态查询结果(含进度/总时长)
    {
        uint8_t play_status;

        play_status = avrcp_subevent_play_status_get_play_status(packet);
        bt_avrcp_ct_set_song_length_ms(avrcp_subevent_play_status_get_song_length(packet));
        bt_avrcp_ct_set_song_position_ms(avrcp_subevent_play_status_get_song_position(packet));
        LOG_I("AVRCP play status, cid=0x%04x, ctype=0x%02x, length=%u ms, position=%u ms, status=%s(0x%02x)",
              avrcp_subevent_play_status_get_avrcp_cid(packet),
              avrcp_subevent_play_status_get_command_type(packet),
              (unsigned int)bt_avrcp_ct_song_length_ms,
              (unsigned int)bt_avrcp_ct_song_position_ms,
              bt_avrcp_ct_play_status_name(play_status),
              play_status);
        bt_avrcp_ct_update_playback_state(play_status, "play_status");
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_STATE:  // 通知注册结果(成功/失败)
    {
        uint8_t event_id;
        uint8_t enabled;
        uint8_t status;

        event_id = avrcp_subevent_notification_state_get_event_id(packet);
        enabled = avrcp_subevent_notification_state_get_enabled(packet);
        status = avrcp_subevent_notification_state_get_status(packet);
        LOG_I("AVRCP notification state, cid=0x%04x, event_id=0x%02x, enabled=%u, status=0x%02x",
              avrcp_subevent_notification_state_get_avrcp_cid(packet),
              event_id,
              enabled,
              status);
        if (event_id == AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED)
        {
            bt_avrcp_ct_set_playback_notify_enabled((rt_bool_t) ((status == ERROR_CODE_SUCCESS) && (enabled != 0u)));
        }
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:  // 播放状态变化通知(播/停/暂停)
    {
        uint8_t play_status;

        play_status = avrcp_subevent_notification_playback_status_changed_get_play_status(packet);
        LOG_I("AVRCP playback status changed, cid=0x%04x, ctype=0x%02x, status=%s(0x%02x)",
              avrcp_subevent_notification_playback_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_playback_status_changed_get_command_type(packet),
              bt_avrcp_ct_play_status_name(play_status),
              play_status);
        bt_avrcp_ct_update_playback_state(play_status, "notification");
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:  // 当前曲目切换通知
    {
        const uint8_t * identifier;

        identifier = avrcp_subevent_notification_track_changed_get_identifier(packet);
        LOG_I("AVRCP track changed, cid=0x%04x, ctype=0x%02x, track_id=%02x%02x%02x%02x%02x%02x%02x%02x",
              avrcp_subevent_notification_track_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_track_changed_get_command_type(packet),
              identifier[0], identifier[1], identifier[2], identifier[3],
              identifier[4], identifier[5], identifier[6], identifier[7]);
        /* 换歌后总时长先清空，等 Now Playing / play_status 再更新。 */
        bt_avrcp_ct_set_song_length_ms(0u);
        bt_avrcp_ct_set_song_position_ms(0u);
        /* 换歌主路径：当前曲目变了就重新拉歌名/歌手/总时长。 */
        bt_avrcp_ct_request_now_playing_info("track_changed");
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:  // 播放进度变化通知
    {
        uint32_t position_ms;

        position_ms = avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet);
        bt_avrcp_ct_log_playback_progress(position_ms,
                                          avrcp_subevent_notification_playback_pos_changed_get_command_type(packet),
                                          avrcp_subevent_notification_playback_pos_changed_get_avrcp_cid(packet));
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_END:  // 曲目播放到结尾
        LOG_I("AVRCP track reached end, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_event_track_reached_end_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_track_reached_end_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_START:  // 曲目回到开头
        LOG_I("AVRCP track reached start, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_event_track_reached_start_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_track_reached_start_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_BATT_STATUS_CHANGED:  // 对端电量状态变化
        LOG_I("AVRCP battery status changed, cid=0x%04x, ctype=0x%02x, battery=0x%02x",
              avrcp_subevent_notification_event_batt_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_batt_status_changed_get_command_type(packet),
              avrcp_subevent_notification_event_batt_status_changed_get_battery_status(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED:  // 系统状态变化
        LOG_I("AVRCP system status changed, cid=0x%04x, ctype=0x%02x, system=0x%02x",
              avrcp_subevent_notification_event_system_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_system_status_changed_get_command_type(packet),
              avrcp_subevent_notification_event_system_status_changed_get_system_status(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED:  // 播放器应用设置变化
        LOG_I("AVRCP player setting changed, cid=0x%04x, ctype=0x%02x, attr=0x%02x, value=0x%02x",
              avrcp_subevent_notification_event_player_application_setting_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_command_type(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_attribute_id(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_value_id(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:  // Now Playing 列表内容变化
        LOG_I("AVRCP now playing content changed, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_now_playing_content_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_now_playing_content_changed_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:  // 可用播放器列表变化
        LOG_I("AVRCP available players changed, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_available_players_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_available_players_changed_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_ADDRESSED_PLAYER_CHANGED:  // 当前寻址播放器变化
        LOG_I("AVRCP addressed player changed, cid=0x%04x, ctype=0x%02x, player_id=%u, uid_counter=%u",
              avrcp_subevent_notification_addressed_player_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_addressed_player_changed_get_command_type(packet),
              avrcp_subevent_notification_addressed_player_changed_get_player_id(packet),
              avrcp_subevent_notification_addressed_player_changed_get_uid_counter(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_UIDS_CHANGED:  // 媒体库 UID 变化
        LOG_I("AVRCP uids changed, cid=0x%04x, ctype=0x%02x, uid_counter=%u",
              avrcp_subevent_notification_event_uids_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_uids_changed_get_command_type(packet),
              avrcp_subevent_notification_event_uids_changed_get_uid_counter(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:  // TG 收到 SetAbsoluteVolume / 音量变化
    {
        uint16_t cid;
        uint8_t volume;

        cid = avrcp_subevent_notification_volume_changed_get_avrcp_cid(packet);
        volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
        LOG_I("AVRCP volume changed, cid=0x%04x, ctype=0x%02x, volume=%u/127 (%u%%)",
              cid,
              avrcp_subevent_notification_volume_changed_get_command_type(packet),
              volume,
              (unsigned int)((volume * 100u) / 127u));
        bt_avrcp_ct_apply_absolute_volume(cid, volume);
        break;
    }

    case AVRCP_SUBEVENT_SET_ABSOLUTE_VOLUME_RESPONSE:  // CT 侧设置绝对音量应答(当前主路径不用)
    {
        uint8_t volume;

        volume = avrcp_subevent_set_absolute_volume_response_get_absolute_volume(packet);
        LOG_I("AVRCP set absolute volume response, cid=0x%04x, ctype=0x%02x, volume=%u/127 (%u%%)",
              avrcp_subevent_set_absolute_volume_response_get_avrcp_cid(packet),
              avrcp_subevent_set_absolute_volume_response_get_command_type(packet),
              volume,
              (unsigned int)((volume * 100u) / 127u));
        break;
    }

    case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE:  // 随机/循环模式结果
        LOG_I("AVRCP shuffle/repeat mode, cid=0x%04x, ctype=0x%02x, repeat=%u, shuffle=%u",
              avrcp_subevent_shuffle_and_repeat_mode_get_avrcp_cid(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_command_type(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:  // 当前曲目序号
        LOG_I("AVRCP now playing track=%u, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_now_playing_track_info_get_track(packet),
              avrcp_subevent_now_playing_track_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_track_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:  // 曲目总数
        LOG_I("AVRCP now playing total_tracks=%u, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet),
              avrcp_subevent_now_playing_total_tracks_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_total_tracks_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_SONG_LENGTH_MS_INFO:  // 歌曲总时长(ms)
        bt_avrcp_ct_set_song_length_ms(avrcp_subevent_now_playing_song_length_ms_info_get_song_length(packet));
        LOG_I("AVRCP now playing song_length=%u ms, cid=0x%04x, ctype=0x%02x",
              (unsigned int)bt_avrcp_ct_song_length_ms,
              avrcp_subevent_now_playing_song_length_ms_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_song_length_ms_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:  // 歌曲名称
        bt_avrcp_ct_log_text("title",
                             avrcp_subevent_now_playing_title_info_get_value(packet),
                             avrcp_subevent_now_playing_title_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:  // 歌手/艺术家
        bt_avrcp_ct_log_text("artist",
                             avrcp_subevent_now_playing_artist_info_get_value(packet),
                             avrcp_subevent_now_playing_artist_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:  // 专辑名称
        bt_avrcp_ct_log_text("album",
                             avrcp_subevent_now_playing_album_info_get_value(packet),
                             avrcp_subevent_now_playing_album_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:  // 曲风/流派
        bt_avrcp_ct_log_text("genre",
                             avrcp_subevent_now_playing_genre_info_get_value(packet),
                             avrcp_subevent_now_playing_genre_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_COVER_ART_INFO:  // 封面图信息
        bt_avrcp_ct_log_text("cover_art",
                             avrcp_subevent_now_playing_cover_art_info_get_value(packet),
                             avrcp_subevent_now_playing_cover_art_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_INFO_DONE:  // Now Playing 信息拉取结束
        LOG_I("AVRCP now playing info done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_now_playing_info_done_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_info_done_get_command_type(packet),
              avrcp_subevent_now_playing_info_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID:  // 对端支持的通知 event id
        LOG_I("AVRCP supported event id, cid=0x%04x, ctype=0x%02x, status=0x%02x, event_id=0x%02x",
              avrcp_subevent_get_capability_event_id_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_event_id_get_command_type(packet),
              avrcp_subevent_get_capability_event_id_get_status(packet),
              avrcp_subevent_get_capability_event_id_get_event_id(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE:  // 支持的 event id 列表结束
        LOG_I("AVRCP supported event ids done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_get_capability_event_id_done_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_event_id_done_get_command_type(packet),
              avrcp_subevent_get_capability_event_id_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_COMPANY_ID:  // 对端支持的 company id
        LOG_I("AVRCP supported company id, cid=0x%04x, ctype=0x%02x, status=0x%02x, company_id=0x%06x",
              avrcp_subevent_get_capability_company_id_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_company_id_get_command_type(packet),
              avrcp_subevent_get_capability_company_id_get_status(packet),
              (unsigned int)avrcp_subevent_get_capability_company_id_get_company_id(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_COMPANY_ID_DONE:  // company id 列表结束
        LOG_I("AVRCP supported company ids done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_get_capability_company_id_done_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_company_id_done_get_command_type(packet),
              avrcp_subevent_get_capability_company_id_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_CUSTOM_COMMAND_RESPONSE:  // 自定义命令应答
        LOG_I("AVRCP custom response, cid=0x%04x, ctype=0x%02x, pdu=0x%02x, params_len=%u",
              avrcp_subevent_custom_command_response_get_avrcp_cid(packet),
              avrcp_subevent_custom_command_response_get_command_type(packet),
              avrcp_subevent_custom_command_response_get_pdu_id(packet),
              avrcp_subevent_custom_command_response_get_params_len(packet));
        break;

    default:
        LOG_D("AVRCP meta event ignored, subevent=0x%02x", event);
        break;
    }
}

rt_err_t bt_avrcp_ct_service_init(void)
{
    bt_avrcp_ct_cid = 0u;
    bt_avrcp_ct_reset_session_state();

    //初始化AVRCP协议栈
    avrcp_init();
    avrcp_target_init();
    avrcp_controller_init();
    //绑定AVRCP事件回调函数
    avrcp_controller_register_packet_handler(btstack_event_avrcp_controller_handler);
    avrcp_target_register_packet_handler(btstack_event_avrcp_controller_handler);
    avrcp_register_packet_handler(btstack_event_avrcp_controller_handler);
    //注册SDP服务
    if (bt_avrcp_ct_sdp_register_service() != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t bt_avrcp_ct_play(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_PLAY);
}

rt_err_t bt_avrcp_ct_pause(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_PAUSE);
}

rt_err_t bt_avrcp_ct_next(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_NEXT);
}

rt_err_t bt_avrcp_ct_previous(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_PREVIOUS);
}

rt_bool_t bt_avrcp_ct_is_absolute_volume_active(void)
{
    rt_base_t level;
    rt_bool_t active;

    level = rt_hw_interrupt_disable();
    active = bt_avrcp_ct_absolute_volume_active;
    rt_hw_interrupt_enable(level);

    return active;
}

rt_err_t bt_avrcp_ct_volume_up(void)
{
    /* 绝对音量：调本地增益并通知手机；相对音量：Pass-Through volume_up。 */
    if (bt_avrcp_ct_is_absolute_volume_active())
    {
        LOG_D("AVRCP volume up via absolute local gain, step=%u", BT_APP_AVRCP_ABS_VOLUME_STEP);
        return bt_avrcp_ct_post_abs_volume_delta((int16_t)BT_APP_AVRCP_ABS_VOLUME_STEP);
    }

    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_VOLUME_UP);
}

rt_err_t bt_avrcp_ct_volume_down(void)
{
    if (bt_avrcp_ct_is_absolute_volume_active())
    {
        LOG_D("AVRCP volume down via absolute local gain, step=%u", BT_APP_AVRCP_ABS_VOLUME_STEP);
        return bt_avrcp_ct_post_abs_volume_delta(-(int16_t)BT_APP_AVRCP_ABS_VOLUME_STEP);
    }

    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_VOLUME_DOWN);
}
