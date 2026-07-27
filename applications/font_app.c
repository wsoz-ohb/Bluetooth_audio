/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       LVGL custom font backed by W25Q128 font partition
 *
 * 实现要点(与 docs/lvgl_chinese_font_design.md §4 一致):
 * - init: 读 ZBFT 头部校验 magic/字数,把索引表(7540*2=15KB)缓存进 RAM,
 *         二分查找走内存,只对每个字的 32B 点阵做一次 fal_partition_read。
 * - 两个 LVGL 回调都独立二分查找(不依赖共享状态),LVGL 单线程渲染,
 *   get_glyph_bitmap 返回的指针在 draw_letter_normal 内立即消费,32B 静态缓冲安全。
 * - ASCII(码点<0x80)adv_w=8,右半 8 列空白,下个字覆盖不冲突;中文 adv_w=16。
 *   box_w 统一 16,与 font.bin 每行 2 字节格式吻合,读点阵无需换算。
 */
#include "font_app.h"

#include <rtthread.h>
#include <fal.h>
#include <string.h>

#define DBG_TAG "font_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>


#define FONT_PART_NAME          "font"
#define FONT_HEADER_SIZE        16
#define FONT_BYTES_PER_GLYPH    32      /* 16x16 1bpp */
#define FONT_GLYPH_W            16
#define FONT_GLYPH_H            16
#define FONT_LINE_HEIGHT        16
#define FONT_BPP                1
/* 基线在底部上方 4px(典型 16px 字体),边界框顶比基线高 12px */
#define FONT_BASE_LINE          4
#define FONT_OFS_Y              (-12)

/* 字库最大字数上限,防头部损坏时乱 malloc */
#define FONT_GLYPH_CNT_LIMIT    20000u

struct font_ctx
{
    const struct fal_partition *part;
    rt_uint32_t                 glyph_cnt;
    rt_uint16_t                *index;      /* RAM 缓存的索引表,glyph_cnt 项 */
    rt_bool_t                   ready;
    lv_font_t                    font;
    rt_uint8_t                   bitmap_buf[FONT_BYTES_PER_GLYPH];  /* 32B 静态 */
};

static struct font_ctx s_ctx;

static rt_uint32_t le32(const rt_uint8_t *p)
{
    return (rt_uint32_t)p[0] | ((rt_uint32_t)p[1] << 8) |
           ((rt_uint32_t)p[2] << 16) | ((rt_uint32_t)p[3] << 24);
}

