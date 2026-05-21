/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "es8311_audio.h"

#include "es8311_driver.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_i2s_ex.h"
#include "audio_define.h"

#define DBG_TAG "es8311_audio"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS        2u
#define ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS         1u
#define ES8311_AUDIO_MAX_INPUT_CHANNELS              2u
#define ES8311_AUDIO_DMA_HALF_FRAMES                 512u
#define ES8311_AUDIO_DMA_BUFFER_FRAMES               (ES8311_AUDIO_DMA_HALF_FRAMES * 2u)
#define ES8311_AUDIO_DMA_TX_SAMPLES                  (ES8311_AUDIO_DMA_BUFFER_FRAMES * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS)
#define ES8311_AUDIO_DMA_RX_SAMPLES                  (ES8311_AUDIO_DMA_BUFFER_FRAMES * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS)
#define ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES          8192u
#define ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES         (ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS)
#define ES8311_AUDIO_CAPTURE_BUFFER_FRAMES           4096u
#define ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES          (ES8311_AUDIO_CAPTURE_BUFFER_FRAMES * ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS)
#define ES8311_AUDIO_PLAYBACK_START_THRESHOLD_FRAMES (ES8311_AUDIO_DMA_BUFFER_FRAMES * 6u)

extern I2S_HandleTypeDef hi2s2;

typedef struct
{
    rt_size_t read;
    rt_size_t write;
    rt_size_t level;
} es8311_audio_ring_t;

typedef enum
{
    ES8311_AUDIO_DMA_MODE_STOPPED = 0,
    ES8311_AUDIO_DMA_MODE_PLAYBACK,
    // Capture 走 I2S full-duplex，因此录音时仍然要维持 TX 侧持续送时钟。
    ES8311_AUDIO_DMA_MODE_DUPLEX,
} es8311_audio_dma_mode_t;

// 统一音频会话上下文。
// 这个文件不把播放/采集拆成两个独立状态机，而是集中在一个上下文里维护：
// - 当前 I2S/DMA 工作模式
// - playback/capture ring buffer
// - codec 当前 sample rate
// - 采集 slot 自动选择结果
typedef struct
{
    DMA_HandleTypeDef hdma_i2s2_rx;
    DMA_HandleTypeDef hdma_i2s2_tx;
    rt_bool_t inited;
    rt_bool_t dma_inited;
    rt_bool_t i2s_inited;
    es8311_audio_dma_mode_t dma_mode;
    rt_bool_t playback_running;
    rt_bool_t capture_running;
    rt_bool_t playback_start_pending;
    rt_bool_t playback_underflow_notice_printed;
    rt_bool_t playback_overflow_notice_printed;
    rt_bool_t capture_overflow_notice_printed;
    rt_uint32_t capture_drop_frames;
    rt_uint32_t sample_rate;
    rt_uint8_t playback_channels;
    rt_uint8_t capture_slot;
    rt_bool_t capture_slot_locked;
    rt_bool_t capture_diag_printed;
    rt_bool_t capture_saturated_notice_printed;
    es8311_audio_ring_t playback_ring;
    es8311_audio_ring_t capture_ring;
} es8311_audio_context_t;

static es8311_audio_context_t es8311_audio_ctx;
static rt_uint16_t es8311_audio_dma_tx_buffer[ES8311_AUDIO_DMA_TX_SAMPLES];
static rt_uint16_t es8311_audio_dma_rx_buffer[ES8311_AUDIO_DMA_RX_SAMPLES];
static rt_int16_t es8311_audio_playback_buffer[ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES];
static rt_int16_t es8311_audio_capture_buffer[ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES];

typedef struct
{
    rt_int16_t min;
    rt_int16_t max;
    rt_uint32_t saturated;
    rt_uint32_t zero;
} es8311_audio_slot_stats_t;

static void es8311_audio_fill_tx_range(rt_size_t offset_frames, rt_size_t frames);
static void es8311_audio_rx_dma_half_callback(DMA_HandleTypeDef * hdma);
static void es8311_audio_rx_dma_full_callback(DMA_HandleTypeDef * hdma);
static void es8311_audio_tx_dma_callback(DMA_HandleTypeDef * hdma);

static uint32_t es8311_audio_sample_rate_to_hal(rt_uint32_t sample_rate)
{
    switch (sample_rate)
    {
    case 8000u:
        return I2S_AUDIOFREQ_8K;
    case 11025u:
        return I2S_AUDIOFREQ_11K;
    case 16000u:
        return I2S_AUDIOFREQ_16K;
    case 22050u:
        return I2S_AUDIOFREQ_22K;
    case 32000u:
        return I2S_AUDIOFREQ_32K;
    case 44100u:
        return I2S_AUDIOFREQ_44K;
    case 48000u:
        return I2S_AUDIOFREQ_48K;
    case 96000u:
        return I2S_AUDIOFREQ_96K;
    default:
        return 0u;
    }
}

static void es8311_audio_reset_playback_ring_locked(void)
{
    es8311_audio_ctx.playback_ring.read = 0u;
    es8311_audio_ctx.playback_ring.write = 0u;
    es8311_audio_ctx.playback_ring.level = 0u;
    es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
    es8311_audio_ctx.playback_overflow_notice_printed = RT_FALSE;
    es8311_audio_ctx.playback_start_pending = RT_FALSE;
}

static void es8311_audio_reset_capture_ring_locked(void)
{
    es8311_audio_ctx.capture_ring.read = 0u;
    es8311_audio_ctx.capture_ring.write = 0u;
    es8311_audio_ctx.capture_ring.level = 0u;
    es8311_audio_ctx.capture_overflow_notice_printed = RT_FALSE;
    es8311_audio_ctx.capture_drop_frames = 0u;
    es8311_audio_ctx.capture_slot = 0u;
    es8311_audio_ctx.capture_slot_locked = RT_FALSE;
    es8311_audio_ctx.capture_diag_printed = RT_FALSE;
    es8311_audio_ctx.capture_saturated_notice_printed = RT_FALSE;
}

