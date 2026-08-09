/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PTT 采集 PCM 出口:
 * 1) uart3  @ 2Mbps  -> 香橙派（int16le mono 流，无 WAV 头）
 * 2) 文件系统 /pcm/last.pcm     -> 本地复查（同格式，可按宏临时打开）
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
#define UART_SEND_PCM_THREAD_STACK_SIZE 4096
#define UART_SEND_PCM_THREAD_PRIORITY   14
#define UART_SEND_PCM_THREAD_TICK       10
#define UART_SEND_PCM_READ_FRAMES       1024u
#define UART_SEND_PCM_START_WAIT_MS     3000u
#define UART_SEND_PCM_STOP_WAIT_MS      3000u
#define UART_SEND_PCM_UART_BACKLOG_LIMIT_FRAMES  1024u
#define UART_SEND_PCM_UART_EXPORT_ENABLE 1
#define UART_SEND_PCM_FILE_RECORD_ENABLE 0
#define UART_SEND_PCM_FILE_CACHE_BYTES  4096u

/* 香橙派下行回复：44100 Hz / mono / signed int16 little-endian 裸 PCM。 */
#define UART_PCM_RX_BUFFER_BYTES         1024u
#define UART_PCM_RX_SAMPLE_RATE          44100u
#define UART_PCM_RX_GAIN_Q15             4096    /* 0.125x, approximately -18dB */
#define UART_PCM_RX_IDLE_TIMEOUT_MS      1000u
#define UART_PCM_RX_THREAD_STACK_SIZE    4096u
#define UART_PCM_RX_THREAD_PRIORITY      13u
#define UART_PCM_RX_THREAD_TICK          10u

/*
 * 轻量语音优化放在导出线程里做，避免在 I2S DMA 中断里增加计算量。
 * 当前参数偏向近讲/语音验证：去低频漂移、压掉高频噪声、补一点数字增益并限幅。
 */
#define UART_SEND_PCM_VOICE_PROCESS_ENABLE 1
#define UART_SEND_PCM_HIGHPASS_A_Q15       32212   /* ~120Hz @44.1kHz */
#define UART_SEND_PCM_LOWPASS_ALPHA_Q15    19800   /* ~6.5kHz @44.1kHz */
#define UART_SEND_PCM_GAIN_Q8              288u    /* ~+1.0dB */
#define UART_SEND_PCM_LIMIT_ABS            30000
#define UART_SEND_PCM_GATE_ENABLE          1
#define UART_SEND_PCM_GATE_OPEN_ABS        900
#define UART_SEND_PCM_GATE_CLOSE_ABS       550
#define UART_SEND_PCM_GATE_CLOSE_GAIN_Q15  3277    /* ~0.10 */
#define UART_SEND_PCM_GATE_ATTACK_SHIFT    2
#define UART_SEND_PCM_GATE_RELEASE_SHIFT   5

/* 单次 PTT 落盘上限，防止把 11MB 分区写满：约 30s @44.1k mono int16 */
#define UART_SEND_PCM_FILE_MAX_BYTES    (44100u * 2u * 30u)
/* littlefs 挂在根 "/"，录音目录 /pcm（见 fs_app.c） */
#define UART_SEND_PCM_FILE_PATH         "/pcm/last.pcm"
#define UART_SEND_PCM_META_PATH         "/pcm/last.txt"

static rt_device_t uart_send_pcm_device;
static struct rt_thread uart_send_pcm_thread_obj;
static rt_thread_t uart_send_pcm_thread;
static rt_bool_t uart_send_pcm_thread_started = RT_FALSE;
static volatile rt_bool_t uart_send_pcm_running = RT_FALSE;
static volatile rt_bool_t uart_send_pcm_done = RT_TRUE;
static volatile rt_bool_t uart_send_pcm_ready = RT_FALSE;
static volatile rt_err_t uart_send_pcm_start_result = -RT_ERROR;
static rt_uint8_t uart_send_pcm_thread_stack[UART_SEND_PCM_THREAD_STACK_SIZE]
    __attribute__((aligned(RT_ALIGN_SIZE), section(".ccmbss.uart_pcm_stack")));
static rt_int16_t uart_send_pcm_buffer[UART_SEND_PCM_READ_FRAMES];

