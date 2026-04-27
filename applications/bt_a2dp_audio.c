/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_a2dp_audio.h"

#include <string.h>

#include "es8311_audio.h"
#include "btstack_util.h"
#include "classic/avdtp.h"
#include "classic/btstack_sbc.h"

#define DBG_TAG "bt_a2dp_audio"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_A2DP_AUDIO_RTP_HEADER_MIN_SIZE     12u
#define BT_A2DP_AUDIO_SBC_HEADER_SIZE         1u
#define BT_A2DP_AUDIO_BACKPRESSURE_LEVEL      8192u

static btstack_sbc_decoder_state_t bt_a2dp_audio_sbc_decoder_state;
static rt_bool_t bt_a2dp_audio_inited = RT_FALSE;
static rt_bool_t bt_a2dp_audio_first_pcm_logged = RT_FALSE;
static rt_bool_t bt_a2dp_audio_parse_error_logged = RT_FALSE;
static rt_bool_t bt_a2dp_audio_fragmented_logged = RT_FALSE;
static rt_bool_t bt_a2dp_audio_backpressure_logged = RT_FALSE;
static es8311_audio_playback_write_status_t bt_a2dp_audio_last_write_status = ES8311_AUDIO_PLAYBACK_WRITE_OK;

static const char * bt_a2dp_audio_write_status_name(es8311_audio_playback_write_status_t status)
{
    switch (status)
    {
    case ES8311_AUDIO_PLAYBACK_WRITE_OK:
        return "ok";
    case ES8311_AUDIO_PLAYBACK_WRITE_INVALID_ARGUMENT:
        return "invalid_argument";
    case ES8311_AUDIO_PLAYBACK_WRITE_NOT_INITED:
        return "not_inited";
    case ES8311_AUDIO_PLAYBACK_WRITE_NOT_RUNNING:
        return "not_running";
    case ES8311_AUDIO_PLAYBACK_WRITE_INVALID_FORMAT:
        return "invalid_format";
    case ES8311_AUDIO_PLAYBACK_WRITE_SAMPLE_RATE_MISMATCH:
        return "sample_rate_mismatch";
    case ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL:
        return "buffer_full";
    default:
        return "unknown";
    }
}

static void bt_a2dp_audio_handle_pcm(int16_t * data,
                                     int num_samples,
                                     int num_channels,
                                     int sample_rate,
                                     void * context)
{
    rt_uint32_t written_frames;
    es8311_audio_playback_write_status_t write_status;

    UNUSED(context);

    if (!bt_a2dp_audio_first_pcm_logged)
    {
        bt_a2dp_audio_first_pcm_logged = RT_TRUE;
        LOG_I("first PCM ready: samples=%d, channels=%d, sample_rate=%d",
              num_samples,
              num_channels,
              sample_rate);
    }

    // 解码层只负责把 PCM 写入 ES8311 统一音频会话层。
    written_frames = es8311_audio_write_playback_checked(data,
                                                         (rt_uint32_t) num_samples,
                                                         (rt_uint8_t) num_channels,
                                                         (rt_uint32_t) sample_rate,
                                                         &write_status);
    if ((written_frames == 0u) && (num_samples > 0))
    {
        if (write_status != bt_a2dp_audio_last_write_status)
        {
            LOG_W("ES8311 playback rejected PCM: reason=%s, samples=%d, channels=%d, sample_rate=%d, running=%d, es8311_rate=%u, level=%u",
                  bt_a2dp_audio_write_status_name(write_status),
                  num_samples,
                  num_channels,
                  sample_rate,
                  es8311_audio_is_playback_running(),
                  es8311_audio_get_sample_rate(),
                  es8311_audio_get_playback_level_frames());
            bt_a2dp_audio_last_write_status = write_status;
        }
        return;
    }

    if (write_status == ES8311_AUDIO_PLAYBACK_WRITE_OK)
    {
        bt_a2dp_audio_last_write_status = ES8311_AUDIO_PLAYBACK_WRITE_OK;
    }
}

