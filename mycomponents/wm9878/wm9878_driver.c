/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     wsoz         the first version
 */
#include "wm9878_driver.h"

#include <string.h>

#define DBG_TAG "wm8978"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define WM8978_REG_CACHE_SIZE               58u
#define WM8978_I2C_WRITE_RETRY_COUNT        16
#define WM8978_I2C_WRITE_RETRY_DELAY_MS     5u

/* R1: Power management 1 */
#define WM8978_R1_BUFDCOPEN                 (1u << 8)
#define WM8978_R1_OUT4MIXEN                 (1u << 7)
#define WM8978_R1_OUT3MIXEN                 (1u << 6)
#define WM8978_R1_PLLEN                     (1u << 5)
#define WM8978_R1_MICBEN                    (1u << 4)
#define WM8978_R1_BIASEN                    (1u << 3)
#define WM8978_R1_BUFIOEN                   (1u << 2)
#define WM8978_R1_VMID_MASK                 0x03u
#define WM8978_R1_VMID_75K                  0x01u
#define WM8978_R1_VMID_300K                 0x02u
#define WM8978_R1_VMID_5K                   0x03u

/* R2: Power management 2 */
#define WM8978_R2_ROUT1EN                   (1u << 8)
#define WM8978_R2_LOUT1EN                   (1u << 7)
#define WM8978_R2_SLEEP                     (1u << 6)
#define WM8978_R2_BOOSTENR                  (1u << 5)
#define WM8978_R2_BOOSTENL                  (1u << 4)
#define WM8978_R2_INPPGAENR                 (1u << 3)
#define WM8978_R2_INPPGAENL                 (1u << 2)
#define WM8978_R2_ADCENR                    (1u << 1)
#define WM8978_R2_ADCENL                    (1u << 0)

/* R3: Power management 3 */
#define WM8978_R3_OUT4EN                    (1u << 8)
#define WM8978_R3_OUT3EN                    (1u << 7)
#define WM8978_R3_LOUT2EN                   (1u << 6)
#define WM8978_R3_ROUT2EN                   (1u << 5)
#define WM8978_R3_RMIXEN                    (1u << 3)
#define WM8978_R3_LMIXEN                    (1u << 2)
#define WM8978_R3_DACENR                    (1u << 1)
#define WM8978_R3_DACENL                    (1u << 0)

/* R4: Audio interface */
#define WM8978_R4_WL_SHIFT                  5u
#define WM8978_R4_FMT_SHIFT                 3u

/* R6: Clock generation */
#define WM8978_R6_CLKSEL_MCLK               (0u << 8)
#define WM8978_R6_MCLKDIV_1                 (0u << 5)

/* R7: Additional control */
#define WM8978_R7_SR_SHIFT                  1u
#define WM8978_R7_SLOWCLKEN                 (1u << 0)

/* R43 */
#define WM8978_R43_MUTERPGA2INV             (1u << 5)
#define WM8978_R43_INVROUT2                 (1u << 4)

/* R49 */
#define WM8978_R49_DACL2RMIX                (1u << 6)
#define WM8978_R49_DACR2LMIX                (1u << 5)
#define WM8978_R49_SPKBOOST                 (1u << 2)
#define WM8978_R49_TSDEN                    (1u << 1)

/* R50 / R51 */
#define WM8978_R50_DACL2LMIX                (1u << 0)
#define WM8978_R51_DACR2RMIX                (1u << 0)

/* Output volume registers */
#define WM8978_OUT_VOL_UPDATE               (1u << 8)
#define WM8978_OUT_VOL_MUTE                 (1u << 6)
#define WM8978_OUT_VOL_MASK                 0x3Fu

