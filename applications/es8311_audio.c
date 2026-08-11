/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "es8311_audio.h"

#include "es8311_driver.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_i2s_ex.h"

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
#define ES8311_AUDIO_CAPTURE_BUFFER_FRAMES           8192u
#define ES8311_AUDIO_CAPTURE_BUFFER_SAMPLES          (ES8311_AUDIO_CAPTURE_BUFFER_FRAMES * ES8311_AUDIO_CAPTURE_OUTPUT_CHANNELS)

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
// - Mixer playback renderer / capture ring buffer
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
    rt_bool_t capture_overflow_notice_printed;
    rt_uint32_t capture_drop_frames;
    rt_uint32_t sample_rate;
    rt_uint8_t volume_0_127;
    rt_uint8_t capture_slot;
    rt_bool_t capture_slot_locked;
    rt_bool_t capture_diag_printed;
    rt_bool_t capture_saturated_notice_printed;
    es8311_audio_ring_t capture_ring;
    es8311_audio_playback_renderer_t playback_renderer;
    void * playback_renderer_context;
} es8311_audio_context_t;

static es8311_audio_context_t es8311_audio_ctx;
/* dma_tx/rx 是 I2S DMA 直接搬运的缓冲,必须留在主 RAM(CCM 对 DMA 不可见) */
static rt_uint16_t es8311_audio_dma_tx_buffer[ES8311_AUDIO_DMA_TX_SAMPLES];
static rt_uint16_t es8311_audio_dma_rx_buffer[ES8311_AUDIO_DMA_RX_SAMPLES];
/* 采集环形缓冲要扛住 littlefs/串口短时阻塞，放主 SRAM 并适当加大。 */
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

