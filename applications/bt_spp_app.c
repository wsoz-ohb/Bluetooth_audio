/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Classic SPP application layer.
 *
 * The BTstack SPP helper only creates the SDP record. This file owns the
 * RFCOMM service, accepts one incoming connection, and stores received data
 * in a 1 KB RT-Thread ring buffer for the future bootloader protocol worker.
 */
#include "bt_spp_app.h"

#include "bt_config.h"
#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "bluetooth.h"
#include "classic/rfcomm.h"
#include "classic/sdp_server.h"
#include "classic/spp_server.h"

#include <ipc/ringbuffer.h>
#define DBG_TAG "bt_spp"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_SPP_RFCOMM_CHANNEL       1u
#define BT_SPP_RFCOMM_MAX_FRAME     1024u
#define BT_SPP_RX_BUFFER_SIZE       1024u
#define BT_SPP_SDP_RECORD_HANDLE    0x00010004u
#define BT_SPP_SDP_RECORD_SIZE      200u
#define BT_SPP_SERVICE_NAME         "WSOZ SPP"
#define BT_SPP_RX_DUMP_ENABLED      0
#define BT_SPP_LOG_CHUNK_SIZE       24u

typedef struct
{
    struct rt_ringbuffer rx_ring;
    rt_uint8_t rx_storage[BT_SPP_RX_BUFFER_SIZE];
    struct rt_mutex lock;
    rt_bool_t lock_inited;

    rt_bool_t service_registered;
    rt_uint16_t pending_rfcomm_cid;
    rt_uint16_t rfcomm_cid;
    rt_uint16_t max_frame_size;
    rt_size_t rx_dropped_bytes;
    rt_bool_t overflow_logged;
} bt_spp_context_t;

static bt_spp_context_t bt_spp_ctx;
static rt_uint8_t bt_spp_sdp_record[BT_SPP_SDP_RECORD_SIZE];

#if BT_SPP_RX_DUMP_ENABLED
static void bt_spp_log_rx_data(const rt_uint8_t *data,
                               rt_size_t size,
                               rt_size_t stored_len)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char hex[BT_SPP_LOG_CHUNK_SIZE * 3u];
    char text[BT_SPP_LOG_CHUNK_SIZE + 1u];
    rt_size_t offset = 0u;

    LOG_I("SPP RX: len=%u, stored=%u",
          (unsigned)size,
          (unsigned)stored_len);

    while (offset < size)
    {
        rt_size_t index;
        rt_size_t chunk_len = size - offset;
        char *hex_ptr = hex;

        if (chunk_len > BT_SPP_LOG_CHUNK_SIZE)
        {
            chunk_len = BT_SPP_LOG_CHUNK_SIZE;
        }

        for (index = 0u; index < chunk_len; index++)
        {
            rt_uint8_t byte = data[offset + index];

            *hex_ptr++ = hex_digits[byte >> 4];
            *hex_ptr++ = hex_digits[byte & 0x0Fu];
            if (index + 1u < chunk_len)
            {
                *hex_ptr++ = ' ';
            }

            text[index] = ((byte >= 0x20u) && (byte <= 0x7Eu)) ?
                          (char)byte : '.';
        }

        *hex_ptr = '\0';
        text[chunk_len] = '\0';
        LOG_I("SPP RX data offset=%u, text=\"%s\", hex=%s",
              (unsigned)offset,
              text,
              hex);
        offset += chunk_len;
    }
}
#endif

static void bt_spp_reset_rx_locked(void)
{
    rt_ringbuffer_reset(&bt_spp_ctx.rx_ring);
    bt_spp_ctx.rx_dropped_bytes = 0u;
    bt_spp_ctx.overflow_logged = RT_FALSE;
}

