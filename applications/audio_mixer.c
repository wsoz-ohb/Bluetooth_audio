/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "audio_mixer.h"

#include <rthw.h>

#include "audio_define.h"
#include "es8311_audio.h"

#define DBG_TAG "audio_mixer"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define AUDIO_MIXER_OUTPUT_CHANNELS             2u
#define AUDIO_MIXER_BACKGROUND_BUFFER_FRAMES    8192u
#define AUDIO_MIXER_VOICE_BUFFER_FRAMES         4096u
#define AUDIO_MIXER_BACKGROUND_START_FRAMES     6144u
#define AUDIO_MIXER_VOICE_START_FRAMES          1024u

#define AUDIO_MIXER_Q15_ONE                     32768
#define AUDIO_MIXER_DUCK_BACKGROUND_GAIN_Q15    1638  /* 0.05 */
#define AUDIO_MIXER_DUCK_ATTACK_FRAMES          ((AUDIO_MIXER_SAMPLE_RATE * 5u) / 1000u)
#define AUDIO_MIXER_DUCK_RELEASE_FRAMES         ((AUDIO_MIXER_SAMPLE_RATE * 250u) / 1000u)
#define AUDIO_MIXER_DUCK_ATTACK_STEP_Q15        ((AUDIO_MIXER_Q15_ONE + AUDIO_MIXER_DUCK_ATTACK_FRAMES - 1u) / AUDIO_MIXER_DUCK_ATTACK_FRAMES)
#define AUDIO_MIXER_DUCK_RELEASE_STEP_Q15       ((AUDIO_MIXER_Q15_ONE + AUDIO_MIXER_DUCK_RELEASE_FRAMES - 1u) / AUDIO_MIXER_DUCK_RELEASE_FRAMES)

/*
 * 音量 1~127 保持原 ES8311 曲线：先映射到 DAC 0x00~0xBF，再按每级
 * 0.5dB 换算成 Q15；音量 0 使用数字静音，避免最低 Q15 产生负向 1 LSB。
 */
static const rt_uint16_t audio_mixer_background_gain_q15[AUDIO_MIXER_VOLUME_MAX + 1u] =
{
        0u,     1u,     1u,     1u,     1u,     1u,     1u,     1u,
        1u,     1u,     1u,     1u,     2u,     2u,     2u,     2u,
        2u,     2u,     3u,     3u,     3u,     3u,     4u,     4u,
        4u,     5u,     5u,     6u,     6u,     7u,     7u,     8u,
        9u,     9u,    10u,    11u,    12u,    13u,    15u,    16u,
       17u,    18u,    21u,    22u,    25u,    26u,    29u,    31u,
       35u,    37u,    41u,    44u,    49u,    52u,    58u,    62u,
       69u,    73u,    82u,    87u,    98u,   104u,   116u,   123u,
      138u,   146u,   164u,   174u,   195u,   207u,   232u,   246u,
      276u,   292u,   328u,   347u,   389u,   413u,   463u,   490u,
      550u,   583u,   654u,   693u,   777u,   823u,   924u,   978u,
     1098u,  1163u,  1305u,  1382u,  1550u,  1642u,  1843u,  1952u,
     2190u,  2320u,  2603u,  2757u,  3093u,  3277u,  3677u,  3894u,
     4370u,  4629u,  5193u,  5501u,  6172u,  6538u,  7336u,  7771u,
     8719u,  9235u, 10362u, 10976u, 12315u, 13045u, 14637u, 15504u,
    17396u, 18427u, 20675u, 21900u, 24573u, 26029u, 29205u, 32768u,
};

typedef struct
{
    rt_uint32_t read_frame;
    rt_uint32_t write_frame;
    rt_uint32_t level_frames;
    rt_uint32_t capacity_frames;
    rt_bool_t active;
    rt_bool_t overflow_logged;
} audio_mixer_ring_t;

typedef struct
{
    rt_bool_t inited;
    rt_int32_t duck_q15;
    rt_int32_t background_gain_q15;
    rt_uint8_t background_volume_0_127;
    audio_mixer_ring_t source[AUDIO_MIXER_SOURCE_COUNT];
} audio_mixer_context_t;

