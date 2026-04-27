#include "es8311_driver.h"

#include <string.h>

#define DBG_TAG "es8311"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define ES8311_RESET_REG                 0x00u
#define ES8311_CLKMGR1_REG               0x01u
#define ES8311_CLKMGR2_REG               0x02u
#define ES8311_CLKMGR3_REG               0x03u
#define ES8311_CLKMGR4_REG               0x04u
#define ES8311_CLKMGR5_REG               0x05u
#define ES8311_CLKMGR6_REG               0x06u
#define ES8311_CLKMGR7_REG               0x07u
#define ES8311_CLKMGR8_REG               0x08u
#define ES8311_SDPIN_REG                 0x09u
#define ES8311_SDPOUT_REG                0x0Au
#define ES8311_SYS3_REG                  0x0Du
#define ES8311_SYS4_REG                  0x0Eu
#define ES8311_SYS8_REG                  0x12u
#define ES8311_SYS9_REG                  0x13u
#define ES8311_SYS10_REG                 0x14u
#define ES8311_ADC1_REG                  0x15u
#define ES8311_ADC2_REG                  0x16u
#define ES8311_ADC3_REG                  0x17u
#define ES8311_ADC4_REG                  0x18u
#define ES8311_ADC5_REG                  0x19u
#define ES8311_ADC6_REG                  0x1Au
#define ES8311_ADC7_REG                  0x1Bu
#define ES8311_ADC8_REG                  0x1Cu
#define ES8311_DAC1_REG                  0x31u
#define ES8311_DAC2_REG                  0x32u
#define ES8311_DAC6_REG                  0x37u
#define ES8311_GPIO_REG                  0x44u
#define ES8311_GP_REG                    0x45u
#define ES8311_CHIPID1_REG               0xFDu
#define ES8311_CHIPID2_REG               0xFEu

#define ES8311_RESET_CSM_ON              (1u << 7)
#define ES8311_RESET_MSC                 (1u << 6)
#define ES8311_RESET_RST_MASK            0x1Fu

#define ES8311_CLKMGR1_SCLK_SEL          (1u << 7)
#define ES8311_CLKMGR1_PLAYBACK_MCLK     0x3Fu
#define ES8311_CLKMGR1_PLAYBACK_SCLK     (ES8311_CLKMGR1_SCLK_SEL | ES8311_CLKMGR1_PLAYBACK_MCLK)

#define ES8311_SDPIN_SEL_SHIFT           7u
#define ES8311_SDP_MUTE                  (1u << 6)
#define ES8311_SDP_WL_SHIFT              2u
#define ES8311_SDP_FMT_I2S               0x00u
#define ES8311_SDP_FMT_LEFT_J            0x01u
#define ES8311_SDP_FMT_DSP               0x03u
#define ES8311_SDP_WL_24                 0x00u
#define ES8311_SDP_WL_20                 0x01u
#define ES8311_SDP_WL_18                 0x02u
#define ES8311_SDP_WL_16                 0x03u
#define ES8311_SDP_WL_32                 0x04u

#define ES8311_SYS8_PDN_DAC              (1u << 1)
#define ES8311_SYS9_HPSW                 (1u << 4)
#define ES8311_SYS9_PDN_ADC              (1u << 0)
#define ES8311_SYS10_DMIC_ON             (1u << 6)
#define ES8311_SYS10_ANALOG_MIC_ENABLE   0x1Au

#define ES8311_DAC1_DSM_MUTE             (1u << 6)
#define ES8311_DAC1_DEM_MUTE             (1u << 5)
#define ES8311_DAC1_MUTE_MASK            (ES8311_DAC1_DSM_MUTE | ES8311_DAC1_DEM_MUTE)

