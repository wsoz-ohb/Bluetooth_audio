/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       SFUD app for onboard W25Q128
 *
 * ---------------------------------------------------------------------------
 * 能扫到 W25Q128 的关键点（踩坑记录，勿随便删）:
 * 1) 探卡前强制重配 SPI1 GPIO，MISO 上拉，CS 明确为推挽高电平
 * 2) 共 SPI1 时先把 LCD CS(PC4) 拉高，避免屏片选悬空抢总线
 * 3) JEDEC 用全双工 rt_spi_transfer 一次读完，不要分两段 send_then_recv
 * 4) 探卡时钟先 1MHz，成功后再提到工作频率
 * 5) 先手动读 JEDEC(EF 40 18) 确认物理层，再交给 SFUD probe
 * 6) 出厂/上电后状态寄存器可能带写保护(BP 位非零)，擦写会被芯片静默丢弃
 *    ——见本文件末尾的“写保护踩坑”说明，初始化时已自动解除
 * ---------------------------------------------------------------------------
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

/* 与 LCD 共用 SPI1：Flash 独占 CS=PA4，总线引脚 PA5/PA6/PA7。 */
#define SFUD_SPI_BUS_NAME       "spi1"
#define SFUD_SPI_DEV_NAME       "w25q"
#define SFUD_FLASH_DEV_NAME     "W25Q128"
#define SFUD_CS_GPIOX           GPIOA
#define SFUD_CS_GPIO_PIN        GPIO_PIN_4

/* LCD CS 必须保持高，避免共总线时 ST7789 片选悬空抢线。 */
#define SFUD_LCD_CS_PIN         GET_PIN(C, 4)

/*
 * 【扫卡关键点】时钟策略
 * PROBE 1MHz : 识别阶段稳，共总线/飞线时显著降低 JEDEC=00 00 00 概率
 * WORK  42MHz: SFUD 成功后再提速; W25Q 常温可读到 50MHz+,与 LCD 共总线时
 *              若中文/字库偶发读坏再降回 30 或 20。
 */
#define SFUD_SPI_PROBE_HZ       (1 * 1000 * 1000)
#define SFUD_SPI_WORK_HZ        (42 * 1000 * 1000)

#define SFUD_CMD_JEDEC_ID       0x9F

#if defined(RT_USING_SPI) && defined(BSP_USING_SPI1) && defined(RT_USING_SFUD)

static rt_spi_flash_device_t g_flash_dev = RT_NULL;
static rt_bool_t g_sfud_ready = RT_FALSE;

/*
 * 【扫卡关键点 1】LCD 与 Flash 共 SPI1。
 * 探 Flash 前必须把 LCD CS 拉高释放总线；
 * 若 PC4 悬空，ST7789 可能被误选中，干扰 MISO/时序，出现 JEDEC=00 00 00。
 */
static void sfud_app_hold_peer_cs_idle(void)
{
    rt_pin_mode(SFUD_LCD_CS_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SFUD_LCD_CS_PIN, PIN_HIGH);
}

/*
 * 【扫卡关键点 2】探卡前强制恢复 SPI1 引脚功能。
 * PA5=SCK, PA6=MISO, PA7=MOSI -> AF5
 * PA4=Flash CS -> GPIO 推挽，默认高电平释放
 *
 * 为什么要这样:
 * - CubeMX / 其他驱动可能改过 GPIO 模式
 * - MISO 开上拉，避免浮空读成全 0，也便于判断是否被硬拉低
 * - CS 必须由软件明确拉高/拉低，片选不干净就会 JEDEC 失败
 */
static void sfud_app_force_spi1_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* SPI 数据线：MISO 上拉是扫卡稳定性的重要一点 */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Flash CS：推挽输出，默认释放（高） */
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