enum wm8978_register
{
    WM8978_REG_RESET              = 0x00,
    WM8978_REG_POWER1             = 0x01,
    WM8978_REG_POWER2             = 0x02,
    WM8978_REG_POWER3             = 0x03,
    WM8978_REG_AUDIO_IF           = 0x04,
    WM8978_REG_COMPANDING         = 0x05,
    WM8978_REG_CLOCK_GEN          = 0x06,
    WM8978_REG_ADDITIONAL_CTRL    = 0x07,
    WM8978_REG_DAC_CTRL           = 0x0A,
    WM8978_REG_LEFT_DAC_VOL       = 0x0B,
    WM8978_REG_RIGHT_DAC_VOL      = 0x0C,
    WM8978_REG_ADC_CTRL           = 0x0E,
    WM8978_REG_LEFT_ADC_VOL       = 0x0F,
    WM8978_REG_RIGHT_ADC_VOL      = 0x10,
    WM8978_REG_BEEP_CTRL          = 0x2B,
    WM8978_REG_INPUT_CTRL         = 0x2C,
    WM8978_REG_LEFT_INP_PGA       = 0x2D,
    WM8978_REG_RIGHT_INP_PGA      = 0x2E,
    WM8978_REG_LEFT_ADC_BOOST     = 0x2F,
    WM8978_REG_RIGHT_ADC_BOOST    = 0x30,
    WM8978_REG_OUTPUT_CTRL        = 0x31,
    WM8978_REG_LEFT_MIXER         = 0x32,
    WM8978_REG_RIGHT_MIXER        = 0x33,
    WM8978_REG_LOUT1_VOL          = 0x34,
    WM8978_REG_ROUT1_VOL          = 0x35,
    WM8978_REG_LOUT2_VOL          = 0x36,
    WM8978_REG_ROUT2_VOL          = 0x37,
};

typedef struct
{
    struct rt_i2c_bus_device * bus;
    rt_uint16_t reg_cache[WM8978_REG_CACHE_SIZE];
    wm8978_config_t config;
    rt_bool_t inited;
    rt_bool_t playback_started;
    rt_bool_t record_started;
    rt_uint8_t hp_left_volume;
    rt_uint8_t hp_right_volume;
    rt_uint8_t spk_left_volume;
    rt_uint8_t spk_right_volume;
} wm8978_context_t;

static wm8978_context_t wm8978_ctx;

static const rt_uint16_t wm8978_reg_defaults[WM8978_REG_CACHE_SIZE] =
{
    [WM8978_REG_RESET]           = 0x000u,
    [WM8978_REG_POWER1]          = 0x000u,
    [WM8978_REG_POWER2]          = 0x000u,
    [WM8978_REG_POWER3]          = 0x000u,
    [WM8978_REG_AUDIO_IF]        = 0x050u,
    [WM8978_REG_COMPANDING]      = 0x000u,
    [WM8978_REG_CLOCK_GEN]       = 0x140u,
    [WM8978_REG_ADDITIONAL_CTRL] = 0x000u,
    [WM8978_REG_DAC_CTRL]        = 0x000u,
    [WM8978_REG_LEFT_DAC_VOL]    = 0x0FFu,
    [WM8978_REG_RIGHT_DAC_VOL]   = 0x0FFu,
    [WM8978_REG_ADC_CTRL]        = 0x100u,
    [WM8978_REG_LEFT_ADC_VOL]    = 0x0FFu,
    [WM8978_REG_RIGHT_ADC_VOL]   = 0x0FFu,
    [WM8978_REG_BEEP_CTRL]       = 0x000u,
    [WM8978_REG_INPUT_CTRL]      = 0x033u,
    [WM8978_REG_LEFT_INP_PGA]    = 0x010u,
    [WM8978_REG_RIGHT_INP_PGA]   = 0x010u,
    [WM8978_REG_LEFT_ADC_BOOST]  = 0x100u,
    [WM8978_REG_RIGHT_ADC_BOOST] = 0x100u,
    [WM8978_REG_OUTPUT_CTRL]     = 0x002u,
    [WM8978_REG_LEFT_MIXER]      = 0x001u,
    [WM8978_REG_RIGHT_MIXER]     = 0x001u,
    [WM8978_REG_LOUT1_VOL]       = 0x039u,
    [WM8978_REG_ROUT1_VOL]       = 0x039u,
    [WM8978_REG_LOUT2_VOL]       = 0x039u,
    [WM8978_REG_ROUT2_VOL]       = 0x039u,
};

