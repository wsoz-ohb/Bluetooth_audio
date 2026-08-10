/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_a2dp_sink_app.h"

#include "bt_a2dp_audio.h"
#include "audio_mixer.h"
#include "bt_config.h"
#include "es8311_audio.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"
#include "hci_cmd.h"
#include "classic/a2dp_sink.h"
#include "classic/avdtp.h"
#include "classic/avdtp_sink.h"
#include "classic/avdtp_util.h"
#include "classic/sdp_server.h"
#include "bt_avrcp_ct_app.h"

#define DBG_TAG "bt_a2dp"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_APP_A2DP_SINK_SDP_RECORD_HANDLE        0x00010001u
#define BT_APP_A2DP_SINK_SDP_RECORD_SIZE          200u
#define BT_APP_A2DP_SINK_CODEC_INFORMATION_SIZE   4u
#define BT_APP_A2DP_SINK_MIN_BITPOOL              2u
#define BT_APP_A2DP_SINK_MAX_BITPOOL              53u
static btstack_packet_callback_registration_t bt_app_hci_event_callback_registration;
static avdtp_stream_endpoint_t * bt_app_a2dp_sink_sep = RT_NULL;
static uint8_t bt_app_a2dp_sink_sdp_record[BT_APP_A2DP_SINK_SDP_RECORD_SIZE];

// 这个 capability 描述本机 Sink 目前接受的 SBC 能力范围。
// Mixer、AI PCM 和采集链路统一使用 44.1 kHz，避免运行时重采样。
static const uint8_t bt_app_a2dp_sbc_capabilities[BT_APP_A2DP_SINK_CODEC_INFORMATION_SIZE] = {
    (uint8_t) (((uint8_t) AVDTP_SBC_44100 << 4) |
               ((uint8_t) AVDTP_SBC_STEREO | (uint8_t) AVDTP_SBC_JOINT_STEREO)),
    (uint8_t) ((((uint8_t) AVDTP_SBC_BLOCK_LENGTH_4 | (uint8_t) AVDTP_SBC_BLOCK_LENGTH_8 |
                 (uint8_t) AVDTP_SBC_BLOCK_LENGTH_12 | (uint8_t) AVDTP_SBC_BLOCK_LENGTH_16) << 4) |
               (((uint8_t) AVDTP_SBC_SUBBANDS_4 | (uint8_t) AVDTP_SBC_SUBBANDS_8) << 2) |
               ((uint8_t) AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS | (uint8_t) AVDTP_SBC_ALLOCATION_METHOD_SNR)),
    BT_APP_A2DP_SINK_MIN_BITPOOL,
    BT_APP_A2DP_SINK_MAX_BITPOOL,
};

// 这个 configuration 是本机默认偏好的 SBC 配置。
// 真正协商结果以后以 A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION 事件为准。
static uint8_t bt_app_a2dp_sbc_configuration[BT_APP_A2DP_SINK_CODEC_INFORMATION_SIZE];
static const avdtp_configuration_sbc_t bt_app_a2dp_sbc_preferred_configuration = {
    .sampling_frequency = 44100,
    .channel_mode = AVDTP_CHANNEL_MODE_JOINT_STEREO,
    .block_length = AVDTP_SBC_BLOCK_LENGTH_16,
    .subbands = AVDTP_SBC_SUBBANDS_8,
    .allocation_method = AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    .min_bitpool_value = BT_APP_A2DP_SINK_MIN_BITPOOL,
    .max_bitpool_value = BT_APP_A2DP_SINK_MAX_BITPOOL,
};

static uint16_t bt_app_a2dp_cid = 0u;
static uint8_t bt_app_a2dp_local_seid = 0u;
static rt_uint32_t bt_app_a2dp_sample_rate = ES8311_AUDIO_DEFAULT_SAMPLE_RATE;
static rt_bool_t bt_app_a2dp_stream_active = RT_FALSE;
static rt_bool_t bt_app_a2dp_local_media_enabled = RT_TRUE;
static rt_bool_t bt_app_a2dp_media_allowed = RT_FALSE;
static rt_bool_t bt_app_a2dp_media_drop_logged = RT_FALSE;
static rt_bool_t bt_app_a2dp_playback_error_logged = RT_FALSE;
static rt_bool_t bt_app_a2dp_suspend_in_progress = RT_FALSE;

