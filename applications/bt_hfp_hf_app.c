#include "bt_hfp_hf_app.h"

#include <stdint.h>
#include <string.h>

#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"
#include "classic/hfp.h"
#include "classic/hfp_hf.h"
#include "classic/hfp_msbc.h"
#include "classic/rfcomm.h"
#include "classic/sdp_server.h"
#include "classic/btstack_sbc_bluedroid.h"
#include "audio_mixer.h"
#include "es8311_audio.h"
#include "ipc/ringbuffer.h"

#define DBG_TAG "bt_hfp_hf"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_HFP_HF_RFCOMM_CHANNEL       2u
#define BT_HFP_HF_SDP_RECORD_HANDLE    0x00010005u
#define BT_HFP_HF_SDP_RECORD_SIZE      200u
#define BT_HFP_HF_SUPPORTED_FEATURES   (1u << HFP_HFSF_CODEC_NEGOTIATION)   //codec negotiation
#define BT_HFP_HF_SERVICE_NAME         "WSOZ Hands-Free"
#define SCO_PLAYBACK_RINGBUFFER_SIZE_BYTES  2048u
#define BT_HFP_HF_SCO_TX_PCM_SAMPLES        128u

static  uint8_t negotiated_codec;

static const uint8_t bt_hfp_hf_codecs[] = {
    HFP_CODEC_CVSD,
    HFP_CODEC_MSBC,
};
static uint8_t bt_hfp_hf_sdp_record[BT_HFP_HF_SDP_RECORD_SIZE];
static rt_bool_t bt_hfp_hf_inited = RT_FALSE;
static hci_con_handle_t bt_hfp_hf_acl_handle = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t bt_hfp_hf_sco_handle = HCI_CON_HANDLE_INVALID;

static struct rt_ringbuffer sco_playback_ringbuffer;
static rt_uint8_t sco_playback_buffer[SCO_PLAYBACK_RINGBUFFER_SIZE_BYTES];
/* mSBC decoder is CPU-only; keep its large working state out of DMA-visible SRAM. */
static btstack_sbc_decoder_bluedroid_t sco_sbc_decoder_context
    __attribute__((section(".ccmbss.hfp_sbc_decoder")));
static const btstack_sbc_decoder_t * sco_sbc_decoder;
static uint8_t bt_hfp_hf_sco_tx_payload_len;
static rt_bool_t bt_hfp_hf_uplink_logged;
static rt_bool_t bt_hfp_hf_rx_logged;
static int16_t bt_hfp_hf_sco_tx_pcm[BT_HFP_HF_SCO_TX_PCM_SAMPLES];
static uint8_t bt_hfp_hf_sco_tx_payload[255];

static void handle_sco_pcm(int16_t * data, int num_samples, int num_channels, int sample_rate, void * context)
{
    rt_size_t pcm_bytes;

    (void) context;

    if ((data == RT_NULL) || (num_samples <= 0) ||
        (num_channels != 1) || (sample_rate != 16000))
    {
        LOG_W("unsupported SCO PCM format, samples=%d, channels=%d, rate=%d",
              num_samples,
              num_channels,
              sample_rate);
        return;
    }

    pcm_bytes = (rt_size_t)num_samples * (rt_size_t)num_channels * sizeof(*data);
    (void)rt_ringbuffer_put(&sco_playback_ringbuffer,
                            (const rt_uint8_t *)data,
                            (rt_uint16_t)pcm_bytes);
}

static rt_uint32_t bt_hfp_hf_render_stereo(rt_int16_t * pcm,
                                            rt_uint32_t frames,
                                            void * context)
{
    rt_size_t read_bytes;
    rt_uint32_t read_frames;
    rt_uint32_t index;

    (void) context;

    if ((pcm == RT_NULL) || (frames == 0u))
    {
        return 0u;
    }

    read_bytes = rt_ringbuffer_get(&sco_playback_ringbuffer,
                                   (rt_uint8_t *)pcm,
                                   (rt_uint16_t)(frames * sizeof(*pcm)));
    read_frames = (rt_uint32_t)(read_bytes / sizeof(*pcm));

    /* Expand mono in place from the end so unread samples are not overwritten. */
    for (index = read_frames; index > 0u; index--)
    {
        rt_int16_t sample;

        sample = pcm[index - 1u];
        pcm[(index - 1u) * 2u] = sample;
        pcm[(index - 1u) * 2u + 1u] = sample;
    }

    return read_frames;
}

