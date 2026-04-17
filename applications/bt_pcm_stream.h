/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_PCM_STREAM_H_
#define APPLICATIONS_BT_PCM_STREAM_H_

#include <rtthread.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct
{
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
} bt_pcm_stream_format_t;

/* sink 代表一个具体 PCM 消费后端，例如 WM/I2S 或后续的 MAX 功放链路。 */
typedef struct
{
    void (*on_configure)(rt_uint32_t sample_rate, rt_uint8_t channels);
    void (*on_start)(void);
    void (*on_stop)(void);
    void (*on_data_available)(void);
} bt_pcm_stream_sink_t;

/* 公共 PCM 管道：A2DP 解码端往里写，具体音频后端从这里读。 */
rt_err_t bt_pcm_stream_init(void);
rt_err_t bt_pcm_stream_register_sink(const bt_pcm_stream_sink_t * sink);
void bt_pcm_stream_unregister_sink(void);
rt_err_t bt_pcm_stream_configure(rt_uint32_t sample_rate, rt_uint8_t channels);
rt_err_t bt_pcm_stream_start(void);
void bt_pcm_stream_stop(void);
void bt_pcm_stream_flush(void);
rt_uint32_t bt_pcm_stream_write(const rt_int16_t * pcm,
                                rt_uint32_t frames,
                                rt_uint8_t channels,
                                rt_uint32_t sample_rate);
rt_uint32_t bt_pcm_stream_read(rt_int16_t * pcm, rt_uint32_t max_frames);
rt_uint32_t bt_pcm_stream_get_level_frames(void);
rt_bool_t bt_pcm_stream_get_format(bt_pcm_stream_format_t * format);
rt_bool_t bt_pcm_stream_is_running(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_PCM_STREAM_H_ */