static rt_bool_t bt_app_a2dp_ensure_playback_started(const char * reason)
{
    if (audio_mixer_source_start(AUDIO_MIXER_SOURCE_BACKGROUND,
                                 bt_app_a2dp_sample_rate,
                                 2u) != RT_EOK)
    {
        if (!bt_app_a2dp_playback_error_logged)
        {
            bt_app_a2dp_playback_error_logged = RT_TRUE;
            LOG_E("start A2DP mixer source failed, reason=%s, sample_rate=%u",
                  reason,
                  bt_app_a2dp_sample_rate);
        }
        return RT_FALSE;
    }

    bt_app_a2dp_playback_error_logged = RT_FALSE;
    LOG_I("A2DP playback armed, reason=%s, sample_rate=%u", reason, bt_app_a2dp_sample_rate);
    return RT_TRUE;
}

static void bt_app_a2dp_stop_playback(void)
{
    bt_app_a2dp_playback_error_logged = RT_FALSE;
    audio_mixer_source_stop(AUDIO_MIXER_SOURCE_BACKGROUND);
}

static void bt_app_a2dp_sink_media_handler(uint8_t local_seid, uint8_t * packet, uint16_t size)
{
    // 协议层只负责把媒体包转交给音频层。
    if ((bt_app_a2dp_local_seid != 0u) && (local_seid != bt_app_a2dp_local_seid))
    {
        if (!bt_app_a2dp_media_drop_logged)
        {
            bt_app_a2dp_media_drop_logged = RT_TRUE;
            LOG_W("A2DP media packet ignored, local_seid mismatch: got=%u, expected=%u",
                  local_seid,
                  bt_app_a2dp_local_seid);
        }
        return;
    }

    if (!bt_app_a2dp_local_media_enabled)
    {
        if (!bt_app_a2dp_media_drop_logged)
        {
            bt_app_a2dp_media_drop_logged = RT_TRUE;
            LOG_W("A2DP media packet ignored, local playback gate is closed");
        }
        return;
    }

    if (!bt_app_a2dp_media_allowed)
    {
        if (!bt_app_a2dp_media_drop_logged)
        {
            bt_app_a2dp_media_drop_logged = RT_TRUE;
            LOG_W("A2DP media packet ignored, stream is not active");
        }
        return;
    }

    bt_a2dp_audio_process_media_packet(local_seid, packet, size);
}

