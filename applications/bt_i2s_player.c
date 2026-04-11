/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     26410       the first version
 */
#include "bt_i2s_player.h"

#include "bt_a2dp_audio.h"
#include "main.h"
#include "wm9878_driver.h"

#define DBG_TAG "bt_i2s"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_I2S_PLAYER_OUTPUT_CHANNELS            2u
#define BT_I2S_PLAYER_DMA_HALF_FRAMES            512u
#define BT_I2S_PLAYER_DMA_BUFFER_FRAMES          (BT_I2S_PLAYER_DMA_HALF_FRAMES * 2u)
#define BT_I2S_PLAYER_DMA_BUFFER_SAMPLES         (BT_I2S_PLAYER_DMA_BUFFER_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
#define BT_I2S_PLAYER_DMA_HALF_SAMPLES           (BT_I2S_PLAYER_DMA_HALF_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
#define BT_I2S_PLAYER_RING_BUFFER_FRAMES         8192u
#define BT_I2S_PLAYER_RING_BUFFER_SAMPLES        (BT_I2S_PLAYER_RING_BUFFER_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
// 等积累到 4 个 DMA buffer 再启动，优先换取更稳的起播和更少的抖动噪声。
#define BT_I2S_PLAYER_START_THRESHOLD_FRAMES     (BT_I2S_PLAYER_DMA_BUFFER_FRAMES * 6u)
#define BT_I2S_PLAYER_START_THRESHOLD_SAMPLES    (BT_I2S_PLAYER_START_THRESHOLD_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
#define BT_I2S_PLAYER_PRIME_TIMEOUT_MS           50u

extern I2S_HandleTypeDef hi2s2;

typedef struct
{
    DMA_HandleTypeDef hdma_i2s2_tx;
    rt_bool_t inited;
    rt_bool_t dma_inited;
    rt_bool_t i2s_inited;
    rt_bool_t playback_started;
    rt_bool_t start_pending;
    rt_bool_t overflow_notice_printed;
    rt_bool_t underflow_notice_printed;
    rt_bool_t first_pcm_logged;
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
    rt_size_t ring_read;
    rt_size_t ring_write;
    rt_size_t ring_level;
} bt_i2s_player_context_t;

static bt_i2s_player_context_t bt_i2s_player_ctx;
static rt_int16_t bt_i2s_player_ring_buffer[BT_I2S_PLAYER_RING_BUFFER_SAMPLES];
static rt_uint16_t bt_i2s_player_dma_buffer[BT_I2S_PLAYER_DMA_BUFFER_SAMPLES];

static uint32_t bt_i2s_player_sample_rate_to_hal(rt_uint32_t sample_rate)
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

static void bt_i2s_player_clear_dma_buffer(void)
{
    rt_memset(bt_i2s_player_dma_buffer, 0, sizeof(bt_i2s_player_dma_buffer));
}

void bt_i2s_player_reset_buffer(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    bt_i2s_player_ctx.ring_read = 0u;
    bt_i2s_player_ctx.ring_write = 0u;
    bt_i2s_player_ctx.ring_level = 0u;
    bt_i2s_player_ctx.overflow_notice_printed = RT_FALSE;
    bt_i2s_player_ctx.underflow_notice_printed = RT_FALSE;
    bt_i2s_player_ctx.first_pcm_logged = RT_FALSE;
    rt_hw_interrupt_enable(level);
}

void bt_i2s_player_flush(void)
{
    bt_i2s_player_reset_buffer();
}

static rt_err_t bt_i2s_player_dma_init(void)
{
    HAL_StatusTypeDef hal_status;

    if (bt_i2s_player_ctx.dma_inited)
    {
        __HAL_LINKDMA(&hi2s2, hdmatx, bt_i2s_player_ctx.hdma_i2s2_tx);
        return RT_EOK;
    }

    __HAL_RCC_DMA1_CLK_ENABLE();

    rt_memset(&bt_i2s_player_ctx.hdma_i2s2_tx, 0, sizeof(bt_i2s_player_ctx.hdma_i2s2_tx));
    bt_i2s_player_ctx.hdma_i2s2_tx.Instance = DMA1_Stream4;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.Channel = DMA_CHANNEL_0;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.MemInc = DMA_MINC_ENABLE;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.Mode = DMA_CIRCULAR;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    bt_i2s_player_ctx.hdma_i2s2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    hal_status = HAL_DMA_Init(&bt_i2s_player_ctx.hdma_i2s2_tx);
    if (hal_status != HAL_OK)
    {
        LOG_E("I2S DMA init failed");
        return -RT_ERROR;
    }

    __HAL_LINKDMA(&hi2s2, hdmatx, bt_i2s_player_ctx.hdma_i2s2_tx);
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

    bt_i2s_player_ctx.dma_inited = RT_TRUE;
    return RT_EOK;
}

