/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-05-20     26410       the first version
 */
#include "lcd_app.h"

#include <rtdevice.h>
#include <board.h>
#include <drv_spi.h>
#include <lcd.h>

#define DBG_TAG "lcd_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define LCD_SPI_BUS_NAME        "spi1"
#define LCD_SPI_DEVICE_NAME     "st7789"
#define LCD_SPI_MAX_HZ          (20 * 1000 * 1000)

#define LCD_CS_GPIOX            GPIOA
#define LCD_CS_GPIO_PIN         GPIO_PIN_4
#define LCD_DC_PIN              GET_PIN(E, 4)
#define LCD_RST_PIN             GET_PIN(E, 5)

#if defined(RT_USING_SPI) && defined(BSP_USING_SPI1)

static struct rt_spi_device *g_lcd_spi = RT_NULL;
static rt_bool_t g_lcd_ready = RT_FALSE;

static int lcd_app_write_command(void *user, const uint8_t *data, size_t size)
{
    struct rt_spi_device *spi = (struct rt_spi_device *)user;

    if (spi == RT_NULL || (data == RT_NULL && size != 0))
    {
        return -1;
    }

    rt_pin_write(LCD_DC_PIN, PIN_LOW);
    return (rt_spi_send(spi, data, size) == size) ? 0 : -1;
}

static int lcd_app_write_data(void *user, const uint8_t *data, size_t size)
{
    struct rt_spi_device *spi = (struct rt_spi_device *)user;

    if (spi == RT_NULL || (data == RT_NULL && size != 0))
    {
        return -1;
    }

    rt_pin_write(LCD_DC_PIN, PIN_HIGH);
    return (rt_spi_send(spi, data, size) == size) ? 0 : -1;
}

static void lcd_app_delay_ms(void *user, uint32_t ms)
{
    (void)user;
    rt_thread_mdelay(ms);
}

static void lcd_app_reset(void *user, int level)
{
    (void)user;
    rt_pin_write(LCD_RST_PIN, level ? PIN_HIGH : PIN_LOW);
}

static rt_err_t lcd_app_prepare_gpio(void)
{
    rt_pin_mode(LCD_DC_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LCD_RST_PIN, PIN_MODE_OUTPUT);

    rt_pin_write(LCD_DC_PIN, PIN_HIGH);
    rt_pin_write(LCD_RST_PIN, PIN_HIGH);
    return RT_EOK;
}

static rt_err_t lcd_app_attach_spi_device(void)
{
    rt_device_t device;
    rt_err_t err;

    device = rt_device_find(LCD_SPI_DEVICE_NAME);
    if (device != RT_NULL)
    {
        g_lcd_spi = (struct rt_spi_device *)device;
        return RT_EOK;
    }

    err = rt_hw_spi_device_attach(LCD_SPI_BUS_NAME,
                                  LCD_SPI_DEVICE_NAME,
                                  LCD_CS_GPIOX,
                                  LCD_CS_GPIO_PIN);
    if (err != RT_EOK)
    {
        LOG_E("attach %s to %s failed: %d", LCD_SPI_DEVICE_NAME, LCD_SPI_BUS_NAME, err);
        return err;
    }

    device = rt_device_find(LCD_SPI_DEVICE_NAME);
    if (device == RT_NULL)
    {
        LOG_E("find spi device %s failed after attach", LCD_SPI_DEVICE_NAME);
        return -RT_ERROR;
    }

    g_lcd_spi = (struct rt_spi_device *)device;
    return RT_EOK;
}

static rt_err_t lcd_app_configure_spi(void)
{
    struct rt_spi_configuration cfg;

    if (g_lcd_spi == RT_NULL)
    {
        return -RT_ERROR;
    }

    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = LCD_SPI_MAX_HZ;

    return rt_spi_configure(g_lcd_spi, &cfg);
}