static void bt_app_handle_a2dp_meta_event(uint8_t * packet)
{
    uint8_t subevent;

    subevent = hci_event_a2dp_meta_get_subevent_code(packet);
    switch (subevent)
    {
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
    {
        bd_addr_t remote_addr;
        uint8_t status;

        status = a2dp_subevent_signaling_connection_established_get_status(packet);
        a2dp_subevent_signaling_connection_established_get_bd_addr(packet, remote_addr);
        if (status != ERROR_CODE_SUCCESS)
        {
            LOG_E("A2DP signaling connect failed, status=0x%02x, remote=%s",
                  status,
                  bd_addr_to_str(remote_addr));
            break;
        }
        
        bt_app_a2dp_cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
        LOG_I("A2DP signaling connected, remote=%s, cid=0x%04x",
              bd_addr_to_str(remote_addr),
              bt_app_a2dp_cid);
        if (bt_avrcp_ct_connect(remote_addr) != RT_EOK)
        {
            LOG_W("AVRCP CT connect request was not accepted, remote=%s",
                  bd_addr_to_str(remote_addr));
        }
        break;
    }

    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
    {
        rt_uint32_t sample_rate;

        sample_rate = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);

        bt_a2dp_audio_reset();
        bt_app_a2dp_sample_rate = sample_rate;
        if (sample_rate != AUDIO_MIXER_SAMPLE_RATE)
        {
            LOG_E("unsupported A2DP sample rate for mixer: %u", sample_rate);
        }
        LOG_I("A2DP SBC config: freq=%u, mode=%u, blocks=%u, subbands=%u, alloc=%u, bitpool=%u-%u",
              sample_rate,
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet));
        break;
    }

    case A2DP_SUBEVENT_STREAM_ESTABLISHED:
    {
        bd_addr_t remote_addr;
        uint8_t status;

        status = a2dp_subevent_stream_established_get_status(packet);
        a2dp_subevent_stream_established_get_bd_addr(packet, remote_addr);
        if (status != ERROR_CODE_SUCCESS)
        {
            LOG_E("A2DP stream establish failed, status=0x%02x, remote=%s",
                  status,
                  bd_addr_to_str(remote_addr));
            break;
        }

        bt_app_a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        bt_app_a2dp_local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
        bt_app_a2dp_stream_active = RT_FALSE;
        bt_app_a2dp_media_allowed = RT_FALSE;
        bt_app_a2dp_media_drop_logged = RT_FALSE;
        bt_app_a2dp_playback_error_logged = RT_FALSE;
        LOG_I("A2DP stream established, remote=%s, cid=0x%04x, local_seid=%u, remote_seid=%u",
              bd_addr_to_str(remote_addr),
              bt_app_a2dp_cid,
              bt_app_a2dp_local_seid,
              a2dp_subevent_stream_established_get_remote_seid(packet));
        break;
    }

    case A2DP_SUBEVENT_STREAM_STARTED:
        bt_app_a2dp_suspend_in_progress = RT_FALSE;
        bt_app_a2dp_stream_active = RT_TRUE;
        if (!bt_app_a2dp_local_media_enabled)
        {
            bt_app_a2dp_media_allowed = RT_FALSE;
            bt_app_a2dp_media_drop_logged = RT_FALSE;
            LOG_I("A2DP stream started while local playback gate is closed, cid=0x%04x, local_seid=%u",
                  a2dp_subevent_stream_started_get_a2dp_cid(packet),
                  a2dp_subevent_stream_started_get_local_seid(packet));
        }
        else if (bt_app_a2dp_ensure_playback_started("stream_started"))
        {
            bt_app_a2dp_media_allowed = RT_TRUE;
            bt_app_a2dp_media_drop_logged = RT_FALSE;
            LOG_I("A2DP stream started, cid=0x%04x, local_seid=%u",
                  a2dp_subevent_stream_started_get_a2dp_cid(packet),
                  a2dp_subevent_stream_started_get_local_seid(packet));
        }
        else
        {
            bt_app_a2dp_media_allowed = RT_FALSE;
            LOG_E("A2DP stream started but local playback is not armed, cid=0x%04x, local_seid=%u",
                  a2dp_subevent_stream_started_get_a2dp_cid(packet),
                  a2dp_subevent_stream_started_get_local_seid(packet));
        }
        break;

    case A2DP_SUBEVENT_STREAM_SUSPENDED:
        bt_app_a2dp_suspend_in_progress = RT_FALSE;
        bt_app_a2dp_stream_active = RT_FALSE;
        bt_app_a2dp_media_allowed = RT_FALSE;
        bt_app_a2dp_media_drop_logged = RT_FALSE;
        bt_app_a2dp_stop_playback();
        LOG_I("A2DP stream suspended, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_suspended_get_a2dp_cid(packet),
              a2dp_subevent_stream_suspended_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_STOPPED:
        bt_app_a2dp_suspend_in_progress = RT_FALSE;
        bt_app_a2dp_stream_active = RT_FALSE;
        bt_app_a2dp_media_allowed = RT_FALSE;
        bt_app_a2dp_media_drop_logged = RT_FALSE;
        bt_app_a2dp_stop_playback();
        LOG_I("A2DP stream stopped, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_stopped_get_a2dp_cid(packet),
              a2dp_subevent_stream_stopped_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_RELEASED:
        bt_app_a2dp_suspend_in_progress = RT_FALSE;
        bt_app_a2dp_stream_active = RT_FALSE;
        bt_app_a2dp_media_allowed = RT_FALSE;
        bt_app_a2dp_media_drop_logged = RT_FALSE;
        bt_app_a2dp_stop_playback();
        bt_a2dp_audio_reset();
        bt_app_a2dp_sample_rate = ES8311_AUDIO_DEFAULT_SAMPLE_RATE;
        LOG_I("A2DP stream released, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_released_get_a2dp_cid(packet),
              a2dp_subevent_stream_released_get_local_seid(packet));
        bt_app_a2dp_local_seid = 0u;
        break;

    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        bt_app_a2dp_suspend_in_progress = RT_FALSE;
        bt_app_a2dp_stream_active = RT_FALSE;
        bt_app_a2dp_media_allowed = RT_FALSE;
        bt_app_a2dp_media_drop_logged = RT_FALSE;
        bt_app_a2dp_stop_playback();
        bt_a2dp_audio_reset();
        bt_app_a2dp_sample_rate = ES8311_AUDIO_DEFAULT_SAMPLE_RATE;
        LOG_I("A2DP signaling released, cid=0x%04x",
              a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet));
        bt_app_a2dp_cid = 0u;
        bt_app_a2dp_local_seid = 0u;
        break;

    case A2DP_SUBEVENT_COMMAND_ACCEPTED:
        if (a2dp_subevent_command_accepted_get_signal_identifier(packet) == AVDTP_SI_SUSPEND)
        {
            LOG_I("A2DP suspend command accepted, cid=0x%04x, local_seid=%u",
                  a2dp_subevent_command_accepted_get_a2dp_cid(packet),
                  a2dp_subevent_command_accepted_get_local_seid(packet));
        }
        else if (a2dp_subevent_command_accepted_get_signal_identifier(packet) == AVDTP_SI_START)
        {
            LOG_I("A2DP start command accepted, cid=0x%04x, local_seid=%u",
                  a2dp_subevent_command_accepted_get_a2dp_cid(packet),
                  a2dp_subevent_command_accepted_get_local_seid(packet));
        }
        break;

    case A2DP_SUBEVENT_COMMAND_REJECTED:
        if (a2dp_subevent_command_rejected_get_signal_identifier(packet) == AVDTP_SI_SUSPEND)
        {
            bt_app_a2dp_suspend_in_progress = RT_FALSE;
            LOG_E("A2DP suspend command rejected, cid=0x%04x, local_seid=%u, is_initiator=%u",
                  a2dp_subevent_command_rejected_get_a2dp_cid(packet),
                  a2dp_subevent_command_rejected_get_local_seid(packet),
                  a2dp_subevent_command_rejected_get_is_initiator(packet));
        }
        else if (a2dp_subevent_command_rejected_get_signal_identifier(packet) == AVDTP_SI_START)
        {
            LOG_E("A2DP start command rejected, cid=0x%04x, local_seid=%u, is_initiator=%u",
                  a2dp_subevent_command_rejected_get_a2dp_cid(packet),
                  a2dp_subevent_command_rejected_get_local_seid(packet),
                  a2dp_subevent_command_rejected_get_is_initiator(packet));
        }
        break;

    default:
        break;
    }
}

