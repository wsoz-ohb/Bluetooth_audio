#include "bt_hfp_hf_app.h"

#include <stdint.h>
#include <string.h>

#include "btstack_event.h"
#include "btstack_util.h"
#include "hci.h"
#include "classic/hfp.h"
#include "classic/hfp_hf.h"
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

        if(negotiated_codec == HFP_CODEC_MSBC)
        {
            handle_and_flags = little_endian_read_16(packet, 0);
            sco_handle = READ_SCO_CONNECTION_HANDLE(packet);
            packet_status = (handle_and_flags >> 12) & 0x03u;
            payload_length = packet[2];
            payload = packet + HCI_SCO_HEADER_SIZE; //skip header

            if (bt_hfp_hf_sco_handle == sco_handle)
            {
                //put into msbc_decoder
                sco_sbc_decoder->decode_signed_16(&sco_sbc_decoder_context,
                                                packet_status,
                                                payload,
                                                payload_length);

            }
        }else if(negotiated_codec == HFP_CODEC_CVSD)
        {
            handle_and_flags = little_endian_read_16(packet, 0);
            sco_handle = READ_SCO_CONNECTION_HANDLE(packet);
            packet_status = (handle_and_flags >> 12) & 0x03u;
            payload_length = packet[2];
            payload = packet + HCI_SCO_HEADER_SIZE; //skip header

            if (bt_hfp_hf_sco_handle == sco_handle)
            {
                //put into ringbuffer
                (void)rt_ringbuffer_put(&sco_playback_ringbuffer,
                                        payload,
                                        payload_length);
            }
        }




        break;

    case HCI_EVENT_PACKET:
        if ((size >= 4u) &&
            (hci_event_packet_get_type(packet) == HCI_EVENT_SCO_CAN_SEND_NOW))
        {
            LOG_I("HFP SCO can send now, sco=0x%04x",
                  (unsigned int)hci_event_sco_can_send_now_get_handle(packet));
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