static audio_mixer_context_t audio_mixer_ctx;
static struct rt_mutex audio_mixer_session_mutex;

/* 背景音乐 ring 取代原 ES8311 playback ring，继续放在 CPU-only CCM。 */
static rt_int16_t audio_mixer_background_buffer[AUDIO_MIXER_BACKGROUND_BUFFER_FRAMES * AUDIO_MIXER_OUTPUT_CHANNELS]
    __attribute__((section(".ccmbss.audio_mixer_background")));
static rt_int16_t audio_mixer_voice_buffer[AUDIO_MIXER_VOICE_BUFFER_FRAMES];

static rt_bool_t audio_mixer_source_valid(audio_mixer_source_t source)
{
    return (rt_bool_t) ((rt_uint32_t) source < (rt_uint32_t) AUDIO_MIXER_SOURCE_COUNT);
}

static void audio_mixer_reset_ring_locked(audio_mixer_source_t source)
{
    audio_mixer_ring_t * ring;

    ring = &audio_mixer_ctx.source[source];
    ring->read_frame = 0u;
    ring->write_frame = 0u;
    ring->level_frames = 0u;
    ring->overflow_logged = RT_FALSE;
}

static void audio_mixer_drop_oldest_locked(audio_mixer_source_t source, rt_uint32_t frames)
{
    audio_mixer_ring_t * ring;

    ring = &audio_mixer_ctx.source[source];
    if (frames > ring->level_frames)
    {
        frames = ring->level_frames;
    }

    ring->read_frame = (ring->read_frame + frames) % ring->capacity_frames;
    ring->level_frames -= frames;
}

static rt_int16_t audio_mixer_saturate_s16(rt_int32_t sample)
{
    if (sample > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (sample < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (rt_int16_t) sample;
}

static rt_bool_t audio_mixer_any_source_active_locked(void)
{
    return (rt_bool_t) (audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_BACKGROUND].active ||
                        audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_VOICE].active);
}

static rt_bool_t audio_mixer_playback_ready_locked(void)
{
    const audio_mixer_ring_t * background;
    const audio_mixer_ring_t * voice;

    background = &audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_BACKGROUND];
    voice = &audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_VOICE];
    return (rt_bool_t) ((background->active &&
                         (background->level_frames >= AUDIO_MIXER_BACKGROUND_START_FRAMES)) ||
                        (voice->active &&
                         (voice->level_frames >= AUDIO_MIXER_VOICE_START_FRAMES)));
}

static void audio_mixer_rollback_source_start(audio_mixer_source_t source)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    audio_mixer_ctx.source[source].active = RT_FALSE;
    audio_mixer_reset_ring_locked(source);
    if (!audio_mixer_any_source_active_locked())
    {
        audio_mixer_ctx.duck_q15 = 0;
    }
    rt_hw_interrupt_enable(level);
}

rt_err_t audio_mixer_init(void)
{
    rt_err_t err;

    if (audio_mixer_ctx.inited)
    {
        return RT_EOK;
    }

    rt_memset(&audio_mixer_ctx, 0, sizeof(audio_mixer_ctx));
    audio_mixer_ctx.background_volume_0_127 = AUDIO_MIXER_VOLUME_MAX;
    audio_mixer_ctx.background_gain_q15 = AUDIO_MIXER_Q15_ONE;
    audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_BACKGROUND].capacity_frames = AUDIO_MIXER_BACKGROUND_BUFFER_FRAMES;
    audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_VOICE].capacity_frames = AUDIO_MIXER_VOICE_BUFFER_FRAMES;

    err = rt_mutex_init(&audio_mixer_session_mutex, "audmix", RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        return err;
    }

    err = es8311_audio_set_playback_renderer(audio_mixer_render_stereo, RT_NULL);
    if (err != RT_EOK)
    {
        rt_mutex_detach(&audio_mixer_session_mutex);
        return err;
    }

    audio_mixer_ctx.inited = RT_TRUE;
    LOG_I("audio mixer init ok, rate=%u, background=%u frames, voice=%u frames",
          AUDIO_MIXER_SAMPLE_RATE,
          AUDIO_MIXER_BACKGROUND_BUFFER_FRAMES,
          AUDIO_MIXER_VOICE_BUFFER_FRAMES);
    return RT_EOK;
}