static void es8311_audio_clear_dma_buffers(void)
{
    rt_memset(es8311_audio_dma_tx_buffer, 0, sizeof(es8311_audio_dma_tx_buffer));
    rt_memset(es8311_audio_dma_rx_buffer, 0, sizeof(es8311_audio_dma_rx_buffer));
}

static void es8311_audio_collect_slot_stats(const rt_uint16_t * source,
                                            rt_uint32_t frames,
                                            rt_uint8_t slot,
                                            es8311_audio_slot_stats_t * stats)
{
    rt_uint32_t frame_index;

    stats->min = INT16_MAX;
    stats->max = INT16_MIN;
    stats->saturated = 0u;
    stats->zero = 0u;

    for (frame_index = 0u; frame_index < frames; frame_index++)
    {
        rt_int16_t sample;

        sample = (rt_int16_t) source[(rt_size_t) frame_index * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS + slot];
        if (sample < stats->min)
        {
            stats->min = sample;
        }
        if (sample > stats->max)
        {
            stats->max = sample;
        }
        if ((sample == INT16_MIN) || (sample == INT16_MAX))
        {
            stats->saturated++;
        }
        if (sample == 0)
        {
            stats->zero++;
        }
    }
}

static rt_uint8_t es8311_audio_pick_capture_slot(const es8311_audio_slot_stats_t * left,
                                                 const es8311_audio_slot_stats_t * right,
                                                 rt_uint32_t frames)
{
    if (left->saturated == frames && right->saturated < frames)
    {
        return 1u;
    }
    if (right->saturated == frames && left->saturated < frames)
    {
        return 0u;
    }
    if (left->zero == frames && right->zero < frames)
    {
        return 1u;
    }
    if (right->zero == frames && left->zero < frames)
    {
        return 0u;
    }

    return 0u;
}

static rt_bool_t es8311_audio_slot_stats_saturated(const es8311_audio_slot_stats_t * stats,
                                                   rt_uint32_t frames)
{
    return (rt_bool_t) ((frames > 0u) && (stats->saturated == frames));
}

static void es8311_audio_drop_oldest_playback_frames_locked(rt_uint32_t frames)
{
    rt_size_t drop_samples;

    drop_samples = (rt_size_t) frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
    if (drop_samples > es8311_audio_ctx.playback_ring.level)
    {
        drop_samples = es8311_audio_ctx.playback_ring.level;
    }

    drop_samples -= drop_samples % ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
    es8311_audio_ctx.playback_ring.read += drop_samples;
    while (es8311_audio_ctx.playback_ring.read >= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES)
    {
        es8311_audio_ctx.playback_ring.read -= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES;
    }
    es8311_audio_ctx.playback_ring.level -= drop_samples;
}

static rt_err_t es8311_audio_apply_codec_state(void)
{
    // I2S 重配后，codec 的采样率和收发状态也要一起同步，
    // 否则会出现 I2S 已经切到新配置，而 codec 仍停留在旧状态的问题。
    if (es8311_set_sample_rate(es8311_audio_ctx.sample_rate) != RT_EOK)
    {
        LOG_E("es8311_set_sample_rate failed: %u", es8311_audio_ctx.sample_rate);
        return -RT_ERROR;
    }

    if (es8311_audio_ctx.playback_running)
    {
        if (es8311_start_playback() != RT_EOK)
        {
            LOG_E("es8311_start_playback failed");
            return -RT_ERROR;
        }
    }
    else
    {
        (void) es8311_stop_playback();
    }

    if (es8311_audio_ctx.capture_running)
    {
        if (es8311_start_record() != RT_EOK)
        {
            LOG_E("es8311_start_record failed");
            return -RT_ERROR;
        }
    }
    else
    {
        (void) es8311_stop_record();
    }

    return RT_EOK;
}

static rt_err_t es8311_audio_dma_init(void)
{
    HAL_StatusTypeDef hal_status;

    if (es8311_audio_ctx.dma_inited)
    {
        __HAL_LINKDMA(&hi2s2, hdmarx, es8311_audio_ctx.hdma_i2s2_rx);
        __HAL_LINKDMA(&hi2s2, hdmatx, es8311_audio_ctx.hdma_i2s2_tx);
        return RT_EOK;
    }

    __HAL_RCC_DMA1_CLK_ENABLE();

    rt_memset(&es8311_audio_ctx.hdma_i2s2_rx, 0, sizeof(es8311_audio_ctx.hdma_i2s2_rx));
    es8311_audio_ctx.hdma_i2s2_rx.Instance = DMA1_Stream3;
    es8311_audio_ctx.hdma_i2s2_rx.Init.Channel = DMA_CHANNEL_3;
    es8311_audio_ctx.hdma_i2s2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    es8311_audio_ctx.hdma_i2s2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    es8311_audio_ctx.hdma_i2s2_rx.Init.MemInc = DMA_MINC_ENABLE;
    es8311_audio_ctx.hdma_i2s2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    es8311_audio_ctx.hdma_i2s2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    es8311_audio_ctx.hdma_i2s2_rx.Init.Mode = DMA_CIRCULAR;
    es8311_audio_ctx.hdma_i2s2_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    es8311_audio_ctx.hdma_i2s2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    hal_status = HAL_DMA_Init(&es8311_audio_ctx.hdma_i2s2_rx);
    if (hal_status != HAL_OK)
    {
        LOG_E("I2S RX DMA init failed");
        return -RT_ERROR;
    }

    rt_memset(&es8311_audio_ctx.hdma_i2s2_tx, 0, sizeof(es8311_audio_ctx.hdma_i2s2_tx));
    es8311_audio_ctx.hdma_i2s2_tx.Instance = DMA1_Stream4;
    es8311_audio_ctx.hdma_i2s2_tx.Init.Channel = DMA_CHANNEL_0;
    es8311_audio_ctx.hdma_i2s2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    es8311_audio_ctx.hdma_i2s2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    es8311_audio_ctx.hdma_i2s2_tx.Init.MemInc = DMA_MINC_ENABLE;
    es8311_audio_ctx.hdma_i2s2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    es8311_audio_ctx.hdma_i2s2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    es8311_audio_ctx.hdma_i2s2_tx.Init.Mode = DMA_CIRCULAR;
    es8311_audio_ctx.hdma_i2s2_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    es8311_audio_ctx.hdma_i2s2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    hal_status = HAL_DMA_Init(&es8311_audio_ctx.hdma_i2s2_tx);
    if (hal_status != HAL_OK)
    {
        LOG_E("I2S TX DMA init failed");
        return -RT_ERROR;
    }

    __HAL_LINKDMA(&hi2s2, hdmarx, es8311_audio_ctx.hdma_i2s2_rx);
    __HAL_LINKDMA(&hi2s2, hdmatx, es8311_audio_ctx.hdma_i2s2_tx);

    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    es8311_audio_ctx.dma_inited = RT_TRUE;
    return RT_EOK;
}

