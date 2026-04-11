/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_a2dp_sink_app.h"

#include "bt_a2dp_audio.h"
#include "bt_config.h"
#include "bt_i2s_player.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"
#include "hci_cmd.h"
#include "classic/a2dp_sink.h"
#include "classic/avdtp_util.h"
#include "classic/sdp_server.h"

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
// 这里先只放最常见的 44.1/48 kHz + Stereo/Joint Stereo，后面再按业务扩展。
static const uint8_t bt_app_a2dp_sbc_capabilities[BT_APP_A2DP_SINK_CODEC_INFORMATION_SIZE] = {
    (uint8_t) ((((uint8_t) AVDTP_SBC_44100 | (uint8_t) AVDTP_SBC_48000) << 4) |
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

static void bt_app_a2dp_sink_media_handler(uint8_t local_seid, uint8_t * packet, uint16_t size)
{
    // 协议层只负责把媒体包转交给音频层。
    // 音频层会继续完成：RTP/SBC 头解析 -> SBC 解码 -> PCM 回调输出。
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
        break;
    }

    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        bt_a2dp_audio_reset();
        if (bt_i2s_player_prepare(
                a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet), 2u) != RT_EOK)
        {
            LOG_W("bt_i2s_player_prepare failed");
        }
        LOG_I("A2DP SBC config: freq=%u, mode=%u, blocks=%u, subbands=%u, alloc=%u, bitpool=%u-%u",
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet),
              a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet));
        break;

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
        LOG_I("A2DP stream established, remote=%s, cid=0x%04x, local_seid=%u, remote_seid=%u",
              bd_addr_to_str(remote_addr),
              bt_app_a2dp_cid,
              bt_app_a2dp_local_seid,
              a2dp_subevent_stream_established_get_remote_seid(packet));
        break;
    }

    case A2DP_SUBEVENT_STREAM_STARTED:
        if (bt_i2s_player_start() != RT_EOK)
        {
            LOG_E("bt_i2s_player_start failed");
        }
        LOG_I("A2DP stream started, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_started_get_a2dp_cid(packet),
              a2dp_subevent_stream_started_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_SUSPENDED:
        (void) bt_i2s_player_stop();
        LOG_I("A2DP stream suspended, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_suspended_get_a2dp_cid(packet),
              a2dp_subevent_stream_suspended_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_STOPPED:
        (void) bt_i2s_player_stop();
        LOG_I("A2DP stream stopped, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_stopped_get_a2dp_cid(packet),
              a2dp_subevent_stream_stopped_get_local_seid(packet));
        break;

    case A2DP_SUBEVENT_STREAM_RELEASED:
        (void) bt_i2s_player_stop();
        bt_a2dp_audio_reset();
        LOG_I("A2DP stream released, cid=0x%04x, local_seid=%u",
              a2dp_subevent_stream_released_get_a2dp_cid(packet),
              a2dp_subevent_stream_released_get_local_seid(packet));
        bt_app_a2dp_local_seid = 0u;
        break;

    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        (void) bt_i2s_player_stop();
        bt_a2dp_audio_reset();
        LOG_I("A2DP signaling released, cid=0x%04x",
              a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet));
        bt_app_a2dp_cid = 0u;
        bt_app_a2dp_local_seid = 0u;
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

