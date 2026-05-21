#ifndef __LCD_BUS_H
#define __LCD_BUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LCD_PANEL_ILI9341 = 0,
    LCD_PANEL_ST7789  = 1,
} lcd_panel_type_t;

typedef enum
{
    LCD_ROTATION_0   = 0,
    LCD_ROTATION_90  = 1,
    LCD_ROTATION_180 = 2,
    LCD_ROTATION_270 = 3,
} lcd_rotation_t;

typedef struct
{
    lcd_panel_type_t panel;
    lcd_rotation_t rotation;
    uint16_t panel_width;
    uint16_t panel_height;
} lcd_bus_config_t;

/*
 * 底层总线抽象：
 * - write_command / write_data 是必须实现项
 * - delay_ms 也是必须实现项
 * - reset / backlight 是可选项
 *
 * 这层不关心 GPIO / HAL / RT-Thread SPI / bitbang 具体做法，
 * 只要求上层把“命令写入”和“数据写入”语义实现出来。
 */
typedef struct
{
    int  (*write_command)(void *user, const uint8_t *data, size_t size);
    int  (*write_data)(void *user, const uint8_t *data, size_t size);
    void (*delay_ms)(void *user, uint32_t ms);
    void (*reset)(void *user, int level);
    void (*backlight)(void *user, int on);
} lcd_bus_ops_t;

#ifdef __cplusplus
}
#endif

#endif