static void lcd_app_show_test_pattern(void)
{
    u16 width = LCD_GetWidth();
    u16 height = LCD_GetHeight();
    u16 band = height / 5;

    LCD_Fill(0, 0, width, height, BLACK);

    LCD_Fill(0, 0, width, band, RED);
    LCD_Fill(0, band, width, band * 2, GREEN);
    LCD_Fill(0, band * 2, width, band * 3, BLUE);
    LCD_Fill(0, band * 3, width, band * 4, YELLOW);
    LCD_Fill(0, band * 4, width, height, WHITE);

    LCD_DrawRectangle(4, 4, width - 5, height - 5, BLACK);
    Draw_Circle(width / 2, height / 2, 36, BLACK);

    LCD_ShowString(12, 12, (const u8 *)"ST7789 OK", WHITE, RED, 16, 0);
    LCD_ShowString(12, 32, (const u8 *)"RT-Thread SPI1", WHITE, RED, 16, 0);
    LCD_ShowString(12, 52, (const u8 *)"240x320 RGB565", WHITE, RED, 16, 0);
}

rt_err_t lcd_app_init(void)
{
    lcd_bus_config_t cfg;
    static const lcd_bus_ops_t ops =
    {
        .write_command = lcd_app_write_command,
        .write_data = lcd_app_write_data,
        .delay_ms = lcd_app_delay_ms,
        .reset = lcd_app_reset,
        .backlight = RT_NULL,
    };
    int lcd_err;
    rt_err_t err;

    if (g_lcd_ready)
    {
        return RT_EOK;
    }

    err = lcd_app_prepare_gpio();
    if (err != RT_EOK)
    {
        return err;
    }

    err = lcd_app_attach_spi_device();
    if (err != RT_EOK)
    {
        return err;
    }

    err = lcd_app_configure_spi();
    if (err != RT_EOK)
    {
        LOG_E("configure %s failed: %d", LCD_SPI_DEVICE_NAME, err);
        return err;
    }

    LCD_ConfigInitDefault(&cfg);
    cfg.panel = LCD_PANEL_ST7789;
    cfg.rotation = LCD_ROTATION_90;
    cfg.panel_width = 240;
    cfg.panel_height = 320;

    lcd_err = LCD_Attach(&ops, g_lcd_spi, &cfg);
    if (lcd_err != LCD_EOK)
    {
        LOG_E("LCD_Attach failed: %d", lcd_err);
        return -RT_ERROR;
    }

    lcd_err = LCD_Init();
    if (lcd_err != LCD_EOK)
    {
        LOG_E("LCD_Init failed: %d", LCD_GetLastError());
        return -RT_ERROR;
    }

    g_lcd_ready = RT_TRUE;
    LOG_I("st7789 init ok, width=%d, height=%d", LCD_GetWidth(), LCD_GetHeight());
    return RT_EOK;
}

rt_err_t lcd_app_run_test(void)
{
    rt_err_t err = lcd_app_init();

    if (err != RT_EOK)
    {
        return err;
    }

    lcd_app_show_test_pattern();
    LOG_I("st7789 test pattern displayed");
    return RT_EOK;
}

#ifdef RT_USING_FINSH
static void lcd_app_test(int argc, char **argv)
{
    rt_err_t err;

    (void)argc;
    (void)argv;

    err = lcd_app_run_test();
    if (err != RT_EOK)
    {
        LOG_E("lcd_app_test failed: %d", err);
    }
}
MSH_CMD_EXPORT(lcd_app_test, ST7789 SPI lcd test);
#endif

#else

rt_err_t lcd_app_init(void)
{
    LOG_E("lcd app unavailable: enable RT_USING_SPI and BSP_USING_SPI1 first");
    return -RT_ERROR;
}

rt_err_t lcd_app_run_test(void)
{
    return lcd_app_init();
}

#ifdef RT_USING_FINSH
static void lcd_app_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    (void)lcd_app_run_test();
}
MSH_CMD_EXPORT(lcd_app_test, ST7789 SPI lcd test);
#endif

#endif
