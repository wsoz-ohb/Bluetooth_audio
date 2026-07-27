/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       YMODEM receive font.bin into FAL "font" partition
 *
 * 踩坑记录(YMODEM 烧字库，排查顺序，勿随便删):
 * 1) 现象：YMODEM 传输字节计数 256376 全对(零重传)、CRC16 全 ACK，但落盘后
 *    font_read_header 读到 bad magic，分区开头还是上一任 OTA demo 的旧数据
 *    "OTA0 A5 A5 A5 A5 ..."。
 * 2) 排除上位机：MobaXterm 和自写 Python sender 两个独立实现结果一致；
 *    YMODEM 停等+每包 CRC16，零重传即证明到板子内存的数据比特级正确。
 * 3) 排除接收代码：received 字节数精确命中，说明 rym 回调+4KB 聚合+截断逻辑全对。
 * 4) 排除 SPI1 总线竞争：font_update 期间持有 rt_spi_take_bus 锁，LCD 被挡，
 *    且校验读在锁释放后是单次原子读，LCD 无法插入。
 * 5) 定位：fal read 0 16 连读四遍完全一致 -> 读稳定，非信号完整性/20MHz 问题；
 *    fal erase + fal write 报 success 但内容纹丝不动 -> 写入被芯片静默丢弃。
 * 6) 真凶：sf status 读出 0x3C，BP 位(TB/BP3..BP0)全置位=整片写保护。
 *    W25Q 系列对受保护区域的 PROGRAM/ERASE 静默丢弃不报错，读完全正常。
 *    sf status 0 00 清零后一切正常。
 *    该解除逻辑已固化到 sfud_app_init()，开机自动执行(见 sfud_app.c)。
 *
 * 用法:
 *   msh> font_update          进入 YMODEM 接收,然后在终端(MobaXterm/XShell/
 *                             Tera Term)选择 "YMODEM 发送" 传 fontlib/font.bin
 *   msh> font_info            读取分区内字库头部并做 CRC32 完整性校验
 *
 * 依赖: RT_USING_FAL + RT_USING_RYM(menuconfig: Utilities -> YMODEM)
 */
#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "font_upd"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#if defined(RT_USING_FAL)

#include <fal.h>
#include <stdlib.h>
#include "sfud_app.h"

#define FONT_PART_NAME          "font"
#define FONT_MAGIC              0x54464251  /* "ZBFT" 小端读出的 u32: 'Z''B''F''T' */
#define FONT_HEADER_SIZE        16
#define FONT_ERASE_BLOCK        4096        /* W25Q128 扇区粒度,聚合缓冲同尺寸 */
#define FONT_BYTES_PER_GLYPH    32

/* ZBFT 头部布局(与 fontlib/font_gen.py 一致,小端) */
struct font_header
{
    rt_uint8_t  magic[4];       /* "ZBFT" */
    rt_uint8_t  version;
    rt_uint8_t  width;
    rt_uint8_t  height;
    rt_uint8_t  bpp;
    rt_uint32_t glyph_cnt;
    rt_uint32_t crc32;          /* 覆盖索引表+点阵区(不含头部) */
};

/* ---------------- CRC32 (zlib 多项式 0xEDB88320, 流式) ---------------- */

static rt_uint32_t font_crc32_update(rt_uint32_t state, const rt_uint8_t *data, rt_size_t len)
{
    rt_size_t i;
    int bit;

    for (i = 0; i < len; i++)
    {
        state ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            state = (state >> 1) ^ (0xEDB88320UL & (-(rt_int32_t)(state & 1)));
        }
    }
    return state;
}

static const struct fal_partition *font_part_get(void)
{
    const struct fal_partition *part;
    static rt_bool_t s_fal_ready = RT_FALSE;

    /* fal_init 依赖 sfud_app_init 已把 "W25Q128" 注册好(main 里最先执行)。
     * 这里惰性初始化并记忆结果,后续 littlefs 挂载模块可复用同一份 FAL。 */
    if (!s_fal_ready)
    {
        if (fal_init() <= 0)
        {
            rt_kprintf("fal_init failed (SFUD flash not ready?)\n");
            return RT_NULL;
        }
        s_fal_ready = RT_TRUE;
    }

    part = fal_partition_find(FONT_PART_NAME);
    if (part == RT_NULL)
    {
        rt_kprintf("partition \"%s\" not found in FAL table\n", FONT_PART_NAME);
    }
    return part;
}