rt_err_t audio_mixer_source_start(audio_mixer_source_t source,
                                  rt_uint32_t sample_rate,
                                  rt_uint8_t channels)
{
    rt_bool_t newly_active;
    rt_base_t level;
    rt_err_t err;

    if (!audio_mixer_ctx.inited)
    {
        return -RT_ERROR;
    }
    if (!audio_mixer_source_valid(source))
    {
        return -RT_EINVAL;
    }
    if (sample_rate != AUDIO_MIXER_SAMPLE_RATE)
    {
        return -RT_EINVAL;
    }
    if (((source == AUDIO_MIXER_SOURCE_BACKGROUND) && ((channels == 0u) || (channels > 2u))) ||
        ((source == AUDIO_MIXER_SOURCE_VOICE) && (channels != 1u)))
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(&audio_mixer_session_mutex, RT_WAITING_FOREVER);

    if (es8311_audio_is_capture_running())
    {
        rt_mutex_release(&audio_mixer_session_mutex);
        return -RT_EBUSY;
    }

    level = rt_hw_interrupt_disable();
    newly_active = (rt_bool_t) !audio_mixer_ctx.source[source].active;
    if (newly_active)
    {
        audio_mixer_reset_ring_locked(source);
        audio_mixer_ctx.source[source].active = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (!es8311_audio_is_playback_running())
    {
        err = es8311_audio_configure(AUDIO_MIXER_SAMPLE_RATE, AUDIO_MIXER_OUTPUT_CHANNELS);
        if (err == RT_EOK)
        {
            err = es8311_audio_start_playback();
        }
        if (err != RT_EOK)
        {
            audio_mixer_rollback_source_start(source);
            rt_mutex_release(&audio_mixer_session_mutex);
            LOG_E("start playback sink failed, source=%d, err=%d", source, err);
            return err;
        }
    }

    rt_mutex_release(&audio_mixer_session_mutex);
    return RT_EOK;
}

void audio_mixer_source_stop(audio_mixer_source_t source)
{
    rt_bool_t stop_sink;
    rt_base_t level;

    if (!audio_mixer_ctx.inited || !audio_mixer_source_valid(source))
    {
        return;
    }

    rt_mutex_take(&audio_mixer_session_mutex, RT_WAITING_FOREVER);
    level = rt_hw_interrupt_disable();
    audio_mixer_ctx.source[source].active = RT_FALSE;
    audio_mixer_reset_ring_locked(source);
    stop_sink = (rt_bool_t) !audio_mixer_any_source_active_locked();
    if (stop_sink)
    {
        audio_mixer_ctx.duck_q15 = 0;
    }
    rt_hw_interrupt_enable(level);

    if (stop_sink)
    {
        es8311_audio_stop_playback();
    }
    rt_mutex_release(&audio_mixer_session_mutex);
}

void audio_mixer_stop_all(void)
{
    rt_base_t level;

    if (!audio_mixer_ctx.inited)
    {
        return;
    }

    rt_mutex_take(&audio_mixer_session_mutex, RT_WAITING_FOREVER);
    level = rt_hw_interrupt_disable();
    audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_BACKGROUND].active = RT_FALSE;
    audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_VOICE].active = RT_FALSE;
    audio_mixer_reset_ring_locked(AUDIO_MIXER_SOURCE_BACKGROUND);
    audio_mixer_reset_ring_locked(AUDIO_MIXER_SOURCE_VOICE);
    audio_mixer_ctx.duck_q15 = 0;
    rt_hw_interrupt_enable(level);
    es8311_audio_stop_playback();
    rt_mutex_release(&audio_mixer_session_mutex);
}