static void bt_hfp_hf_reset_uplink(void)
{
    bt_hfp_hf_sco_tx_payload_len = 0u;
    bt_hfp_hf_uplink_logged = RT_FALSE;
    bt_hfp_hf_rx_logged = RT_FALSE;
    if (negotiated_codec == HFP_CODEC_MSBC)
    {
        hfp_msbc_deinit();
    }
}

static void bt_hfp_hf_read_mic_pcm(int16_t * pcm, uint16_t samples)
{
    uint32_t level;
    uint32_t keep;
    int16_t discard[32];
    uint32_t got;

    if ((pcm == RT_NULL) || (samples == 0u))
    {
        return;
    }

    /* Drop stale capture so the packet just sent tracks the latest mic audio. */
    keep = (uint32_t)samples * 2u;
    level = es8311_audio_get_capture_level_frames();
    while (level > keep)
    {
        uint32_t drop;

        drop = level - keep;
        if (drop > (uint32_t)(sizeof(discard) / sizeof(discard[0])))
        {
            drop = (uint32_t)(sizeof(discard) / sizeof(discard[0]));
        }

        got = es8311_audio_read_capture(discard, drop);
        if (got == 0u)
        {
            break;
        }
        level -= got;
    }

    got = es8311_audio_read_capture(pcm, samples);
    if (got < samples)
    {
        memset(&pcm[got], 0, ((size_t)samples - got) * sizeof(*pcm));
    }
}

static void bt_hfp_hf_send_uplink_packet(void)
{
    uint8_t payload_len;
    uint8_t * sco_packet;
    hci_con_handle_t sco_handle;

    sco_handle = bt_hfp_hf_sco_handle;
    payload_len = bt_hfp_hf_sco_tx_payload_len;
    if ((sco_handle == HCI_CON_HANDLE_INVALID) || (payload_len == 0u))
    {
        return;
    }

    memset(bt_hfp_hf_sco_tx_payload, 0, payload_len);
    if (negotiated_codec == HFP_CODEC_MSBC)
    {
        uint8_t attempts;

        attempts = 0u;
        while ((hfp_msbc_num_bytes_in_stream() < (int)payload_len) &&
               (hfp_msbc_can_encode_audio_frame_now() != 0) &&
               (attempts < 2u))
        {
            int samples;

            samples = hfp_msbc_num_audio_samples_per_frame();
            if ((samples <= 0) || (samples > (int)BT_HFP_HF_SCO_TX_PCM_SAMPLES))
            {
                break;
            }

            bt_hfp_hf_read_mic_pcm(bt_hfp_hf_sco_tx_pcm, (uint16_t)samples);
            hfp_msbc_encode_audio_frame(bt_hfp_hf_sco_tx_pcm);
            attempts++;
        }

        if (hfp_msbc_num_bytes_in_stream() >= (int)payload_len)
        {
            hfp_msbc_read_from_stream(bt_hfp_hf_sco_tx_payload, payload_len);
        }
    }
    else if (negotiated_codec == HFP_CODEC_CVSD)
    {
        uint16_t samples;

        samples = (uint16_t)(payload_len / 2u);
        if (samples > BT_HFP_HF_SCO_TX_PCM_SAMPLES)
        {
            samples = BT_HFP_HF_SCO_TX_PCM_SAMPLES;
        }
        if (samples > 0u)
        {
            bt_hfp_hf_read_mic_pcm(bt_hfp_hf_sco_tx_pcm, samples);
            memcpy(bt_hfp_hf_sco_tx_payload,
                   bt_hfp_hf_sco_tx_pcm,
                   (size_t)samples * sizeof(bt_hfp_hf_sco_tx_pcm[0]));
        }
    }

    hci_reserve_packet_buffer();
    sco_packet = hci_get_outgoing_packet_buffer();
    little_endian_store_16(sco_packet, 0, sco_handle);
    sco_packet[2] = payload_len;
    memcpy(&sco_packet[HCI_SCO_HEADER_SIZE], bt_hfp_hf_sco_tx_payload, payload_len);

    if (!bt_hfp_hf_uplink_logged)
    {
        bt_hfp_hf_uplink_logged = RT_TRUE;
        LOG_I("HFP SCO uplink started, sco=0x%04x, payload=%u",
              sco_handle,
              payload_len);
    }

    (void)hci_send_sco_packet_buffer((int)payload_len + (int)HCI_SCO_HEADER_SIZE);
}

