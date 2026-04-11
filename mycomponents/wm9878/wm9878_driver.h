/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     wsoz         the first version
 */
#ifndef MYCOMPONENTS_WM9878_WM9878_DRIVER_H_
#define MYCOMPONENTS_WM9878_WM9878_DRIVER_H_

#include <rtdevice.h>
#include <rtthread.h>
#include <board.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * 下面这些宏保留为“板级可替换项”。
 * 当前 codec 驱动实际只直接使用 RT_I2C_DEVICE，
 * I2S 引脚宏预留给后续 bt_i2s_player / HAL_I2S 初始化继续复用。
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

/* WM8978 控制总线定义 */
#define RT_I2C_DEVICE               "i2c1"
#define RT_I2C_SCL_PORT             GPIOC
#define RT_I2C_SCL_PIN              GPIO_PIN11
#define RT_I2C_SDA_PORT             GPIOC
#define RT_I2C_SDA_PIN              GPIO_PIN12

/* WM8978 板级默认配置，可按项目需要覆盖 */
#define WM8978_I2C_ADDR             0x1Au
#define WM8978_VMID_STARTUP_DELAY_MS 500u
#define WM8978_DEFAULT_SAMPLE_RATE  44100u
#define WM8978_DEFAULT_HP_VOLUME    0x39u
#define WM8978_DEFAULT_SPK_VOLUME   0x28u
#define WM8978_ENABLE_SLOWCLK       1
#define WM8978_DEFAULT_ROUTE        WM8978_ROUTE_HEADPHONE
#define WM8978_SPK_USE_BTL          0
#define WM8978_ENABLE_SPKBOOST      0
#define WM8978_ENABLE_MICBIAS       0

typedef enum
{
    WM8978_AUDIO_FMT_RIGHT_J = 0u,
    WM8978_AUDIO_FMT_LEFT_J  = 1u,
    WM8978_AUDIO_FMT_I2S     = 2u,
    WM8978_AUDIO_FMT_DSP     = 3u,
} wm8978_audio_format_t;

typedef enum
{
    WM8978_WORD_LENGTH_16 = 16u,
    WM8978_WORD_LENGTH_20 = 20u,
    WM8978_WORD_LENGTH_24 = 24u,
    WM8978_WORD_LENGTH_32 = 32u,
} wm8978_word_length_t;

typedef enum
{
    WM8978_ROUTE_NONE      = 0x00u,
    WM8978_ROUTE_HEADPHONE = 0x01u,
    WM8978_ROUTE_SPEAKER   = 0x02u,
} wm8978_output_route_t;

typedef struct
{
    rt_uint32_t sample_rate;
    wm8978_audio_format_t audio_format;
    wm8978_word_length_t word_length;
    rt_uint8_t output_route;
} wm8978_config_t;

/* 初始化 codec 控制面，不会自动启动 I2S 发送。 */
rt_err_t wm8978_init(void);

/* 软件复位，并把本地寄存器缓存恢复到数据手册默认值。 */
rt_err_t wm8978_reset(void);

/* 配置数字音频接口。当前默认用于“MCU 为 I2S Master，WM8978 为 Slave”的场景。 */
rt_err_t wm8978_configure(const wm8978_config_t * config);

/* 设置采样率。44.1kHz 与 48kHz 都映射到同一组 SR 配置。 */
rt_err_t wm8978_set_sample_rate(rt_uint32_t sample_rate);

/* 选择默认输出路径。当前支持耳机输出与扬声器输出。 */
rt_err_t wm8978_set_output_route(rt_uint8_t output_route);

/* 设置耳机输出音量，参数直接对应寄存器 6bit 音量值。0x39 为 0dB。 */
rt_err_t wm8978_set_headphone_volume(rt_uint8_t left, rt_uint8_t right);

/* 设置扬声器输出音量，参数直接对应寄存器 6bit 音量值。0x39 为 0dB。 */
rt_err_t wm8978_set_speaker_volume(rt_uint8_t left, rt_uint8_t right);

/* 启动/停止 DAC 播放路径。建议在 I2S 时钟已经准备好之后调用 start。 */
rt_err_t wm8978_start_playback(void);
rt_err_t wm8978_stop_playback(void);

/* 预留基础 ADC 采集路径控制，方便后面接 I2S Full-Duplex Rx。 */
rt_err_t wm8978_start_record(void);
rt_err_t wm8978_stop_record(void);

/* 读取当前驱动配置快照。 */
const wm8978_config_t * wm8978_get_config(void);

#if defined(__cplusplus)
}
#endif

#endif /* MYCOMPONENTS_WM9878_WM9878_DRIVER_H_ */