static void bt_i2s_player_refill_dma_range(rt_size_t offset, rt_size_t samples)
{
    rt_size_t copied;
    rt_base_t level;

    copied = 0u;

    level = rt_hw_interrupt_disable();
    while ((copied < samples) && (bt_i2s_player_ctx.ring_level > 0u))
    {
        bt_i2s_player_dma_buffer[offset + copied] =
            (rt_uint16_t) bt_i2s_player_ring_buffer[bt_i2s_player_ctx.ring_read];
        bt_i2s_player_ctx.ring_read++;
        if (bt_i2s_player_ctx.ring_read >= BT_I2S_PLAYER_RING_BUFFER_SAMPLES)
        {
            bt_i2s_player_ctx.ring_read = 0u;
        }
        bt_i2s_player_ctx.ring_level--;
        copied++;
    }
    rt_hw_interrupt_enable(level);

    while (copied < samples)
    {
        if (bt_i2s_player_ctx.playback_started && !bt_i2s_player_ctx.underflow_notice_printed)
        {
            bt_i2s_player_ctx.underflow_notice_printed = RT_TRUE;
            LOG_W("I2S DMA underflow, output silence until PCM buffer refills");
        }
        bt_i2s_player_dma_buffer[offset + copied] = 0u;
        copied++;
    }
}

static void bt_i2s_player_prefill_dma_buffer(void)
{
    bt_i2s_player_refill_dma_range(0u, BT_I2S_PLAYER_DMA_HALF_SAMPLES);
    bt_i2s_player_refill_dma_range(BT_I2S_PLAYER_DMA_HALF_SAMPLES,
                                   BT_I2S_PLAYER_DMA_HALF_SAMPLES);
}

static rt_bool_t bt_i2s_player_can_start_dma(void)
{
    rt_base_t level;
    rt_bool_t ready;

    level = rt_hw_interrupt_disable();
    ready = (rt_bool_t) (bt_i2s_player_ctx.start_pending &&
                         !bt_i2s_player_ctx.playback_started &&
                         (bt_i2s_player_ctx.ring_level >= BT_I2S_PLAYER_START_THRESHOLD_SAMPLES));
    rt_hw_interrupt_enable(level);
    return ready;
}