static void bt_hfp_hf_packet_handler(uint8_t packet_type,
                                     uint16_t channel,
                                     uint8_t * packet,
                                     uint16_t size)
{
    uint8_t subevent;

    (void) channel;
    (void) size;

    if ((packet_type != HCI_EVENT_PACKET) ||
        (hci_event_packet_get_type(packet) != HCI_EVENT_HFP_META))
    {
        return;
    }

    subevent = hci_event_hfp_meta_get_subevent_code(packet);
    switch (subevent)
    {
    case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:     //SLC
    {
        uint8_t status;
        bd_addr_t address;

        status = hfp_subevent_service_level_connection_established_get_status(packet);
        hfp_subevent_service_level_connection_established_get_bd_addr(packet, address);
        if (status == ERROR_CODE_SUCCESS)
        {
            bt_hfp_hf_acl_handle =
                hfp_subevent_service_level_connection_established_get_acl_handle(packet);
            LOG_I("HFP service level connected, addr=%s, acl=0x%04x",
                  bd_addr_to_str(address),
                  bt_hfp_hf_acl_handle);
        }
        else
        {
            bt_hfp_hf_acl_handle = HCI_CON_HANDLE_INVALID;
            LOG_E("HFP service level connection failed, addr=%s, status=0x%02x",
                  bd_addr_to_str(address),
                  status);
        }
        break;
    }

    case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
        LOG_I("HFP service level released, acl=0x%04x",
              hfp_subevent_service_level_connection_released_get_acl_handle(packet));
        bt_hfp_hf_acl_handle = HCI_CON_HANDLE_INVALID;
        bt_hfp_hf_sco_handle = HCI_CON_HANDLE_INVALID;
        bt_hfp_hf_reset_uplink();
        break;

    case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:     //SCO
    {
        uint8_t status;
        const char *codec_name;
        uint32_t sample_rate;
        rt_err_t err;

        status = hfp_subevent_audio_connection_established_get_status(packet);
        if (status == ERROR_CODE_SUCCESS)
        {
            bt_hfp_hf_sco_handle =
                hfp_subevent_audio_connection_established_get_sco_handle(packet);
            negotiated_codec =
                hfp_subevent_audio_connection_established_get_negotiated_codec(packet);
            if (negotiated_codec == HFP_CODEC_MSBC)
            {
                codec_name = "mSBC";
                sample_rate = 16000u;
            }
            else
            {
                codec_name = "CVSD";
                sample_rate = 8000u;
            }
            LOG_I("HFP SCO connected, sco=0x%04x, codec=%s(%u), rate=%u",
                  bt_hfp_hf_sco_handle,
                  codec_name,
                  negotiated_codec,
                  sample_rate);
            bt_hfp_hf_sco_tx_payload_len = 60u;
            bt_hfp_hf_uplink_logged = RT_FALSE;
            bt_hfp_hf_rx_logged = RT_FALSE;
            if (negotiated_codec == HFP_CODEC_MSBC)
            {
                hfp_msbc_init();
            }
        }
        else
        {
            bt_hfp_hf_sco_handle = HCI_CON_HANDLE_INVALID;
            LOG_E("HFP SCO connection failed, status=0x%02x", status);
            break;
        }

        err = es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_IDLE);
        if (err != RT_EOK)
        {
            LOG_E("stop current audio route failed: %d", err);
            break;
        }

        rt_ringbuffer_reset(&sco_playback_ringbuffer);
        if(negotiated_codec == HFP_CODEC_MSBC)
        {
            sco_sbc_decoder->configure(&sco_sbc_decoder_context,
                                    SBC_MODE_mSBC,
                                    handle_sco_pcm,
                                    RT_NULL);
        }


        err = es8311_audio_set_playback_renderer(bt_hfp_hf_render_stereo, RT_NULL);
        if (err != RT_EOK)
        {
            LOG_E("set HFP playback renderer failed: %d", err);
            break;
        }
        
        if(negotiated_codec == HFP_CODEC_CVSD)
        {
            es8311_audio_set_call_sample_rate(ES8311_AUDIO_CVSD_SAMPLE_RATE);
            err = es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_CALL_DUPLEX);            
        }else if(negotiated_codec == HFP_CODEC_MSBC)
        {
            es8311_audio_set_call_sample_rate(ES8311_AUDIO_MSBC_SAMPLE_RATE);
            err = es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_CALL_DUPLEX);
        }

        if (err != RT_EOK)
        {
            LOG_E("start HFP call duplex failed: %d", err);
            (void)es8311_audio_set_playback_renderer(audio_mixer_render_stereo, RT_NULL);
            (void)es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_PLAYBACK);
        }
        break;
    }

    case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
        LOG_I("HFP SCO released, sco=0x%04x",
              hfp_subevent_audio_connection_released_get_sco_handle(packet));
        bt_hfp_hf_sco_handle = HCI_CON_HANDLE_INVALID;
        bt_hfp_hf_reset_uplink();

        //switch audio_to_music
        (void)es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_IDLE);
        rt_ringbuffer_reset(&sco_playback_ringbuffer);
        if (es8311_audio_set_playback_renderer(audio_mixer_render_stereo, RT_NULL) != RT_EOK)
        {
            LOG_E("restore mixer playback renderer failed");
            break;
        }
        if (es8311_audio_set_run_mode(ES8311_AUDIO_RUN_MODE_PLAYBACK) != RT_EOK)
        {
            LOG_E("restore music playback mode failed");
        }

        break;

    default:
        break;
    }
}

