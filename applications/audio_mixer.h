/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_AUDIO_MIXER_H_
#define APPLICATIONS_AUDIO_MIXER_H_

#include <rtthread.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define AUDIO_MIXER_SAMPLE_RATE 44100u
#define AUDIO_MIXER_VOLUME_MAX  127u

typedef enum
{
    AUDIO_MIXER_SOURCE_BACKGROUND = 0,
    AUDIO_MIXER_SOURCE_VOICE,
    AUDIO_MIXER_SOURCE_COUNT,
} audio_mixer_source_t;

typedef enum
{
    AUDIO_MIXER_WRITE_OK = 0,
    AUDIO_MIXER_WRITE_INVALID_ARGUMENT,
    AUDIO_MIXER_WRITE_NOT_INITED,
    AUDIO_MIXER_WRITE_SOURCE_INACTIVE,
    AUDIO_MIXER_WRITE_INVALID_FORMAT,
    AUDIO_MIXER_WRITE_SAMPLE_RATE_MISMATCH,
} audio_mixer_write_status_t;

rt_err_t audio_mixer_init(void);

rt_err_t audio_mixer_source_start(audio_mixer_source_t source,
                                  rt_uint32_t sample_rate,
                                  rt_uint8_t channels);
void audio_mixer_source_stop(audio_mixer_source_t source);
void audio_mixer_stop_all(void);
rt_bool_t audio_mixer_source_is_active(audio_mixer_source_t source);

/* A2DP 背景音量，范围 0~127，与 AVRCP Absolute Volume 对齐。 */
rt_err_t audio_mixer_set_background_volume(rt_uint8_t volume_0_127);
rt_uint8_t audio_mixer_get_background_volume(void);

rt_uint32_t audio_mixer_write(audio_mixer_source_t source,
                              const rt_int16_t * pcm,
                              rt_uint32_t frames,
                              rt_uint8_t channels,
                              rt_uint32_t sample_rate,
                              audio_mixer_write_status_t * status);

rt_uint32_t audio_mixer_get_source_level_frames(audio_mixer_source_t source);
rt_uint32_t audio_mixer_get_source_free_frames(audio_mixer_source_t source);

/* 由 ES8311 DMA 填充路径调用，输出固定为双声道 44.1kHz。 */
rt_uint32_t audio_mixer_render_stereo(rt_int16_t * pcm, rt_uint32_t frames, void * context);
rt_err_t boot_prompt_play_once(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_AUDIO_MIXER_H_ */