static rt_err_t es8311_audio_start_playback_dma(void)
{
    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_PLAYBACK)
    {
        return RT_EOK;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_STOPPED)
    {
        LOG_E("cannot start playback DMA while dma_mode=%d", es8311_audio_ctx.dma_mode);
        return -RT_EBUSY;
    }

    es8311_audio_clear_dma_buffers();
    // DMA 启动前先把两个 half 都预填好，避免一上来就发未初始化数据。
    es8311_audio_fill_tx_range(0u, ES8311_AUDIO_DMA_HALF_FRAMES);
    es8311_audio_fill_tx_range(ES8311_AUDIO_DMA_HALF_FRAMES, ES8311_AUDIO_DMA_HALF_FRAMES);
    if (HAL_I2S_Transmit_DMA(&hi2s2,
                             es8311_audio_dma_tx_buffer,
                             ES8311_AUDIO_DMA_TX_SAMPLES) != HAL_OK)
    {
        LOG_E("HAL_I2S_Transmit_DMA failed");
        return -RT_ERROR;
    }

    es8311_audio_ctx.dma_mode = ES8311_AUDIO_DMA_MODE_PLAYBACK;
    LOG_I("es8311 audio playback DMA started");
    return RT_EOK;
}

static rt_err_t es8311_audio_start_duplex_dma(void)
{
    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_DUPLEX)
    {
        return RT_EOK;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_STOPPED)
    {
        LOG_E("cannot start duplex DMA while dma_mode=%d", es8311_audio_ctx.dma_mode);
        return -RT_EBUSY;
    }

    es8311_audio_clear_dma_buffers();
    // 采集走全双工 DMA：TX 侧持续送静音/播放数据，RX 侧同步把 slot 采回来。
    if (HAL_I2SEx_TransmitReceive_DMA(&hi2s2,
                                      es8311_audio_dma_tx_buffer,
                                      es8311_audio_dma_rx_buffer,
                                      ES8311_AUDIO_DMA_TX_SAMPLES) != HAL_OK)
    {
        LOG_E("HAL_I2SEx_TransmitReceive_DMA failed");
        return -RT_ERROR;
    }

    es8311_audio_ctx.hdma_i2s2_rx.XferHalfCpltCallback = es8311_audio_rx_dma_half_callback;
    es8311_audio_ctx.hdma_i2s2_rx.XferCpltCallback = es8311_audio_rx_dma_full_callback;
    es8311_audio_ctx.hdma_i2s2_tx.XferHalfCpltCallback = es8311_audio_tx_dma_callback;
    es8311_audio_ctx.hdma_i2s2_tx.XferCpltCallback = es8311_audio_tx_dma_callback;

    es8311_audio_ctx.dma_mode = ES8311_AUDIO_DMA_MODE_DUPLEX;
    LOG_I("es8311 audio duplex DMA started");
    return RT_EOK;
}

static void es8311_audio_stop_dma(void)
{
    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_STOPPED)
    {
        return;
    }

    (void) HAL_I2S_DMAStop(&hi2s2);
    es8311_audio_ctx.dma_mode = ES8311_AUDIO_DMA_MODE_STOPPED;
}