static int uart_send_pcm_file_fd = -1;
static rt_uint32_t uart_send_pcm_file_bytes = 0;
static rt_uint32_t uart_send_pcm_uart_bytes = 0;
static rt_uint32_t uart_send_pcm_uart_skipped_bytes = 0;
static rt_uint32_t uart_send_pcm_file_drops = 0;
static rt_bool_t uart_send_pcm_file_limit_logged = RT_FALSE;
static rt_uint8_t uart_send_pcm_file_cache[UART_SEND_PCM_FILE_CACHE_BYTES];
static rt_size_t uart_send_pcm_file_cache_len = 0;
static rt_uint32_t uart_send_pcm_file_flushes = 0;
static rt_int32_t uart_send_pcm_hp_prev_x = 0;
static rt_int32_t uart_send_pcm_hp_prev_y = 0;
static rt_int32_t uart_send_pcm_lp_y = 0;
static rt_int32_t uart_send_pcm_gate_env = 0;
static rt_int32_t uart_send_pcm_gate_gain_q15 = UART_SEND_PCM_GATE_CLOSE_GAIN_Q15;
static rt_uint32_t uart_send_pcm_peak_abs = 0;
static rt_uint32_t uart_send_pcm_limiter_hits = 0;

/* UART3 在 PTT 期间发送，松手后自动切换为接收回复 PCM。 */
static rt_device_t uart_pcm_rx_device;
static struct rt_thread uart_pcm_rx_thread_obj;
static rt_thread_t uart_pcm_rx_thread;
static rt_bool_t uart_pcm_rx_thread_started = RT_FALSE;
static volatile rt_bool_t uart_pcm_rx_enabled = RT_FALSE;
static volatile rt_bool_t uart_pcm_rx_done = RT_TRUE;
static struct rt_semaphore uart_pcm_rx_sem;
static rt_bool_t uart_pcm_rx_sem_inited = RT_FALSE;
static rt_uint8_t uart_pcm_rx_thread_stack[UART_PCM_RX_THREAD_STACK_SIZE]
    __attribute__((aligned(RT_ALIGN_SIZE), section(".ccmbss.uart_pcm_rx_stack")));
static rt_uint8_t uart_pcm_rx_read_buffer[UART_PCM_RX_BUFFER_BYTES];
static rt_int16_t uart_pcm_rx_pcm_buffer[UART_PCM_RX_BUFFER_BYTES / 2u];
static rt_bool_t uart_pcm_rx_pending_byte_valid = RT_FALSE;
static rt_uint8_t uart_pcm_rx_pending_byte;
static rt_bool_t uart_pcm_rx_audio_started = RT_FALSE;
static rt_bool_t uart_pcm_rx_received_any = RT_FALSE;
static rt_tick_t uart_pcm_rx_last_data_tick;

static void uart_send_pcm_set_ready(rt_err_t result)
{
    uart_send_pcm_start_result = result;
    uart_send_pcm_ready = RT_TRUE;
}

static void uart_send_pcm_reset_voice_process(void)
{
    uart_send_pcm_hp_prev_x = 0;
    uart_send_pcm_hp_prev_y = 0;
    uart_send_pcm_lp_y = 0;
    uart_send_pcm_gate_env = 0;
    uart_send_pcm_gate_gain_q15 = UART_SEND_PCM_GATE_CLOSE_GAIN_Q15;
    uart_send_pcm_peak_abs = 0;
    uart_send_pcm_limiter_hits = 0;
}