static void wm8978_load_default_config(wm8978_config_t * config)
{
    RT_ASSERT(config != RT_NULL);

    config->sample_rate = WM8978_DEFAULT_SAMPLE_RATE;
    config->audio_format = WM8978_AUDIO_FMT_I2S;
    config->word_length = WM8978_WORD_LENGTH_16;
    config->output_route = WM8978_DEFAULT_ROUTE;
}

static void wm8978_reset_cache(void)
{
    rt_memset(wm8978_ctx.reg_cache, 0, sizeof(wm8978_ctx.reg_cache));
    rt_memcpy(wm8978_ctx.reg_cache, wm8978_reg_defaults, sizeof(wm8978_reg_defaults));
}

static rt_err_t wm8978_write_register(rt_uint8_t reg, rt_uint16_t value)
{
    rt_uint8_t buffer[2];
    struct rt_i2c_msg msg;
    rt_size_t transferred;
    int retry;

    if ((wm8978_ctx.bus == RT_NULL) || (reg >= WM8978_REG_CACHE_SIZE))
    {
        return -RT_ERROR;
    }

    buffer[0] = (rt_uint8_t) ((reg << 1) | ((value >> 8) & 0x01u));
    buffer[1] = (rt_uint8_t) (value & 0xFFu);

    msg.addr = WM8978_I2C_ADDR;
    msg.flags = RT_I2C_WR;
    msg.buf = buffer;
    msg.len = sizeof(buffer);

    // 软复位或上电边沿附近，codec 可能会短暂 NACK。
    // 这里做几次轻量重试，避免一次瞬时失败直接中断初始化。
    for (retry = 0; retry < WM8978_I2C_WRITE_RETRY_COUNT; retry++)
    {
        transferred = rt_i2c_transfer(wm8978_ctx.bus, &msg, 1);
        if (transferred == 1u)
        {
            wm8978_ctx.reg_cache[reg] = (rt_uint16_t) (value & 0x01FFu);
            return RT_EOK;
        }
        rt_thread_mdelay(WM8978_I2C_WRITE_RETRY_DELAY_MS);
    }

    LOG_E("wm8978 write failed, reg=0x%02x, value=0x%03x", reg, value);
    return -RT_ERROR;
}

static rt_err_t wm8978_update_register(rt_uint8_t reg, rt_uint16_t mask, rt_uint16_t value)
{
    rt_uint16_t old_value;
    rt_uint16_t new_value;

    if (reg >= WM8978_REG_CACHE_SIZE)
    {
        return -RT_EINVAL;
    }

    old_value = wm8978_ctx.reg_cache[reg];
    new_value = (rt_uint16_t) ((old_value & (~mask)) | (value & mask));
    if (new_value == old_value)
    {
        return RT_EOK;
    }

    return wm8978_write_register(reg, new_value);
}

static rt_uint16_t wm8978_build_output_volume(rt_uint8_t volume, rt_bool_t mute, rt_bool_t sync_update)
{
    rt_uint16_t reg_value;

    reg_value = (rt_uint16_t) (volume & WM8978_OUT_VOL_MASK);
    if (mute)
    {
        reg_value |= WM8978_OUT_VOL_MUTE;
    }
    if (sync_update)
    {
        reg_value |= WM8978_OUT_VOL_UPDATE;
    }
    return reg_value;
}

static rt_uint16_t wm8978_word_length_to_reg(wm8978_word_length_t word_length)
{
    switch (word_length)
    {
    case WM8978_WORD_LENGTH_16:
        return 0u;
    case WM8978_WORD_LENGTH_20:
        return 1u;
    case WM8978_WORD_LENGTH_24:
        return 2u;
    case WM8978_WORD_LENGTH_32:
        return 3u;
    default:
        return 0xFFFFu;
    }
}