static rt_err_t es8311_audio_i2s_reconfigure(rt_uint32_t sample_rate)
{
    uint32_t hal_sample_rate;
    rt_bool_t was_i2s_inited;
    es8311_audio_dma_mode_t was_dma_mode;
    rt_uint32_t old_sample_rate;

    hal_sample_rate = es8311_audio_sample_rate_to_hal(sample_rate);
    if (hal_sample_rate == 0u)
    {
        LOG_E("unsupported sample rate: %u", sample_rate);
        return -RT_EINVAL;
    }

    if (es8311_audio_ctx.i2s_inited && (es8311_audio_ctx.sample_rate == sample_rate))
    {
        return RT_EOK;
    }

    was_i2s_inited = es8311_audio_ctx.i2s_inited;
    was_dma_mode = es8311_audio_ctx.dma_mode;
    old_sample_rate = es8311_audio_ctx.sample_rate;

    // 所有采样率切换都统一走这里：
    // 先停 DMA，再重建 I2S，再把 codec 和运行态恢复回去。
    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_STOPPED)
    {
        es8311_audio_stop_dma();
    }

    if (es8311_audio_ctx.i2s_inited)
    {
        (void) HAL_I2S_DeInit(&hi2s2);
    }

    hi2s2.Instance = SPI2;
    hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    hi2s2.Init.AudioFreq = hal_sample_rate;
    hi2s2.Init.CPOL = I2S_CPOL_LOW;
    hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
    hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;

    if (HAL_I2S_Init(&hi2s2) != HAL_OK)
    {
        LOG_E("HAL_I2S_Init failed, sample_rate=%u", sample_rate);
        es8311_audio_ctx.i2s_inited = RT_FALSE;
        es8311_audio_ctx.dma_mode = ES8311_AUDIO_DMA_MODE_STOPPED;
        return -RT_ERROR;
    }

    es8311_audio_ctx.i2s_inited = RT_TRUE;
    es8311_audio_ctx.sample_rate = sample_rate;

    if (es8311_audio_dma_init() != RT_EOK)
    {
        es8311_audio_ctx.i2s_inited = was_i2s_inited;
        es8311_audio_ctx.sample_rate = old_sample_rate;
        return -RT_ERROR;
    }

    if (es8311_audio_apply_codec_state() != RT_EOK)
    {
        es8311_audio_ctx.i2s_inited = was_i2s_inited;
        es8311_audio_ctx.sample_rate = old_sample_rate;
        return -RT_ERROR;
    }

    if (es8311_audio_ctx.playback_running && !es8311_audio_ctx.playback_start_pending)
    {
        if (es8311_audio_start_playback_dma() != RT_EOK)
        {
            es8311_audio_ctx.i2s_inited = was_i2s_inited;
            es8311_audio_ctx.sample_rate = old_sample_rate;
            es8311_audio_ctx.dma_mode = was_dma_mode;
            return -RT_ERROR;
        }
    }
    else if (es8311_audio_ctx.capture_running)
    {
        if (es8311_audio_start_duplex_dma() != RT_EOK)
        {
            es8311_audio_ctx.i2s_inited = was_i2s_inited;
            es8311_audio_ctx.sample_rate = old_sample_rate;
            es8311_audio_ctx.dma_mode = was_dma_mode;
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

static rt_uint32_t es8311_audio_read_playback_frames(rt_uint16_t * target, rt_uint32_t frames)
{
    rt_uint32_t frame_index;
    rt_uint32_t read_frames;
    rt_base_t level;

    read_frames = 0u;

    level = rt_hw_interrupt_disable();
    for (frame_index = 0u; frame_index < frames; frame_index++)
    {
        rt_int16_t left_sample;
        rt_int16_t right_sample;

        if ((es8311_audio_ctx.playback_ring.level < ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS) ||
            !es8311_audio_ctx.playback_running ||
            es8311_audio_ctx.playback_start_pending)
        {
            break;
        }

        left_sample = es8311_audio_playback_buffer[es8311_audio_ctx.playback_ring.read];
        es8311_audio_ctx.playback_ring.read++;
        if (es8311_audio_ctx.playback_ring.read >= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.playback_ring.read = 0u;
        }

        right_sample = es8311_audio_playback_buffer[es8311_audio_ctx.playback_ring.read];
        es8311_audio_ctx.playback_ring.read++;
        if (es8311_audio_ctx.playback_ring.read >= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.playback_ring.read = 0u;
        }

        es8311_audio_ctx.playback_ring.level -= ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
        target[(rt_size_t) frame_index * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS] = (rt_uint16_t) left_sample;
        target[(rt_size_t) frame_index * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS + 1u] = (rt_uint16_t) right_sample;
        read_frames++;
    }
    rt_hw_interrupt_enable(level);

    return read_frames;
}

static void es8311_audio_fill_tx_range(rt_size_t offset_frames, rt_size_t frames)
{
    rt_uint32_t copied_frames;
    rt_size_t total_samples;
    rt_size_t copied_samples;
    rt_size_t sample_index;

    total_samples = frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
    copied_frames = es8311_audio_read_playback_frames(&es8311_audio_dma_tx_buffer[offset_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS],
                                                      (rt_uint32_t) frames);
    copied_samples = (rt_size_t) copied_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;

    if ((copied_frames < frames) && es8311_audio_ctx.playback_running &&
        !es8311_audio_ctx.playback_underflow_notice_printed)
    {
        es8311_audio_ctx.playback_underflow_notice_printed = RT_TRUE;
        LOG_W("playback underflow, TX outputs silence");
    }

    // ring 里的数据不够时，剩余部分补静音，保证 DMA 连续跑。
    for (sample_index = copied_samples; sample_index < total_samples; sample_index++)
    {
        es8311_audio_dma_tx_buffer[offset_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS + sample_index] = 0u;
    }
}

static void es8311_audio_write_capture_mono(const rt_uint16_t * source, rt_uint32_t frames)
{
    rt_uint32_t frame_index;
    rt_bool_t log_overflow;
    rt_base_t level;
    rt_uint8_t capture_slot;

    if (!es8311_audio_ctx.capture_running)
    {
        return;
    }

    if (!es8311_audio_ctx.capture_slot_locked)
    {
        es8311_audio_slot_stats_t left;
        es8311_audio_slot_stats_t right;
        rt_bool_t left_saturated;
        rt_bool_t right_saturated;

        es8311_audio_collect_slot_stats(source, frames, 0u, &left);
        es8311_audio_collect_slot_stats(source, frames, 1u, &right);
        left_saturated = es8311_audio_slot_stats_saturated(&left, frames);
        right_saturated = es8311_audio_slot_stats_saturated(&right, frames);
        // 当前采集最终导出的是 mono。
        // 这里先看左右 slot 的统计特征，选一条更像“有效音频”的路。
        es8311_audio_ctx.capture_slot = es8311_audio_pick_capture_slot(&left, &right, frames);

        if (!es8311_audio_ctx.capture_diag_printed)
        {
            es8311_audio_ctx.capture_diag_printed = RT_TRUE;
            LOG_I("capture slot diag: left[min=%d max=%d sat=%u zero=%u] right[min=%d max=%d sat=%u zero=%u] pick=%s",
                  left.min,
                  left.max,
                  left.saturated,
                  left.zero,
                  right.min,
                  right.max,
                  right.saturated,
                  right.zero,
                  (es8311_audio_ctx.capture_slot == 0u) ? "left" : "right");
        }

        if (left_saturated && right_saturated)
        {
            if (!es8311_audio_ctx.capture_saturated_notice_printed)
            {
                es8311_audio_ctx.capture_saturated_notice_printed = RT_TRUE;
                LOG_W("capture RX saturated on both slots, drop this DMA half");
            }
            level = rt_hw_interrupt_disable();
            es8311_audio_ctx.capture_drop_frames += frames;
            rt_hw_interrupt_enable(level);
            return;
        }

        es8311_audio_ctx.capture_slot_locked = RT_TRUE;
    }

    log_overflow = RT_FALSE;
    capture_slot = es8311_audio_ctx.capture_slot;
    level = rt_hw_interrupt_disable();
    for (frame_index = 0u; frame_index < frames; frame_index++)
    {
        rt_int16_t sample;

        if ((ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES - es8311_audio_ctx.capture_ring.level) <
            ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS)
        {
            es8311_audio_ctx.capture_drop_frames += (frames - frame_index);
            if (!es8311_audio_ctx.capture_overflow_notice_printed)
            {
                es8311_audio_ctx.capture_overflow_notice_printed = RT_TRUE;
                log_overflow = RT_TRUE;
            }
            break;
        }

        sample = (rt_int16_t) source[(rt_size_t) frame_index * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS + capture_slot];
        es8311_audio_capture_buffer[es8311_audio_ctx.capture_ring.write] = sample;
        es8311_audio_ctx.capture_ring.write++;
        if (es8311_audio_ctx.capture_ring.write >= ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.capture_ring.write = 0u;
        }

        es8311_audio_ctx.capture_ring.level += ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS;
    }
    rt_hw_interrupt_enable(level);

    if (log_overflow)
    {
        LOG_W("capture overflow, drop RX frames");
    }
}

static void es8311_audio_process_dma_half(rt_size_t offset_frames)
{
    // 同一个 DMA half 完成两件事：
    // 1. 给 TX half 补下一段播放数据
    // 2. 从 RX half 抽取一段采集数据
    es8311_audio_fill_tx_range(offset_frames, ES8311_AUDIO_DMA_HALF_FRAMES);
    es8311_audio_write_capture_mono(&es8311_audio_dma_rx_buffer[offset_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS],
                                    ES8311_AUDIO_DMA_HALF_FRAMES);
}

static void es8311_audio_rx_dma_half_callback(DMA_HandleTypeDef * hdma)
{
    if (hdma != &es8311_audio_ctx.hdma_i2s2_rx)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_DUPLEX)
    {
        return;
    }

    es8311_audio_process_dma_half(0u);
}

static void es8311_audio_rx_dma_full_callback(DMA_HandleTypeDef * hdma)
{
    if (hdma != &es8311_audio_ctx.hdma_i2s2_rx)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_DUPLEX)
    {
        return;
    }

    es8311_audio_process_dma_half(ES8311_AUDIO_DMA_HALF_FRAMES);
}