static rt_err_t bt_a2dp_audio_parse_rtp_payload_offset(const uint8_t * packet, uint16_t size, uint16_t * payload_offset)
{
    uint8_t csrc_count;
    uint8_t extension;
    uint16_t offset;

    if ((packet == RT_NULL) || (payload_offset == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if (size < BT_A2DP_AUDIO_RTP_HEADER_MIN_SIZE)
    {
        return -RT_ERROR;
    }

    csrc_count = packet[0] & 0x0Fu;
    extension = (packet[0] >> 4) & 0x01u;
    offset = (uint16_t) (BT_A2DP_AUDIO_RTP_HEADER_MIN_SIZE + (uint16_t) csrc_count * 4u);
    if (size < offset)
    {
        return -RT_ERROR;
    }

    if (extension != 0u)
    {
        uint16_t extension_words;

        if (size < (uint16_t) (offset + 4u))
        {
            return -RT_ERROR;
        }

        extension_words = (uint16_t) big_endian_read_16(packet, offset + 2);
        offset = (uint16_t) (offset + 4u + extension_words * 4u);
        if (size < offset)
        {
            return -RT_ERROR;
        }
    }

    *payload_offset = offset;
    return RT_EOK;
}

static rt_err_t bt_a2dp_audio_parse_sbc_payload(const uint8_t * packet,
                                                uint16_t size,
                                                const uint8_t ** sbc_payload,
                                                uint16_t * sbc_payload_size,
                                                avdtp_sbc_codec_header_t * sbc_header)
{
    uint16_t payload_offset;
    uint8_t header_byte;
    rt_err_t err;

    if ((packet == RT_NULL) || (sbc_payload == RT_NULL) || (sbc_payload_size == RT_NULL) || (sbc_header == RT_NULL))
    {
        return -RT_EINVAL;
    }

    err = bt_a2dp_audio_parse_rtp_payload_offset(packet, size, &payload_offset);
    if (err != RT_EOK)
    {
        return err;
    }

    if (size < (uint16_t) (payload_offset + BT_A2DP_AUDIO_SBC_HEADER_SIZE))
    {
        return -RT_ERROR;
    }

    header_byte = packet[payload_offset];
    sbc_header->fragmentation = (header_byte >> 7) & 0x01u;
    sbc_header->starting_packet = (header_byte >> 6) & 0x01u;
    sbc_header->last_packet = (header_byte >> 5) & 0x01u;
    sbc_header->num_frames = header_byte & 0x0Fu;

    *sbc_payload = &packet[payload_offset + BT_A2DP_AUDIO_SBC_HEADER_SIZE];
    *sbc_payload_size = (uint16_t) (size - payload_offset - BT_A2DP_AUDIO_SBC_HEADER_SIZE);
    return RT_EOK;
}

rt_err_t bt_a2dp_audio_init(void)
{
    btstack_sbc_decoder_init(&bt_a2dp_audio_sbc_decoder_state,
                             SBC_MODE_STANDARD,
                             bt_a2dp_audio_handle_pcm,
                             RT_NULL);
    bt_a2dp_audio_inited = RT_TRUE;
    bt_a2dp_audio_first_pcm_logged = RT_FALSE;
    bt_a2dp_audio_parse_error_logged = RT_FALSE;
    bt_a2dp_audio_fragmented_logged = RT_FALSE;
    bt_a2dp_audio_backpressure_logged = RT_FALSE;
    bt_a2dp_audio_last_write_status = ES8311_AUDIO_PLAYBACK_WRITE_OK;
    return RT_EOK;
}

void bt_a2dp_audio_reset(void)
{
    if (!bt_a2dp_audio_inited)
    {
        return;
    }

    // 重新初始化一次 decoder，就能把上一个流留下的 PLC/帧缓存状态清掉。
    btstack_sbc_decoder_init(&bt_a2dp_audio_sbc_decoder_state,
                             SBC_MODE_STANDARD,
                             bt_a2dp_audio_handle_pcm,
                             RT_NULL);
    bt_a2dp_audio_first_pcm_logged = RT_FALSE;
    bt_a2dp_audio_parse_error_logged = RT_FALSE;
    bt_a2dp_audio_fragmented_logged = RT_FALSE;
    bt_a2dp_audio_backpressure_logged = RT_FALSE;
    bt_a2dp_audio_last_write_status = ES8311_AUDIO_PLAYBACK_WRITE_OK;
}

void bt_a2dp_audio_process_media_packet(uint8_t local_seid, const uint8_t * packet, uint16_t size)
{
    avdtp_sbc_codec_header_t sbc_header;
    const uint8_t * sbc_payload;
    uint16_t sbc_payload_size;
    rt_err_t err;

    UNUSED(local_seid);

    if (!bt_a2dp_audio_inited)
    {
        LOG_W("A2DP audio layer not initialized yet");
        return;
    }

    err = bt_a2dp_audio_parse_sbc_payload(packet, size, &sbc_payload, &sbc_payload_size, &sbc_header);
    if (err != RT_EOK)
    {
        if (!bt_a2dp_audio_parse_error_logged)
        {
            bt_a2dp_audio_parse_error_logged = RT_TRUE;
            LOG_W("A2DP media packet parse failed, size=%u", size);
        }
        return;
    }
    bt_a2dp_audio_parse_error_logged = RT_FALSE;

    // 这里只先处理最常见的非分片 SBC 包。
    // 如果后面遇到分片，再在这里补重组逻辑即可。
    if ((sbc_header.fragmentation != 0u) || (sbc_header.starting_packet != 0u) || (sbc_header.last_packet != 0u))
    {
        if (!bt_a2dp_audio_fragmented_logged)
        {
            bt_a2dp_audio_fragmented_logged = RT_TRUE;
            LOG_W("A2DP SBC fragmentation is not handled yet");
        }
        return;
    }
    bt_a2dp_audio_fragmented_logged = RT_FALSE;

    if (sbc_payload_size == 0u)
    {
        return;
    }

    if (es8311_audio_get_playback_level_frames() >= BT_A2DP_AUDIO_BACKPRESSURE_LEVEL)
    {
        if (!bt_a2dp_audio_backpressure_logged)
        {
            bt_a2dp_audio_backpressure_logged = RT_TRUE;
            LOG_W("A2DP media packet dropped by playback backpressure, level=%u, free=%u",
                  es8311_audio_get_playback_level_frames(),
                  es8311_audio_get_playback_free_frames());
        }
        return;
    }
    bt_a2dp_audio_backpressure_logged = RT_FALSE;

    btstack_sbc_decoder_process_data(&bt_a2dp_audio_sbc_decoder_state,
                                     0,
                                     sbc_payload,
                                     (int) sbc_payload_size);
}

