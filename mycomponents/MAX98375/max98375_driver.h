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
#ifndef MYCOMPONENTS_MAX98375_MAX98375_DRIVER_H_
#define MYCOMPONENTS_MAX98375_MAX98375_DRIVER_H_

#include <rtthread.h>
#include <board.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * 当前模块按“板上引出的实际接口”建模，不按芯片所有内部能力建模。
 * 你这块 MAX98375 模块暴露的是 3 根 I2S 音频线：BCLK / LRCK / DOUT。
 */
#define MAX98375_I2S_BCLK_PORT            GPIOB
#define MAX98375_I2S_BCLK_PIN             GPIO_PIN10
#define MAX98375_I2S_LRCK_PORT            GPIOB
#define MAX98375_I2S_LRCK_PIN             GPIO_PIN12
#define MAX98375_I2S_DOUT_PORT            GPIOC
#define MAX98375_I2S_DOUT_PIN             GPIO_PIN3

#define MAX98375_DEFAULT_SAMPLE_RATE      44100u
#define MAX98375_DEFAULT_CHANNELS         2u
#define MAX98375_DEFAULT_BITS_PER_SAMPLE  16u

typedef struct
{
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
    rt_uint8_t bits_per_sample;
} max98375_config_t;

/*
 * 这个驱动当前只维护“模块侧播放配置快照”和运行状态。
 * 真正的 I2S 时钟/数据发送仍由 STM32 的 I2S 外设负责。
 */
rt_err_t max98375_init(void);
rt_err_t max98375_configure(const max98375_config_t * config);
rt_err_t max98375_set_sample_rate(rt_uint32_t sample_rate);
rt_err_t max98375_start_playback(void);
rt_err_t max98375_stop_playback(void);
rt_bool_t max98375_is_playback_started(void);
const max98375_config_t * max98375_get_config(void);

#if defined(__cplusplus)
}
#endif

#endif /* MYCOMPONENTS_MAX98375_MAX98375_DRIVER_H_ */
