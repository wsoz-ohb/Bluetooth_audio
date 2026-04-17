/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bt_pcm_stream.h"

#define DBG_TAG "bt_pcm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_PCM_STREAM_OUTPUT_CHANNELS   2u
#define BT_PCM_STREAM_BUFFER_FRAMES     8192u
#define BT_PCM_STREAM_BUFFER_SAMPLES    (BT_PCM_STREAM_BUFFER_FRAMES * BT_PCM_STREAM_OUTPUT_CHANNELS)

typedef struct
{
    rt_bool_t inited;
    rt_bool_t configured;
    rt_bool_t running;
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
    rt_size_t ring_read;
    rt_size_t ring_write;
    rt_size_t ring_level;
    rt_bool_t overflow_notice_printed;
    const bt_pcm_stream_sink_t * sink;
} bt_pcm_stream_context_t;

static bt_pcm_stream_context_t bt_pcm_stream_ctx;
static rt_int16_t bt_pcm_stream_buffer[BT_PCM_STREAM_BUFFER_SAMPLES];

static void bt_pcm_stream_reset_locked(void)
{
    bt_pcm_stream_ctx.ring_read = 0u;
    bt_pcm_stream_ctx.ring_write = 0u;
    bt_pcm_stream_ctx.ring_level = 0u;
    bt_pcm_stream_ctx.overflow_notice_printed = RT_FALSE;
}

rt_err_t bt_pcm_stream_init(void)
{
    if (bt_pcm_stream_ctx.inited)
    {
        return RT_EOK;
    }

    rt_memset(&bt_pcm_stream_ctx, 0, sizeof(bt_pcm_stream_ctx));
    bt_pcm_stream_ctx.inited = RT_TRUE;
    return RT_EOK;
}

