/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PTT 采集 PCM 出口:
 * 1) uart3  @ 2Mbps  -> 香橙派（原始 int16le mono 流，无 WAV 头）
 * 2) 文件系统 /pcm/last.pcm     -> 本地复查（同格式）
 *
 * 调试串口 uart1 只打摘要日志，不刷原始 PCM。
 */
#include "uart_send_pcm.h"

#include <rtdevice.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "es8311_audio.h"
#include "fs_app.h"

#define DBG_TAG "uart_send_pcm"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define UART_SEND_PCM_DEVICE_NAME       "uart3"
#define UART_SEND_PCM_BAUD_RATE         BAUD_RATE_2000000
#define UART_SEND_PCM_THREAD_STACK_SIZE 2048
#define UART_SEND_PCM_THREAD_PRIORITY   20
#define UART_SEND_PCM_THREAD_TICK       10
#define UART_SEND_PCM_READ_FRAMES       256u

/* 单次 PTT 落盘上限，防止把 11MB 分区写满：约 30s @44.1k mono int16 */
#define UART_SEND_PCM_FILE_MAX_BYTES    (44100u * 2u * 30u)
/* littlefs 挂在根 "/"，录音目录 /pcm（见 fs_app.c） */
#define UART_SEND_PCM_FILE_PATH         "/pcm/last.pcm"
#define UART_SEND_PCM_META_PATH         "/pcm/last.txt"

static rt_device_t uart_send_pcm_device;
static rt_thread_t uart_send_pcm_thread;
static rt_bool_t uart_send_pcm_running = RT_FALSE;
static rt_int16_t uart_send_pcm_buffer[UART_SEND_PCM_READ_FRAMES];

static int uart_send_pcm_file_fd = -1;
static rt_uint32_t uart_send_pcm_file_bytes = 0;
static rt_uint32_t uart_send_pcm_uart_bytes = 0;
static rt_uint32_t uart_send_pcm_file_drops = 0;
static rt_bool_t uart_send_pcm_file_limit_logged = RT_FALSE;

static void uart_send_pcm_close_device(void)
{
    if (uart_send_pcm_device != RT_NULL)
    {
        (void)rt_device_close(uart_send_pcm_device);
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

    LOG_I("%s opened, baud=%u (orange-pi link)", UART_SEND_PCM_DEVICE_NAME, UART_SEND_PCM_BAUD_RATE);
    return RT_EOK;
}

static void uart_send_pcm_write_all(const rt_uint8_t *data, rt_size_t bytes)
{
    rt_size_t offset = 0;

    if (uart_send_pcm_device == RT_NULL)
    {
        return;
    }

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
        uart_send_pcm_uart_bytes += (rt_uint32_t)written;
    }
}

static void uart_send_pcm_close_file(void)
{
    if (uart_send_pcm_file_fd >= 0)
    {
        close(uart_send_pcm_file_fd);
        uart_send_pcm_file_fd = -1;
    }
}

static void uart_send_pcm_write_meta(void)
{
    int fd;
    char buf[192];
    int n;
    es8311_audio_capture_format_t fmt;
    rt_uint32_t sr = 44100;
    rt_uint32_t ch = 1;

    if (es8311_audio_get_capture_format(&fmt))
    {
        sr = fmt.sample_rate;
        ch = fmt.channels;
    }

    fd = open(UART_SEND_PCM_META_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        LOG_W("open meta %s failed, errno=%d", UART_SEND_PCM_META_PATH, rt_get_errno());
        return;
    }

    n = rt_snprintf(buf, sizeof(buf),
                    "file=%s\n"
                    "format=s16le\n"
                    "channels=%u\n"
                    "sample_rate=%u\n"
                    "bytes=%u\n"
                    "frames=%u\n"
                    "uart3_bytes=%u\n"
                    "file_drops=%u\n",
                    UART_SEND_PCM_FILE_PATH,
                    (unsigned)ch,
                    (unsigned)sr,
                    (unsigned)uart_send_pcm_file_bytes,
                    (unsigned)(uart_send_pcm_file_bytes / 2u),
                    (unsigned)uart_send_pcm_uart_bytes,
                    (unsigned)uart_send_pcm_file_drops);
    if (n > 0)
    {
        write(fd, buf, (rt_size_t)n);
    }
    close(fd);
}