static void es8311_audio_reset_playback_state_locked(void)
{
    es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
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

static void es8311_audio_fill_tx_range(rt_size_t offset_frames, rt_size_t frames)
{
    rt_uint32_t rendered_frames;
    rt_size_t total_samples;
    rt_size_t rendered_samples;
    rt_size_t sample_index;
    rt_int16_t * target;

    total_samples = frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;
    target = (rt_int16_t *) &es8311_audio_dma_tx_buffer[offset_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS];
    rendered_frames = 0u;

    if (es8311_audio_ctx.playback_running &&
        !es8311_audio_ctx.playback_start_pending &&
        (es8311_audio_ctx.playback_renderer != RT_NULL))
    {
        rendered_frames = es8311_audio_ctx.playback_renderer(target,
                                                             (rt_uint32_t) frames,
                                                             es8311_audio_ctx.playback_renderer_context);
        if (rendered_frames > frames)
        {
            rendered_frames = (rt_uint32_t) frames;
        }
    }
    rendered_samples = (rt_size_t) rendered_frames * ES8311_AUDIO_PLAYBACK_OUTPUT_CHANNELS;

    if ((rendered_frames < frames) && es8311_audio_ctx.playback_running &&
        !es8311_audio_ctx.playback_underflow_notice_printed)
    {
        es8311_audio_ctx.playback_underflow_notice_printed = RT_TRUE;
        LOG_W("playback renderer underflow, TX outputs silence");
    }

    /* renderer 不足或尚未注册时补静音，保证 DMA 连续运行。 */
    for (sample_index = rendered_samples; sample_index < total_samples; sample_index++)
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


/* 最终播放主音量 0~127 映射到 ES8311 DAC 寄存器。
 * 0x00 最小，0xBF 约 0dB；超过 0xBF 的正增益先不用。
 * 默认保持满量程；AVRCP 音量在 Mixer 的 A2DP 背景源上处理，
 * 避免手机音量同时衰减 AI 回复语音。 */
#define ES8311_AUDIO_VOLUME_MAX           127u
#define ES8311_AUDIO_DAC_REG_MAX          0xBFu
#define ES8311_AUDIO_DEFAULT_VOLUME       127u

static rt_uint8_t es8311_audio_map_volume_to_dac_reg(rt_uint8_t volume_0_127)
{
    rt_uint32_t mapped;

    if (volume_0_127 > ES8311_AUDIO_VOLUME_MAX)
    {
        volume_0_127 = ES8311_AUDIO_VOLUME_MAX;
    }

    mapped = ((rt_uint32_t) volume_0_127 * ES8311_AUDIO_DAC_REG_MAX) / ES8311_AUDIO_VOLUME_MAX;
    return (rt_uint8_t) mapped;
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

    es8311_audio_ctx.sample_rate = ES8311_AUDIO_DEFAULT_SAMPLE_RATE;
    es8311_audio_ctx.volume_0_127 = ES8311_AUDIO_DEFAULT_VOLUME;
    if (es8311_set_dac_volume(es8311_audio_map_volume_to_dac_reg(es8311_audio_ctx.volume_0_127)) != RT_EOK)
    {
        LOG_W("es8311 default dac volume apply failed");
    }
    es8311_audio_ctx.inited = RT_TRUE;
    es8311_audio_reset_playback_state_locked();
    es8311_audio_reset_capture_ring_locked();
    es8311_audio_clear_dma_buffers();

    if (es8311_audio_i2s_reconfigure(es8311_audio_ctx.sample_rate) != RT_EOK)
    {
        es8311_audio_ctx.inited = RT_FALSE;
        return -RT_ERROR;
    }

    LOG_I("es8311 audio init ok, sample_rate=%u, volume=%u/127, dac_reg=0x%02x",
          es8311_audio_ctx.sample_rate,
          es8311_audio_ctx.volume_0_127,
          es8311_audio_map_volume_to_dac_reg(es8311_audio_ctx.volume_0_127));
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

rt_err_t es8311_audio_set_volume(rt_uint8_t volume_0_127)
{
    rt_uint8_t dac_reg;
    rt_err_t err;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (volume_0_127 > ES8311_AUDIO_VOLUME_MAX)
    {
        volume_0_127 = ES8311_AUDIO_VOLUME_MAX;
    }

    dac_reg = es8311_audio_map_volume_to_dac_reg(volume_0_127);
    err = es8311_set_dac_volume(dac_reg);
    if (err != RT_EOK)
    {
        return err;
    }

    es8311_audio_ctx.volume_0_127 = volume_0_127;
    LOG_I("playback master volume=%u/127, dac_reg=0x%02x",
          volume_0_127,
          dac_reg);
    return RT_EOK;
}

rt_uint8_t es8311_audio_get_volume(void)
{
    return es8311_audio_ctx.volume_0_127;
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

    if (es8311_audio_ctx.playback_running)
    {
        return RT_EOK;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.playback_running = RT_TRUE;
    /* 播放先进入 pending，由 Mixer 根据当前音源的起播水位拉起 DMA。 */
    es8311_audio_ctx.playback_start_pending = RT_TRUE;
    es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
    rt_hw_interrupt_enable(level);

    if (es8311_start_playback() != RT_EOK)
    {
        LOG_E("es8311_start_playback failed");
        level = rt_hw_interrupt_disable();
        es8311_audio_ctx.playback_running = RT_FALSE;
        es8311_audio_reset_playback_state_locked();
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }

    LOG_I("playback waiting for mixer data before DMA start");
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
    es8311_audio_reset_playback_state_locked();
    rt_hw_interrupt_enable(level);

    (void) es8311_stop_playback();
    if (es8311_audio_ctx.dma_mode == ES8311_AUDIO_DMA_MODE_PLAYBACK)
    {
        es8311_audio_stop_dma();
    }
}

rt_err_t es8311_audio_set_playback_renderer(es8311_audio_playback_renderer_t renderer,
                                            void * context)
{
    rt_base_t level;

    if (!es8311_audio_ctx.inited || (renderer == RT_NULL))
    {
        return -RT_EINVAL;
    }
    if (es8311_audio_ctx.playback_running)
    {
        return -RT_EBUSY;
    }

    level = rt_hw_interrupt_disable();
    es8311_audio_ctx.playback_renderer = renderer;
    es8311_audio_ctx.playback_renderer_context = context;
    rt_hw_interrupt_enable(level);
    return RT_EOK;
}

rt_err_t es8311_audio_notify_playback_ready(void)
{
    rt_bool_t start_dma;
    rt_base_t level;

    if (!es8311_audio_ctx.inited)
    {
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    start_dma = (rt_bool_t) (es8311_audio_ctx.playback_running &&
                             es8311_audio_ctx.playback_start_pending &&
                             (es8311_audio_ctx.playback_renderer != RT_NULL));
    if (start_dma)
    {
        es8311_audio_ctx.playback_start_pending = RT_FALSE;
        es8311_audio_ctx.playback_underflow_notice_printed = RT_FALSE;
    }
    rt_hw_interrupt_enable(level);

    if (!start_dma)
    {
        return RT_EOK;
    }

    if (es8311_audio_start_playback_dma() != RT_EOK)
    {
        level = rt_hw_interrupt_disable();
        es8311_audio_ctx.playback_running = RT_FALSE;
        es8311_audio_reset_playback_state_locked();
        rt_hw_interrupt_enable(level);
        return -RT_ERROR;
    }
    return RT_EOK;
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
    rt_hw_interrupt_enable(level);

    /* stop 只停硬件采集，ring 留给导出线程收尾；flush_capture 才清缓存/统计。 */
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