static void es8311_audio_tx_dma_callback(DMA_HandleTypeDef * hdma)
{
    RT_UNUSED(hdma);
}

rt_err_t es8311_audio_init(void)
{
    if (es8311_audio_ctx.inited)
    {
        return RT_EOK;
    }

    rt_memset(&es8311_audio_ctx, 0, sizeof(es8311_audio_ctx));

    if (es8311_init() != RT_EOK)
    {
        LOG_E("es8311_init failed");
        return -RT_ERROR;
    }

    es8311_audio_ctx.playback_channels = ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
    es8311_audio_ctx.sample_rate = ES8311_AUDIO_DEFAULT_SAMPLE_RATE;
    es8311_audio_ctx.inited = RT_TRUE;
    es8311_audio_reset_playback_ring_locked();
    es8311_audio_reset_capture_ring_locked();
    es8311_audio_clear_dma_buffers();

    if (es8311_audio_i2s_reconfigure(es8311_audio_ctx.sample_rate) != RT_EOK)
    {
        es8311_audio_ctx.inited = RT_FALSE;
        return -RT_ERROR;
    }

    LOG_I("es8311 audio init ok, sample_rate=%u", es8311_audio_ctx.sample_rate);
    return RT_EOK;
}

rt_bool_t es8311_audio_is_inited(void)
{
    rt_base_t level;
    rt_bool_t inited;

    level = rt_hw_interrupt_disable();
    inited = es8311_audio_ctx.inited;
    rt_hw_interrupt_enable(level);

    return inited;
}

rt_err_t es8311_audio_configure(rt_uint32_t sample_rate, rt_uint8_t playback_channels)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    if ((sample_rate == 0u) || (playback_channels == 0u) || (playback_channels > ES8311_AUDIO_MAX_INPUT_CHANNELS))
    {
        return -RT_EINVAL;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.playback_channels = playback_channels;
    es8311_audio_reset_playback_ring_locked();
    es8311_audio_reset_capture_ring_locked();
    rt_hw_interrupt_enable(level);

    return es8311_audio_i2s_reconfigure(sample_rate);
}

const char * es8311_audio_run_mode_name(es8311_audio_run_mode_t mode)
{
    switch (mode)
    {
    case ES8311_AUDIO_RUN_MODE_IDLE:
        return "idle";
    case ES8311_AUDIO_RUN_MODE_PLAYBACK:
        return "playback";
    case ES8311_AUDIO_RUN_MODE_CAPTURE:
        return "capture";
    default:
        return "unknown";
    }
}

