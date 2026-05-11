/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_avrcp_ct_app.h"
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
static uint16_t bt_avrcp_ct_cid = 0u;
static rt_bool_t bt_avrcp_ct_connected = RT_FALSE;
static btstack_context_callback_registration_t bt_avrcp_ct_command_registration;
static bt_avrcp_ct_command_t bt_avrcp_ct_command_queue[BT_APP_AVRCP_CT_COMMAND_QUEUE_SIZE];
static uint8_t bt_avrcp_ct_command_read_index = 0u;
static uint8_t bt_avrcp_ct_command_write_index = 0u;
static uint8_t bt_avrcp_ct_command_count = 0u;
static rt_bool_t bt_avrcp_ct_command_callback_pending = RT_FALSE;

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

    if (!bt_avrcp_ct_connected || (bt_avrcp_ct_cid == 0u))
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
    if (!bt_avrcp_ct_connected || (bt_avrcp_ct_cid == 0u))
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

    if (bt_avrcp_ct_connected)
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
    case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:
    {
        bd_addr_t remote_addr;
        uint8_t status;

        status = avrcp_subevent_connection_established_get_status(packet);
        avrcp_subevent_connection_established_get_bd_addr(packet, remote_addr);
        if (status != ERROR_CODE_SUCCESS)
        {
            LOG_E("AVRCP CT connect failed, status=0x%02x, remote=%s",
                  status,
                  bd_addr_to_str(remote_addr));
            break;
        }

        bt_avrcp_ct_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        bt_avrcp_ct_connected = RT_TRUE;
        LOG_I("AVRCP CT connected, remote=%s, cid=0x%04x, handle=0x%04x",
              bd_addr_to_str(remote_addr),
              bt_avrcp_ct_cid,
              avrcp_subevent_connection_established_get_con_handle(packet));
        break;
    }

    case AVRCP_SUBEVENT_CONNECTION_RELEASED:
    {
        uint16_t cid;

        cid = avrcp_subevent_connection_released_get_avrcp_cid(packet);
        LOG_I("AVRCP CT released, cid=0x%04x", cid);
        if (bt_avrcp_ct_cid == cid)
        {
            bt_avrcp_ct_cid = 0u;
            bt_avrcp_ct_connected = RT_FALSE;
        }
        break;
    }

    case AVRCP_SUBEVENT_OPERATION_START:
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

    case AVRCP_SUBEVENT_OPERATION_COMPLETE:
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

    case AVRCP_SUBEVENT_OPERATION:
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

    case AVRCP_SUBEVENT_PLAY_STATUS:
    {
        uint8_t play_status;

        play_status = avrcp_subevent_play_status_get_play_status(packet);
        LOG_I("AVRCP play status, cid=0x%04x, ctype=0x%02x, length=%u ms, position=%u ms, status=%s(0x%02x)",
              avrcp_subevent_play_status_get_avrcp_cid(packet),
              avrcp_subevent_play_status_get_command_type(packet),
              (unsigned int)avrcp_subevent_play_status_get_song_length(packet),
              (unsigned int)avrcp_subevent_play_status_get_song_position(packet),
              bt_avrcp_ct_play_status_name(play_status),
              play_status);
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_STATE:
        LOG_I("AVRCP notification state, cid=0x%04x, event_id=0x%02x, enabled=%u, status=0x%02x",
              avrcp_subevent_notification_state_get_avrcp_cid(packet),
              avrcp_subevent_notification_state_get_event_id(packet),
              avrcp_subevent_notification_state_get_enabled(packet),
              avrcp_subevent_notification_state_get_status(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
    {
        uint8_t play_status;

        play_status = avrcp_subevent_notification_playback_status_changed_get_play_status(packet);
        LOG_I("AVRCP playback status changed, cid=0x%04x, ctype=0x%02x, status=%s(0x%02x)",
              avrcp_subevent_notification_playback_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_playback_status_changed_get_command_type(packet),
              bt_avrcp_ct_play_status_name(play_status),
              play_status);
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
    {
        const uint8_t * identifier;

        identifier = avrcp_subevent_notification_track_changed_get_identifier(packet);
        LOG_I("AVRCP track changed, cid=0x%04x, ctype=0x%02x, track_id=%02x%02x%02x%02x%02x%02x%02x%02x",
              avrcp_subevent_notification_track_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_track_changed_get_command_type(packet),
              identifier[0], identifier[1], identifier[2], identifier[3],
              identifier[4], identifier[5], identifier[6], identifier[7]);
        break;
    }

    case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:
        LOG_I("AVRCP playback position changed, cid=0x%04x, ctype=0x%02x, position=%u ms",
              avrcp_subevent_notification_playback_pos_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_playback_pos_changed_get_command_type(packet),
              (unsigned int)avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_END:
        LOG_I("AVRCP track reached end, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_event_track_reached_end_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_track_reached_end_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_START:
        LOG_I("AVRCP track reached start, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_event_track_reached_start_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_track_reached_start_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_BATT_STATUS_CHANGED:
        LOG_I("AVRCP battery status changed, cid=0x%04x, ctype=0x%02x, battery=0x%02x",
              avrcp_subevent_notification_event_batt_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_batt_status_changed_get_command_type(packet),
              avrcp_subevent_notification_event_batt_status_changed_get_battery_status(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_SYSTEM_STATUS_CHANGED:
        LOG_I("AVRCP system status changed, cid=0x%04x, ctype=0x%02x, system=0x%02x",
              avrcp_subevent_notification_event_system_status_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_system_status_changed_get_command_type(packet),
              avrcp_subevent_notification_event_system_status_changed_get_system_status(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_PLAYER_APPLICATION_SETTING_CHANGED:
        LOG_I("AVRCP player setting changed, cid=0x%04x, ctype=0x%02x, attr=0x%02x, value=0x%02x",
              avrcp_subevent_notification_event_player_application_setting_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_command_type(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_attribute_id(packet),
              avrcp_subevent_notification_event_player_application_setting_changed_get_value_id(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
        LOG_I("AVRCP now playing content changed, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_now_playing_content_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_now_playing_content_changed_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:
        LOG_I("AVRCP available players changed, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_notification_available_players_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_available_players_changed_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_ADDRESSED_PLAYER_CHANGED:
        LOG_I("AVRCP addressed player changed, cid=0x%04x, ctype=0x%02x, player_id=%u, uid_counter=%u",
              avrcp_subevent_notification_addressed_player_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_addressed_player_changed_get_command_type(packet),
              avrcp_subevent_notification_addressed_player_changed_get_player_id(packet),
              avrcp_subevent_notification_addressed_player_changed_get_uid_counter(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_EVENT_UIDS_CHANGED:
        LOG_I("AVRCP uids changed, cid=0x%04x, ctype=0x%02x, uid_counter=%u",
              avrcp_subevent_notification_event_uids_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_event_uids_changed_get_command_type(packet),
              avrcp_subevent_notification_event_uids_changed_get_uid_counter(packet));
        break;

    case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
    {
        uint8_t volume;

        volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
        LOG_I("AVRCP volume changed, cid=0x%04x, ctype=0x%02x, volume=%u/127 (%u%%)",
              avrcp_subevent_notification_volume_changed_get_avrcp_cid(packet),
              avrcp_subevent_notification_volume_changed_get_command_type(packet),
              volume,
              (unsigned int)((volume * 100u) / 127u));
        break;
    }

    case AVRCP_SUBEVENT_SET_ABSOLUTE_VOLUME_RESPONSE:
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

    case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE:
        LOG_I("AVRCP shuffle/repeat mode, cid=0x%04x, ctype=0x%02x, repeat=%u, shuffle=%u",
              avrcp_subevent_shuffle_and_repeat_mode_get_avrcp_cid(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_command_type(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet),
              avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
        LOG_I("AVRCP now playing track=%u, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_now_playing_track_info_get_track(packet),
              avrcp_subevent_now_playing_track_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_track_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:
        LOG_I("AVRCP now playing total_tracks=%u, cid=0x%04x, ctype=0x%02x",
              avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet),
              avrcp_subevent_now_playing_total_tracks_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_total_tracks_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_SONG_LENGTH_MS_INFO:
        LOG_I("AVRCP now playing song_length=%u ms, cid=0x%04x, ctype=0x%02x",
              (unsigned int)avrcp_subevent_now_playing_song_length_ms_info_get_song_length(packet),
              avrcp_subevent_now_playing_song_length_ms_info_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_song_length_ms_info_get_command_type(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
        bt_avrcp_ct_log_text("title",
                             avrcp_subevent_now_playing_title_info_get_value(packet),
                             avrcp_subevent_now_playing_title_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
        bt_avrcp_ct_log_text("artist",
                             avrcp_subevent_now_playing_artist_info_get_value(packet),
                             avrcp_subevent_now_playing_artist_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
        bt_avrcp_ct_log_text("album",
                             avrcp_subevent_now_playing_album_info_get_value(packet),
                             avrcp_subevent_now_playing_album_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
        bt_avrcp_ct_log_text("genre",
                             avrcp_subevent_now_playing_genre_info_get_value(packet),
                             avrcp_subevent_now_playing_genre_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_COVER_ART_INFO:
        bt_avrcp_ct_log_text("cover_art",
                             avrcp_subevent_now_playing_cover_art_info_get_value(packet),
                             avrcp_subevent_now_playing_cover_art_info_get_value_len(packet));
        break;

    case AVRCP_SUBEVENT_NOW_PLAYING_INFO_DONE:
        LOG_I("AVRCP now playing info done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_now_playing_info_done_get_avrcp_cid(packet),
              avrcp_subevent_now_playing_info_done_get_command_type(packet),
              avrcp_subevent_now_playing_info_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID:
        LOG_I("AVRCP supported event id, cid=0x%04x, ctype=0x%02x, status=0x%02x, event_id=0x%02x",
              avrcp_subevent_get_capability_event_id_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_event_id_get_command_type(packet),
              avrcp_subevent_get_capability_event_id_get_status(packet),
              avrcp_subevent_get_capability_event_id_get_event_id(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE:
        LOG_I("AVRCP supported event ids done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_get_capability_event_id_done_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_event_id_done_get_command_type(packet),
              avrcp_subevent_get_capability_event_id_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_COMPANY_ID:
        LOG_I("AVRCP supported company id, cid=0x%04x, ctype=0x%02x, status=0x%02x, company_id=0x%06x",
              avrcp_subevent_get_capability_company_id_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_company_id_get_command_type(packet),
              avrcp_subevent_get_capability_company_id_get_status(packet),
              (unsigned int)avrcp_subevent_get_capability_company_id_get_company_id(packet));
        break;

    case AVRCP_SUBEVENT_GET_CAPABILITY_COMPANY_ID_DONE:
        LOG_I("AVRCP supported company ids done, cid=0x%04x, ctype=0x%02x, status=0x%02x",
              avrcp_subevent_get_capability_company_id_done_get_avrcp_cid(packet),
              avrcp_subevent_get_capability_company_id_done_get_command_type(packet),
              avrcp_subevent_get_capability_company_id_done_get_status(packet));
        break;

    case AVRCP_SUBEVENT_CUSTOM_COMMAND_RESPONSE:
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

rt_err_t bt_avrcp_ct_volume_up(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_VOLUME_UP);
}

rt_err_t bt_avrcp_ct_volume_down(void)
{
    return bt_avrcp_ct_post_command(BT_AVRCP_CT_COMMAND_VOLUME_DOWN);
}