static rt_err_t bt_i2s_player_try_start_dma(void)
{
    rt_base_t level;

    if (!bt_i2s_player_can_start_dma())
    {
        return RT_EOK;
    }

    bt_i2s_player_prefill_dma_buffer();
    if (HAL_I2S_Transmit_DMA(&hi2s2,
                             bt_i2s_player_dma_buffer,
                             BT_I2S_PLAYER_DMA_BUFFER_SAMPLES) != HAL_OK)
    {
        LOG_E("HAL_I2S_Transmit_DMA failed");
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();
    bt_i2s_player_ctx.playback_started = RT_TRUE;
    bt_i2s_player_ctx.start_pending = RT_FALSE;
    rt_hw_interrupt_enable(level);

    LOG_I("bt_i2s_player started");
    return RT_EOK;
}

static rt_err_t bt_i2s_player_prime_i2s_clock(void)
{
    bt_i2s_player_clear_dma_buffer();
    if (HAL_I2S_Transmit(&hi2s2,
                         bt_i2s_player_dma_buffer,
                         BT_I2S_PLAYER_DMA_HALF_SAMPLES,
                         BT_I2S_PLAYER_PRIME_TIMEOUT_MS) != HAL_OK)
    {
        LOG_W("I2S clock prime failed");
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t bt_i2s_player_i2s_reconfigure(rt_uint32_t sample_rate)
{
    uint32_t hal_sample_rate;
    rt_bool_t need_restart;

    hal_sample_rate = bt_i2s_player_sample_rate_to_hal(sample_rate);
    if (hal_sample_rate == 0u)
    {
        LOG_E("unsupported sample rate: %u", sample_rate);
        return -RT_EINVAL;
    }

    if (bt_i2s_player_ctx.i2s_inited && (bt_i2s_player_ctx.sample_rate == sample_rate))
    {
        return RT_EOK;
    }

    need_restart = bt_i2s_player_ctx.playback_started;
    if (need_restart)
    {
        bt_i2s_player_ctx.playback_started = RT_FALSE;
        (void) HAL_I2S_DMAStop(&hi2s2);
    }

    if (bt_i2s_player_ctx.i2s_inited)
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
        return -RT_ERROR;
    }

    bt_i2s_player_ctx.i2s_inited = RT_TRUE;

    if (bt_i2s_player_dma_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (wm8978_set_sample_rate(sample_rate) != RT_EOK)
    {
        LOG_E("wm8978_set_sample_rate failed: %u", sample_rate);
        return -RT_ERROR;
    }

    bt_i2s_player_ctx.sample_rate = sample_rate;

    if (need_restart)
    {
        bt_i2s_player_ctx.start_pending = RT_TRUE;
        if (bt_i2s_player_try_start_dma() != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    LOG_I("I2S sample rate set to %u", sample_rate);
    return RT_EOK;
}

static void bt_i2s_player_pcm_callback(const int16_t * pcm,
                                       uint16_t num_samples,
                                       uint8_t num_channels,
                                       uint32_t sample_rate,
                                       void * context)
{
    rt_uint16_t frame_index;
    rt_base_t level;

    UNUSED(context);

    if ((pcm == RT_NULL) || (num_samples == 0u))
    {
        return;
    }

    if (!bt_i2s_player_ctx.inited)
    {
        return;
    }

    if ((num_channels == 0u) || (num_channels > 2u))
    {
        LOG_W("unsupported pcm channels: %u", num_channels);
        return;
    }

    if ((sample_rate != bt_i2s_player_ctx.sample_rate) ||
        (bt_i2s_player_ctx.channels != num_channels))
    {
        if (bt_i2s_player_prepare(sample_rate, num_channels) != RT_EOK)
        {
            return;
        }
    }

    if (!bt_i2s_player_ctx.first_pcm_logged)
    {
        bt_i2s_player_ctx.first_pcm_logged = RT_TRUE;
        LOG_I("first PCM queued to I2S: frames=%u, channels=%u, sample_rate=%u",
              num_samples,
              num_channels,
              sample_rate);
    }

    level = rt_hw_interrupt_disable();
    for (frame_index = 0u; frame_index < num_samples; frame_index++)
    {
        rt_int16_t left_sample;
        rt_int16_t right_sample;

        if ((BT_I2S_PLAYER_RING_BUFFER_SAMPLES - bt_i2s_player_ctx.ring_level) < BT_I2S_PLAYER_OUTPUT_CHANNELS)
        {
            if (!bt_i2s_player_ctx.overflow_notice_printed)
            {
                bt_i2s_player_ctx.overflow_notice_printed = RT_TRUE;
                LOG_W("PCM ring buffer overflow, drop audio frames");
            }
            break;
        }

        if (num_channels == 1u)
        {
            left_sample = pcm[frame_index];
            right_sample = left_sample;
        }
        else
        {
            left_sample = pcm[(rt_size_t) frame_index * num_channels];
            right_sample = pcm[(rt_size_t) frame_index * num_channels + 1u];
        }

        bt_i2s_player_ring_buffer[bt_i2s_player_ctx.ring_write] = left_sample;
        bt_i2s_player_ctx.ring_write++;
        if (bt_i2s_player_ctx.ring_write >= BT_I2S_PLAYER_RING_BUFFER_SAMPLES)
        {
            bt_i2s_player_ctx.ring_write = 0u;
        }

        bt_i2s_player_ring_buffer[bt_i2s_player_ctx.ring_write] = right_sample;
        bt_i2s_player_ctx.ring_write++;
        if (bt_i2s_player_ctx.ring_write >= BT_I2S_PLAYER_RING_BUFFER_SAMPLES)
        {
            bt_i2s_player_ctx.ring_write = 0u;
        }

        bt_i2s_player_ctx.ring_level += BT_I2S_PLAYER_OUTPUT_CHANNELS;
    }
    rt_hw_interrupt_enable(level);

    if (bt_i2s_player_ctx.start_pending)
    {
        (void) bt_i2s_player_try_start_dma();
    }
}

rt_err_t bt_i2s_player_prepare(rt_uint32_t sample_rate, rt_uint8_t channels)
{
    if (!bt_i2s_player_ctx.inited)
    {
        return -RT_ERROR;
    }

    if ((channels == 0u) || (channels > 2u))
    {
        return -RT_EINVAL;
    }

    bt_i2s_player_ctx.channels = channels;
    bt_i2s_player_reset_buffer();
    return bt_i2s_player_i2s_reconfigure(sample_rate);
}

rt_err_t bt_i2s_player_set_sample_rate(rt_uint32_t sample_rate)
{
    return bt_i2s_player_prepare(sample_rate,
                                 bt_i2s_player_ctx.channels == 0u ?
                                 BT_I2S_PLAYER_OUTPUT_CHANNELS :
                                 bt_i2s_player_ctx.channels);
}

rt_err_t bt_i2s_player_init(void)
{
    if (bt_i2s_player_ctx.inited)
    {
        return RT_EOK;
    }

    if (wm8978_init() != RT_EOK)
    {
        LOG_E("wm8978_init failed");
        return -RT_ERROR;
    }

    if (wm8978_set_output_route(WM8978_ROUTE_SPEAKER) != RT_EOK)
    {
        LOG_E("wm8978_set_output_route failed");
        return -RT_ERROR;
    }

    bt_i2s_player_ctx.inited = RT_TRUE;
    bt_i2s_player_ctx.channels = BT_I2S_PLAYER_OUTPUT_CHANNELS;
    bt_i2s_player_reset_buffer();
    bt_i2s_player_clear_dma_buffer();

    if (bt_i2s_player_i2s_reconfigure(WM8978_DEFAULT_SAMPLE_RATE) != RT_EOK)
    {
        bt_i2s_player_ctx.inited = RT_FALSE;
        return -RT_ERROR;
    }

    (void) bt_i2s_player_prime_i2s_clock();
    if (wm8978_start_playback() != RT_EOK)
    {
        bt_i2s_player_ctx.inited = RT_FALSE;
        LOG_E("wm8978_start_playback failed");
        return -RT_ERROR;
    }

    bt_a2dp_audio_register_pcm_callback(bt_i2s_player_pcm_callback, RT_NULL);

    LOG_I("bt_i2s_player init ok, route=speaker, sample_rate=%u", bt_i2s_player_ctx.sample_rate);
    return RT_EOK;
}

rt_err_t bt_i2s_player_start(void)
{
    if (!bt_i2s_player_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (bt_i2s_player_ctx.playback_started)
    {
        return RT_EOK;
    }

    bt_i2s_player_ctx.start_pending = RT_TRUE;
    if (bt_i2s_player_try_start_dma() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        LOG_I("bt_i2s_player waiting PCM buffer before DMA start, threshold_frames=%u",
              BT_I2S_PLAYER_START_THRESHOLD_FRAMES);
    }

    return RT_EOK;
}

rt_err_t bt_i2s_player_stop(void)
{
    if (!bt_i2s_player_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        bt_i2s_player_ctx.start_pending = RT_FALSE;
        bt_i2s_player_reset_buffer();
        return RT_EOK;
    }

    bt_i2s_player_ctx.playback_started = RT_FALSE;
    bt_i2s_player_ctx.start_pending = RT_FALSE;
    (void) HAL_I2S_DMAStop(&hi2s2);
    bt_i2s_player_reset_buffer();
    bt_i2s_player_clear_dma_buffer();
    LOG_I("bt_i2s_player stopped");
    return RT_EOK;
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        return;
    }

    bt_i2s_player_refill_dma_range(0u, BT_I2S_PLAYER_DMA_HALF_SAMPLES);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        return;
    }

    bt_i2s_player_refill_dma_range(BT_I2S_PLAYER_DMA_HALF_SAMPLES,
                                   BT_I2S_PLAYER_DMA_HALF_SAMPLES);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef * hi2s)
{
    if (hi2s != &hi2s2)
    {
        return;
    }

    LOG_E("HAL_I2S error: 0x%08x", hi2s->ErrorCode);
}

void DMA1_Stream4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&bt_i2s_player_ctx.hdma_i2s2_tx);
}


