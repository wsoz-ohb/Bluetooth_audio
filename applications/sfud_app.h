/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       SFUD app for onboard W25Q128
 */
#ifndef APPLICATIONS_SFUD_APP_H_
#define APPLICATIONS_SFUD_APP_H_

#include <rtthread.h>
#include <spi_flash_sfud.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Attach SPI flash device and probe it with SFUD.
 * Safe to call multiple times; returns RT_EOK when already ready.
 */
rt_err_t sfud_app_init(void);

/**
 * @return RT_TRUE if probe succeeded.
 */
rt_bool_t sfud_app_is_ready(void);

/**
 * Block device name registered by SFUD, e.g. "W25Q128".
 */
const char *sfud_app_flash_name(void);

/**
 * Underlying SFUD flash handle. RT_NULL if not ready.
 */
sfud_flash_t sfud_app_get_sfud(void);

/**
 * RT-Thread SPI flash block device wrapper. RT_NULL if not ready.
 */
rt_spi_flash_device_t sfud_app_get_device(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_SFUD_APP_H_ */