static void uart_send_pcm_process_samples(rt_int16_t *pcm, rt_uint32_t frames)
{
    rt_uint32_t i;

    if ((pcm == RT_NULL) || (frames == 0u))
    {
        return;
    }

    for (i = 0u; i < frames; i++)
    {
        rt_int32_t sample;
        rt_int32_t abs_sample;

        sample = pcm[i];

#if UART_SEND_PCM_VOICE_PROCESS_ENABLE
        {
            rt_int32_t hp;
            rt_int32_t limited;

            hp = sample - uart_send_pcm_hp_prev_x +
                 (rt_int32_t)(((rt_int64_t)UART_SEND_PCM_HIGHPASS_A_Q15 *
                                uart_send_pcm_hp_prev_y) >> 15);
            uart_send_pcm_hp_prev_x = sample;
            uart_send_pcm_hp_prev_y = hp;

            uart_send_pcm_lp_y +=
                (rt_int32_t)(((rt_int64_t)UART_SEND_PCM_LOWPASS_ALPHA_Q15 *
                              (hp - uart_send_pcm_lp_y)) >> 15);

            limited = (rt_int32_t)(((rt_int64_t)uart_send_pcm_lp_y *
                                    UART_SEND_PCM_GAIN_Q8) >> 8);
            if (limited > UART_SEND_PCM_LIMIT_ABS)
            {
                limited = UART_SEND_PCM_LIMIT_ABS;
                uart_send_pcm_limiter_hits++;
            }
            else if (limited < -UART_SEND_PCM_LIMIT_ABS)
            {
                limited = -UART_SEND_PCM_LIMIT_ABS;
                uart_send_pcm_limiter_hits++;
            }

            sample = limited;

#if UART_SEND_PCM_GATE_ENABLE
            {
                rt_int32_t target_gain_q15;
                rt_int32_t gated;

                if (sample < 0)
                {
                    abs_sample = -sample;
                }
                else
                {
                    abs_sample = sample;
                }

                if (abs_sample > uart_send_pcm_gate_env)
                {
                    uart_send_pcm_gate_env += (rt_int32_t)((abs_sample - uart_send_pcm_gate_env) >> 5);
                }
                else
                {
                    uart_send_pcm_gate_env -= (rt_int32_t)((uart_send_pcm_gate_env - abs_sample) >> 5);
                }

                if (uart_send_pcm_gate_env > UART_SEND_PCM_GATE_OPEN_ABS)
                {
                    target_gain_q15 = 32768;
                }
                else if (uart_send_pcm_gate_env < UART_SEND_PCM_GATE_CLOSE_ABS)
                {
                    target_gain_q15 = UART_SEND_PCM_GATE_CLOSE_GAIN_Q15;
                }
                else
                {
                    target_gain_q15 = uart_send_pcm_gate_gain_q15;
                }

                if (target_gain_q15 > uart_send_pcm_gate_gain_q15)
                {
                    uart_send_pcm_gate_gain_q15 +=
                        (rt_int32_t)((target_gain_q15 - uart_send_pcm_gate_gain_q15) >> UART_SEND_PCM_GATE_ATTACK_SHIFT);
                }
                else if (target_gain_q15 < uart_send_pcm_gate_gain_q15)
                {
                    uart_send_pcm_gate_gain_q15 -=
                        (rt_int32_t)((uart_send_pcm_gate_gain_q15 - target_gain_q15) >> UART_SEND_PCM_GATE_RELEASE_SHIFT);
                }

                gated = (rt_int32_t)(((rt_int64_t)sample * uart_send_pcm_gate_gain_q15) >> 15);
                sample = gated;
            }
#endif
            pcm[i] = (rt_int16_t)sample;
        }
#endif

        abs_sample = sample;
        if (abs_sample < 0)
        {
            abs_sample = -abs_sample;
        }
        if ((rt_uint32_t)abs_sample > uart_send_pcm_peak_abs)
        {
            uart_send_pcm_peak_abs = (rt_uint32_t)abs_sample;
        }
    }
}

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

    if ((uart_send_pcm_device == RT_NULL) || (data == RT_NULL) || (bytes == 0u))
    {
        return;
    }

    /*
     * running 只决定是否继续读取下一块采集数据。当前块一旦开始发送，
     * 即使此时松开 PTT，也必须完整发完，避免在一个 16-bit 采样中间停下。
     */
    while (offset < bytes)
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

static rt_err_t uart_pcm_rx_indicate(rt_device_t device, rt_size_t size)
{
    RT_UNUSED(device);
    RT_UNUSED(size);

    if (uart_pcm_rx_sem_inited)
    {
        rt_sem_release(&uart_pcm_rx_sem);
    }

    return RT_EOK;
}