es8311_audio_run_mode_t es8311_audio_get_run_mode(void)
{
    rt_base_t level;
    es8311_audio_run_mode_t mode;

    level = rt_hw_interrupt_disable();
    if (es8311_audio_ctx.capture_running)
    {
        mode = ES8311_AUDIO_RUN_MODE_CAPTURE;
    }
    else if (es8311_audio_ctx.playback_running)
    {
        mode = ES8311_AUDIO_RUN_MODE_PLAYBACK;
    }
    else
    {
        mode = ES8311_AUDIO_RUN_MODE_IDLE;
    }
    rt_hw_interrupt_enable(level);

    return mode;
}

rt_err_t es8311_audio_set_run_mode(es8311_audio_run_mode_t mode)
{
    rt_err_t err;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    switch (mode)
    {
    case ES8311_AUDIO_RUN_MODE_IDLE:
        es8311_audio_stop_playback();
        es8311_audio_stop_capture();
        es8311_audio_flush_playback();
        es8311_audio_flush_capture();
        LOG_I("manual audio mode switched to idle");
        return RT_EOK;

    case ES8311_AUDIO_RUN_MODE_PLAYBACK:
        if (es8311_audio_is_playback_running())
        {
            return RT_EOK;
        }

        // 当前实现里 playback/capture 是互斥的，切模式时直接清空对侧缓存。
        es8311_audio_stop_capture();
        es8311_audio_flush_capture();
        err = es8311_audio_start_playback();
        if (err == RT_EOK)
        {
            LOG_I("manual audio mode switched to playback");
        }
        return err;

    case ES8311_AUDIO_RUN_MODE_CAPTURE:
        if (es8311_audio_is_capture_running())
        {
            return RT_EOK;
        }

        es8311_audio_stop_playback();
        es8311_audio_flush_playback();
        // capture 目前固定回到默认采样率，先保证链路简单稳定。
        if (es8311_audio_i2s_reconfigure(ES8311_AUDIO_DEFAULT_SAMPLE_RATE) != RT_EOK)
        {
            LOG_E("switch capture sample rate failed: %u", ES8311_AUDIO_DEFAULT_SAMPLE_RATE);
            return -RT_ERROR;
        }
        err = es8311_audio_start_capture();
        if (err == RT_EOK)
        {
            LOG_I("manual audio mode switched to capture");
        }
        return err;

    default:
        return -RT_EINVAL;
    }
}

rt_err_t es8311_audio_start_playback(void)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (es8311_audio_ctx.capture_running)
    {
        LOG_W("stop capture before playback start");
        es8311_audio_stop_capture();
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.playback_running = RT_TRUE;
    // 播放不是一 start 就立刻起 DMA，而是先攒到阈值，
    // 这样能减少刚起播时 ring 太浅导致的连续 underflow。
    es8311_audio_ctx.playback_start_pending = RT_TRUE;
    es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
    rt_hw_interrupt_enable(level);

    if (es8311_start_playback() != RT_EOK)
    {
        LOG_E("es8311_start_playback failed");
        level = rt_hw_interrupt_disable();
        es8311_audio_ctx.playback_running = RT_FALSE;
        es8311_audio_reset_playback_ring_locked();
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    LOG_I("playback waiting PCM before DMA start, threshold_frames=%u",
          ES8311_AUDIO_PLAYBACK_START_THRESHOLD_FRAMES);
    return RT_EOK;
}

void es8311_audio_stop_playback(void)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.playback_running = RT_FALSE;
    es8311_audio_reset_playback_ring_locked();
    rt_hw_interrupt_enable(level);

    (void) es8311_stop_playback();
    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_PLAYBACK)
    {
        es8311_audio_stop_dma();
    }
}

void es8311_audio_flush_playback(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    es8311_audio_reset_playback_ring_locked();
    rt_hw_interrupt_enable(level);
}

