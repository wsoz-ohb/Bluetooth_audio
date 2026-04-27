/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "uart_send_pcm.h"

#include <rtdevice.h>

#include "es8311_audio.h"

#define DBG_TAG "uart_send_pcm"
#define DBG_LVL DBG_WARNING
#include <rtdbg.h>

#define UART_SEND_PCM_DEVICE_NAME       "uart3"
#define UART_SEND_PCM_BAUD_RATE         BAUD_RATE_2000000
#define UART_SEND_PCM_THREAD_STACK_SIZE 2048
#define UART_SEND_PCM_THREAD_PRIORITY   20
#define UART_SEND_PCM_THREAD_TICK       10
#define UART_SEND_PCM_READ_FRAMES       256u

static rt_device_t uart_send_pcm_device;
static rt_thread_t uart_send_pcm_thread;
static rt_bool_t uart_send_pcm_running = RT_FALSE;
static rt_int16_t uart_send_pcm_buffer[UART_SEND_PCM_READ_FRAMES];

static void uart_send_pcm_close_device(void)
{
    if (uart_send_pcm_device != RT_NULL)
    {
        (void) rt_device_close(uart_send_pcm_device);
        uart_send_pcm_device = RT_NULL;
    }
}

static rt_err_t uart_send_pcm_open_device(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t err;

    if (uart_send_pcm_device != RT_NULL)
    {
        return RT_EOK;
    }

    uart_send_pcm_device = rt_device_find(UART_SEND_PCM_DEVICE_NAME);
    if (uart_send_pcm_device == RT_NULL)
    {
        LOG_E("%s not found", UART_SEND_PCM_DEVICE_NAME);
        return -RT_ERROR;
    }

    config.baud_rate = UART_SEND_PCM_BAUD_RATE;
    err = rt_device_control(uart_send_pcm_device, RT_DEVICE_CTRL_CONFIG, &config);
    if (err != RT_EOK)
    {
        LOG_E("%s config failed: %d", UART_SEND_PCM_DEVICE_NAME, err);
        uart_send_pcm_device = RT_NULL;
        return err;
    }

    err = rt_device_open(uart_send_pcm_device, RT_DEVICE_OFLAG_WRONLY);
    if (err != RT_EOK)
    {
        LOG_E("%s open failed: %d", UART_SEND_PCM_DEVICE_NAME, err);
        uart_send_pcm_device = RT_NULL;
        return err;
    }

    LOG_I("%s opened, baud=%u", UART_SEND_PCM_DEVICE_NAME, UART_SEND_PCM_BAUD_RATE);
    return RT_EOK;
}

static void uart_send_pcm_write_all(const rt_uint8_t * data, rt_size_t bytes)
{
    rt_size_t offset = 0;

    while ((offset < bytes) && uart_send_pcm_running)
    {
        rt_size_t written;

        written = rt_device_write(uart_send_pcm_device, 0, data + offset, bytes - offset);
        if (written == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }

        offset += written;
    }
}

static void uart_send_pcm_thread_entry(void * parameter)
{
    RT_UNUSED(parameter);

    if (uart_send_pcm_open_device() != RT_EOK)
    {
        uart_send_pcm_running = RT_FALSE;
        uart_send_pcm_thread = RT_NULL;
        return;
    }

    while (uart_send_pcm_running)
    {
        rt_uint32_t frames;

        frames = es8311_audio_read_capture(uart_send_pcm_buffer, UART_SEND_PCM_READ_FRAMES);
        if (frames == 0u)
        {
            rt_thread_mdelay(1);
            continue;
        }

        uart_send_pcm_write_all((const rt_uint8_t *) uart_send_pcm_buffer,
                                (rt_size_t) frames * sizeof(uart_send_pcm_buffer[0]));
    }

    uart_send_pcm_close_device();
    uart_send_pcm_thread = RT_NULL;
}

rt_err_t uart_send_pcm_start(void)
{
    if (uart_send_pcm_running)
    {
        return RT_EOK;
    }

    uart_send_pcm_running = RT_TRUE;
    uart_send_pcm_thread = rt_thread_create("uart_pcm",
                                            uart_send_pcm_thread_entry,
                                            RT_NULL,
                                            UART_SEND_PCM_THREAD_STACK_SIZE,
                                            UART_SEND_PCM_THREAD_PRIORITY,
                                            UART_SEND_PCM_THREAD_TICK);
    if (uart_send_pcm_thread == RT_NULL)
    {
        uart_send_pcm_running = RT_FALSE;
        LOG_E("create uart_pcm thread failed");
        return -RT_ENOMEM;
    }

    rt_thread_startup(uart_send_pcm_thread);
    return RT_EOK;
}

void uart_send_pcm_stop(void)
{
    rt_uint32_t wait_ms;

    if (!uart_send_pcm_running && (uart_send_pcm_thread == RT_NULL))
    {
        return;
    }

    uart_send_pcm_running = RT_FALSE;

    for (wait_ms = 0u; (wait_ms < 100u) && (uart_send_pcm_thread != RT_NULL); wait_ms++)
    {
        rt_thread_mdelay(1);
    }
}