#define ES8311_ADC1_RECORD_DEFAULT       0x40u
#define ES8311_ADC2_INPUT_BOOST          0x20u
#define ES8311_ADC2_GAIN_SCALE_MASK      0x0Fu
#define ES8311_ADC3_VOLUME_DEFAULT       0xBFu
#define ES8311_ADC4_DEFAULT              0x0Cu
#define ES8311_ADC5_DEFAULT              0x00u
#define ES8311_ADC6_DEFAULT              0x30u
#define ES8311_ADC7_DEFAULT              0x0Au
#define ES8311_ADC8_DEFAULT              0x6Au

#define ES8311_CHIPID1_VALUE             0x83u
#define ES8311_CHIPID2_VALUE             0x11u

#define ES8311_I2C_RETRY_COUNT           8
#define ES8311_I2C_RETRY_DELAY_MS        2u
#define ES8311_VMID_STARTUP_DELAY_MS     20u
#define ES8311_DEFAULT_DAC_VOLUME        0xC0u
#define ES8311_POWER_UP_ANALOG           0x01u
#define ES8311_POWER_UP_ADC_DAC          0x02u
#define ES8311_PLAYBACK_BCLK_CFG         0x03u
#define ES8311_PLAYBACK_LRCK_HIGH        0x00u
#define ES8311_PLAYBACK_LRCK_LOW         0xFFu

typedef struct
{
    struct rt_i2c_bus_device * bus;
    es8311_config_t config;
    rt_bool_t inited;
    rt_bool_t playback_started;
    rt_bool_t record_started;
} es8311_context_t;

static es8311_context_t es8311_ctx;

static void es8311_load_default_config(es8311_config_t * config)
{
    RT_ASSERT(config != RT_NULL);

    config->sample_rate = ES8311_DEFAULT_SAMPLE_RATE;
    config->channels = ES8311_DEFAULT_CHANNELS;
    config->bits_per_sample = ES8311_DEFAULT_BITS_PER_SAMPLE;
    config->audio_format = ES8311_AUDIO_FMT_I2S;
    config->use_mclk = (rt_bool_t) ES8311_DEFAULT_USE_MCLK;
    config->dac_source = ES8311_DAC_SOURCE_LEFT;
    config->input_mode = ES8311_INPUT_MIC;
    config->mic_gain = ES8311_MIC_GAIN_0DB;
}

static rt_err_t es8311_write_register(rt_uint8_t reg, rt_uint8_t value)
{
    rt_uint8_t buffer[2];
    struct rt_i2c_msg msg;
    rt_size_t transferred;
    int retry;

    if (es8311_ctx.bus == RT_NULL)
    {
        return -RT_ERROR;
    }

    buffer[0] = reg;
    buffer[1] = value;

    msg.addr = ES8311_I2C_ADDR;
    msg.flags = RT_I2C_WR;
    msg.buf = buffer;
    msg.len = sizeof(buffer);

    for (retry = 0; retry < ES8311_I2C_RETRY_COUNT; retry++)
    {
        transferred = rt_i2c_transfer(es8311_ctx.bus, &msg, 1);
        if (transferred == 1u)
        {
            return RT_EOK;
        }
        rt_thread_mdelay(ES8311_I2C_RETRY_DELAY_MS);
    }

    LOG_E("es8311 write failed, reg=0x%02x, value=0x%02x", reg, value);
    return -RT_ERROR;
}

static rt_err_t es8311_read_register(rt_uint8_t reg, rt_uint8_t * value)
{
    struct rt_i2c_msg msgs[2];
    rt_size_t transferred;
    int retry;

    if ((es8311_ctx.bus == RT_NULL) || (value == RT_NULL))
    {
        return -RT_EINVAL;
    }

    msgs[0].addr = ES8311_I2C_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1u;

    msgs[1].addr = ES8311_I2C_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = value;
    msgs[1].len = 1u;

    for (retry = 0; retry < ES8311_I2C_RETRY_COUNT; retry++)
    {
        transferred = rt_i2c_transfer(es8311_ctx.bus, msgs, 2);
        if (transferred == 2u)
        {
            return RT_EOK;
        }
        rt_thread_mdelay(ES8311_I2C_RETRY_DELAY_MS);
    }

    LOG_E("es8311 read failed, reg=0x%02x", reg);
    return -RT_ERROR;
}

