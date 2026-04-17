/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     wsoz         the first version
 * 2026-04-17     Codex        adjust MAX98375 driver to real module pins
 */
#include "max98375_driver.h"

#define DBG_TAG "max98375"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

typedef struct
{
    max98375_config_t config;
    rt_bool_t inited;
    rt_bool_t playback_started;
} max98375_context_t;

static max98375_context_t max98375_ctx;

static void max98375_load_default_config(max98375_config_t * config)
{
    RT_ASSERT(config != RT_NULL);

    config->sample_rate = MAX98375_DEFAULT_SAMPLE_RATE;
    config->channels = MAX98375_DEFAULT_CHANNELS;
    config->bits_per_sample = MAX98375_DEFAULT_BITS_PER_SAMPLE;
}

rt_err_t max98375_init(void)
{
    rt_memset(&max98375_ctx, 0, sizeof(max98375_ctx));
    max98375_load_default_config(&max98375_ctx.config);
    max98375_ctx.inited = RT_TRUE;

    LOG_I("MAX98375 init ok, module uses exposed 3-wire I2S pins only");
    return RT_EOK;
}

rt_err_t max98375_configure(const max98375_config_t * config)
{
    if (!max98375_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (config == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (config->sample_rate == 0u)
    {
        return -RT_EINVAL;
    }

    if ((config->channels == 0u) || (config->channels > 2u))
    {
        return -RT_EINVAL;
    }

    if (config->bits_per_sample == 0u)
    {
        return -RT_EINVAL;
    }

    max98375_ctx.config = *config;
    LOG_I("MAX98375 configured: sample_rate=%u, channels=%u, bits=%u",
          max98375_ctx.config.sample_rate,
          max98375_ctx.config.channels,
          max98375_ctx.config.bits_per_sample);
    return RT_EOK;
}

rt_err_t max98375_set_sample_rate(rt_uint32_t sample_rate)
{
    max98375_config_t config;

    if (!max98375_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (max98375_ctx.config.sample_rate == sample_rate)
    {
        return RT_EOK;
    }

    config = max98375_ctx.config;
    config.sample_rate = sample_rate;
    return max98375_configure(&config);
}

rt_err_t max98375_start_playback(void)
{
    if (!max98375_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (max98375_ctx.playback_started)
    {
        return RT_EOK;
    }

    max98375_ctx.playback_started = RT_TRUE;
    LOG_I("MAX98375 playback started");
    return RT_EOK;
}

rt_err_t max98375_stop_playback(void)
{
    if (!max98375_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!max98375_ctx.playback_started)
    {
        return RT_EOK;
    }

    max98375_ctx.playback_started = RT_FALSE;
    LOG_I("MAX98375 playback stopped");
    return RT_EOK;
}

rt_bool_t max98375_is_playback_started(void)
{
    return max98375_ctx.playback_started;
}

const max98375_config_t * max98375_get_config(void)
{
    return &max98375_ctx.config;
}