/*
 * 【扫卡关键点 3 + 4】手动读 JEDEC，确认物理层先通。
 *
 * 旧写法 rt_spi_send_then_recv(发1字节 + 再收3字节) 在 STM32 HAL 主模式
 * 全双工下容易因为 RX 残留/分段收发读到 00 00 00。
 *
 * 成功写法：全双工一次 transfer
 *   TX: 9F FF FF FF
 *   RX: xx ID0 ID1 ID2  -> 有效 ID 在 rx[1..3]
 *
 * 探卡时钟用 SFUD_SPI_PROBE_HZ(1MHz)，飞线/共总线时更稳；
 * W25Q128 正常应读到 EF 40 18（或 EF 70 18 等同容量变体）。
 */
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

    /* 先用低速配置 SPI，这是扫卡成功的关键条件之一 */
    err = sfud_app_configure_spi(SFUD_SPI_PROBE_HZ);
    if (err != RT_EOK)
    {
        return err;
    }

    /* 全双工一次完成命令+读 ID，不要拆成 send + recv 两段 */
    n = rt_spi_transfer(spi, tx, rx, sizeof(tx));
    if (n != sizeof(tx))
    {
        return -RT_EIO;
    }

    /* rx[0] 是发命令时的时钟占位回读，真正的 JEDEC 从 rx[1] 开始 */
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

    /* 已有同名 flash 设备则复用，避免重复 probe。
     * rt_device_find 返回的是内嵌 rt_device，真正的 wrapper 在 user_data。 */
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

    /*
     * 【扫卡成功流程】顺序很重要，不要乱:
     * A. LCD CS 拉高释放总线
     * B. 强制恢复 SPI1/Flash CS 引脚
     * C. attach "w25q" 到 spi1
     * D. 低速全双工读 JEDEC，先证明物理层
     * E. JEDEC 有效后再 rt_sfud_flash_probe_ex
     * F. probe 成功后提高工作时钟
     */
    sfud_app_hold_peer_cs_idle();   /* A: 共总线保护 */
    sfud_app_force_spi1_gpio();     /* B: GPIO/CS/MISO 上拉 */

    err = sfud_app_attach_spi_device(); /* C */
    if (err != RT_EOK)
    {
        return err;
    }

    /* D: 手动 JEDEC。读到 EF 40 18 才说明线通，否则别急着怪 SFUD */
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
        /* 物理层失败时直接返回，避免 SFUD 刷 SFDP/not support 误导日志 */
        LOG_E("skip SFUD probe because JEDEC is invalid");
        return -RT_ERROR;
    }

    /* E: 物理层 OK 后，再用同样的低速配置交给 SFUD 做完整初始化 */
    spi_cfg.data_width = 8;
    spi_cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    spi_cfg.max_hz = SFUD_SPI_PROBE_HZ; /* 探卡继续保持 1MHz */

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

    /* F: 识别成功后再提速；探卡阶段不要一上来就 20~50MHz */
    (void)sfud_app_configure_spi(SFUD_SPI_WORK_HZ);

    sfud_dev = (sfud_flash_t)g_flash_dev->user_data;

    /*
     * 【写保护踩坑】实测这颗 W25Q128 上电后状态寄存器 = 0x3C，BP 位(TB/BP3..BP0)
     * 全置位，等于把整片 16MB 都保护了。表现极其隐蔽：读完全正常，PROGRAM/ERASE
     * 命令被芯片"静默丢弃"(不报错、不写入)，SFUD/FAL 都会回 success 但内容纹丝不动。
     * 现象：擦完不变 FF、写完读回原值、YMODEM 传 256KB 字节计数全对但落盘全是旧数据。
     * 对策：初始化时主动读状态寄存器，BP 位非零就写 0x00 解除保护。
     * 上一任用户(OTA demo)残留的保护位就是这么被继承下来的。
     */
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

/* 硬件自检：看 CS 电平和 JEDEC，方便区分接线问题/软件问题 */
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

    /* CS 释放时采样 */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    rt_thread_mdelay(1);
    idr_cs_high = GPIOA->IDR;

    /* CS 拉低时采样 */
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

#else /* !SPI/SFUD */

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