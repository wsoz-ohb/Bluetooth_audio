/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       SFUD app for onboard W25Q128
 */
#include "sfud_app.h"

#include <rtdevice.h>
#include <board.h>
#include <drv_spi.h>
#include <sfud.h>

#define DBG_TAG "sfud_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define SFUD_SPI_BUS_NAME       "spi1"
#define SFUD_SPI_DEV_NAME       "w25q"
#define SFUD_FLASH_DEV_NAME     "W25Q128"
#define SFUD_CS_GPIOX           GPIOA
#define SFUD_CS_GPIO_PIN        GPIO_PIN_4

#define SFUD_LCD_CS_PIN         GET_PIN(C, 4)

#define SFUD_SPI_PROBE_HZ       (1 * 1000 * 1000)
#define SFUD_SPI_WORK_HZ        (42 * 1000 * 1000)

#define SFUD_CMD_JEDEC_ID       0x9F

#if defined(RT_USING_SPI) && defined(BSP_USING_SPI1) && defined(RT_USING_SFUD)

static rt_spi_flash_device_t g_flash_dev = RT_NULL;
static rt_bool_t g_sfud_ready = RT_FALSE;

static void sfud_app_hold_peer_cs_idle(void)
{
    rt_pin_mode(SFUD_LCD_CS_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SFUD_LCD_CS_PIN, PIN_HIGH);
}