rt_bool_t audio_mixer_source_is_active(audio_mixer_source_t source)
{
    rt_bool_t active;
    rt_base_t level;

    if (!audio_mixer_ctx.inited || !audio_mixer_source_valid(source))
    {
        return RT_FALSE;
    }

    level = rt_hw_interrupt_disable();
    active = audio_mixer_ctx.source[source].active;
    rt_hw_interrupt_enable(level);
    return active;
}

rt_err_t audio_mixer_set_background_volume(rt_uint8_t volume_0_127)
{
    rt_int32_t gain_q15;
    rt_base_t level;

    if (!audio_mixer_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (volume_0_127 > AUDIO_MIXER_VOLUME_MAX)
    {
        volume_0_127 = AUDIO_MIXER_VOLUME_MAX;
    }
    gain_q15 = audio_mixer_background_gain_q15[volume_0_127];

    level = rt_hw_interrupt_disable();
    audio_mixer_ctx.background_volume_0_127 = volume_0_127;
    audio_mixer_ctx.background_gain_q15 = gain_q15;
    rt_hw_interrupt_enable(level);

    LOG_I("background volume=%u/127, gain_q15=%d",
          volume_0_127,
          gain_q15);
    return RT_EOK;
}

rt_uint8_t audio_mixer_get_background_volume(void)
{
    rt_uint8_t volume;
    rt_base_t level;

    if (!audio_mixer_ctx.inited)
    {
        return AUDIO_MIXER_VOLUME_MAX;
    }

    level = rt_hw_interrupt_disable();
    volume = audio_mixer_ctx.background_volume_0_127;
    rt_hw_interrupt_enable(level);
    return volume;
}

rt_uint32_t audio_mixer_write(audio_mixer_source_t source,
                              const rt_int16_t * pcm,
                              rt_uint32_t frames,
                              rt_uint8_t channels,
                              rt_uint32_t sample_rate,
                              audio_mixer_write_status_t * status)
{
    audio_mixer_ring_t * ring;
    rt_uint32_t start_frame;
    rt_uint32_t frames_to_write;
    rt_uint32_t frame_index;
    rt_uint32_t written_frames;
    rt_bool_t log_overflow;
    rt_bool_t notify_ready;
    rt_base_t level;

    if (status != RT_NULL)
    {
        *status = AUDIO_MIXER_WRITE_OK;
    }
    if ((pcm == RT_NULL) || (frames == 0u) || !audio_mixer_source_valid(source))
    {
        if (status != RT_NULL)
        {
            *status = AUDIO_MIXER_WRITE_INVALID_ARGUMENT;
        }
        return 0u;
    }
    if (!audio_mixer_ctx.inited)
    {
        if (status != RT_NULL)
        {
            *status = AUDIO_MIXER_WRITE_NOT_INITED;
        }
        return 0u;
    }
    if (sample_rate != AUDIO_MIXER_SAMPLE_RATE)
    {
        if (status != RT_NULL)
        {
            *status = AUDIO_MIXER_WRITE_SAMPLE_RATE_MISMATCH;
        }
        return 0u;
    }
    if (((source == AUDIO_MIXER_SOURCE_BACKGROUND) && ((channels == 0u) || (channels > 2u))) ||
        ((source == AUDIO_MIXER_SOURCE_VOICE) && (channels != 1u)))
    {
        if (status != RT_NULL)
        {
            *status = AUDIO_MIXER_WRITE_INVALID_FORMAT;
        }
        return 0u;
    }

    ring = &audio_mixer_ctx.source[source];
    start_frame = 0u;
    frames_to_write = frames;
    written_frames = 0u;
    log_overflow = RT_FALSE;
    notify_ready = RT_FALSE;

    if (frames_to_write > ring->capacity_frames)
    {
        start_frame = frames_to_write - ring->capacity_frames;
        frames_to_write = ring->capacity_frames;
    }

    level = rt_hw_interrupt_disable();
    if (!ring->active)
    {
        rt_hw_interrupt_enable(level);
        if (status != RT_NULL)
        {
            *status = AUDIO_MIXER_WRITE_SOURCE_INACTIVE;
        }
        return 0u;
    }

    if ((ring->capacity_frames - ring->level_frames) < frames_to_write)
    {
        audio_mixer_drop_oldest_locked(source,
                                       frames_to_write - (ring->capacity_frames - ring->level_frames));
        if (!ring->overflow_logged)
        {
            ring->overflow_logged = RT_TRUE;
            log_overflow = RT_TRUE;
        }
    }

    for (frame_index = start_frame; frame_index < (start_frame + frames_to_write); frame_index++)
    {
        if (source == AUDIO_MIXER_SOURCE_BACKGROUND)
        {
            rt_int16_t left;
            rt_int16_t right;
            rt_uint32_t buffer_index;

            if (channels == 1u)
            {
                left = pcm[frame_index];
                right = left;
            }
            else
            {
                left = pcm[(rt_size_t) frame_index * channels];
                right = pcm[(rt_size_t) frame_index * channels + 1u];
            }

            buffer_index = ring->write_frame * AUDIO_MIXER_OUTPUT_CHANNELS;
            audio_mixer_background_buffer[buffer_index] = left;
            audio_mixer_background_buffer[buffer_index + 1u] = right;
        }
        else
        {
            audio_mixer_voice_buffer[ring->write_frame] = pcm[frame_index];
        }

        ring->write_frame++;
        if (ring->write_frame >= ring->capacity_frames)
        {
            ring->write_frame = 0u;
        }
        ring->level_frames++;
        written_frames++;
    }

    notify_ready = audio_mixer_playback_ready_locked();
    rt_hw_interrupt_enable(level);

    if (log_overflow)
    {
        LOG_W("source %d ring overflow, drop oldest frames", source);
    }
    if (notify_ready)
    {
        /* 与 source stop 串行化，避免 ready 判定后、DMA 启动前被 stop 穿插。 */
        rt_mutex_take(&audio_mixer_session_mutex, RT_WAITING_FOREVER);
        level = rt_hw_interrupt_disable();
        notify_ready = audio_mixer_playback_ready_locked();
        rt_hw_interrupt_enable(level);
        if (notify_ready)
        {
            (void) es8311_audio_notify_playback_ready();
        }
        rt_mutex_release(&audio_mixer_session_mutex);
    }
    return written_frames;
}

rt_uint32_t audio_mixer_get_source_level_frames(audio_mixer_source_t source)
{
    rt_uint32_t frames;
    rt_base_t level;

    if (!audio_mixer_ctx.inited || !audio_mixer_source_valid(source))
    {
        return 0u;
    }

    level = rt_hw_interrupt_disable();
    frames = audio_mixer_ctx.source[source].level_frames;
    rt_hw_interrupt_enable(level);
    return frames;
}

rt_uint32_t audio_mixer_get_source_free_frames(audio_mixer_source_t source)
{
    rt_uint32_t frames;
    rt_base_t level;

    if (!audio_mixer_ctx.inited || !audio_mixer_source_valid(source))
    {
        return 0u;
    }

    level = rt_hw_interrupt_disable();
    frames = audio_mixer_ctx.source[source].capacity_frames -
             audio_mixer_ctx.source[source].level_frames;
    rt_hw_interrupt_enable(level);
    return frames;
}

rt_uint32_t audio_mixer_render_stereo(rt_int16_t * pcm, rt_uint32_t frames, void * context)
{
    audio_mixer_ring_t * background;
    audio_mixer_ring_t * voice;
    rt_uint32_t frame_index;
    rt_base_t level;

    RT_UNUSED(context);

    if ((pcm == RT_NULL) || (frames == 0u) || !audio_mixer_ctx.inited)
    {
        return 0u;
    }

    background = &audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_BACKGROUND];
    voice = &audio_mixer_ctx.source[AUDIO_MIXER_SOURCE_VOICE];

    level = rt_hw_interrupt_disable();
    for (frame_index = 0u; frame_index < frames; frame_index++)
    {
        rt_int32_t background_left;
        rt_int32_t background_right;
        rt_int32_t voice_sample;
        rt_int32_t background_duck_gain_q15;
        rt_int32_t background_gain_q15;
        rt_int32_t mixed_left;
        rt_int32_t mixed_right;
        rt_bool_t voice_has_frame;

        background_left = 0;
        background_right = 0;
        voice_sample = 0;
        voice_has_frame = RT_FALSE;

        if (background->active && (background->level_frames > 0u))
        {
            rt_uint32_t buffer_index;

            buffer_index = background->read_frame * AUDIO_MIXER_OUTPUT_CHANNELS;
            background_left = audio_mixer_background_buffer[buffer_index];
            background_right = audio_mixer_background_buffer[buffer_index + 1u];
            background->read_frame++;
            if (background->read_frame >= background->capacity_frames)
            {
                background->read_frame = 0u;
            }
            background->level_frames--;
            background->overflow_logged = RT_FALSE;
        }

        if (voice->active && (voice->level_frames > 0u))
        {
            voice_sample = audio_mixer_voice_buffer[voice->read_frame];
            voice_has_frame = RT_TRUE;
            voice->read_frame++;
            if (voice->read_frame >= voice->capacity_frames)
            {
                voice->read_frame = 0u;
            }
            voice->level_frames--;
            voice->overflow_logged = RT_FALSE;
        }

        if (background->active && voice_has_frame)
        {
            audio_mixer_ctx.duck_q15 += AUDIO_MIXER_DUCK_ATTACK_STEP_Q15;
            if (audio_mixer_ctx.duck_q15 > AUDIO_MIXER_Q15_ONE)
            {
                audio_mixer_ctx.duck_q15 = AUDIO_MIXER_Q15_ONE;
            }
        }
        else if (audio_mixer_ctx.duck_q15 > 0)
        {
            audio_mixer_ctx.duck_q15 -= AUDIO_MIXER_DUCK_RELEASE_STEP_Q15;
            if (audio_mixer_ctx.duck_q15 < 0)
            {
                audio_mixer_ctx.duck_q15 = 0;
            }
        }

        /* A2DP 音量和 Duck 都只作用于背景音，AI 语音不受手机音量影响。 */
        background_duck_gain_q15 = AUDIO_MIXER_Q15_ONE -
            ((audio_mixer_ctx.duck_q15 *
              (AUDIO_MIXER_Q15_ONE - AUDIO_MIXER_DUCK_BACKGROUND_GAIN_Q15)) >> 15);
        background_gain_q15 =
            (audio_mixer_ctx.background_gain_q15 * background_duck_gain_q15) >> 15;

        mixed_left = ((background_left * background_gain_q15) >> 15) +
                     voice_sample;
        mixed_right = ((background_right * background_gain_q15) >> 15) +
                      voice_sample;

        pcm[(rt_size_t) frame_index * AUDIO_MIXER_OUTPUT_CHANNELS] =
            audio_mixer_saturate_s16(mixed_left);
        pcm[(rt_size_t) frame_index * AUDIO_MIXER_OUTPUT_CHANNELS + 1u] =
            audio_mixer_saturate_s16(mixed_right);
    }
    rt_hw_interrupt_enable(level);

    return frames;
}