/* 读头部并解析,成功返回 RT_EOK 且填充 hdr */
static rt_err_t font_read_header(const struct fal_partition *part, struct font_header *hdr)
{
    rt_uint8_t raw[FONT_HEADER_SIZE];

    if (fal_partition_read(part, 0, raw, sizeof(raw)) < 0)
    {
        return -RT_EIO;
    }

    rt_memcpy(hdr->magic, raw, 4);
    hdr->version   = raw[4];
    hdr->width     = raw[5];
    hdr->height    = raw[6];
    hdr->bpp       = raw[7];
    hdr->glyph_cnt = (rt_uint32_t)raw[8] | ((rt_uint32_t)raw[9] << 8) |
                     ((rt_uint32_t)raw[10] << 16) | ((rt_uint32_t)raw[11] << 24);
    hdr->crc32     = (rt_uint32_t)raw[12] | ((rt_uint32_t)raw[13] << 8) |
                     ((rt_uint32_t)raw[14] << 16) | ((rt_uint32_t)raw[15] << 24);

    if (rt_memcmp(hdr->magic, "ZBFT", 4) != 0)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

/* 流式校验分区内容: 逐块读出索引+点阵区算 CRC32,与头部记录值比对。
 * 板上堆只有 ~24KB(bss 末尾到 RAM 顶),所有大缓冲一律临时借、用完即还,
 * 禁止常驻 static —— 之前 4KB+1KB 静态缓冲直接把 control 线程和 LVGL 挤挂了。 */
static rt_err_t font_verify_crc(const struct fal_partition *part,
                                const struct font_header *hdr,
                                rt_uint32_t *crc_out)
{
    rt_uint8_t *buf;
    rt_uint32_t payload_len;
    rt_uint32_t offset = FONT_HEADER_SIZE;
    rt_uint32_t remain;
    rt_uint32_t state = 0xFFFFFFFFUL;
    rt_err_t err = RT_EOK;

    payload_len = hdr->glyph_cnt * 2 + hdr->glyph_cnt * FONT_BYTES_PER_GLYPH;
    if (FONT_HEADER_SIZE + payload_len > part->len)
    {
        return -RT_EINVAL;
    }

    buf = rt_malloc(1024);
    if (buf == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    remain = payload_len;
    while (remain > 0)
    {
        rt_uint32_t chunk = remain > 1024 ? 1024 : remain;

        if (fal_partition_read(part, offset, buf, chunk) < 0)
        {
            err = -RT_EIO;
            break;
        }
        state = font_crc32_update(state, buf, chunk);
        offset += chunk;
        remain -= chunk;
    }

    rt_free(buf);
    if (err != RT_EOK)
    {
        return err;
    }

    *crc_out = ~state;
    return (*crc_out == hdr->crc32) ? RT_EOK : -RT_ERROR;
}

static void font_info(int argc, char **argv)
{
    const struct fal_partition *part;
    struct font_header hdr;
    rt_uint32_t crc;
    rt_err_t err;

    (void)argc;
    (void)argv;

    part = font_part_get();
    if (part == RT_NULL)
    {
        return;
    }

    if (font_read_header(part, &hdr) != RT_EOK)
    {
        rt_kprintf("no valid font in partition (magic mismatch) -> run font_update first\n");
        return;
    }

    rt_kprintf("magic    : ZBFT v%d\n", hdr.version);
    rt_kprintf("glyph    : %dx%d %dbpp, count=%u\n",
               hdr.width, hdr.height, hdr.bpp, (unsigned)hdr.glyph_cnt);
    rt_kprintf("size     : %u bytes\n",
               (unsigned)(FONT_HEADER_SIZE + hdr.glyph_cnt * (2 + FONT_BYTES_PER_GLYPH)));
    rt_kprintf("crc32    : 0x%08X (header)\n", (unsigned)hdr.crc32);

    rt_kprintf("verifying...\n");
    err = font_verify_crc(part, &hdr, &crc);
    rt_kprintf("computed : 0x%08X -> %s\n", (unsigned)crc,
               (err == RT_EOK) ? "PASS" : "FAIL");
}
MSH_CMD_EXPORT(font_info, show and verify font partition content);

#if defined(RT_USING_RYM)

#include <ymodem.h>

/* ---------------- YMODEM 接收上下文 ---------------- */

struct font_rym_ctx
{
    struct rym_ctx parent;
    const struct fal_partition *part;
    rt_int32_t  file_size;      /* 发送端报告的文件大小,-1 表示未知 */
    rt_uint32_t received;       /* 已计入的有效字节数(去除 YMODEM 填充) */
    rt_uint32_t write_offset;   /* 下一个 4KB 块在分区内的偏移 */
    rt_uint32_t buf_used;       /* 聚合缓冲已用字节 */
    rt_bool_t   write_failed;
};

static struct font_rym_ctx s_rym;
/* 4KB 聚合缓冲: YMODEM 一包 128/1024B,攒满一个擦除扇区再写。
 * 堆内存紧张,只在 font_update 会话期间 rt_malloc,结束立即释放 */
static rt_uint8_t *s_block_buf = RT_NULL;

static enum rym_code font_rym_flush_block(void)
{
    if (s_rym.buf_used == 0)
    {
        return RYM_CODE_ACK;
    }

    if (fal_partition_erase(s_rym.part, s_rym.write_offset, FONT_ERASE_BLOCK) < 0 ||
        fal_partition_write(s_rym.part, s_rym.write_offset, s_block_buf, s_rym.buf_used) < 0)
    {
        s_rym.write_failed = RT_TRUE;
        return RYM_CODE_CAN;
    }

    s_rym.write_offset += FONT_ERASE_BLOCK;
    s_rym.buf_used = 0;
    return RYM_CODE_ACK;
}

static enum rym_code font_rym_on_begin(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    const char *name = (const char *)buf;
    const char *size_str;

    (void)ctx;

    /* 起始包: "文件名\0大小..." */
    size_str = name + rt_strnlen(name, len - 1) + 1;
    s_rym.file_size = atoi(size_str);
    if (s_rym.file_size <= 0)
    {
        s_rym.file_size = -1;   /* 发送端未报大小,收多少算多少 */
    }

    if (s_rym.file_size > 0 && (rt_uint32_t)s_rym.file_size > s_rym.part->len)
    {
        return RYM_CODE_CAN;    /* 比 2MB 分区还大,必然不是字库 */
    }

    s_rym.received = 0;
    s_rym.write_offset = 0;
    s_rym.buf_used = 0;
    s_rym.write_failed = RT_FALSE;
    return RYM_CODE_ACK;
}

static enum rym_code font_rym_on_data(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    rt_size_t valid = len;

    (void)ctx;

    /* 最后一包会被 YMODEM 用 0x1A 填充到整包,按文件真实大小截断 */
    if (s_rym.file_size > 0)
    {
        rt_uint32_t remain = (rt_uint32_t)s_rym.file_size - s_rym.received;
        if (valid > remain)
        {
            valid = remain;
        }
    }

    while (valid > 0)
    {
        rt_size_t space = FONT_ERASE_BLOCK - s_rym.buf_used;
        rt_size_t chunk = valid > space ? space : valid;

        rt_memcpy(s_block_buf + s_rym.buf_used, buf, chunk);
        s_rym.buf_used += chunk;
        s_rym.received += chunk;
        buf += chunk;
        valid -= chunk;

        if (s_rym.buf_used == FONT_ERASE_BLOCK)
        {
            if (font_rym_flush_block() != RYM_CODE_ACK)
            {
                return RYM_CODE_CAN;
            }
        }
    }

    return RYM_CODE_ACK;
}

static enum rym_code font_rym_on_end(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;

    font_rym_flush_block();     /* 落盘最后不足 4KB 的尾巴 */
    return RYM_CODE_ACK;
}

static void font_update(int argc, char **argv)
{
    const struct fal_partition *part;
    struct font_header hdr;
    rt_uint32_t crc;
    rt_device_t dev;
    struct rt_spi_device *flash_spi = RT_NULL;
    rt_spi_flash_device_t flash_dev;
    rt_err_t err;

    (void)argc;
    (void)argv;

    part = font_part_get();
    if (part == RT_NULL)
    {
        return;
    }

    dev = rt_console_get_device();
    if (dev == RT_NULL)
    {
        rt_kprintf("no console device\n");
        return;
    }

    /* LCD(st7789) 与 Flash(w25q) 共 SPI1。SPI 框架的总线锁只保证单次
     * transfer 原子,而 SFUD 一次写 = WREN + PROGRAM + 状态轮询多次 transfer,
     * 期间 LVGL 刷屏可插队。总线锁是递归互斥量:本线程整个会话按住它,
     * SFUD 在同线程内照常加锁不死锁,LVGL 线程则被完全挡住(GUI 冻结~30s)。 */
    flash_dev = sfud_app_get_device();
    if (flash_dev != RT_NULL)
    {
        flash_spi = flash_dev->rt_spi_device;
    }
    if (flash_spi != RT_NULL)
    {
        rt_spi_take_bus(flash_spi);
    }

    rt_memset(&s_rym, 0, sizeof(s_rym));
    s_rym.part = part;

    s_block_buf = rt_malloc(FONT_ERASE_BLOCK);
    if (s_block_buf == RT_NULL)
    {
        rt_kprintf("no memory for 4KB transfer buffer\n");
        return;
    }

    rt_kprintf("start YMODEM send of font.bin in your terminal now"
               " (console will be silent during transfer)...\n");

    /* rym 会独占控制台串口直到传输结束/超时(60s 无握手自动退出) */
    err = rym_recv_on_device(&s_rym.parent, dev,
                             RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                             font_rym_on_begin, font_rym_on_data, font_rym_on_end,
                             60);

    /* 等终端把 YMODEM 会话尾巴吐完,避免打印和协议字节混在一起 */
    rt_thread_mdelay(200);

    rt_free(s_block_buf);
    s_block_buf = RT_NULL;

    /* 写入阶段结束,归还总线让 GUI 恢复刷屏。
     * 后面的 CRC 校验只是逐次单笔读,SPI 框架能保证单笔原子性,无需再霸占。 */
    if (flash_spi != RT_NULL)
    {
        rt_spi_release_bus(flash_spi);
    }

    if (err != RT_EOK || s_rym.write_failed)
    {
        rt_kprintf("\ntransfer failed (err=%d, stage=%d%s), partition content is undefined,"
                   " please retry font_update\n",
                   err, s_rym.parent.stage,
                   s_rym.write_failed ? ", flash write error" : "");
        return;
    }

    rt_kprintf("\nreceived %u bytes, verifying...\n", (unsigned)s_rym.received);

    if (font_read_header(part, &hdr) != RT_EOK)
    {
        rt_kprintf("verify FAIL: bad magic, not a ZBFT font file?\n");
        return;
    }

    if (font_verify_crc(part, &hdr, &crc) != RT_EOK)
    {
        rt_kprintf("verify FAIL: crc computed 0x%08X != header 0x%08X, please retry\n",
                   (unsigned)crc, (unsigned)hdr.crc32);
        return;
    }

    rt_kprintf("font OK: %u glyphs %dx%d, crc 0x%08X pass\n",
               (unsigned)hdr.glyph_cnt, hdr.width, hdr.height, (unsigned)crc);
}
MSH_CMD_EXPORT(font_update, receive font.bin via YMODEM into font partition);

#else /* !RT_USING_RYM */

static void font_update(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("YMODEM not enabled: RT-Thread Settings -> Utilities -> YMODEM (RT_USING_RYM)\n");
}
MSH_CMD_EXPORT(font_update, receive font.bin via YMODEM into font partition);

#endif /* RT_USING_RYM */

#endif /* RT_USING_FAL */