static void sfud_app_force_spi1_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static rt_err_t sfud_app_attach_spi_device(void)
{
    rt_device_t device;
    rt_err_t err;

    device = rt_device_find(SFUD_SPI_DEV_NAME);
    if (device != RT_NULL)
    {
        return RT_EOK;
    }

    err = rt_hw_spi_device_attach(SFUD_SPI_BUS_NAME,
                                  SFUD_SPI_DEV_NAME,
                                  SFUD_CS_GPIOX,
                                  SFUD_CS_GPIO_PIN);
    if (err != RT_EOK)
    {
        LOG_E("attach %s to %s failed: %d", SFUD_SPI_DEV_NAME, SFUD_SPI_BUS_NAME, err);
        return err;
    }

    device = rt_device_find(SFUD_SPI_DEV_NAME);
    if (device == RT_NULL)
    {
        LOG_E("find spi device %s failed after attach", SFUD_SPI_DEV_NAME);
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t sfud_app_configure_spi(rt_uint32_t max_hz)
{
    struct rt_spi_device *spi;
    struct rt_spi_configuration cfg;

    spi = (struct rt_spi_device *)rt_device_find(SFUD_SPI_DEV_NAME);
    if (spi == RT_NULL)
    {
        return -RT_ERROR;
    }

    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = max_hz;

    return rt_spi_configure(spi, &cfg);
}

static rt_err_t sfud_app_read_jedec_id(rt_uint8_t id[3])
{
    struct rt_spi_device *spi;
    rt_uint8_t tx[4] = {SFUD_CMD_JEDEC_ID, 0xFF, 0xFF, 0xFF};
    rt_uint8_t rx[4] = {0};
    rt_size_t n;
    rt_err_t err;

    if (id == RT_NULL)
    {
        return -RT_EINVAL;
    }

    spi = (struct rt_spi_device *)rt_device_find(SFUD_SPI_DEV_NAME);
    if (spi == RT_NULL)
    {
        return -RT_ERROR;
    }

    err = sfud_app_configure_spi(SFUD_SPI_PROBE_HZ);
    if (err != RT_EOK)
    {
        return err;
    }

    n = rt_spi_transfer(spi, tx, rx, sizeof(tx));
    if (n != sizeof(tx))
    {
        return -RT_EIO;
    }

    id[0] = rx[1];
    id[1] = rx[2];
    id[2] = rx[3];
    return RT_EOK;
}

static void sfud_app_log_jedec_id(const char *tag, const rt_uint8_t id[3])
{
    LOG_I("%s JEDEC ID: %02X %02X %02X",
          tag,
          id[0], id[1], id[2]);

    if ((id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00) ||
        (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF))
    {
        LOG_E("JEDEC invalid (all 0x%02X). 这是物理层读数，不是 SFUD 表问题。", id[0]);
        LOG_E("请查: Flash 3V3/GND, CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7, WP#/HOLD# 上拉");
    }
    else if (id[0] == 0xEF && id[2] == 0x18)
    {
        LOG_I("JEDEC looks like Winbond 128Mbit family (type=0x%02X)", id[1]);
    }
}

rt_err_t sfud_app_init(void)
{
    sfud_flash_t sfud_dev;
    struct rt_spi_configuration spi_cfg;
    rt_uint8_t jedec[3] = {0};
    rt_err_t err;

    if (g_sfud_ready)
    {
        return RT_EOK;
    }

    {
        rt_device_t existed = rt_device_find(SFUD_FLASH_DEV_NAME);
        if (existed != RT_NULL && existed->user_data != RT_NULL)
        {
            g_flash_dev = (rt_spi_flash_device_t)existed->user_data;
            g_sfud_ready = RT_TRUE;
            LOG_I("flash device %s already exists, reuse", SFUD_FLASH_DEV_NAME);
            return RT_EOK;
        }
    }

    sfud_app_hold_peer_cs_idle();
    sfud_app_force_spi1_gpio();

    err = sfud_app_attach_spi_device();
    if (err != RT_EOK)
    {
        return err;
    }

    err = sfud_app_read_jedec_id(jedec);
    if (err != RT_EOK)
    {
        LOG_E("manual JEDEC read failed: %d", err);
        return err;
    }
    sfud_app_log_jedec_id("pre-probe", jedec);

    if ((jedec[0] == 0x00 && jedec[1] == 0x00 && jedec[2] == 0x00) ||
        (jedec[0] == 0xFF && jedec[1] == 0xFF && jedec[2] == 0xFF))
    {
        LOG_E("skip SFUD probe because JEDEC is invalid");
        return -RT_ERROR;
    }

    spi_cfg.data_width = 8;
    spi_cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    spi_cfg.max_hz = SFUD_SPI_PROBE_HZ;

    g_flash_dev = rt_sfud_flash_probe_ex(SFUD_FLASH_DEV_NAME,
                                         SFUD_SPI_DEV_NAME,
                                         &spi_cfg,
                                         RT_NULL);
    if (g_flash_dev == RT_NULL)
    {
        LOG_E("rt_sfud_flash_probe_ex(%s, %s) failed, jedec=%02X %02X %02X",
              SFUD_FLASH_DEV_NAME, SFUD_SPI_DEV_NAME,
              jedec[0], jedec[1], jedec[2]);
        return -RT_ERROR;
    }

    (void)sfud_app_configure_spi(SFUD_SPI_WORK_HZ);

    sfud_dev = (sfud_flash_t)g_flash_dev->user_data;

    if (sfud_dev != RT_NULL)
    {
        uint8_t sreg = 0;
        if (sfud_read_status(sfud_dev, &sreg) == SFUD_SUCCESS && (sreg & 0x3C) != 0)
        {
            LOG_W("W25Q status reg=0x%02X, BP bits set -> clearing write protect", sreg);
            if (sfud_write_status(sfud_dev, RT_FALSE, 0x00) == SFUD_SUCCESS)
            {
                LOG_I("write protect cleared");
            }
            else
            {
                LOG_E("clear write protect FAILED, erase/write will be silently ignored!");
            }
        }
    }

    if (sfud_dev != RT_NULL)
    {
        LOG_I("SFUD ready: name=%s, capacity=%d KB, erase=%d, write_mode=0x%02x, jedec=%02X%02X%02X",
              sfud_dev->chip.name ? sfud_dev->chip.name : SFUD_FLASH_DEV_NAME,
              (int)(sfud_dev->chip.capacity / 1024),
              (int)sfud_dev->chip.erase_gran,
              (int)sfud_dev->chip.write_mode,
              sfud_dev->chip.mf_id,
              sfud_dev->chip.type_id,
              sfud_dev->chip.capacity_id);
    }
    else
    {
        LOG_I("SFUD block device %s registered", SFUD_FLASH_DEV_NAME);
    }

    g_sfud_ready = RT_TRUE;
    return RT_EOK;
}

rt_bool_t sfud_app_is_ready(void)
{
    return g_sfud_ready;
}

const char *sfud_app_flash_name(void)
{
    return SFUD_FLASH_DEV_NAME;
}

sfud_flash_t sfud_app_get_sfud(void)
{
    if (!g_sfud_ready || g_flash_dev == RT_NULL)
    {
        return RT_NULL;
    }

    return (sfud_flash_t)g_flash_dev->user_data;
}

rt_spi_flash_device_t sfud_app_get_device(void)
{
    return g_sfud_ready ? g_flash_dev : RT_NULL;
}

#ifdef RT_USING_FINSH
static void sfud_app_info(int argc, char **argv)
{
    sfud_flash_t sfud_dev;
    struct rt_device_blk_geometry geo;
    rt_device_t dev;
    rt_uint8_t jedec[3] = {0};

    (void)argc;
    (void)argv;

    sfud_app_hold_peer_cs_idle();
    sfud_app_force_spi1_gpio();
    if (sfud_app_attach_spi_device() == RT_EOK &&
        sfud_app_read_jedec_id(jedec) == RT_EOK)
    {
        rt_kprintf("raw JEDEC    : %02X %02X %02X\n", jedec[0], jedec[1], jedec[2]);
    }

    if (sfud_app_init() != RT_EOK)
    {
        rt_kprintf("sfud_app not ready\n");
        return;
    }

    sfud_dev = sfud_app_get_sfud();
    dev = rt_device_find(SFUD_FLASH_DEV_NAME);

    rt_kprintf("spi bus     : %s\n", SFUD_SPI_BUS_NAME);
    rt_kprintf("spi device  : %s (CS=PA4)\n", SFUD_SPI_DEV_NAME);
    rt_kprintf("flash device: %s\n", SFUD_FLASH_DEV_NAME);

    if (sfud_dev != RT_NULL)
    {
        rt_kprintf("chip name   : %s\n", sfud_dev->chip.name ? sfud_dev->chip.name : "(null)");
        rt_kprintf("mf_id       : 0x%02X\n", sfud_dev->chip.mf_id);
        rt_kprintf("type_id     : 0x%02X\n", sfud_dev->chip.type_id);
        rt_kprintf("capacity_id : 0x%02X\n", sfud_dev->chip.capacity_id);
        rt_kprintf("capacity    : %u bytes (%u KB)\n",
                   (unsigned)sfud_dev->chip.capacity,
                   (unsigned)(sfud_dev->chip.capacity / 1024));
        rt_kprintf("erase_gran  : %u\n", (unsigned)sfud_dev->chip.erase_gran);
    }

    if (dev != RT_NULL)
    {
        rt_memset(&geo, 0, sizeof(geo));
        if (rt_device_control(dev, RT_DEVICE_CTRL_BLK_GETGEOME, &geo) == RT_EOK)
        {
            rt_kprintf("blk sector  : %u bytes\n", (unsigned)geo.bytes_per_sector);
            rt_kprintf("blk count   : %u\n", (unsigned)geo.sector_count);
            rt_kprintf("blk block   : %u\n", (unsigned)geo.block_size);
        }
    }
}
MSH_CMD_EXPORT(sfud_app_info, show onboard SPI flash SFUD info);

static void sfud_app_jedec(int argc, char **argv)
{
    rt_uint8_t jedec[3] = {0};
    rt_err_t err;

    (void)argc;
    (void)argv;

    sfud_app_hold_peer_cs_idle();
    sfud_app_force_spi1_gpio();
    err = sfud_app_attach_spi_device();
    if (err != RT_EOK)
    {
        rt_kprintf("attach failed: %d\n", err);
        return;
    }

    err = sfud_app_read_jedec_id(jedec);
    if (err != RT_EOK)
    {
        rt_kprintf("JEDEC read failed: %d\n", err);
        return;
    }

    rt_kprintf("JEDEC ID: %02X %02X %02X\n", jedec[0], jedec[1], jedec[2]);
    if ((jedec[0] == 0x00 && jedec[1] == 0x00 && jedec[2] == 0x00) ||
        (jedec[0] == 0xFF && jedec[1] == 0xFF && jedec[2] == 0xFF))
    {
        rt_kprintf("invalid id -> hardware path still broken\n");
    }
}
MSH_CMD_EXPORT(sfud_app_jedec, read raw JEDEC ID from SPI flash);

static void sfud_app_hwcheck(int argc, char **argv)
{
    rt_uint8_t jedec[3] = {0};
    uint32_t idr_cs_high;
    uint32_t idr_cs_low;
    rt_err_t err;

    (void)argc;
    (void)argv;

    sfud_app_hold_peer_cs_idle();
    sfud_app_force_spi1_gpio();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    rt_thread_mdelay(1);
    idr_cs_high = GPIOA->IDR;

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    rt_thread_mdelay(1);
    idr_cs_low = GPIOA->IDR;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

    rt_kprintf("GPIOA IDR CS=1: 0x%08X  (PA4=%d PA5=%d PA6=%d PA7=%d)\n",
               (unsigned)idr_cs_high,
               (idr_cs_high >> 4) & 1,
               (idr_cs_high >> 5) & 1,
               (idr_cs_high >> 6) & 1,
               (idr_cs_high >> 7) & 1);
    rt_kprintf("GPIOA IDR CS=0: 0x%08X  (PA4=%d PA5=%d PA6=%d PA7=%d)\n",
               (unsigned)idr_cs_low,
               (idr_cs_low >> 4) & 1,
               (idr_cs_low >> 5) & 1,
               (idr_cs_low >> 6) & 1,
               (idr_cs_low >> 7) & 1);

    err = sfud_app_attach_spi_device();
    if (err != RT_EOK)
    {
        rt_kprintf("attach failed: %d\n", err);
        return;
    }

    err = sfud_app_read_jedec_id(jedec);
    if (err != RT_EOK)
    {
        rt_kprintf("JEDEC transfer failed: %d\n", err);
        return;
    }

    rt_kprintf("JEDEC ID: %02X %02X %02X\n", jedec[0], jedec[1], jedec[2]);
    rt_kprintf("expect Winbond W25Q128 ~= EF 40 18 or EF 70 18\n");
    rt_kprintf("if still 00/FF: measure Flash VCC, CS pulse, SCK, MOSI, MISO with scope/meter\n");
}
MSH_CMD_EXPORT(sfud_app_hwcheck, check SPI flash GPIO and JEDEC path);
#endif

#else

rt_err_t sfud_app_init(void)
{
    LOG_E("sfud app unavailable: enable RT_USING_SPI, BSP_USING_SPI1 and RT_USING_SFUD first");
    return -RT_ERROR;
}

rt_bool_t sfud_app_is_ready(void)
{
    return RT_FALSE;
}

const char *sfud_app_flash_name(void)
{
    return SFUD_FLASH_DEV_NAME;
}

sfud_flash_t sfud_app_get_sfud(void)
{
    return RT_NULL;
}

rt_spi_flash_device_t sfud_app_get_device(void)
{
    return RT_NULL;
}

#ifdef RT_USING_FINSH
static void sfud_app_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("sfud app unavailable\n");
}
MSH_CMD_EXPORT(sfud_app_info, show onboard SPI flash SFUD info);

static void sfud_app_jedec(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("sfud app unavailable\n");
}
MSH_CMD_EXPORT(sfud_app_jedec, read raw JEDEC ID from SPI flash);

static void sfud_app_hwcheck(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("sfud app unavailable\n");
}
MSH_CMD_EXPORT(sfud_app_hwcheck, check SPI flash GPIO and JEDEC path);
#endif

#endif