static rt_err_t uart_pcm_rx_open(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t err;

    uart_pcm_rx_device = rt_device_find(UART_SEND_PCM_DEVICE_NAME);
    if (uart_pcm_rx_device == RT_NULL)
    {
        LOG_E("%s not found for RX", UART_SEND_PCM_DEVICE_NAME);
        return -RT_ERROR;
    }

    config.baud_rate = UART_SEND_PCM_BAUD_RATE;
    err = rt_device_control(uart_pcm_rx_device, RT_DEVICE_CTRL_CONFIG, &config);
    if (err != RT_EOK)
    {
        LOG_E("%s RX config failed: %d", UART_SEND_PCM_DEVICE_NAME, err);
        uart_pcm_rx_device = RT_NULL;
        return err;
    }

    rt_device_set_rx_indicate(uart_pcm_rx_device, uart_pcm_rx_indicate);
    err = rt_device_open(uart_pcm_rx_device,
                         RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (err != RT_EOK)
    {
        rt_device_set_rx_indicate(uart_pcm_rx_device, RT_NULL);
        uart_pcm_rx_device = RT_NULL;
        LOG_E("%s RX DMA open failed: %d", UART_SEND_PCM_DEVICE_NAME, err);
        return err;
    }

    LOG_I("%s RX ready, format=44100Hz/mono/s16le", UART_SEND_PCM_DEVICE_NAME);
    return RT_EOK;
}

static void uart_pcm_rx_close(void)
{
    if (uart_pcm_rx_device != RT_NULL)
    {
        rt_device_set_rx_indicate(uart_pcm_rx_device, RT_NULL);
        (void)rt_device_close(uart_pcm_rx_device);
        uart_pcm_rx_device = RT_NULL;
    }
}

static void uart_pcm_rx_stop_audio(void)
{
    if (uart_pcm_rx_audio_started)
    {
        es8311_audio_stop_playback();
        es8311_audio_flush_playback();
        uart_pcm_rx_audio_started = RT_FALSE;
    }
}

static rt_uint32_t uart_pcm_rx_write(const rt_uint8_t *data, rt_size_t bytes)
{
    rt_size_t offset = 0u;
    rt_size_t sample_count;
    rt_uint32_t frames = 0u;
    rt_uint32_t i;

    if ((data == RT_NULL) || (bytes == 0u))
    {
        return 0u;
    }

    if (uart_pcm_rx_pending_byte_valid)
    {
        uart_pcm_rx_pcm_buffer[0] = (rt_int16_t)((rt_uint16_t)uart_pcm_rx_pending_byte |
                                                 ((rt_uint16_t)data[0] << 8));
        uart_pcm_rx_pending_byte_valid = RT_FALSE;
        offset = 1u;
        frames = 1u;
    }

    if (((bytes - offset) & 1u) != 0u)
    {
        uart_pcm_rx_pending_byte = data[bytes - 1u];
        uart_pcm_rx_pending_byte_valid = RT_TRUE;
        bytes--;
    }

    sample_count = bytes / 2u;
    if (sample_count > (sizeof(uart_pcm_rx_pcm_buffer) / sizeof(uart_pcm_rx_pcm_buffer[0])) - frames)
    {
        sample_count = (sizeof(uart_pcm_rx_pcm_buffer) / sizeof(uart_pcm_rx_pcm_buffer[0])) - frames;
    }

    for (i = 0u; i < (rt_uint32_t)sample_count; i++)
    {
        rt_size_t index = offset + (rt_size_t)i * 2u;
        uart_pcm_rx_pcm_buffer[frames + i] =
            (rt_int16_t)((rt_uint16_t)data[index] | ((rt_uint16_t)data[index + 1u] << 8));
    }
    frames += (rt_uint32_t)sample_count;

    if (frames == 0u)
    {
        return 0u;
    }

    /* 只衰减香橙派回复语音，不改变蓝牙和麦克风上行的音量。 */
    for (i = 0u; i < frames; i++)
    {
        uart_pcm_rx_pcm_buffer[i] =
            (rt_int16_t)(((rt_int64_t)uart_pcm_rx_pcm_buffer[i] * UART_PCM_RX_GAIN_Q15) >> 15);
    }

    if (!uart_pcm_rx_audio_started)
    {
        if (es8311_audio_configure(UART_PCM_RX_SAMPLE_RATE, 1u) != RT_EOK ||
            es8311_audio_start_playback() != RT_EOK)
        {
            LOG_E("start ES8311 playback for UART3 PCM failed");
            return 0u;
        }
        uart_pcm_rx_audio_started = RT_TRUE;
        LOG_I("UART3 reply PCM playback started");
    }

    return es8311_audio_write_playback(uart_pcm_rx_pcm_buffer,
                                       frames,
                                       1u,
                                       UART_PCM_RX_SAMPLE_RATE);
}

static void uart_pcm_rx_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        rt_size_t bytes;

        if (!uart_pcm_rx_enabled)
        {
            uart_pcm_rx_done = RT_TRUE;
            rt_thread_mdelay(1);
            continue;
        }

        uart_pcm_rx_done = RT_FALSE;
        uart_pcm_rx_pending_byte_valid = RT_FALSE;
        uart_pcm_rx_received_any = RT_FALSE;
        uart_pcm_rx_last_data_tick = 0u;

        if (uart_pcm_rx_open() != RT_EOK)
        {
            uart_pcm_rx_enabled = RT_FALSE;
            uart_pcm_rx_done = RT_TRUE;
            continue;
        }

        while (uart_pcm_rx_enabled)
        {
            (void)rt_sem_take(&uart_pcm_rx_sem, rt_tick_from_millisecond(100));
            do
            {
                bytes = rt_device_read(uart_pcm_rx_device,
                                       0,
                                       uart_pcm_rx_read_buffer,
                                       sizeof(uart_pcm_rx_read_buffer));
                if (bytes == 0u)
                {
                    break;
                }

                uart_pcm_rx_received_any = RT_TRUE;
                uart_pcm_rx_last_data_tick = rt_tick_get();
                (void)uart_pcm_rx_write(uart_pcm_rx_read_buffer, bytes);
            } while (bytes == sizeof(uart_pcm_rx_read_buffer));

            if (uart_pcm_rx_received_any &&
                ((rt_tick_get() - uart_pcm_rx_last_data_tick) >=
                 rt_tick_from_millisecond(UART_PCM_RX_IDLE_TIMEOUT_MS)))
            {
                uart_pcm_rx_stop_audio();
                uart_pcm_rx_received_any = RT_FALSE;
                uart_pcm_rx_last_data_tick = 0u;
                LOG_I("UART3 reply PCM idle, waiting for next reply");
            }
        }

        uart_pcm_rx_stop_audio();
        uart_pcm_rx_close();
        uart_pcm_rx_done = RT_TRUE;
    }
}

