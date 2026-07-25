/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_ES8311_AUDIO_H_
#define APPLICATIONS_ES8311_AUDIO_H_

#include <rtthread.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define ES8311_AUDIO_DEFAULT_SAMPLE_RATE 44100u

typedef struct
{
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
} es8311_audio_capture_format_t;

typedef enum
{
    ES8311_AUDIO_RUN_MODE_IDLE = 0,
    ES8311_AUDIO_RUN_MODE_PLAYBACK,
    ES8311_AUDIO_RUN_MODE_CAPTURE,
} es8311_audio_run_mode_t;

typedef enum
{
    ES8311_AUDIO_PLAYBACK_WRITE_OK = 0,
    ES8311_AUDIO_PLAYBACK_WRITE_INVALID_ARGUMENT,
    ES8311_AUDIO_PLAYBACK_WRITE_NOT_INITED,
    ES8311_AUDIO_PLAYBACK_WRITE_NOT_RUNNING,
    ES8311_AUDIO_PLAYBACK_WRITE_INVALID_FORMAT,
    ES8311_AUDIO_PLAYBACK_WRITE_SAMPLE_RATE_MISMATCH,
    ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL,
} es8311_audio_playback_write_status_t;

rt_err_t es8311_audio_init(void);
rt_bool_t es8311_audio_is_inited(void);
rt_err_t es8311_audio_configure(rt_uint32_t sample_rate, rt_uint8_t playback_channels);
rt_err_t es8311_audio_set_run_mode(es8311_audio_run_mode_t mode);
es8311_audio_run_mode_t es8311_audio_get_run_mode(void);
const char * es8311_audio_run_mode_name(es8311_audio_run_mode_t mode);

rt_err_t es8311_audio_start_playback(void);
void es8311_audio_stop_playback(void);
void es8311_audio_flush_playback(void);
rt_uint32_t es8311_audio_write_playback_checked(const rt_int16_t * pcm,
                                                rt_uint32_t frames,
                                                rt_uint8_t channels,
                                                rt_uint32_t sample_rate,
                                                es8311_audio_playback_write_status_t * status);
rt_uint32_t es8311_audio_write_playback(const rt_int16_t * pcm,
                                        rt_uint32_t frames,
                                        rt_uint8_t channels,
                                        rt_uint32_t sample_rate);
rt_uint32_t es8311_audio_get_playback_level_frames(void);
rt_uint32_t es8311_audio_get_playback_free_frames(void);
rt_uint32_t es8311_audio_get_sample_rate(void);
rt_bool_t es8311_audio_is_playback_running(void);

/* 本地播放音量，范围 0~127，与 AVRCP Absolute Volume 对齐。 */
rt_err_t es8311_audio_set_volume(rt_uint8_t volume_0_127);
rt_uint8_t es8311_audio_get_volume(void);

rt_err_t es8311_audio_start_capture(void);
void es8311_audio_stop_capture(void);
void es8311_audio_flush_capture(void);
/* 读取的是 I2S RX 两个 slot 中自动挑选出的 mono PCM。 */
rt_uint32_t es8311_audio_read_capture(rt_int16_t * pcm, rt_uint32_t max_frames);
rt_uint32_t es8311_audio_get_capture_level_frames(void);
rt_uint32_t es8311_audio_get_capture_drop_frames(void);
rt_bool_t es8311_audio_get_capture_format(es8311_audio_capture_format_t * format);
rt_bool_t es8311_audio_is_capture_running(void);

rt_err_t boot_prompt_play_once(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_ES8311_AUDIO_H_ */