static rt_err_t uart_send_pcm_open_file(void)
{
    if (!fs_app_is_ready())
    {
        if (fs_app_init() != RT_EOK)
        {
            LOG_W("fs not ready, skip record-to-file");
            return -RT_ERROR;
        }
    }

    /* 每次 PTT 覆盖 last.pcm，方便 msh 直接取最新一截 */
    uart_send_pcm_file_fd = open(UART_SEND_PCM_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (uart_send_pcm_file_fd < 0)
    {
        LOG_W("open %s failed, errno=%d (uart3 still works)",
              UART_SEND_PCM_FILE_PATH, rt_get_errno());
        return -RT_ERROR;
    }

    uart_send_pcm_file_bytes = 0;
    uart_send_pcm_file_drops = 0;
    uart_send_pcm_file_limit_logged = RT_FALSE;
    LOG_I("recording to %s (s16le mono, cap %u bytes)",
          UART_SEND_PCM_FILE_PATH,
          (unsigned)UART_SEND_PCM_FILE_MAX_BYTES);
    return RT_EOK;
}

static void uart_send_pcm_write_file(const rt_uint8_t *data, rt_size_t bytes)
{
    int written;
    rt_size_t remain;
    rt_size_t offset;

    if (uart_send_pcm_file_fd < 0 || bytes == 0)
    {
        return;
    }

    if (uart_send_pcm_file_bytes >= UART_SEND_PCM_FILE_MAX_BYTES)
    {
        uart_send_pcm_file_drops += (rt_uint32_t)bytes;
        if (!uart_send_pcm_file_limit_logged)
        {
            uart_send_pcm_file_limit_logged = RT_TRUE;
            LOG_W("pcm file hit %u byte cap, stop writing file (uart3 continues)",
                  (unsigned)UART_SEND_PCM_FILE_MAX_BYTES);
        }
        return;
    }

    remain = bytes;
    if (uart_send_pcm_file_bytes + remain > UART_SEND_PCM_FILE_MAX_BYTES)
    {
        remain = (rt_size_t)(UART_SEND_PCM_FILE_MAX_BYTES - uart_send_pcm_file_bytes);
    }

    offset = 0;
    while (offset < remain)
    {
        written = write(uart_send_pcm_file_fd, data + offset, remain - offset);
        if (written <= 0)
        {
            uart_send_pcm_file_drops += (rt_uint32_t)(remain - offset);
            LOG_W("write %s failed, errno=%d", UART_SEND_PCM_FILE_PATH, rt_get_errno());
            break;
        }
        offset += (rt_size_t)written;
        uart_send_pcm_file_bytes += (rt_uint32_t)written;
    }

    /* 超出 cap 的尾巴算 drop */
    if (bytes > remain)
    {
        uart_send_pcm_file_drops += (rt_uint32_t)(bytes - remain);
    }
}

static void uart_send_pcm_thread_entry(void *parameter)
{
    rt_bool_t uart_ok;
    rt_bool_t file_ok;

    RT_UNUSED(parameter);

    uart_send_pcm_uart_bytes = 0;
    uart_send_pcm_file_bytes = 0;
    uart_send_pcm_file_drops = 0;

    uart_ok = (uart_send_pcm_open_device() == RT_EOK) ? RT_TRUE : RT_FALSE;
    file_ok = (uart_send_pcm_open_file() == RT_EOK) ? RT_TRUE : RT_FALSE;

    if (!uart_ok && !file_ok)
    {
        LOG_E("neither uart3 nor file available, pcm export exit");
        uart_send_pcm_running = RT_FALSE;
        uart_send_pcm_thread = RT_NULL;
        return;
    }

    if (!uart_ok)
    {
        LOG_W("uart3 unavailable, only record to file");
    }

    while (uart_send_pcm_running)
    {
        rt_uint32_t frames;
        rt_size_t bytes;

        frames = es8311_audio_read_capture(uart_send_pcm_buffer, UART_SEND_PCM_READ_FRAMES);
        if (frames == 0u)
        {
            rt_thread_mdelay(1);
            continue;
        }

        bytes = (rt_size_t)frames * sizeof(uart_send_pcm_buffer[0]);

        if (uart_ok)
        {
            uart_send_pcm_write_all((const rt_uint8_t *)uart_send_pcm_buffer, bytes);
        }
        if (file_ok)
        {
            uart_send_pcm_write_file((const rt_uint8_t *)uart_send_pcm_buffer, bytes);
        }
    }

    uart_send_pcm_close_file();
    if (file_ok)
    {
        uart_send_pcm_write_meta();
        LOG_I("pcm saved: %s (%u bytes, ~%u ms @44.1k mono), uart3=%u, drops=%u",
              UART_SEND_PCM_FILE_PATH,
              (unsigned)uart_send_pcm_file_bytes,
              (unsigned)((uart_send_pcm_file_bytes / 2u) * 1000u / 44100u),
              (unsigned)uart_send_pcm_uart_bytes,
              (unsigned)uart_send_pcm_file_drops);
        LOG_I("meta: %s  |  msh: fs_app_info / ls /pcm", UART_SEND_PCM_META_PATH);
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

    /* 等线程把文件 close + meta 写完；落盘可能稍慢 */
    for (wait_ms = 0u; (wait_ms < 500u) && (uart_send_pcm_thread != RT_NULL); wait_ms++)
    {
        rt_thread_mdelay(1);
    }
}