static rt_err_t es8311_update_register(rt_uint8_t reg, rt_uint8_t mask, rt_uint8_t value)
{
    rt_uint8_t old_value;
    rt_uint8_t new_value;

    if (es8311_read_register(reg, &old_value) != RT_EOK)
    {
        return -RT_ERROR;
    }

    new_value = (rt_uint8_t) ((old_value & (~mask)) | (value & mask));
    if (new_value == old_value)
    {
        return RT_EOK;
    }

    return es8311_write_register(reg, new_value);
}

static rt_uint8_t es8311_word_length_to_reg(rt_uint8_t bits_per_sample)
{
    switch (bits_per_sample)
    {
    case 16u:
        return ES8311_SDP_WL_16;
    case 18u:
        return ES8311_SDP_WL_18;
    case 20u:
        return ES8311_SDP_WL_20;
    case 24u:
        return ES8311_SDP_WL_24;
    case 32u:
        return ES8311_SDP_WL_32;
    default:
        return 0xFFu;
    }
}

static rt_uint8_t es8311_format_to_reg(es8311_audio_format_t format)
{
    switch (format)
    {
    case ES8311_AUDIO_FMT_I2S:
        return ES8311_SDP_FMT_I2S;
    case ES8311_AUDIO_FMT_LEFT_J:
        return ES8311_SDP_FMT_LEFT_J;
    case ES8311_AUDIO_FMT_DSP:
        return ES8311_SDP_FMT_DSP;
    default:
        return 0xFFu;
    }
}

static rt_uint8_t es8311_mic_gain_to_reg(es8311_mic_gain_t mic_gain)
{
    if ((rt_uint32_t) mic_gain > (rt_uint32_t) ES8311_MIC_GAIN_42DB)
    {
        return 0xFFu;
    }

    return (rt_uint8_t) mic_gain;
}

static rt_err_t es8311_validate_config(const es8311_config_t * config)
{
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

    if (es8311_word_length_to_reg(config->bits_per_sample) == 0xFFu)
    {
        return -RT_EINVAL;
    }

    if (es8311_format_to_reg(config->audio_format) == 0xFFu)
    {
        return -RT_EINVAL;
    }

    if ((config->dac_source != ES8311_DAC_SOURCE_LEFT) &&
        (config->dac_source != ES8311_DAC_SOURCE_RIGHT))
    {
        return -RT_EINVAL;
    }

    if ((config->input_mode != ES8311_INPUT_MIC) &&
        (config->input_mode != ES8311_INPUT_DMIC))
    {
        return -RT_EINVAL;
    }

    if (es8311_mic_gain_to_reg(config->mic_gain) == 0xFFu)
    {
        return -RT_EINVAL;
    }

    /*
     * 当前蓝牙 A2DP 播放链路只实际协商 44.1k / 48k。
     * 这里故意只放开已经核实过的采样率，避免宣称支持更多但时钟参数不可靠。
     */
    if ((config->sample_rate != 44100u) && (config->sample_rate != 48000u))
    {
        return -RT_EINVAL;
    }

    return RT_EOK;
}

static rt_err_t es8311_soft_reset(void)
{
    rt_err_t err;

    err = es8311_write_register(ES8311_RESET_REG, ES8311_RESET_RST_MASK);
    if (err != RT_EOK)
    {
        return err;
    }

    rt_thread_mdelay(5);

    err = es8311_write_register(ES8311_RESET_REG, 0x00u);
    if (err != RT_EOK)
    {
        return err;
    }

    rt_thread_mdelay(1);

    err = es8311_write_register(ES8311_RESET_REG, ES8311_RESET_CSM_ON);
    if (err != RT_EOK)
    {
        return err;
    }

    rt_thread_mdelay(5);
    return RT_EOK;
}