static rt_bool_t wm8978_sample_rate_supported(rt_uint32_t sample_rate)
{
    switch (sample_rate)
    {
    case 48000u:
    case 44100u:
    case 32000u:
    case 24000u:
    case 22050u:
    case 16000u:
    case 12000u:
    case 11025u:
    case 8000u:
        return RT_TRUE;
    default:
        return RT_FALSE;
    }
}

static rt_err_t wm8978_apply_playback_defaults(void)
{
    rt_err_t err;

    /*
     * 参考工程在“纯 DAC 播放”场景下会把输入侧相关寄存器清回最简状态，
     * 避免 ADC/PGA/BOOST 这些残留配置影响当前的扬声器播放链路。
     */
    err = wm8978_write_register(WM8978_REG_INPUT_CTRL, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_ADC_CTRL, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_LEFT_ADC_BOOST, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_RIGHT_ADC_BOOST, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_LEFT_ADC_VOL, 0x0FFu);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_RIGHT_ADC_VOL, 0x1FFu);
    if (err != RT_EOK)
    {
        return err;
    }

    return RT_EOK;
}

static rt_err_t wm8978_apply_interface_config(const wm8978_config_t * config)
{
    rt_uint16_t wl;
    rt_uint16_t reg_value;
    rt_err_t err;

    wl = wm8978_word_length_to_reg(config->word_length);
    if ((wl == 0xFFFFu) || !wm8978_sample_rate_supported(config->sample_rate))
    {
        return -RT_EINVAL;
    }

    /*
     * 当前项目中 STM32 作为 I2S Master，WM8978 作为 Slave。
     * 这里固定使用外部 MCLK，不启用 codec 内部 PLL。
     */
    reg_value = (rt_uint16_t) ((wl << WM8978_R4_WL_SHIFT) |
                               (((rt_uint16_t) config->audio_format) << WM8978_R4_FMT_SHIFT));
    err = wm8978_write_register(WM8978_REG_AUDIO_IF, reg_value);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_COMPANDING, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_CLOCK_GEN, WM8978_R6_CLKSEL_MCLK | WM8978_R6_MCLKDIV_1);
    if (err != RT_EOK)
    {
        return err;
    }

    /*
     * 参考工程只配置 R4/R6，不依赖 R7 的 SR/SLOWCLK 位来匹配采样率。
     * 当前项目中真正决定采样率的是 MCU 侧 I2S 时钟，因此这里把 R7 清零，
     * 避免额外的 codec 内部时钟配置干扰已经验证可工作的外部时钟链路。
     */
    err = wm8978_write_register(WM8978_REG_ADDITIONAL_CTRL, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_DAC_CTRL, 0x000u);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_LEFT_DAC_VOL, 0x0FFu);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_RIGHT_DAC_VOL, 0x1FFu);
    if (err != RT_EOK)
    {
        return err;
    }

    return RT_EOK;
}

static rt_err_t wm8978_apply_route_registers(rt_uint8_t route)
{
    rt_uint16_t power2;
    rt_uint16_t power3;
    rt_uint16_t output_ctrl;
    rt_err_t err;

    if ((route & (WM8978_ROUTE_HEADPHONE | WM8978_ROUTE_SPEAKER)) == 0u)
    {
        return -RT_EINVAL;
    }

    power2 = wm8978_ctx.reg_cache[WM8978_REG_POWER2];
    power3 = wm8978_ctx.reg_cache[WM8978_REG_POWER3];

    power2 &= (rt_uint16_t) (~(WM8978_R2_ROUT1EN | WM8978_R2_LOUT1EN));
    power3 &= (rt_uint16_t) (~(WM8978_R3_LOUT2EN | WM8978_R3_ROUT2EN));

    if ((route & WM8978_ROUTE_HEADPHONE) != 0u)
    {
        power2 |= (WM8978_R2_ROUT1EN | WM8978_R2_LOUT1EN);
    }

    if ((route & WM8978_ROUTE_SPEAKER) != 0u)
    {
        power3 |= (WM8978_R3_LOUT2EN | WM8978_R3_ROUT2EN);
    }

    power3 |= (WM8978_R3_DACENL | WM8978_R3_DACENR | WM8978_R3_LMIXEN | WM8978_R3_RMIXEN);

    err = wm8978_write_register(WM8978_REG_POWER2, power2);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_POWER3, power3);
    if (err != RT_EOK)
    {
        return err;
    }

    output_ctrl = (rt_uint16_t) (WM8978_R49_TSDEN | WM8978_R49_DACL2RMIX | WM8978_R49_DACR2LMIX);