static void bt_app_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
    {
        return;
    }

    switch (hci_event_packet_get_type(packet))
    {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
        {
            LOG_I("BTstack is working, A2DP Sink service is discoverable now");
        }
        break;

    case HCI_EVENT_A2DP_META:
        bt_app_handle_a2dp_meta_event(packet);
        break;

    default:
        break;
    }
}

rt_err_t bt_a2dp_sink_service_init(void)
{
    uint8_t status;

#if !BT_CFG_ENABLE_CLASSIC
    LOG_E("A2DP Sink requires Classic support");
    return -RT_ERROR;
#elif !BT_CFG_CLASSIC_ENABLE_SDP
    LOG_E("A2DP Sink requires SDP support");
    return -RT_ERROR;
#else
    if (bt_a2dp_audio_init() != RT_EOK)
    {
        LOG_E("bt_a2dp_audio_init failed");
        return -RT_ERROR;
    }

    if (!es8311_audio_is_inited())
    {
        LOG_E("es8311 audio is not initialized");
        return -RT_ERROR;
    }

    // 这里先把 HCI 通用事件挂上，后面既能看上电状态，也能统一接 A2DP META 事件。
    bt_app_hci_event_callback_registration.callback = &bt_app_packet_handler;
    hci_add_event_handler(&bt_app_hci_event_callback_registration);

    // 先准备默认 SBC 配置，再创建本地 Sink 端点。
    status = avdtp_config_sbc_store(bt_app_a2dp_sbc_configuration, &bt_app_a2dp_sbc_preferred_configuration);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("avdtp_config_sbc_store failed: 0x%02x", status);
        return -RT_ERROR;
    }

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(bt_app_packet_handler);
    a2dp_sink_register_media_handler(bt_app_a2dp_sink_media_handler);

    // 创建本地 Sink SEP。
    // 后面远端查询 capability 时，看到的就是这里注册的 codec 能力。
    bt_app_a2dp_sink_sep = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO,
                                                            AVDTP_CODEC_SBC,
                                                            bt_app_a2dp_sbc_capabilities,
                                                            sizeof(bt_app_a2dp_sbc_capabilities),
                                                            bt_app_a2dp_sbc_configuration,
                                                            sizeof(bt_app_a2dp_sbc_configuration));
    if (bt_app_a2dp_sink_sep == RT_NULL)
    {
        LOG_E("a2dp_sink_create_stream_endpoint failed, check AVDTP resource config");
        return -RT_ERROR;
    }

    // 注册 SDP 服务记录。
    // 这样手机或电脑在发现本机后，才能从 SDP 里识别出这是一个 A2DP Sink。
    a2dp_sink_create_sdp_record(bt_app_a2dp_sink_sdp_record,
                                BT_APP_A2DP_SINK_SDP_RECORD_HANDLE,
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE,
                                "WSOZ A2DP Sink",
                                BT_CFG_LOCAL_NAME);
    status = sdp_register_service(bt_app_a2dp_sink_sdp_record);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("sdp_register_service failed: 0x%02x", status);
        return -RT_ERROR;
    }

    LOG_I("A2DP Sink service registered, local_seid=%u, wait remote source to discover and connect",
          avdtp_stream_endpoint_seid(bt_app_a2dp_sink_sep));
    return RT_EOK;