static void bt_hfp_hf_sco_packet_handler(uint8_t packet_type,
                                         uint16_t channel,
                                         uint8_t * packet,
                                         uint16_t size)
{
    (void) channel;

    if (packet == RT_NULL)
    {
        LOG_W("HFP SCO callback received null packet");
        return;
    }

    switch (packet_type)
    {
    case HCI_SCO_DATA_PACKET:
        if (size < HCI_SCO_HEADER_SIZE)
        {
            LOG_W("HFP SCO RX packet too short, size=%u", (unsigned int)size);
            break;
        }
        //SCO包:
        /*
        packet[0..1]  handle + packet status flags
        packet[2]     payload length
        packet[3..]   SCO payload   
        */

        uint16_t handle_and_flags;
        uint16_t sco_handle;
        uint8_t packet_status;
        uint8_t payload_length;
        uint8_t *payload;

        handle_and_flags = little_endian_read_16(packet, 0);
        sco_handle = READ_SCO_CONNECTION_HANDLE(packet);
        packet_status = (uint8_t)((handle_and_flags >> 12) & 0x03u);
        payload_length = packet[2];
        payload = packet + HCI_SCO_HEADER_SIZE;

        if (sco_handle != bt_hfp_hf_sco_handle)
        {
            break;
        }
        if ((payload_length > 0u) &&
            ((uint16_t)payload_length + HCI_SCO_HEADER_SIZE > size))
        {
            break;
        }

        if ((payload_length > 0u) && (negotiated_codec == HFP_CODEC_MSBC))
        {
            sco_sbc_decoder->decode_signed_16(&sco_sbc_decoder_context,
                                              packet_status,
                                              payload,
                                              payload_length);
        }
        else if ((payload_length > 0u) && (negotiated_codec == HFP_CODEC_CVSD))
        {
            (void)rt_ringbuffer_put(&sco_playback_ringbuffer,
                                    payload,
                                    payload_length);
        }

        /* One received SCO packet schedules one uplink request. The actual
         * packet is still sent only from HCI_EVENT_SCO_CAN_SEND_NOW. */
        if (payload_length > 0u)
        {
            bt_hfp_hf_sco_tx_payload_len = payload_length;
        }
        else if (bt_hfp_hf_sco_tx_payload_len == 0u)
        {
            bt_hfp_hf_sco_tx_payload_len = 60u;
        }
        if (!bt_hfp_hf_rx_logged)
        {
            bt_hfp_hf_rx_logged = RT_TRUE;
            LOG_I("HFP SCO rx, payload=%u, uplink=%u",
                  payload_length,
                  bt_hfp_hf_sco_tx_payload_len);
        }
        hci_request_sco_can_send_now_event_for_con_handle(sco_handle);
        break;

    case HCI_EVENT_PACKET:
        if ((size >= 4u) &&
            (hci_event_packet_get_type(packet) == HCI_EVENT_SCO_CAN_SEND_NOW) &&
            (hci_event_sco_can_send_now_get_handle(packet) == bt_hfp_hf_sco_handle))
        {
            if (hci_can_send_sco_packet_now_for_con_handle(bt_hfp_hf_sco_handle))
            {
                bt_hfp_hf_send_uplink_packet();
            }
        }
        break;

    default:
        LOG_W("HFP SCO callback unexpected packet type=0x%02x, size=%u",
              (unsigned int)packet_type,
              (unsigned int)size);
        break;
    }
}

