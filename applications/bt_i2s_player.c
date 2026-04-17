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

#include "bt_pcm_stream.h"
#include "main.h"

#define BT_I2S_PLAYER_BACKEND_WM8978      1u
#define BT_I2S_PLAYER_BACKEND_MAX98375    2u

#ifndef BT_I2S_PLAYER_BACKEND
#define BT_I2S_PLAYER_BACKEND             BT_I2S_PLAYER_BACKEND_MAX98375
#endif

#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
#include "wm9878_driver.h"
#define BT_I2S_PLAYER_DEFAULT_SAMPLE_RATE WM8978_DEFAULT_SAMPLE_RATE
#elif BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_MAX98375
#include "max98375_driver.h"
#define BT_I2S_PLAYER_DEFAULT_SAMPLE_RATE MAX98375_DEFAULT_SAMPLE_RATE
#else
#error "Unsupported BT_I2S_PLAYER_BACKEND"
#endif

#define DBG_TAG "bt_i2s"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BT_I2S_PLAYER_OUTPUT_CHANNELS            2u
#define BT_I2S_PLAYER_DMA_HALF_FRAMES            512u
#define BT_I2S_PLAYER_DMA_BUFFER_FRAMES          (BT_I2S_PLAYER_DMA_HALF_FRAMES * 2u)
#define BT_I2S_PLAYER_DMA_BUFFER_SAMPLES         (BT_I2S_PLAYER_DMA_BUFFER_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
#define BT_I2S_PLAYER_DMA_HALF_SAMPLES           (BT_I2S_PLAYER_DMA_HALF_FRAMES * BT_I2S_PLAYER_OUTPUT_CHANNELS)
// 等积累到 6 个 DMA buffer 再启动，优先换取更稳的起播和更少的抖动噪声。
#define BT_I2S_PLAYER_START_THRESHOLD_FRAMES     (BT_I2S_PLAYER_DMA_BUFFER_FRAMES * 6u)
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
    rt_bool_t underflow_notice_printed;
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
} bt_i2s_player_context_t;

static bt_i2s_player_context_t bt_i2s_player_ctx;
static rt_uint16_t bt_i2s_player_dma_buffer[BT_I2S_PLAYER_DMA_BUFFER_SAMPLES];

static const char * bt_i2s_player_backend_name(void)
{
#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
    return "wm8978";
#else
    return "max98375";
#endif
}

static rt_err_t bt_i2s_player_backend_init(void)
{
#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
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
#else
    if (max98375_init() != RT_EOK)
    {
        LOG_E("max98375_init failed");
        return -RT_ERROR;
    }
#endif

    return RT_EOK;
}

static rt_err_t bt_i2s_player_backend_set_sample_rate(rt_uint32_t sample_rate)
{
#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
    if (wm8978_set_sample_rate(sample_rate) != RT_EOK)
    {
        LOG_E("wm8978_set_sample_rate failed: %u", sample_rate);
        return -RT_ERROR;
    }
#else
    if (max98375_set_sample_rate(sample_rate) != RT_EOK)
    {
        LOG_E("max98375_set_sample_rate failed: %u", sample_rate);
        return -RT_ERROR;
    }
#endif

    return RT_EOK;
}

static rt_err_t bt_i2s_player_backend_start(void)
{
#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
    if (wm8978_start_playback() != RT_EOK)
    {
        LOG_E("wm8978_start_playback failed");
        return -RT_ERROR;
    }
#else
    if (max98375_start_playback() != RT_EOK)
    {
        LOG_E("max98375_start_playback failed");
        return -RT_ERROR;
    }
#endif

    return RT_EOK;
}

static rt_err_t bt_i2s_player_backend_stop(void)
{
#if BT_I2S_PLAYER_BACKEND == BT_I2S_PLAYER_BACKEND_WM8978
    if (wm8978_stop_playback() != RT_EOK)
    {
        LOG_E("wm8978_stop_playback failed");
        return -RT_ERROR;
    }
#else
    if (max98375_stop_playback() != RT_EOK)
    {
        LOG_E("max98375_stop_playback failed");
        return -RT_ERROR;
    }
#endif

    return RT_EOK;
}

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