rt_uint32_t es8311_audio_write_playback_checked(const rt_int16_t * pcm,
                                                rt_uint32_t frames,
                                                rt_uint8_t channels,
                                                rt_uint32_t sample_rate,
                                                es8311_audio_playback_write_status_t * status)
{
    rt_uint32_t frame_index;
    rt_uint32_t start_frame;
    rt_uint32_t frames_to_write;
    rt_uint32_t written_frames;
    rt_bool_t overflow_happened;
    rt_bool_t log_overflow;
    rt_bool_t start_dma;
    rt_base_t level;

    if (status != RT_NULL)
    {
        *status = ES8311_AUDIO_PLAYBACK_WRITE_OK;
    }

    if ((pcm == RT_NULL) || (frames == 0u))
    {
        if (status != RT_NULL)
        {
            *status = ES8311_AUDIO_PLAYBACK_WRITE_INVALID_ARGUMENT;
        }
        return 0u;
    }

    if (!es8311_audio_ctx.inited)
    {
        if (status != RT_NULL)
        {
            *status = ES8311_AUDIO_PLAYBACK_WRITE_NOT_INITED;
        }
        return 0u;
    }

    if (!es8311_audio_ctx.playback_running)
    {
        if (status != RT_NULL)
        {
            *status = ES8311_AUDIO_PLAYBACK_WRITE_NOT_RUNNING;
        }
        return 0u;
    }

    if ((channels == 0u) || (channels > ES8311_AUDIO_MAX_INPUT_CHANNELS))
    {
        if (status != RT_NULL)
        {
            *status = ES8311_AUDIO_PLAYBACK_WRITE_INVALID_FORMAT;
        }
        return 0u;
    }

    if (sample_rate != es8311_audio_ctx.sample_rate)
    {
        if (status != RT_NULL)
        {
            *status = ES8311_AUDIO_PLAYBACK_WRITE_SAMPLE_RATE_MISMATCH;
        }
        return 0u;
    }

    written_frames = 0u;
    overflow_happened = RT_FALSE;
    log_overflow = RT_FALSE;
    start_dma = RT_FALSE;
    start_frame = 0u;
    frames_to_write = frames;
    if (frames_to_write > ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES)
    {
        // 单次写入过大时，只保留尾部最新一段，避免陈旧 PCM 拉高延迟。
        start_frame = frames_to_write - ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES;
        frames_to_write = ES8311_AUDIO_PLAYBACK_BUFFER_FRAMES;
        overflow_happened = RT_TRUE;
    }

    level = rt_hw_interrupt_disable();
    if (frames_to_write > 0u)
    {
        rt_uint32_t free_frames;

        free_frames = (rt_uint32_t) ((ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES -
                                      es8311_audio_ctx.playback_ring.level) /
                                     ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS);
        if (free_frames < frames_to_write)
        {
            // ring 满了优先丢最旧的数据，保持“尽量播放最新 PCM”的策略。
            es8311_audio_drop_oldest_playback_frames_locked(frames_to_write - free_frames);
            overflow_happened = RT_TRUE;
        }
    }

    for (frame_index = start_frame; frame_index < (start_frame + frames_to_write); frame_index++)
    {
        rt_int16_t left_sample;
        rt_int16_t right_sample;

        if ((ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES - es8311_audio_ctx.playback_ring.level) <
            ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS)
        {
            overflow_happened = RT_TRUE;
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

        es8311_audio_playback_buffer[es8311_audio_ctx.playback_ring.write] = left_sample;
        es8311_audio_ctx.playback_ring.write++;
        if (es8311_audio_ctx.playback_ring.write >= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.playback_ring.write = 0u;
        }

        es8311_audio_playback_buffer[es8311_audio_ctx.playback_ring.write] = right_sample;
        es8311_audio_ctx.playback_ring.write++;
        if (es8311_audio_ctx.playback_ring.write >= ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.playback_ring.write = 0u;
        }

        es8311_audio_ctx.playback_ring.level += ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
        written_frames++;
    }

    if (es8311_audio_ctx.playback_start_pending &&
        (es8311_audio_ctx.playback_ring.level / ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS >=
         ES8311_AUDIO_PLAYBACK_START_THRESHOLD_FRAMES))
    {
        // 达到起播水位后，再真正拉起 DMA。
        es8311_audio_ctx.playback_start_pending = RT_FALSE;
        es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
        start_dma = RT_TRUE;
    }

    if (overflow_happened && !es8311_audio_ctx.playback_overflow_notice_printed)
    {
        es8311_audio_ctx.playback_overflow_notice_printed = RT_TRUE;
        log_overflow = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    if (log_overflow)
    {
        LOG_W("playback ring overflow, drop audio frames");
    }

    if (start_dma)
    {
        if (es8311_audio_start_playback_dma() != RT_EOK)
        {
            level = rt_hw_interrupt_disable();
            es8311_audio_ctx.playback_running = RT_FALSE;
            es8311_audio_reset_playback_ring_locked();
            rt_hw_interrupt_enable(level);
            if (status != RT_NULL)
            {
                *status = ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL;
            }
            return 0u;
        }
    }

    if ((written_frames == 0u) && (frames > 0u) && (status != RT_NULL))
    {
        *status = ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL;
    }

    return written_frames;
}

rt_uint32_t es8311_audio_write_playback(const rt_int16_t * pcm,
                                        rt_uint32_t frames,
                                        rt_uint8_t channels,
                                        rt_uint32_t sample_rate)
{
    return es8311_audio_write_playback_checked(pcm, frames, channels, sample_rate, RT_NULL);
}

rt_uint32_t es8311_audio_get_playback_level_frames(void)
{
    rt_base_t level;
    rt_uint32_t frames;

    level = rt_hw_interrupt_disable();
    frames = (rt_uint32_t) (es8311_audio_ctx.playback_ring.level / ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS);
    rt_hw_interrupt_enable(level);

    return frames;
}

rt_uint32_t es8311_audio_get_playback_free_frames(void)
{
    rt_base_t level;
    rt_uint32_t frames;

    level = rt_hw_interrupt_disable();
    frames = (rt_uint32_t) ((ES8311_AUDIO_PLAYBACK_BUFFER_SAMPLES - es8311_audio_ctx.playback_ring.level) /
                            ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS);
    rt_hw_interrupt_enable(level);

    return frames;
}

rt_uint32_t es8311_audio_get_sample_rate(void)
{
    rt_base_t level;
    rt_uint32_t sample_rate;

    level = rt_hw_interrupt_disable();
    sample_rate = es8311_audio_ctx.sample_rate;
    rt_hw_interrupt_enable(level);

    return sample_rate;
}

rt_bool_t es8311_audio_is_playback_running(void)
{
    rt_base_t level;
    rt_bool_t running;

    level = rt_hw_interrupt_disable();
    running = es8311_audio_ctx.playback_running;
    rt_hw_interrupt_enable(level);

    return running;
}

rt_err_t es8311_audio_start_capture(void)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (es8311_audio_ctx.playback_running)
    {
        LOG_W("capture start rejected while playback is running");
        return -RT_EBUSY;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_reset_capture_ring_locked();
    rt_hw_interrupt_enable(level);

    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_STOPPED)
    {
        if (es8311_audio_start_duplex_dma() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    if (es8311_start_record() != RT_EOK)
    {
        LOG_E("es8311_start_record failed");
        es8311_audio_stop_dma();
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.capture_running = RT_TRUE;
    rt_hw_interrupt_enable(level);

    return RT_EOK;
}

void es8311_audio_stop_capture(void)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.capture_running = RT_FALSE;
    es8311_audio_reset_capture_ring_locked();
    rt_hw_interrupt_enable(level);

    (void) es8311_stop_record();
    if ((es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_DUPLEX) &&
        !es8311_audio_ctx.playback_running)
    {
        es8311_audio_stop_dma();
    }
}

void es8311_audio_flush_capture(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    es8311_audio_reset_capture_ring_locked();
    rt_hw_interrupt_enable(level);
}

rt_uint32_t es8311_audio_read_capture(rt_int16_t * pcm, rt_uint32_t max_frames)
{
    rt_uint32_t read_frames;
    rt_base_t level;

    if ((pcm == RT_NULL) || (max_frames == 0u))
    {
        return 0u;
    }

    read_frames = 0u;
    level = rt_hw_interrupt_disable();
    while ((read_frames < max_frames) &&
           (es8311_audio_ctx.capture_ring.level >= ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS))
    {
        pcm[read_frames] = es8311_audio_capture_buffer[es8311_audio_ctx.capture_ring.read];
        es8311_audio_ctx.capture_ring.read++;
        if (es8311_audio_ctx.capture_ring.read >= ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES)
        {
            es8311_audio_ctx.capture_ring.read = 0u;
        }

        es8311_audio_ctx.capture_ring.level -= ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS;
        read_frames++;
    }
    rt_hw_interrupt_enable(level);

    return read_frames;
}

rt_uint32_t es8311_audio_get_capture_level_frames(void)
{
    rt_base_t level;
    rt_uint32_t frames;

    level = rt_hw_interrupt_disable();
    frames = (rt_uint32_t) (es8311_audio_ctx.capture_ring.level / ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS);
    rt_hw_interrupt_enable(level);

    return frames;
}

rt_uint32_t es8311_audio_get_capture_drop_frames(void)
{
    rt_base_t level;
    rt_uint32_t frames;

    level = rt_hw_interrupt_disable();
    frames = es8311_audio_ctx.capture_drop_frames;
    rt_hw_interrupt_enable(level);

    return frames;
}

rt_bool_t es8311_audio_get_capture_format(es8311_audio_capture_format_t * format)
{
    rt_base_t level;

    if (format == RT_NULL)
    {
        return RT_FALSE;
    }

    level = rt_hw_interrupt_disable();
    format->sample_rate = es8311_audio_ctx.sample_rate;
    format->channels = ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS;
    rt_hw_interrupt_enable(level);

    return es8311_audio_ctx.inited;
}

rt_bool_t es8311_audio_is_capture_running(void)
{
    rt_base_t level;
    rt_bool_t running;

    level = rt_hw_interrupt_disable();
    running = es8311_audio_ctx.capture_running;
    rt_hw_interrupt_enable(level);

    return running;
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_DUPLEX)
    {
        return;
    }

    es8311_audio_process_dma_half(0u);
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_DUPLEX)
    {
        return;
    }

    es8311_audio_process_dma_half(ES8311_AUDIO_DMA_HALF_FRAMES);
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_PLAYBACK)
    {
        return;
    }

    es8311_audio_fill_tx_range(0u, ES8311_AUDIO_DMA_HALF_FRAMES);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (es8311_audio_ctx.dma_mode != ES8311_AUDIO_DMA_MODE_PLAYBACK)
    {
        return;
    }

    es8311_audio_fill_tx_range(ES8311_AUDIO_DMA_HALF_FRAMES, ES8311_AUDIO_DMA_HALF_FRAMES);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    LOG_E("HAL_I2S error: 0x%08x", hi2s->ErrorCode);
}