rt_err_t bt_hfp_hf_service_init(void)
{
    uint8_t status;

    if (bt_hfp_hf_inited)
    {
        return RT_EOK;
    }

    sco_sbc_decoder = btstack_sbc_decoder_bluedroid_init_instance(&sco_sbc_decoder_context);

    status = hfp_hf_init(BT_HFP_HF_RFCOMM_CHANNEL);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("hfp_hf_init failed: 0x%02x", status);
        return -RT_ERROR;
    }

    hfp_hf_init_supported_features(BT_HFP_HF_SUPPORTED_FEATURES);
    hfp_hf_init_codecs((uint8_t)sizeof(bt_hfp_hf_codecs), bt_hfp_hf_codecs);
    hfp_hf_register_packet_handler(bt_hfp_hf_packet_handler);
    hci_register_sco_packet_handler(bt_hfp_hf_sco_packet_handler);

    memset(bt_hfp_hf_sdp_record, 0, sizeof(bt_hfp_hf_sdp_record));
    hfp_hf_create_sdp_record_with_codecs(bt_hfp_hf_sdp_record,
                                         BT_HFP_HF_SDP_RECORD_HANDLE,
                                         BT_HFP_HF_RFCOMM_CHANNEL,
                                         BT_HFP_HF_SERVICE_NAME,
                                         BT_HFP_HF_SUPPORTED_FEATURES,
                                         (uint8_t)sizeof(bt_hfp_hf_codecs),
                                         bt_hfp_hf_codecs);
    status = sdp_register_service(bt_hfp_hf_sdp_record);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("HFP SDP service register failed: 0x%02x", status);
        rfcomm_unregister_service(BT_HFP_HF_RFCOMM_CHANNEL);
        hfp_hf_deinit();
        return -RT_ERROR;
    }

    rt_ringbuffer_init(&sco_playback_ringbuffer,
                       sco_playback_buffer,
                       (rt_int16_t)sizeof(sco_playback_buffer));

    bt_hfp_hf_acl_handle = HCI_CON_HANDLE_INVALID;
    bt_hfp_hf_sco_handle = HCI_CON_HANDLE_INVALID;
    bt_hfp_hf_inited = RT_TRUE;
    LOG_I("HFP HF service ready, rfcomm=%u, codecs=CVSD,mSBC",
          BT_HFP_HF_RFCOMM_CHANNEL);
    return RT_EOK;
}