static void bt_i2s_player_reset_runtime(void)
{
    bt_i2s_player_ctx.start_pending = RT_FALSE;
    bt_i2s_player_ctx.underflow_notice_printed = RT_FALSE;
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
    rt_size_t copied_samples;
    rt_size_t read_frames;

    copied_samples = 0u;
    // 具体后端现在只负责从公共 PCM stream 消费数据，
    // 不再由解码层直接把 PCM 推进来。
    read_frames = bt_pcm_stream_read((rt_int16_t *) &bt_i2s_player_dma_buffer[offset],
                                     (rt_uint32_t) (samples / BT_I2S_PLAYER_OUTPUT_CHANNELS));
    copied_samples = read_frames * BT_I2S_PLAYER_OUTPUT_CHANNELS;

    while (copied_samples < samples)
    {
        if (bt_i2s_player_ctx.playback_started && !bt_i2s_player_ctx.underflow_notice_printed)
        {
            bt_i2s_player_ctx.underflow_notice_printed = RT_TRUE;
            LOG_W("I2S DMA underflow, output silence until PCM stream refills");
        }
        bt_i2s_player_dma_buffer[offset + copied_samples] = 0u;
        copied_samples++;
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
    return (rt_bool_t) (bt_i2s_player_ctx.start_pending &&
                        !bt_i2s_player_ctx.playback_started &&
                        (bt_pcm_stream_get_level_frames() >= BT_I2S_PLAYER_START_THRESHOLD_FRAMES));
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
    bt_i2s_player_ctx.underflow_notice_printed = RT_FALSE;
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

    if (bt_i2s_player_backend_set_sample_rate(sample_rate) != RT_EOK)
    {
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

static rt_err_t bt_i2s_player_prepare(rt_uint32_t sample_rate, rt_uint8_t channels)
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
    bt_i2s_player_reset_runtime();
    bt_i2s_player_clear_dma_buffer();
    return bt_i2s_player_i2s_reconfigure(sample_rate);
}

static rt_err_t bt_i2s_player_start(void)
{
    if (!bt_i2s_player_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (bt_i2s_player_ctx.playback_started)
    {
        return RT_EOK;
    }

    if (bt_i2s_player_backend_start() != RT_EOK)
    {
        return -RT_ERROR;
    }

    bt_i2s_player_ctx.start_pending = RT_TRUE;
    if (bt_i2s_player_try_start_dma() != RT_EOK)
    {
        (void) bt_i2s_player_backend_stop();
        bt_i2s_player_ctx.start_pending = RT_FALSE;
        return -RT_ERROR;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        LOG_I("bt_i2s_player waiting PCM stream before DMA start, threshold_frames=%u",
              BT_I2S_PLAYER_START_THRESHOLD_FRAMES);
    }

    return RT_EOK;
}

static rt_err_t bt_i2s_player_stop(void)
{
    if (!bt_i2s_player_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!bt_i2s_player_ctx.playback_started)
    {
        bt_i2s_player_reset_runtime();
        (void) bt_i2s_player_backend_stop();
        bt_i2s_player_clear_dma_buffer();
        return RT_EOK;
    }

    bt_i2s_player_ctx.playback_started = RT_FALSE;
    bt_i2s_player_reset_runtime();
    (void) HAL_I2S_DMAStop(&hi2s2);
    (void) bt_i2s_player_backend_stop();
    bt_i2s_player_clear_dma_buffer();
    LOG_I("bt_i2s_player stopped");
    return RT_EOK;
}

static void bt_i2s_player_on_stream_configure(rt_uint32_t sample_rate, rt_uint8_t channels)
{
    if (bt_i2s_player_prepare(sample_rate, channels) != RT_EOK)
    {
        LOG_W("bt_i2s_player_prepare failed");
    }
}

static void bt_i2s_player_on_stream_start(void)
{
    if (bt_i2s_player_start() != RT_EOK)
    {
        LOG_E("bt_i2s_player_start failed");
    }
}

static void bt_i2s_player_on_stream_stop(void)
{
    (void) bt_i2s_player_stop();
}

static void bt_i2s_player_on_stream_data_available(void)
{
    if (bt_i2s_player_ctx.start_pending)
    {
        if (bt_i2s_player_try_start_dma() != RT_EOK)
        {
            (void) bt_i2s_player_backend_stop();
            bt_i2s_player_ctx.start_pending = RT_FALSE;
            LOG_E("bt_i2s_player delayed DMA start failed");
        }
    }
}

static const bt_pcm_stream_sink_t bt_i2s_player_stream_sink = {
    .on_configure = bt_i2s_player_on_stream_configure,
    .on_start = bt_i2s_player_on_stream_start,
    .on_stop = bt_i2s_player_on_stream_stop,
    .on_data_available = bt_i2s_player_on_stream_data_available,
};

rt_err_t bt_i2s_player_init(void)
{
    if (bt_i2s_player_ctx.inited)
    {
        (void) bt_pcm_stream_register_sink(&bt_i2s_player_stream_sink);
        return RT_EOK;
    }

    if (bt_i2s_player_backend_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    bt_i2s_player_ctx.inited = RT_TRUE;
    bt_i2s_player_ctx.channels = BT_I2S_PLAYER_OUTPUT_CHANNELS;
    bt_i2s_player_clear_dma_buffer();

    if (bt_i2s_player_i2s_reconfigure(BT_I2S_PLAYER_DEFAULT_SAMPLE_RATE) != RT_EOK)
    {
        bt_i2s_player_ctx.inited = RT_FALSE;
        return -RT_ERROR;
    }

    (void) bt_i2s_player_prime_i2s_clock();

    if (bt_pcm_stream_init() != RT_EOK)
    {
        bt_i2s_player_ctx.inited = RT_FALSE;
        (void) bt_i2s_player_backend_stop();
        LOG_E("bt_pcm_stream_init failed");
        return -RT_ERROR;
    }

    // 当前播放后端统一通过同一个 sink 从公共 PCM stream 取数。
    if (bt_pcm_stream_register_sink(&bt_i2s_player_stream_sink) != RT_EOK)
    {
        bt_i2s_player_ctx.inited = RT_FALSE;
        (void) bt_i2s_player_backend_stop();
        LOG_E("bt_pcm_stream_register_sink failed");
        return -RT_ERROR;
    }

    LOG_I("bt_i2s_player init ok, backend=%s, sample_rate=%u",
          bt_i2s_player_backend_name(),
          bt_i2s_player_ctx.sample_rate);
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