rt_err_t boot_prompt_play_once(void)
{
#define BOOT_PROMPT_SAMPLE_RATE          44100u
#define BOOT_PROMPT_CHANNELS            1u
#define BOOT_PROMPT_CHUNK_FRAMES        512u
#define BOOT_PROMPT_TAIL_SILENCE_FRAMES 2048u
#define BOOT_PROMPT_STOP_LEVEL_FRAMES   512u
#define BOOT_PROMPT_WAIT_MS             2u
#define BOOT_PROMPT_VOLUME_PERCENT      0   //10

    static const rt_int16_t boot_prompt_silence[BOOT_PROMPT_CHUNK_FRAMES] = {0};
    rt_int16_t scaled_pcm[BOOT_PROMPT_CHUNK_FRAMES];
    rt_uint32_t offset = 0u;
    rt_uint32_t tail_offset = 0u;
    rt_bool_t source_started = RT_FALSE;
    rt_err_t result = RT_EOK;

    if (audio_mixer_source_start(AUDIO_MIXER_SOURCE_BACKGROUND,
                                 BOOT_PROMPT_SAMPLE_RATE,
                                 BOOT_PROMPT_CHANNELS) != RT_EOK)
    {
        result = -RT_ERROR;
        goto exit;
    }
    source_started = RT_TRUE;

    while (offset < boot_prompt_pcm_len)
    {
        rt_uint32_t free_frames;
        rt_uint32_t frames;
        rt_uint32_t written;
        rt_uint32_t frame_index;

        free_frames = audio_mixer_get_source_free_frames(AUDIO_MIXER_SOURCE_BACKGROUND);
        if (free_frames == 0u)
        {
            rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
            continue;
        }

        frames = boot_prompt_pcm_len - offset;
        if (frames > BOOT_PROMPT_CHUNK_FRAMES)
        {
            frames = BOOT_PROMPT_CHUNK_FRAMES;
        }
        if (frames > free_frames)
        {
            frames = free_frames;
        }

        for (frame_index = 0u; frame_index < frames; frame_index++)
        {
            scaled_pcm[frame_index] = (rt_int16_t) (((rt_int32_t) boot_prompt_pcm[offset + frame_index] *
                                                     BOOT_PROMPT_VOLUME_PERCENT) / 100);
        }

        written = audio_mixer_write(AUDIO_MIXER_SOURCE_BACKGROUND,
                                    scaled_pcm,
                                    frames,
                                    BOOT_PROMPT_CHANNELS,
                                    BOOT_PROMPT_SAMPLE_RATE,
                                    RT_NULL);
        if (written == 0u)
        {
            result = -RT_ERROR;
            goto stop_playback;
        }

        offset += written;
    }

    while (tail_offset < BOOT_PROMPT_TAIL_SILENCE_FRAMES)
    {
        rt_uint32_t free_frames;
        rt_uint32_t frames;
        rt_uint32_t written;

        free_frames = audio_mixer_get_source_free_frames(AUDIO_MIXER_SOURCE_BACKGROUND);
        if (free_frames == 0u)
        {
            rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
            continue;
        }

        frames = BOOT_PROMPT_TAIL_SILENCE_FRAMES - tail_offset;
        if (frames > BOOT_PROMPT_CHUNK_FRAMES)
        {
            frames = BOOT_PROMPT_CHUNK_FRAMES;
        }
        if (frames > free_frames)
        {
            frames = free_frames;
        }

        written = audio_mixer_write(AUDIO_MIXER_SOURCE_BACKGROUND,
                                    boot_prompt_silence,
                                    frames,
                                    BOOT_PROMPT_CHANNELS,
                                    BOOT_PROMPT_SAMPLE_RATE,
                                    RT_NULL);
        if (written == 0u)
        {
            result = -RT_ERROR;
            goto stop_playback;
        }

        tail_offset += written;
    }

    while (audio_mixer_get_source_level_frames(AUDIO_MIXER_SOURCE_BACKGROUND) >
           BOOT_PROMPT_STOP_LEVEL_FRAMES)
    {
        rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
    }

stop_playback:
    if (source_started)
    {
        audio_mixer_source_stop(AUDIO_MIXER_SOURCE_BACKGROUND);
    }

exit:
#undef BOOT_PROMPT_SAMPLE_RATE
#undef BOOT_PROMPT_CHANNELS
#undef BOOT_PROMPT_CHUNK_FRAMES
#undef BOOT_PROMPT_TAIL_SILENCE_FRAMES
#undef BOOT_PROMPT_STOP_LEVEL_FRAMES
#undef BOOT_PROMPT_WAIT_MS
#undef BOOT_PROMPT_VOLUME_PERCENT

    return result;
}