rt_err_t bt_pcm_stream_register_sink(const bt_pcm_stream_sink_t * sink)
{
    rt_base_t level;
    rt_bool_t configured;
    rt_bool_t running;
    rt_uint32_t sample_rate;
    rt_uint8_t channels;

    if (bt_pcm_stream_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    if ((bt_pcm_stream_ctx.sink != RT_NULL) &&
        (sink != RT_NULL) &&
        (bt_pcm_stream_ctx.sink != sink))
    {
        rt_hw_interrupt_enable(level);
        LOG_E("PCM stream sink already registered, unregister it before binding another backend");
        return -RT_EBUSY;
    }

    bt_pcm_stream_ctx.sink = sink;
    configured = bt_pcm_stream_ctx.configured;
    running = bt_pcm_stream_ctx.running;
    sample_rate = bt_pcm_stream_ctx.sample_rate;
    channels = bt_pcm_stream_ctx.channels;
    rt_hw_interrupt_enable(level);

    // 当前先保留同步通知模型：PCM stream 负责把格式/起停事件推给当前选中的后端。
    // 后面如果要继续降耦合，可以把这里改成“只投递事件，不直接执行业务逻辑”。
    if ((sink != RT_NULL) && configured && (sink->on_configure != RT_NULL))
    {
        sink->on_configure(sample_rate, channels);
    }

    if ((sink != RT_NULL) && running && (sink->on_start != RT_NULL))
    {
        sink->on_start();
    }

    return RT_EOK;
}

void bt_pcm_stream_unregister_sink(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_pcm_stream_ctx.sink = RT_NULL;
    rt_hw_interrupt_enable(level);
}

rt_err_t bt_pcm_stream_configure(rt_uint32_t sample_rate, rt_uint8_t channels)
{
    rt_base_t level;
    const bt_pcm_stream_sink_t * sink;
    rt_bool_t need_stop;

    if ((sample_rate == 0u) || (channels == 0u) || (channels > 2u))
    {
        return -RT_EINVAL;
    }

    if (bt_pcm_stream_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    need_stop = bt_pcm_stream_ctx.running;
    bt_pcm_stream_ctx.running = RT_FALSE;
    bt_pcm_stream_ctx.configured = RT_TRUE;
    bt_pcm_stream_ctx.sample_rate = sample_rate;
    bt_pcm_stream_ctx.channels = channels;
    sink = bt_pcm_stream_ctx.sink;
    bt_pcm_stream_reset_locked();
    rt_hw_interrupt_enable(level);

    if (need_stop && (sink != RT_NULL) && (sink->on_stop != RT_NULL))
    {
        sink->on_stop();
    }

    if ((sink != RT_NULL) && (sink->on_configure != RT_NULL))
    {
        sink->on_configure(sample_rate, channels);
    }

    return RT_EOK;
}

rt_err_t bt_pcm_stream_start(void)
{
    rt_base_t level;
    const bt_pcm_stream_sink_t * sink;
    rt_bool_t notify;

    if (!bt_pcm_stream_ctx.configured)
    {
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    notify = (rt_bool_t) !bt_pcm_stream_ctx.running;
    bt_pcm_stream_ctx.running = RT_TRUE;
    sink = bt_pcm_stream_ctx.sink;
    rt_hw_interrupt_enable(level);

    if (notify && (sink != RT_NULL) && (sink->on_start != RT_NULL))
    {
        sink->on_start();
    }

    return RT_EOK;
}

void bt_pcm_stream_stop(void)
{
    rt_base_t level;
    const bt_pcm_stream_sink_t * sink;
    rt_bool_t notify;

    level = rt_hw_interrupt_disable();
    notify = bt_pcm_stream_ctx.running;
    bt_pcm_stream_ctx.running = RT_FALSE;
    sink = bt_pcm_stream_ctx.sink;
    rt_hw_interrupt_enable(level);

    if (notify && (sink != RT_NULL) && (sink->on_stop != RT_NULL))
    {
        sink->on_stop();
    }
}

void bt_pcm_stream_flush(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_pcm_stream_reset_locked();
    rt_hw_interrupt_enable(level);
}

rt_uint32_t bt_pcm_stream_write(const rt_int16_t * pcm,
                                rt_uint32_t frames,
                                rt_uint8_t channels,
                                rt_uint32_t sample_rate)
{
    rt_uint32_t frame_index;
    rt_uint32_t written_frames;
    rt_bool_t log_overflow;
    const bt_pcm_stream_sink_t * sink;
    rt_base_t level;

    if ((pcm == RT_NULL) || (frames == 0u))
    {
        return 0u;
    }

    if (!bt_pcm_stream_ctx.configured)
    {
        return 0u;
    }

    if ((channels == 0u) || (channels > 2u))
    {
        return 0u;
    }

    if (sample_rate != bt_pcm_stream_ctx.sample_rate)
    {
        LOG_W("PCM sample rate mismatch, drop frames: %u != %u",
              sample_rate,
              bt_pcm_stream_ctx.sample_rate);
        return 0u;
    }

    written_frames = 0u;
    log_overflow = RT_FALSE;

    level = rt_hw_interrupt_disable();
    for (frame_index = 0u; frame_index < frames; frame_index++)
    {
        rt_int16_t left_sample;
        rt_int16_t right_sample;

        if ((BT_PCM_STREAM_BUFFER_SAMPLES - bt_pcm_stream_ctx.ring_level) < BT_PCM_STREAM_OUTPUT_CHANNELS)
        {
            if (!bt_pcm_stream_ctx.overflow_notice_printed)
            {
                bt_pcm_stream_ctx.overflow_notice_printed = RT_TRUE;
                log_overflow = RT_TRUE;
            }
            break;
        }

        if (channels == 1u)
        {
            left_sample = pcm[frame_index];
            right_sample = left_sample;
        }
        else
        {
            left_sample = pcm[(rt_size_t) frame_index * channels];
            right_sample = pcm[(rt_size_t) frame_index * channels + 1u];
        }

        bt_pcm_stream_buffer[bt_pcm_stream_ctx.ring_write] = left_sample;
        bt_pcm_stream_ctx.ring_write++;
        if (bt_pcm_stream_ctx.ring_write >= BT_PCM_STREAM_BUFFER_SAMPLES)
        {
            bt_pcm_stream_ctx.ring_write = 0u;
        }

        bt_pcm_stream_buffer[bt_pcm_stream_ctx.ring_write] = right_sample;
        bt_pcm_stream_ctx.ring_write++;
        if (bt_pcm_stream_ctx.ring_write >= BT_PCM_STREAM_BUFFER_SAMPLES)
        {
            bt_pcm_stream_ctx.ring_write = 0u;
        }

        bt_pcm_stream_ctx.ring_level += BT_PCM_STREAM_OUTPUT_CHANNELS;
        written_frames++;
    }
    sink = bt_pcm_stream_ctx.sink;
    rt_hw_interrupt_enable(level);

    if (log_overflow)
    {
        LOG_W("PCM stream overflow, drop audio frames");
    }

    if ((written_frames > 0u) && (sink != RT_NULL) && (sink->on_data_available != RT_NULL))
    {
        sink->on_data_available();
    }

    return written_frames;
}

rt_uint32_t bt_pcm_stream_read(rt_int16_t * pcm, rt_uint32_t max_frames)
{
    rt_uint32_t read_frames;
    rt_base_t level;

    if ((pcm == RT_NULL) || (max_frames == 0u))
    {
        return 0u;
    }

    read_frames = 0u;

    level = rt_hw_interrupt_disable();
    while ((read_frames < max_frames) && (bt_pcm_stream_ctx.ring_level >= BT_PCM_STREAM_OUTPUT_CHANNELS))
    {
        pcm[(rt_size_t) read_frames * BT_PCM_STREAM_OUTPUT_CHANNELS] =
            bt_pcm_stream_buffer[bt_pcm_stream_ctx.ring_read];
        bt_pcm_stream_ctx.ring_read++;
        if (bt_pcm_stream_ctx.ring_read >= BT_PCM_STREAM_BUFFER_SAMPLES)
        {
            bt_pcm_stream_ctx.ring_read = 0u;
        }

        pcm[(rt_size_t) read_frames * BT_PCM_STREAM_OUTPUT_CHANNELS + 1u] =
            bt_pcm_stream_buffer[bt_pcm_stream_ctx.ring_read];
        bt_pcm_stream_ctx.ring_read++;
        if (bt_pcm_stream_ctx.ring_read >= BT_PCM_STREAM_BUFFER_SAMPLES)
        {
            bt_pcm_stream_ctx.ring_read = 0u;
        }

        bt_pcm_stream_ctx.ring_level -= BT_PCM_STREAM_OUTPUT_CHANNELS;
        read_frames++;
    }
    rt_hw_interrupt_enable(level);

    return read_frames;
}

rt_uint32_t bt_pcm_stream_get_level_frames(void)
{
    rt_base_t level;
    rt_uint32_t frames;

    level = rt_hw_interrupt_disable();
    frames = (rt_uint32_t) (bt_pcm_stream_ctx.ring_level / BT_PCM_STREAM_OUTPUT_CHANNELS);
    rt_hw_interrupt_enable(level);
    return frames;
}

rt_bool_t bt_pcm_stream_get_format(bt_pcm_stream_format_t * format)
{
    rt_base_t level;
    rt_bool_t valid;

    if (format == RT_NULL)
    {
        return RT_FALSE;
    }

    level = rt_hw_interrupt_disable();
    valid = bt_pcm_stream_ctx.configured;
    if (valid)
    {
        format->sample_rate = bt_pcm_stream_ctx.sample_rate;
        format->channels = bt_pcm_stream_ctx.channels;
    }
    rt_hw_interrupt_enable(level);
    return valid;
}

rt_bool_t bt_pcm_stream_is_running(void)
{
    rt_base_t level;
    rt_bool_t running;

    level = rt_hw_interrupt_disable();
    running = bt_pcm_stream_ctx.running;
    rt_hw_interrupt_enable(level);
    return running;
}