#if WM8978_ENABLE_SPKBOOST
    if ((route & WM8978_ROUTE_SPEAKER) != 0u)
    {
        // 喇叭路径的 1.5x 模拟增益会明显放大底噪，默认关闭，后面需要更大声压再单独打开。
        output_ctrl |= WM8978_R49_SPKBOOST;
    }
#endif

    err = wm8978_write_register(WM8978_REG_OUTPUT_CTRL, output_ctrl);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_LEFT_MIXER, WM8978_R50_DACL2LMIX);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_RIGHT_MIXER, WM8978_R51_DACR2RMIX);
    if (err != RT_EOK)
    {
        return err;
    }

#if WM8978_SPK_USE_BTL
    if ((route & WM8978_ROUTE_SPEAKER) != 0u)
    {
        err = wm8978_update_register(WM8978_REG_BEEP_CTRL,
                                     WM8978_R43_INVROUT2 | WM8978_R43_MUTERPGA2INV,
                                     WM8978_R43_INVROUT2);
    }
    else
#endif
    {
        err = wm8978_update_register(WM8978_REG_BEEP_CTRL,
                                     WM8978_R43_INVROUT2 | WM8978_R43_MUTERPGA2INV,
                                     0u);
    }

    return err;
}

static rt_err_t wm8978_apply_output_volumes(rt_bool_t mute)
{
    rt_err_t err;

    err = wm8978_write_register(WM8978_REG_LOUT1_VOL,
                                wm8978_build_output_volume(wm8978_ctx.hp_left_volume, mute, RT_FALSE));
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_ROUT1_VOL,
                                wm8978_build_output_volume(wm8978_ctx.hp_right_volume, mute, RT_TRUE));
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_LOUT2_VOL,
                                wm8978_build_output_volume(wm8978_ctx.spk_left_volume, mute, RT_FALSE));
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_write_register(WM8978_REG_ROUT2_VOL,
                                wm8978_build_output_volume(wm8978_ctx.spk_right_volume, mute, RT_TRUE));
    if (err != RT_EOK)
    {
        return err;
    }

    return RT_EOK;
}

static void wm8978_clear_analogue_power_if_idle(void)
{
    if (!wm8978_ctx.playback_started && !wm8978_ctx.record_started)
    {
        (void) wm8978_write_register(WM8978_REG_POWER1, 0x000u);
    }
}