static rt_err_t uart_pcm_rx_ensure_thread(void)
{
    rt_err_t err;

    if (uart_pcm_rx_thread_started)
    {
        return RT_EOK;
    }

    err = rt_sem_init(&uart_pcm_rx_sem, "pcmrx", 0, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        return err;
    }
    uart_pcm_rx_sem_inited = RT_TRUE;

    uart_pcm_rx_thread = &uart_pcm_rx_thread_obj;
    err = rt_thread_init(uart_pcm_rx_thread,
                         "pcm_rx",
                         uart_pcm_rx_thread_entry,
                         RT_NULL,
                         uart_pcm_rx_thread_stack,
                         sizeof(uart_pcm_rx_thread_stack),
                         UART_PCM_RX_THREAD_PRIORITY,
                         UART_PCM_RX_THREAD_TICK);
    if (err != RT_EOK)
    {
        uart_pcm_rx_thread = RT_NULL;
        rt_sem_detach(&uart_pcm_rx_sem);
        uart_pcm_rx_sem_inited = RT_FALSE;
        return err;
    }

    err = rt_thread_startup(uart_pcm_rx_thread);
    if (err != RT_EOK)
    {
        (void)rt_thread_detach(uart_pcm_rx_thread);
        uart_pcm_rx_thread = RT_NULL;
        rt_sem_detach(&uart_pcm_rx_sem);
        uart_pcm_rx_sem_inited = RT_FALSE;
        return err;
    }

    uart_pcm_rx_thread_started = RT_TRUE;
    return RT_EOK;
}

static rt_err_t uart_pcm_rx_disable(void)
{
    rt_uint32_t wait_ms;

    uart_pcm_rx_enabled = RT_FALSE;
    if (uart_pcm_rx_sem_inited)
    {
        rt_sem_release(&uart_pcm_rx_sem);
    }

    for (wait_ms = 0u;
         (wait_ms < UART_SEND_PCM_STOP_WAIT_MS) && !uart_pcm_rx_done;
         wait_ms++)
    {
        rt_thread_mdelay(1);
    }

    return uart_pcm_rx_done ? RT_EOK : -RT_ETIMEOUT;
}