/* 二分查找 Unicode 码点在索引表中的下标,未命中返回 -1 */
static rt_int32_t font_find_glyph(rt_uint32_t letter)
{
    rt_int32_t lo = 0;
    rt_int32_t hi = (rt_int32_t)s_ctx.glyph_cnt - 1;

    while (lo <= hi)
    {
        rt_int32_t mid = lo + ((hi - lo) >> 1);
        rt_uint16_t cp = s_ctx.index[mid];

        if (cp == letter)
        {
            return mid;
        }
        if (cp < letter)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return -1;
}

static bool font_get_glyph_dsc_cb(const lv_font_t *font,
                                  lv_font_glyph_dsc_t *dsc_out,
                                  uint32_t letter, uint32_t letter_next)
{
    rt_int32_t idx;

    LV_UNUSED(font);
    LV_UNUSED(letter_next);

    if (!s_ctx.ready)
    {
        return false;
    }

    idx = font_find_glyph(letter);
    if (idx < 0)
    {
        return false;     /* LVGL 会走 fallback 或画占位符 */
    }

    /* box_w 统一 16(与 font.bin 每行 2B 格式一致);
     * ASCII 用 adv_w=8 实现半角步进,右 8 列空白不与下字冲突 */
    dsc_out->adv_w         = (letter < 0x80) ? 8 : 16;
    dsc_out->box_w         = FONT_GLYPH_W;
    dsc_out->box_h         = FONT_GLYPH_H;
    dsc_out->ofs_x         = 0;
    dsc_out->ofs_y         = FONT_OFS_Y;
    dsc_out->bpp           = FONT_BPP;
    dsc_out->is_placeholder = 0;
    return true;
}

static const uint8_t *font_get_glyph_bitmap_cb(const lv_font_t *font, uint32_t letter)
{
    rt_int32_t idx;
    rt_uint32_t offset;

    LV_UNUSED(font);

    if (!s_ctx.ready)
    {
        return NULL;
    }

    /* 自包含二分,不依赖 dsc 阶段存的 index(防御性,LVGL fallback 解析也无碍) */
    idx = font_find_glyph(letter);
    if (idx < 0)
    {
        return NULL;
    }

    offset = FONT_HEADER_SIZE + s_ctx.glyph_cnt * 2 + (rt_uint32_t)idx * FONT_BYTES_PER_GLYPH;
    if (fal_partition_read(s_ctx.part, offset, s_ctx.bitmap_buf, FONT_BYTES_PER_GLYPH) < 0)
    {
        LOG_W("read glyph U+%04lX at off=%lu failed", (unsigned long)letter, (unsigned long)offset);
        return NULL;
    }
    return s_ctx.bitmap_buf;
}

rt_err_t font_app_init(void)
{
    rt_uint8_t hdr[FONT_HEADER_SIZE];
    rt_uint32_t cnt;

    if (s_ctx.ready)
    {
        return RT_EOK;
    }

    /* FAL 依赖 sfud_app_init() 已把 "W25Q128" 注册好(main 最先执行) */
    if (fal_init() <= 0)
    {
        LOG_E("fal_init failed");
        return -RT_ERROR;
    }

    s_ctx.part = fal_partition_find(FONT_PART_NAME);
    if (s_ctx.part == RT_NULL)
    {
        LOG_E("partition \"%s\" not found", FONT_PART_NAME);
        return -RT_ERROR;
    }

    if (fal_partition_read(s_ctx.part, 0, hdr, FONT_HEADER_SIZE) < 0)
    {
        LOG_E("read header failed");
        return -RT_EIO;
    }

    if (memcmp(hdr, "ZBFT", 4) != 0)
    {
        LOG_W("font magic mismatch (not burned?). run font_update first");
        return -RT_ERROR;
    }

    cnt = le32(hdr + 8);
    if (cnt == 0 || cnt > FONT_GLYPH_CNT_LIMIT)
    {
        LOG_E("bad glyph_cnt %u (header corrupt?)", (unsigned)cnt);
        return -RT_ERROR;
    }
    s_ctx.glyph_cnt = cnt;

    /* 缓存索引表: 7540*2 = 15KB,堆宽裕(~80KB),值得换"二分走内存"的零延迟 */
    s_ctx.index = (rt_uint16_t *)rt_malloc(cnt * 2);
    if (s_ctx.index == RT_NULL)
    {
        LOG_E("no memory for index table (%u bytes)", (unsigned)(cnt * 2));
        return -RT_ENOMEM;
    }

    if (fal_partition_read(s_ctx.part, FONT_HEADER_SIZE,
                           (rt_uint8_t *)s_ctx.index, cnt * 2) < 0)
    {
        LOG_E("read index table failed");
        rt_free(s_ctx.index);
        s_ctx.index = RT_NULL;
        return -RT_EIO;
    }

    /* 填充 lv_font_t。fallback=NULL:缺字时 LVGL 自行画占位符,不串到英文字体 */
    s_ctx.font.get_glyph_dsc    = font_get_glyph_dsc_cb;
    s_ctx.font.get_glyph_bitmap = font_get_glyph_bitmap_cb;
    s_ctx.font.line_height      = FONT_LINE_HEIGHT;
    s_ctx.font.base_line        = FONT_BASE_LINE;
    s_ctx.font.subpx            = LV_FONT_SUBPX_NONE;
    s_ctx.font.underline_position   = -2;
    s_ctx.font.underline_thickness   = 1;
    s_ctx.font.dsc              = RT_NULL;
    s_ctx.font.fallback         = RT_NULL;

    s_ctx.ready = RT_TRUE;
    LOG_I("font ready: %u glyphs, index cached %u bytes, base_line=%d",
          (unsigned)cnt, (unsigned)(cnt * 2), FONT_BASE_LINE);
    return RT_EOK;
}

const lv_font_t *font_app_get16(void)
{
    if (!s_ctx.ready)
    {
        font_app_init();
    }
    return s_ctx.ready ? &s_ctx.font : NULL;
}

rt_bool_t font_app_is_ready(void)
{
    return s_ctx.ready;
}