rt_err_t wm8978_reset(void)
{
    if (wm8978_ctx.bus == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (wm8978_write_register(WM8978_REG_RESET, 0x000u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    wm8978_reset_cache();
    rt_thread_mdelay(5);
    return RT_EOK;
}

rt_err_t wm8978_init(void)
{
    rt_err_t err;

    rt_memset(&wm8978_ctx, 0, sizeof(wm8978_ctx));
    wm8978_ctx.bus = (struct rt_i2c_bus_device *) rt_device_find(RT_I2C_DEVICE);
    if (wm8978_ctx.bus == RT_NULL)
    {
        LOG_E("wm8978 i2c bus not found: %s", RT_I2C_DEVICE);
        return -RT_ERROR;
    }

    wm8978_reset_cache();
    wm8978_load_default_config(&wm8978_ctx.config);
    wm8978_ctx.hp_left_volume = WM8978_DEFAULT_HP_VOLUME;
    wm8978_ctx.hp_right_volume = WM8978_DEFAULT_HP_VOLUME;
    wm8978_ctx.spk_left_volume = WM8978_DEFAULT_SPK_VOLUME;
    wm8978_ctx.spk_right_volume = WM8978_DEFAULT_SPK_VOLUME;

    err = wm8978_reset();
    if (err != RT_EOK)
    {
        LOG_E("wm8978 reset failed");
        return err;
    }

    err = wm8978_apply_interface_config(&wm8978_ctx.config);
    if (err != RT_EOK)
    {
        LOG_E("wm8978 apply interface config failed");
        return err;
    }

    wm8978_ctx.inited = RT_TRUE;
    LOG_I("wm8978 init ok, sample_rate=%u, route=0x%02x",
          wm8978_ctx.config.sample_rate,
          wm8978_ctx.config.output_route);
    return RT_EOK;
}

rt_err_t wm8978_configure(const wm8978_config_t * config)
{
    if ((config == RT_NULL) || (wm8978_ctx.bus == RT_NULL))
    {
        return -RT_EINVAL;
    }

    // 相同的数字音频接口参数没必要重复写一遍 codec 寄存器。
    // 启动阶段连续重复写会放大软 I2C 的偶发抖动，导致“第一次成功、第二次随机 NACK”。
    if (wm8978_ctx.inited &&
        (wm8978_ctx.config.sample_rate == config->sample_rate) &&
        (wm8978_ctx.config.audio_format == config->audio_format) &&
        (wm8978_ctx.config.word_length == config->word_length))
    {
        wm8978_ctx.config = *config;
        return RT_EOK;
    }

    if (wm8978_apply_interface_config(config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    wm8978_ctx.config = *config;
    return RT_EOK;
}

rt_err_t wm8978_set_sample_rate(rt_uint32_t sample_rate)
{
    wm8978_config_t config;

    if (wm8978_ctx.inited && (wm8978_ctx.config.sample_rate == sample_rate))
    {
        return RT_EOK;
    }

    config = wm8978_ctx.config;
    config.sample_rate = sample_rate;
    return wm8978_configure(&config);
}

rt_err_t wm8978_set_output_route(rt_uint8_t output_route)
{
    if ((output_route & (WM8978_ROUTE_HEADPHONE | WM8978_ROUTE_SPEAKER)) == 0u)
    {
        return -RT_EINVAL;
    }

    wm8978_ctx.config.output_route = output_route;
    if (wm8978_ctx.playback_started)
    {
        return wm8978_apply_route_registers(output_route);
    }
    return RT_EOK;
}

rt_err_t wm8978_set_headphone_volume(rt_uint8_t left, rt_uint8_t right)
{
    wm8978_ctx.hp_left_volume = (rt_uint8_t) (left & WM8978_OUT_VOL_MASK);
    wm8978_ctx.hp_right_volume = (rt_uint8_t) (right & WM8978_OUT_VOL_MASK);

    if (wm8978_ctx.inited)
    {
        return wm8978_apply_output_volumes(!wm8978_ctx.playback_started);
    }
    return RT_EOK;
}

rt_err_t wm8978_set_speaker_volume(rt_uint8_t left, rt_uint8_t right)
{
    wm8978_ctx.spk_left_volume = (rt_uint8_t) (left & WM8978_OUT_VOL_MASK);
    wm8978_ctx.spk_right_volume = (rt_uint8_t) (right & WM8978_OUT_VOL_MASK);

    if (wm8978_ctx.inited)
    {
        return wm8978_apply_output_volumes(!wm8978_ctx.playback_started);
    }
    return RT_EOK;
}

rt_err_t wm8978_start_playback(void)
{
    rt_err_t err;
    rt_uint16_t power1_value;

    if (!wm8978_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (wm8978_ctx.playback_started)
    {
        return RT_EOK;
    }

    /*
     * 这里改成更接近参考工程的上电方式：
     * 先一次性拉起模拟偏置和 VMID，再把输入侧清到纯 DAC 播放状态，
     * 最后再配置输出路径和音量。
     */
    power1_value = (rt_uint16_t) (WM8978_R1_BIASEN | WM8978_R1_VMID_5K);
    err = wm8978_write_register(WM8978_REG_POWER1, power1_value);
    if (err != RT_EOK)
    {
        return err;
    }

    rt_thread_mdelay(WM8978_VMID_STARTUP_DELAY_MS);

    err = wm8978_apply_playback_defaults();
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_apply_route_registers(wm8978_ctx.config.output_route);
    if (err != RT_EOK)
    {
        return err;
    }

    err = wm8978_apply_output_volumes(RT_FALSE);
    if (err != RT_EOK)
    {
        return err;
    }

    wm8978_ctx.playback_started = RT_TRUE;
    LOG_I("wm8978 playback started, route=0x%02x", wm8978_ctx.config.output_route);
    return RT_EOK;
}

rt_err_t wm8978_stop_playback(void)
{
    rt_uint16_t power2;
    rt_uint16_t power3;

    if (!wm8978_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!wm8978_ctx.playback_started)
    {
        return RT_EOK;
    }

    (void) wm8978_apply_output_volumes(RT_TRUE);

    power2 = (rt_uint16_t) (wm8978_ctx.reg_cache[WM8978_REG_POWER2] &
                            (~(WM8978_R2_ROUT1EN | WM8978_R2_LOUT1EN)));
    power3 = (rt_uint16_t) (wm8978_ctx.reg_cache[WM8978_REG_POWER3] &
                            (~(WM8978_R3_LOUT2EN | WM8978_R3_ROUT2EN |
                               WM8978_R3_LMIXEN | WM8978_R3_RMIXEN |
                               WM8978_R3_DACENL | WM8978_R3_DACENR)));

    (void) wm8978_write_register(WM8978_REG_POWER2, power2);
    (void) wm8978_write_register(WM8978_REG_POWER3, power3);

    wm8978_ctx.playback_started = RT_FALSE;
    wm8978_clear_analogue_power_if_idle();
    return RT_EOK;
}

rt_err_t wm8978_start_record(void)
{
    rt_err_t err;
    rt_uint16_t power1_value;
    rt_uint16_t power2_value;

    if (!wm8978_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (wm8978_ctx.record_started)
    {
        return RT_EOK;
    }

    power1_value = (rt_uint16_t) (WM8978_R1_BUFIOEN | WM8978_R1_VMID_5K | WM8978_R1_BIASEN);
#if WM8978_ENABLE_MICBIAS
    power1_value |= WM8978_R1_MICBEN;
#endif
    err = wm8978_write_register(WM8978_REG_POWER1, power1_value);
    if (err != RT_EOK)
    {
        return err;
    }

    rt_thread_mdelay(WM8978_VMID_STARTUP_DELAY_MS);

    power2_value = (rt_uint16_t) (WM8978_R2_INPPGAENL | WM8978_R2_INPPGAENR |
                                  WM8978_R2_ADCENL | WM8978_R2_ADCENR);
    err = wm8978_write_register(WM8978_REG_POWER2, power2_value);
    if (err != RT_EOK)
    {
        return err;
    }

    /* 默认保持数据手册的差分输入连接：LIP/LIN、RIP/RIN。 */
    err = wm8978_write_register(WM8978_REG_INPUT_CTRL, 0x033u);
    if (err != RT_EOK)
    {
        return err;
    }

    wm8978_ctx.record_started = RT_TRUE;
    LOG_I("wm8978 record path started");
    return RT_EOK;
}

rt_err_t wm8978_stop_record(void)
{
    rt_uint16_t power2;

    if (!wm8978_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!wm8978_ctx.record_started)
    {
        return RT_EOK;
    }

    power2 = (rt_uint16_t) (wm8978_ctx.reg_cache[WM8978_REG_POWER2] &
                            (~(WM8978_R2_INPPGAENL | WM8978_R2_INPPGAENR |
                               WM8978_R2_ADCENL | WM8978_R2_ADCENR |
                               WM8978_R2_BOOSTENL | WM8978_R2_BOOSTENR)));
    (void) wm8978_write_register(WM8978_REG_POWER2, power2);

    wm8978_ctx.record_started = RT_FALSE;
    wm8978_clear_analogue_power_if_idle();
    return RT_EOK;
}

const wm8978_config_t * wm8978_get_config(void)
{
    return &wm8978_ctx.config;
}