static void uart_pcm_rx_enable(void)
{
    if (uart_pcm_rx_ensure_thread() != RT_EOK)
    {
        LOG_E("start UART3 PCM RX thread failed");
        return;
    }

    uart_pcm_rx_done = RT_FALSE;
    uart_pcm_rx_enabled = RT_TRUE;
    rt_sem_release(&uart_pcm_rx_sem);
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
    char buf[512];
    int n;
    es8311_audio_capture_format_t fmt;
    rt_uint32_t sr = 44100;
    rt_uint32_t ch = 1;
    rt_uint32_t capture_drops;
    const char *processing;

    if (es8311_audio_get_capture_format(&fmt))
    {
        sr = fmt.sample_rate;
        ch = fmt.channels;
    }
    capture_drops = es8311_audio_get_capture_drop_frames();
#if UART_SEND_PCM_VOICE_PROCESS_ENABLE
    processing = "hp120_lp6500_gate_gain_limiter";
#else
    processing = "off";
#endif

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
                    "uart3_enabled=%u\n"
                    "bytes=%u\n"
                    "frames=%u\n"
                    "uart3_bytes=%u\n"
                    "uart3_skipped_bytes=%u\n"
                    "file_drops=%u\n"
                    "file_cache_bytes=%u\n"
                    "file_flushes=%u\n"
                    "capture_drops=%u\n"
                    "processing=%s\n"
                    "gain_q8=%u\n"
                    "limit_abs=%u\n"
                    "gate_open_abs=%u\n"
                    "gate_close_abs=%u\n"
                    "gate_close_gain_q15=%u\n"
                    "peak_abs=%u\n"
                    "limiter_hits=%u\n",
                    UART_SEND_PCM_FILE_PATH,
                    (unsigned)ch,
                    (unsigned)sr,
                    (unsigned)UART_SEND_PCM_UART_EXPORT_ENABLE,
                    (unsigned)uart_send_pcm_file_bytes,
                    (unsigned)(uart_send_pcm_file_bytes / 2u),
                    (unsigned)uart_send_pcm_uart_bytes,
                    (unsigned)uart_send_pcm_uart_skipped_bytes,
                    (unsigned)uart_send_pcm_file_drops,
                    (unsigned)UART_SEND_PCM_FILE_CACHE_BYTES,
                    (unsigned)uart_send_pcm_file_flushes,
                    (unsigned)capture_drops,
                    processing,
                    (unsigned)UART_SEND_PCM_GAIN_Q8,
                    (unsigned)UART_SEND_PCM_LIMIT_ABS,
                    (unsigned)UART_SEND_PCM_GATE_OPEN_ABS,
                    (unsigned)UART_SEND_PCM_GATE_CLOSE_ABS,
                    (unsigned)UART_SEND_PCM_GATE_CLOSE_GAIN_Q15,
                    (unsigned)uart_send_pcm_peak_abs,
                    (unsigned)uart_send_pcm_limiter_hits);
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
    uart_send_pcm_file_cache_len = 0;
    uart_send_pcm_file_flushes = 0;
    uart_send_pcm_file_limit_logged = RT_FALSE;
    LOG_I("recording to %s (s16le mono, cap %u bytes)",
          UART_SEND_PCM_FILE_PATH,
          (unsigned)UART_SEND_PCM_FILE_MAX_BYTES);
    return RT_EOK;
}

static void uart_send_pcm_write_file_direct(const rt_uint8_t *data, rt_size_t bytes)
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

static void uart_send_pcm_flush_file_cache(void)
{
    if (uart_send_pcm_file_cache_len == 0u)
    {
        return;
    }

    uart_send_pcm_write_file_direct(uart_send_pcm_file_cache, uart_send_pcm_file_cache_len);
    uart_send_pcm_file_cache_len = 0u;
    uart_send_pcm_file_flushes++;
}

static void uart_send_pcm_write_file(const rt_uint8_t *data, rt_size_t bytes)
{
    rt_size_t offset;

    if ((uart_send_pcm_file_fd < 0) || (data == RT_NULL) || (bytes == 0u))
    {
        return;
    }

    offset = 0u;
    while (offset < bytes)
    {
        rt_size_t free_bytes;
        rt_size_t copy_bytes;

        free_bytes = (rt_size_t)UART_SEND_PCM_FILE_CACHE_BYTES - uart_send_pcm_file_cache_len;
        if (free_bytes == 0u)
        {
            uart_send_pcm_flush_file_cache();
            free_bytes = (rt_size_t)UART_SEND_PCM_FILE_CACHE_BYTES;
        }

        copy_bytes = bytes - offset;
        if (copy_bytes > free_bytes)
        {
            copy_bytes = free_bytes;
        }

        rt_memcpy(&uart_send_pcm_file_cache[uart_send_pcm_file_cache_len],
                  data + offset,
                  copy_bytes);
        uart_send_pcm_file_cache_len += copy_bytes;
        offset += copy_bytes;

        if (uart_send_pcm_file_cache_len >= (rt_size_t)UART_SEND_PCM_FILE_CACHE_BYTES)
        {
            uart_send_pcm_flush_file_cache();
        }
    }
}

static void uart_send_pcm_export_frames(rt_bool_t uart_ok,
                                        rt_bool_t file_ok,
                                        rt_bool_t write_uart,
                                        rt_uint32_t frames)
{
    rt_size_t bytes;

    if (frames == 0u)
    {
        return;
    }

    bytes = (rt_size_t)frames * sizeof(uart_send_pcm_buffer[0]);
    uart_send_pcm_process_samples(uart_send_pcm_buffer, frames);

    if (uart_ok && write_uart)
    {
        uart_send_pcm_write_all((const rt_uint8_t *)uart_send_pcm_buffer, bytes);
    }
    if (file_ok)
    {
        uart_send_pcm_write_file((const rt_uint8_t *)uart_send_pcm_buffer, bytes);
    }
}