#endif
}

bt_a2dp_sink_suspend_result_t bt_a2dp_sink_request_media_suspend(void)
{
    uint8_t status;

    if ((bt_app_a2dp_cid == 0u) || (bt_app_a2dp_local_seid == 0u) || !bt_app_a2dp_stream_active)
    {
        return BT_A2DP_SINK_SUSPEND_NOT_NEEDED;
    }

    if (bt_app_a2dp_suspend_in_progress)
    {
        return BT_A2DP_SINK_SUSPEND_PENDING;
    }

    status = avdtp_sink_suspend(bt_app_a2dp_cid, bt_app_a2dp_local_seid);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("avdtp_sink_suspend failed, status=0x%02x, cid=0x%04x, local_seid=%u",
              status,
              bt_app_a2dp_cid,
              bt_app_a2dp_local_seid);
        return BT_A2DP_SINK_SUSPEND_FAILED;
    }

    bt_app_a2dp_suspend_in_progress = RT_TRUE;
    LOG_I("A2DP suspend requested, cid=0x%04x, local_seid=%u",
          bt_app_a2dp_cid,
          bt_app_a2dp_local_seid);
    return BT_A2DP_SINK_SUSPEND_PENDING;
}

rt_err_t bt_a2dp_sink_set_local_media_enabled(rt_bool_t enabled)
{
    bt_app_a2dp_local_media_enabled = enabled ? RT_TRUE : RT_FALSE;
    bt_app_a2dp_media_drop_logged = RT_FALSE;

    if (!bt_app_a2dp_local_media_enabled)
    {
        bt_app_a2dp_media_allowed = RT_FALSE;
        audio_mixer_source_stop(AUDIO_MIXER_SOURCE_BACKGROUND);
        return RT_EOK;
    }

    if (!bt_app_a2dp_stream_active)
    {
        bt_app_a2dp_media_allowed = RT_FALSE;
        return RT_EOK;
    }

    if (!bt_app_a2dp_ensure_playback_started("local_media_enabled"))
    {
        bt_app_a2dp_media_allowed = RT_FALSE;
        return -RT_ERROR;
    }

    bt_app_a2dp_media_allowed = RT_TRUE;
    return RT_EOK;
}

rt_err_t bt_a2dp_sink_restore_local_playback(void)
{
    if (es8311_audio_get_run_mode() == ES8311_AUDIO_RUN_MODE_CAPTURE)
    {
        es8311_audio_stop_capture();
        es8311_audio_flush_capture();
    }

    if (audio_mixer_source_start(AUDIO_MIXER_SOURCE_BACKGROUND,
                                 bt_app_a2dp_sample_rate,
                                 2u) != RT_EOK)
    {
        LOG_E("restore A2DP mixer source failed, sample_rate=%u", bt_app_a2dp_sample_rate);
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t bt_a2dp_sink_resume_media_stream(void)
{
    uint8_t status;

    // 退出 capture 后，先按 A2DP 当前协商参数把本地播放链路恢复好，
    // 再主动发 START，让远端 source 立即恢复媒体流。
    if (bt_a2dp_sink_restore_local_playback() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if ((bt_app_a2dp_cid == 0u) || (bt_app_a2dp_local_seid == 0u))
    {
        LOG_I("local playback restored, no suspended A2DP stream to start");
        return RT_EOK;
    }

    if (bt_app_a2dp_stream_active)
    {
        LOG_I("local playback restored, A2DP stream is already active");
        return RT_EOK;
    }

    status = avdtp_sink_start_stream(bt_app_a2dp_cid, bt_app_a2dp_local_seid);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("avdtp_sink_start_stream failed, status=0x%02x, cid=0x%04x, local_seid=%u",
              status,
              bt_app_a2dp_cid,
              bt_app_a2dp_local_seid);
        return -RT_ERROR;
    }

    LOG_I("A2DP start requested, cid=0x%04x, local_seid=%u",
          bt_app_a2dp_cid,
          bt_app_a2dp_local_seid);
    return RT_EOK;
}

rt_bool_t bt_a2dp_sink_is_stream_active(void)
{
    return bt_app_a2dp_stream_active;
}

rt_bool_t bt_a2dp_sink_is_suspend_in_progress(void)
{
    return bt_app_a2dp_suspend_in_progress;
}