static void bt_spp_packet_handler(uint8_t packet_type,
                                  uint16_t channel,
                                  uint8_t *packet,
                                  uint16_t size)
{
    if (packet_type == RFCOMM_DATA_PACKET)
    {
        rt_size_t stored_len;

        if ((packet == RT_NULL) || (size == 0u))
        {
            return;
        }

        (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
        if (bt_spp_ctx.rfcomm_cid != channel)
        {
            rt_mutex_release(&bt_spp_ctx.lock);
            return;
        }

        stored_len = rt_ringbuffer_put(&bt_spp_ctx.rx_ring, packet, size);
        if (stored_len < size)
        {
            bt_spp_ctx.rx_dropped_bytes += (rt_size_t)(size - stored_len);
            if (!bt_spp_ctx.overflow_logged)
            {
                bt_spp_ctx.overflow_logged = RT_TRUE;
                LOG_W("SPP RX ring full, dropped bytes; consume data before it reaches 1 KB");
            }
        }
        rt_mutex_release(&bt_spp_ctx.lock);

#if BT_SPP_RX_DUMP_ENABLED
        bt_spp_log_rx_data(packet, size, stored_len);
#endif
        return;
    }

    if ((packet_type != HCI_EVENT_PACKET) || (packet == RT_NULL) || (size == 0u))
    {
        return;
    }

    switch (hci_event_packet_get_type(packet))
    {
    case RFCOMM_EVENT_INCOMING_CONNECTION:
    {
        rt_uint16_t rfcomm_cid;
        rt_bool_t accept_connection = RT_FALSE;

        if (rfcomm_event_incoming_connection_get_server_channel(packet) != BT_SPP_RFCOMM_CHANNEL)
        {
            return;
        }

        rfcomm_cid = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
        (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
        if ((bt_spp_ctx.pending_rfcomm_cid == 0u) && (bt_spp_ctx.rfcomm_cid == 0u))
        {
            bt_spp_ctx.pending_rfcomm_cid = rfcomm_cid;
            accept_connection = RT_TRUE;
        }
        rt_mutex_release(&bt_spp_ctx.lock);

        if (accept_connection)
        {
            LOG_I("SPP incoming connection, cid=0x%04x", rfcomm_cid);
            if (rfcomm_accept_connection(rfcomm_cid) != ERROR_CODE_SUCCESS)
            {
                (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
                if (bt_spp_ctx.pending_rfcomm_cid == rfcomm_cid)
                {
                    bt_spp_ctx.pending_rfcomm_cid = 0u;
                }
                rt_mutex_release(&bt_spp_ctx.lock);
                LOG_E("SPP accept connection failed, cid=0x%04x", rfcomm_cid);
            }
        }
        else
        {
            LOG_W("SPP already has a connection, decline cid=0x%04x", rfcomm_cid);
            (void)rfcomm_decline_connection(rfcomm_cid);
        }
        break;
    }

    case RFCOMM_EVENT_CHANNEL_OPENED:
    {
        rt_uint8_t status;
        rt_uint16_t rfcomm_cid;

        status = rfcomm_event_channel_opened_get_status(packet);
        rfcomm_cid = rfcomm_event_channel_opened_get_rfcomm_cid(packet);

        (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
        if (status == ERROR_CODE_SUCCESS)
        {
            bt_spp_ctx.pending_rfcomm_cid = 0u;
            bt_spp_ctx.rfcomm_cid = rfcomm_cid;
            bt_spp_ctx.max_frame_size = rfcomm_event_channel_opened_get_max_frame_size(packet);
            bt_spp_reset_rx_locked();
        }
        else if (bt_spp_ctx.pending_rfcomm_cid == rfcomm_cid)
        {
            bt_spp_ctx.pending_rfcomm_cid = 0u;
        }
        rt_mutex_release(&bt_spp_ctx.lock);

        if (status == ERROR_CODE_SUCCESS)
        {
            LOG_I("SPP channel opened, cid=0x%04x, mtu=%u",
                  rfcomm_cid,
                  rfcomm_event_channel_opened_get_max_frame_size(packet));
        }
        else
        {
            LOG_E("SPP channel open failed, cid=0x%04x, status=0x%02x",
                  rfcomm_cid,
                  status);
        }
        break;
    }

    case RFCOMM_EVENT_CHANNEL_CLOSED:
    {
        rt_uint16_t rfcomm_cid;
        rt_bool_t clear_state = RT_FALSE;

        rfcomm_cid = rfcomm_event_channel_closed_get_rfcomm_cid(packet);
        (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
        if ((bt_spp_ctx.rfcomm_cid == rfcomm_cid) ||
            (bt_spp_ctx.pending_rfcomm_cid == rfcomm_cid))
        {
            bt_spp_ctx.pending_rfcomm_cid = 0u;
            bt_spp_ctx.rfcomm_cid = 0u;
            bt_spp_ctx.max_frame_size = 0u;
            bt_spp_reset_rx_locked();
            clear_state = RT_TRUE;
        }
        rt_mutex_release(&bt_spp_ctx.lock);

        if (clear_state)
        {
            LOG_I("SPP channel closed, cid=0x%04x", rfcomm_cid);
        }
        break;
    }

    default:
        break;
    }
}

rt_err_t bt_spp_service_init(void)
{
    rt_err_t err;
    rt_uint8_t status;

#if !BT_CFG_ENABLE_CLASSIC
    LOG_E("SPP requires Classic support");
    return -RT_ERROR;
#elif !BT_CFG_CLASSIC_ENABLE_SDP || !BT_CFG_CLASSIC_ENABLE_RFCOMM
    LOG_E("SPP requires SDP and RFCOMM support");
    return -RT_ERROR;
#endif

    if (bt_spp_ctx.service_registered)
    {
        return RT_EOK;
    }

    if (!bt_spp_ctx.lock_inited)
    {
        err = rt_mutex_init(&bt_spp_ctx.lock, "sppmtx", RT_IPC_FLAG_FIFO);
        if (err != RT_EOK)
        {
            LOG_E("SPP mutex init failed: %d", err);
            return err;
        }
        bt_spp_ctx.lock_inited = RT_TRUE;
    }

    rt_ringbuffer_init(&bt_spp_ctx.rx_ring,
                       bt_spp_ctx.rx_storage,
                       sizeof(bt_spp_ctx.rx_storage));

    status = rfcomm_register_service(bt_spp_packet_handler,
                                     BT_SPP_RFCOMM_CHANNEL,
                                     BT_SPP_RFCOMM_MAX_FRAME);
    if (status != ERROR_CODE_SUCCESS)
    {
        LOG_E("SPP RFCOMM service register failed: 0x%02x", status);
        return -RT_ERROR;
    }

    spp_create_sdp_record(bt_spp_sdp_record,
                          BT_SPP_SDP_RECORD_HANDLE,
                          BT_SPP_RFCOMM_CHANNEL,
                          BT_SPP_SERVICE_NAME);
    status = sdp_register_service(bt_spp_sdp_record);
    if (status != ERROR_CODE_SUCCESS)
    {
        rfcomm_unregister_service(BT_SPP_RFCOMM_CHANNEL);
        LOG_E("SPP SDP service register failed: 0x%02x", status);
        return -RT_ERROR;
    }

    bt_spp_ctx.service_registered = RT_TRUE;
    LOG_I("SPP service registered, channel=%u, RX ring=%u bytes",
          BT_SPP_RFCOMM_CHANNEL,
          BT_SPP_RX_BUFFER_SIZE);
    return RT_EOK;
}

rt_size_t bt_spp_rx_read(rt_uint8_t *buffer, rt_size_t size)
{
    rt_size_t read_len;

    if (!bt_spp_ctx.lock_inited || (buffer == RT_NULL) || (size == 0u))
    {
        return 0u;
    }

    if (size > BT_SPP_RX_BUFFER_SIZE)
    {
        size = BT_SPP_RX_BUFFER_SIZE;
    }

    (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
    read_len = rt_ringbuffer_get(&bt_spp_ctx.rx_ring,
                                 buffer,
                                 (rt_uint16_t)size);
    rt_mutex_release(&bt_spp_ctx.lock);
    return read_len;
}

rt_size_t bt_spp_rx_data_len(void)
{
    rt_size_t data_len;

    if (!bt_spp_ctx.lock_inited)
    {
        return 0u;
    }

    (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
    data_len = rt_ringbuffer_data_len(&bt_spp_ctx.rx_ring);
    rt_mutex_release(&bt_spp_ctx.lock);
    return data_len;
}

rt_size_t bt_spp_rx_dropped_bytes(void)
{
    rt_size_t dropped_bytes;

    if (!bt_spp_ctx.lock_inited)
    {
        return 0u;
    }

    (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
    dropped_bytes = bt_spp_ctx.rx_dropped_bytes;
    rt_mutex_release(&bt_spp_ctx.lock);
    return dropped_bytes;
}

rt_bool_t bt_spp_is_connected(void)
{
    rt_bool_t connected;

    if (!bt_spp_ctx.lock_inited)
    {
        return RT_FALSE;
    }

    (void)rt_mutex_take(&bt_spp_ctx.lock, RT_WAITING_FOREVER);
    connected = (bt_spp_ctx.rfcomm_cid != 0u) ? RT_TRUE : RT_FALSE;
    rt_mutex_release(&bt_spp_ctx.lock);
    return connected;
}