static void uart_send_pcm_drain_file_tail(rt_bool_t file_ok)
{
    if (!file_ok || es8311_audio_is_capture_running())
    {
        return;
    }

    while (es8311_audio_get_capture_level_frames() > 0u)
    {
        rt_uint32_t frames;

        frames = es8311_audio_read_capture(uart_send_pcm_buffer, UART_SEND_PCM_READ_FRAMES);
        if (frames == 0u)
        {
            break;
        }

        uart_send_pcm_export_frames(RT_FALSE, file_ok, RT_FALSE, frames);
    }
}

static void uart_send_pcm_run_session(void)
{
    rt_bool_t uart_ok;
    rt_bool_t file_ok;

    uart_send_pcm_uart_bytes = 0;
    uart_send_pcm_uart_skipped_bytes = 0;
    uart_send_pcm_file_bytes = 0;
    uart_send_pcm_file_drops = 0;
    uart_send_pcm_reset_voice_process();

    if (UART_SEND_PCM_FILE_RECORD_ENABLE)
    {
        file_ok = (uart_send_pcm_open_file() == RT_EOK) ? RT_TRUE : RT_FALSE;
    }
    else
    {
        file_ok = RT_FALSE;
        LOG_I("record-to-file disabled, stream raw s16le mono to uart3 only");
    }

    if (UART_SEND_PCM_UART_EXPORT_ENABLE || !file_ok)
    {
        uart_ok = (uart_send_pcm_open_device() == RT_EOK) ? RT_TRUE : RT_FALSE;
    }
    else
    {
        uart_ok = RT_FALSE;
    }

    if (!uart_ok && !file_ok)
    {
        LOG_E("neither uart3 nor file available, pcm export exit");
        uart_send_pcm_set_ready(-RT_ERROR);
        uart_send_pcm_running = RT_FALSE;
        uart_send_pcm_done = RT_TRUE;
        return;
    }

    if (!uart_ok && file_ok)
    {
        LOG_W("uart3 export disabled/unavailable, only record to file");
    }
    else if (uart_ok && !file_ok)
    {
        LOG_I("uart3 export enabled, file record disabled");
    }
    uart_send_pcm_set_ready(RT_EOK);

    while (uart_send_pcm_running)
    {
        rt_uint32_t frames;
        rt_bool_t write_uart;

        frames = es8311_audio_read_capture(uart_send_pcm_buffer, UART_SEND_PCM_READ_FRAMES);
        if (frames == 0u)
        {
            rt_thread_mdelay(1);
            continue;
        }

        write_uart = uart_ok;
        if (write_uart &&
            (es8311_audio_get_capture_level_frames() > UART_SEND_PCM_UART_BACKLOG_LIMIT_FRAMES))
        {
            write_uart = RT_FALSE;
            uart_send_pcm_uart_skipped_bytes += (rt_uint32_t)frames * 2u;
        }

        uart_send_pcm_export_frames(uart_ok, file_ok, write_uart, frames);
    }

    uart_send_pcm_drain_file_tail(file_ok);
    uart_send_pcm_flush_file_cache();
    uart_send_pcm_close_file();
    if (file_ok)
    {
        uart_send_pcm_write_meta();
        LOG_I("pcm saved: %s (%u bytes, ~%u ms @44.1k mono), uart3=%u, uart3_skip=%u, file_drops=%u, capture_drops=%u, peak=%u, limiter=%u",
              UART_SEND_PCM_FILE_PATH,
              (unsigned)uart_send_pcm_file_bytes,
              (unsigned)((uart_send_pcm_file_bytes / 2u) * 1000u / 44100u),
              (unsigned)uart_send_pcm_uart_bytes,
              (unsigned)uart_send_pcm_uart_skipped_bytes,
              (unsigned)uart_send_pcm_file_drops,
              (unsigned)es8311_audio_get_capture_drop_frames(),
              (unsigned)uart_send_pcm_peak_abs,
              (unsigned)uart_send_pcm_limiter_hits);
        LOG_I("meta: %s  |  msh: fs_app_info / ls /pcm", UART_SEND_PCM_META_PATH);
    }
    else
    {
        LOG_I("pcm streamed: uart3=%u, uart3_skip=%u, capture_drops=%u, peak=%u, limiter=%u",
              (unsigned)uart_send_pcm_uart_bytes,
              (unsigned)uart_send_pcm_uart_skipped_bytes,
              (unsigned)es8311_audio_get_capture_drop_frames(),
              (unsigned)uart_send_pcm_peak_abs,
              (unsigned)uart_send_pcm_limiter_hits);
    }

    uart_send_pcm_close_device();
    uart_send_pcm_running = RT_FALSE;
    uart_send_pcm_done = RT_TRUE;
}