void DMA1_Stream3_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&es8311_audio_ctx.hdma_i2s2_rx);
    rt_interrupt_leave();
}

void DMA1_Stream4_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&es8311_audio_ctx.hdma_i2s2_tx);
    rt_interrupt_leave();
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
    rt_bool_t playback_started = RT_FALSE;
    rt_err_t result = RT_EOK;

    if (es8311_audio_configure(BOOT_PROMPT_SAMPLE_RATE, BOOT_PROMPT_CHANNELS) != RT_EOK)
    {
        result = -RT_ERROR;
        goto exit;
    }

    if (es8311_audio_start_playback() != RT_EOK)
    {
        result = -RT_ERROR;
        goto exit;
    }
    playback_started = RT_TRUE;

    while (offset < boot_prompt_pcm_len)
    {
        rt_uint32_t free_frames;
        rt_uint32_t frames;
        rt_uint32_t written;
        rt_uint32_t frame_index;
        es8311_audio_playback_write_status_t status;

        free_frames = es8311_audio_get_playback_free_frames();
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

        written = es8311_audio_write_playback_checked(scaled_pcm,
                                                      frames,
                                                      BOOT_PROMPT_CHANNELS,
                                                      BOOT_PROMPT_SAMPLE_RATE,
                                                      &status);
        if (written == 0u)
        {
            if (status == ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL)
            {
                rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
                continue;
            }

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
        es8311_audio_playback_write_status_t status;

        free_frames = es8311_audio_get_playback_free_frames();
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

        written = es8311_audio_write_playback_checked(boot_prompt_silence,
                                                      frames,
                                                      BOOT_PROMPT_CHANNELS,
                                                      BOOT_PROMPT_SAMPLE_RATE,
                                                      &status);
        if (written == 0u)
        {
            if (status == ES8311_AUDIO_PLAYBACK_WRITE_BUFFER_FULL)
            {
                rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
                continue;
            }

            result = -RT_ERROR;
            goto stop_playback;
        }

        tail_offset += written;
    }

    while (es8311_audio_get_playback_level_frames() > BOOT_PROMPT_STOP_LEVEL_FRAMES)
    {
        rt_thread_mdelay(BOOT_PROMPT_WAIT_MS);
    }

stop_playback:
    if (playback_started)
    {
        es8311_audio_stop_playback();
        es8311_audio_flush_playback();
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
