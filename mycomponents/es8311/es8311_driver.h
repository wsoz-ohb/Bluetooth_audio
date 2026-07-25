/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     wsoz         the first version
 * 2026-04-20     Codex        add minimal ES8311 playback driver interface
 */
#ifndef MYCOMPONENTS_ES8311_ES8311_DRIVER_H_
#define MYCOMPONENTS_ES8311_ES8311_DRIVER_H_

#include <rtdevice.h>
#include <rtthread.h>
#include <board.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * 这些宏保留为“板级可替换项”。
 * 当前 ES8311 驱动只直接使用 RT_I2C_DEVICE，
 * I2S 引脚宏预留给 bt_i2s_player / HAL_I2S 侧继续复用。
 */

/* I2S 引脚定义 */
#define I2S_CK_PORT                 GPIOB
#define I2S_CK_PIN                  GPIO_PIN10
#define I2S_WS_PORT                 GPIOB
#define I2S_WS_PIN                  GPIO_PIN12
#define I2S_SD_PORT                 GPIOC
#define I2S_SD_PIN                  GPIO_PIN3
#define I2S_EXT_SD_PORT             GPIOC
#define I2S_EXT_SD_PIN              GPIO_PIN2
#define I2S_MCK_PORT                GPIOC
#define I2S_MCK_PIN                 GPIO_PIN6

/* ES8311 控制总线定义 */
#define RT_I2C_DEVICE               "i2c1"
#define RT_I2C_SCL_PORT             GPIOC
#define RT_I2C_SCL_PIN              GPIO_PIN11
#define RT_I2C_SDA_PORT             GPIOC
#define RT_I2C_SDA_PIN              GPIO_PIN12

#define ES8311_I2C_ADDR             0x18u
#define ES8311_DEFAULT_SAMPLE_RATE      44100u
#define ES8311_DEFAULT_CHANNELS         2u
#define ES8311_DEFAULT_BITS_PER_SAMPLE  16u
#define ES8311_DEFAULT_USE_MCLK         1u

typedef enum
{
    ES8311_AUDIO_FMT_I2S    = 0u,
    ES8311_AUDIO_FMT_LEFT_J = 1u,
    ES8311_AUDIO_FMT_DSP    = 3u,
} es8311_audio_format_t;

typedef enum
{
    ES8311_DAC_SOURCE_LEFT  = 0u,
    ES8311_DAC_SOURCE_RIGHT = 1u,
} es8311_dac_source_t;

typedef enum
{
    ES8311_INPUT_MIC  = 0u,
    ES8311_INPUT_DMIC = 1u,
} es8311_input_mode_t;

typedef enum
{
    ES8311_MIC_GAIN_0DB  = 0u,
    ES8311_MIC_GAIN_6DB  = 1u,
    ES8311_MIC_GAIN_12DB = 2u,
    ES8311_MIC_GAIN_18DB = 3u,
    ES8311_MIC_GAIN_24DB = 4u,
    ES8311_MIC_GAIN_30DB = 5u,
    ES8311_MIC_GAIN_36DB = 6u,
    ES8311_MIC_GAIN_42DB = 7u,
} es8311_mic_gain_t;

typedef struct
{
    rt_uint32_t sample_rate;
    rt_uint8_t channels;
    rt_uint8_t bits_per_sample;
    es8311_audio_format_t audio_format;
    rt_bool_t use_mclk;
    es8311_dac_source_t dac_source;
    es8311_input_mode_t input_mode;
    es8311_mic_gain_t mic_gain;
} es8311_config_t;

/* 初始化 codec 控制面，不会自动启动 I2S 发送。 */
rt_err_t es8311_init(void);

/* 配置数字音频接口和时钟参数。默认场景为 STM32 I2S Master，ES8311 Slave。 */
rt_err_t es8311_configure(const es8311_config_t * config);

/* 只更新采样率，其他参数保持当前配置。 */
rt_err_t es8311_set_sample_rate(rt_uint32_t sample_rate);

/* 启动/停止 DAC 播放路径。建议在 I2S 时钟已经准备好之后调用 start。 */
rt_err_t es8311_start_playback(void);
rt_err_t es8311_stop_playback(void);

/* 启动/停止 ADC 输入路径。这里只负责 codec 控制面，不包含 I2S Rx / DMA。 */
rt_err_t es8311_start_record(void);
rt_err_t es8311_stop_record(void);

/* 设置模拟 MIC PGA 增益。当前只对模拟 MIC 输入路径生效。 */
rt_err_t es8311_set_mic_gain(es8311_mic_gain_t mic_gain);

/* 设置 DAC 数字音量寄存器值(0x32)。0x00 最小，约 0xBF 为 0dB。 */
rt_err_t es8311_set_dac_volume(rt_uint8_t volume_reg);

/* 读取当前缓存的 DAC 数字音量寄存器值。 */
rt_uint8_t es8311_get_dac_volume(void);

/* 读取当前驱动配置快照。 */
const es8311_config_t * es8311_get_config(void);

#if defined(__cplusplus)
}
#endif

#endif /* MYCOMPONENTS_ES8311_ES8311_DRIVER_H_ */