static void uart_send_pcm_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        if (!uart_send_pcm_running)
        {
            rt_thread_mdelay(1);
            continue;
        }

        uart_send_pcm_done = RT_FALSE;
        uart_send_pcm_run_session();
    }
}

static rt_err_t uart_send_pcm_ensure_thread(void)
{
    rt_err_t err;

    if (uart_send_pcm_thread_started)
    {
        return RT_EOK;
    }

    uart_send_pcm_thread = &uart_send_pcm_thread_obj;
    err = rt_thread_init(uart_send_pcm_thread,
                         "uart_pcm",
                         uart_send_pcm_thread_entry,
                         RT_NULL,
                         uart_send_pcm_thread_stack,
                         sizeof(uart_send_pcm_thread_stack),
                         UART_SEND_PCM_THREAD_PRIORITY,
                         UART_SEND_PCM_THREAD_TICK);
    if (err != RT_EOK)
    {
        uart_send_pcm_thread = RT_NULL;
        LOG_E("init uart_pcm thread failed: %d", err);
        return err;
    }

    err = rt_thread_startup(uart_send_pcm_thread);
    if (err != RT_EOK)
    {
        (void)rt_thread_detach(uart_send_pcm_thread);
        uart_send_pcm_thread = RT_NULL;
        LOG_E("startup uart_pcm thread failed: %d", err);
        return err;
    }

    uart_send_pcm_thread_started = RT_TRUE;
    return RT_EOK;
}

rt_err_t uart_send_pcm_start(void)
{
    rt_uint32_t wait_ms;
    rt_err_t err;

    if (uart_send_pcm_running)
    {
        for (wait_ms = 0u; (wait_ms < UART_SEND_PCM_START_WAIT_MS) && !uart_send_pcm_ready; wait_ms++)
        {
            rt_thread_mdelay(1);
        }
        return uart_send_pcm_ready ? uart_send_pcm_start_result : -RT_ETIMEOUT;
    }

    /* 新一轮 PTT 开始前先关闭下行接收，UART3 切回发送方向。 */
    err = uart_pcm_rx_disable();
    if (err != RT_EOK)
    {
        LOG_W("stop UART3 PCM RX timeout");
        return err;
    }

    for (wait_ms = 0u; (wait_ms < UART_SEND_PCM_STOP_WAIT_MS) && !uart_send_pcm_done; wait_ms++)
    {
        rt_thread_mdelay(1);
    }
    if (!uart_send_pcm_done)
    {
        return -RT_EBUSY;
    }

    err = uart_send_pcm_ensure_thread();
    if (err != RT_EOK)
    {
        return err;
    }

    uart_send_pcm_ready = RT_FALSE;
    uart_send_pcm_start_result = -RT_ERROR;
    uart_send_pcm_done = RT_FALSE;
    uart_send_pcm_running = RT_TRUE;

    for (wait_ms = 0u; (wait_ms < UART_SEND_PCM_START_WAIT_MS) && !uart_send_pcm_ready; wait_ms++)
    {
        rt_thread_mdelay(1);
    }

    if (!uart_send_pcm_ready)
    {
        uart_send_pcm_running = RT_FALSE;
        LOG_W("pcm export prepare timeout");
        return -RT_ETIMEOUT;
    }

    return uart_send_pcm_start_result;
}

void uart_send_pcm_stop(void)
{
    rt_uint32_t wait_ms;

    if (uart_send_pcm_running || !uart_send_pcm_done)
    {
        uart_send_pcm_running = RT_FALSE;

        /* 等线程完成 UART TX close；关闭之后才能把 uart3 切为 DMA RX。 */
        for (wait_ms = 0u; (wait_ms < UART_SEND_PCM_STOP_WAIT_MS) && !uart_send_pcm_done; wait_ms++)
        {
            rt_thread_mdelay(1);
        }
    }

    if (!uart_send_pcm_done)
    {
        LOG_W("pcm export stop timeout");
        return;
    }

    /* PTT 松手后直接进入等待回复状态，不需要额外的 MSH 命令。 */
    uart_pcm_rx_enable();
}