static rt_err_t es8311_probe_chip_id(void)
{
    rt_uint8_t chipid1;
    rt_uint8_t chipid2;

    if (es8311_read_register(ES8311_CHIPID1_REG, &chipid1) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_read_register(ES8311_CHIPID2_REG, &chipid2) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if ((chipid1 != ES8311_CHIPID1_VALUE) || (chipid2 != ES8311_CHIPID2_VALUE))
    {
        LOG_W("unexpected chip id: 0x%02x 0x%02x", chipid1, chipid2);
    }
    else
    {
        LOG_I("chip id ok: 0x%02x 0x%02x", chipid1, chipid2);
    }

    return RT_EOK;
}

static rt_err_t es8311_apply_clock_config(const es8311_config_t * config)
{
    rt_uint8_t clkmgr1;
    rt_uint8_t clkmgr2;
    rt_uint8_t clkmgr3;
    rt_uint8_t clkmgr4;
    rt_uint8_t clkmgr5;
    rt_uint8_t clkmgr6;
    rt_uint8_t clkmgr7;
    rt_uint8_t clkmgr8;

    /*
     * 按当前项目的实际场景固定最小稳定配置：
     * STM32 I2S2 Master TX + 16bit stereo slot + ES8311 Slave DAC。
     * 44.1k / 48k 在 256*Fs MCLK 下可共用这组寄存器。
     */
    clkmgr1 = config->use_mclk ? ES8311_CLKMGR1_PLAYBACK_MCLK : ES8311_CLKMGR1_PLAYBACK_SCLK;
    clkmgr2 = 0x00u;
    clkmgr3 = 0x10u;
    clkmgr4 = 0x10u;
    clkmgr5 = 0x00u;
    clkmgr6 = ES8311_PLAYBACK_BCLK_CFG;
    clkmgr7 = ES8311_PLAYBACK_LRCK_HIGH;
    clkmgr8 = ES8311_PLAYBACK_LRCK_LOW;

    if (es8311_write_register(ES8311_CLKMGR1_REG, clkmgr1) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR2_REG, clkmgr2) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR3_REG, clkmgr3) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR4_REG, clkmgr4) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR5_REG, clkmgr5) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR6_REG, clkmgr6) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR7_REG, clkmgr7) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_CLKMGR8_REG, clkmgr8) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t es8311_apply_interface_config(const es8311_config_t * config)
{
    rt_uint8_t wl;
    rt_uint8_t fmt;
    rt_uint8_t sdp_value;
    rt_uint8_t dac_sel;

    wl = es8311_word_length_to_reg(config->bits_per_sample);
    fmt = es8311_format_to_reg(config->audio_format);
    if ((wl == 0xFFu) || (fmt == 0xFFu))
    {
        return -RT_EINVAL;
    }

    dac_sel = (rt_uint8_t) (((rt_uint8_t) config->dac_source) << ES8311_SDPIN_SEL_SHIFT);
    sdp_value = (rt_uint8_t) (dac_sel | (wl << ES8311_SDP_WL_SHIFT) | fmt);

    if (es8311_update_register(ES8311_RESET_REG, ES8311_RESET_MSC, 0u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SDPIN_REG, sdp_value) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SDPOUT_REG, (rt_uint8_t) ((wl << ES8311_SDP_WL_SHIFT) | fmt)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t es8311_apply_playback_defaults(const es8311_config_t * config)
{
    if (config == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (es8311_write_register(ES8311_SYS4_REG, 0x02u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SYS10_REG, 0x00u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC8_REG, ES8311_ADC8_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_DAC6_REG, 0x08u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_DAC2_REG, ES8311_DEFAULT_DAC_VOLUME) != RT_EOK)
    {
        return -RT_ERROR;
    }

    /*
     * ES8311 是单 DAC，双声道 I2S 输入场景下只能选择 Left 或 Right 时隙。
     * 这里不做上层 PCM downmix，只固定到当前配置指定的一个声道。
     */
    if (es8311_update_register(ES8311_SDPIN_REG,
                               (rt_uint8_t) (1u << ES8311_SDPIN_SEL_SHIFT),
                               (rt_uint8_t) (((rt_uint8_t) config->dac_source) << ES8311_SDPIN_SEL_SHIFT)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_GPIO_REG, 0x00u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t es8311_apply_record_defaults(const es8311_config_t * config)
{
    rt_uint8_t sys10;
    rt_uint8_t pga_gain;

    if (config == RT_NULL)
    {
        return -RT_EINVAL;
    }

    pga_gain = es8311_mic_gain_to_reg(config->mic_gain);
    if (pga_gain == 0xFFu)
    {
        return -RT_EINVAL;
    }

    sys10 = ES8311_SYS10_ANALOG_MIC_ENABLE;
    if (config->input_mode == ES8311_INPUT_DMIC)
    {
        sys10 |= ES8311_SYS10_DMIC_ON;
    }

    if (es8311_write_register(ES8311_SYS10_REG, sys10) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC1_REG, ES8311_ADC1_RECORD_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC2_REG,
                              (rt_uint8_t) (ES8311_ADC2_INPUT_BOOST |
                                            (pga_gain & ES8311_ADC2_GAIN_SCALE_MASK))) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC3_REG, ES8311_ADC3_VOLUME_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC4_REG, ES8311_ADC4_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC5_REG, ES8311_ADC5_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC6_REG, ES8311_ADC6_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC7_REG, ES8311_ADC7_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_ADC8_REG, ES8311_ADC8_DEFAULT) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_GP_REG, 0x00u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t es8311_apply_config(const es8311_config_t * config)
{
    if (es8311_validate_config(config) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    if (es8311_apply_clock_config(config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_apply_interface_config(config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_apply_playback_defaults(config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t es8311_init(void)
{
    rt_err_t err;

    rt_memset(&es8311_ctx, 0, sizeof(es8311_ctx));
    es8311_ctx.bus = (struct rt_i2c_bus_device *) rt_device_find(RT_I2C_DEVICE);
    if (es8311_ctx.bus == RT_NULL)
    {
        LOG_E("es8311 i2c bus not found: %s", RT_I2C_DEVICE);
        return -RT_ERROR;
    }

    es8311_load_default_config(&es8311_ctx.config);

    err = es8311_soft_reset();
    if (err != RT_EOK)
    {
        LOG_E("es8311 reset failed");
        return err;
    }

    (void) es8311_probe_chip_id();

    err = es8311_apply_config(&es8311_ctx.config);
    if (err != RT_EOK)
    {
        LOG_E("es8311 apply default config failed");
        return err;
    }

    es8311_ctx.inited = RT_TRUE;
    LOG_I("es8311 init ok, sample_rate=%u, bits=%u, use_mclk=%d, dac_source=%u",
          es8311_ctx.config.sample_rate,
          es8311_ctx.config.bits_per_sample,
          es8311_ctx.config.use_mclk,
          es8311_ctx.config.dac_source);
    return RT_EOK;
}

rt_err_t es8311_configure(const es8311_config_t * config)
{
    if (!es8311_ctx.inited || (config == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if (es8311_apply_config(config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    es8311_ctx.config = *config;
    return RT_EOK;
}

rt_err_t es8311_set_sample_rate(rt_uint32_t sample_rate)
{
    es8311_config_t config;

    if (!es8311_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (es8311_ctx.config.sample_rate == sample_rate)
    {
        return RT_EOK;
    }

    config = es8311_ctx.config;
    config.sample_rate = sample_rate;
    return es8311_configure(&config);
}

rt_err_t es8311_start_playback(void)
{
    if (!es8311_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (es8311_ctx.playback_started)
    {
        return RT_EOK;
    }

    if (es8311_apply_config(&es8311_ctx.config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SYS3_REG, ES8311_POWER_UP_ANALOG) != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_thread_mdelay(ES8311_VMID_STARTUP_DELAY_MS);

    if (es8311_write_register(ES8311_SYS8_REG, 0x00u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SYS9_REG, ES8311_SYS9_HPSW) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_update_register(ES8311_SDPIN_REG, ES8311_SDP_MUTE, 0u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_update_register(ES8311_DAC1_REG, ES8311_DAC1_MUTE_MASK, 0u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    es8311_ctx.playback_started = RT_TRUE;
    LOG_I("es8311 playback started, sample_rate=%u, source=%s",
          es8311_ctx.config.sample_rate,
          (es8311_ctx.config.dac_source == ES8311_DAC_SOURCE_LEFT) ? "left" : "right");
    return RT_EOK;
}

rt_err_t es8311_stop_playback(void)
{
    if (!es8311_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!es8311_ctx.playback_started)
    {
        return RT_EOK;
    }

    (void) es8311_update_register(ES8311_DAC1_REG, ES8311_DAC1_MUTE_MASK, ES8311_DAC1_MUTE_MASK);
    (void) es8311_update_register(ES8311_SDPIN_REG, ES8311_SDP_MUTE, ES8311_SDP_MUTE);

    es8311_ctx.playback_started = RT_FALSE;
    LOG_I("es8311 playback stopped");
    return RT_EOK;
}

rt_err_t es8311_start_record(void)
{
    if (!es8311_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (es8311_ctx.record_started)
    {
        return RT_EOK;
    }

    if (es8311_apply_clock_config(&es8311_ctx.config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_apply_interface_config(&es8311_ctx.config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_apply_record_defaults(&es8311_ctx.config) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SYS3_REG, ES8311_POWER_UP_ANALOG) != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_thread_mdelay(ES8311_VMID_STARTUP_DELAY_MS);

    if (es8311_write_register(ES8311_SYS4_REG, ES8311_POWER_UP_ADC_DAC) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_write_register(ES8311_SYS8_REG, 0x00u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_update_register(ES8311_SYS9_REG, ES8311_SYS9_PDN_ADC, 0u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (es8311_update_register(ES8311_SDPOUT_REG, ES8311_SDP_MUTE, 0u) != RT_EOK)
    {
        return -RT_ERROR;
    }

    es8311_ctx.record_started = RT_TRUE;
    LOG_I("es8311 record path started, input=%s, mic_gain=%u",
          (es8311_ctx.config.input_mode == ES8311_INPUT_DMIC) ? "dmic" : "mic",
          (rt_uint32_t) es8311_ctx.config.mic_gain * 6u);
    return RT_EOK;
}

rt_err_t es8311_stop_record(void)
{
    if (!es8311_ctx.inited)
    {
        return -RT_ERROR;
    }

    if (!es8311_ctx.record_started)
    {
        return RT_EOK;
    }

    (void) es8311_update_register(ES8311_SDPOUT_REG, ES8311_SDP_MUTE, ES8311_SDP_MUTE);
    (void) es8311_update_register(ES8311_SYS9_REG, ES8311_SYS9_PDN_ADC, ES8311_SYS9_PDN_ADC);

    es8311_ctx.record_started = RT_FALSE;
    LOG_I("es8311 record path stopped");
    return RT_EOK;
}

rt_err_t es8311_set_mic_gain(es8311_mic_gain_t mic_gain)
{
    rt_uint8_t reg_value;

    reg_value = es8311_mic_gain_to_reg(mic_gain);
    if (reg_value == 0xFFu)
    {
        return -RT_EINVAL;
    }

    es8311_ctx.config.mic_gain = mic_gain;

    if (es8311_ctx.record_started)
    {
        if (es8311_update_register(ES8311_ADC2_REG, ES8311_ADC2_GAIN_SCALE_MASK, reg_value) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

const es8311_config_t * es8311_get_config(void)
{
    return &es8311_ctx.config;
}
