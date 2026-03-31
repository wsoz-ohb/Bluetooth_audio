/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     wsoz       the first version
 */
#include "mypool.h"

/**
 * @brief  初始化内存池，将 buf 切成 count 个块并串成空闲链表
 */
void mpool_init(mpool_t *pool, void *buf, uint16_t blk_size, uint16_t count)
{
    uint16_t stride = MPOOL_ALIGN_UP(blk_size + sizeof(mpool_node_t));
    uint8_t *p = (uint8_t *)buf;

    pool->free_list = (mpool_node_t *)p;
    pool->blk_size  = blk_size;
    pool->total     = count;
    pool->used      = 0;

    for (uint16_t i = 0; i < count - 1; i++) {
        ((mpool_node_t *)p)->next = (mpool_node_t *)(p + stride);
        p += stride;
    }
    ((mpool_node_t *)p)->next = NULL;
}

/**
 * @brief  从内存池分配一个块，返回清零后的用户指针，池空则返回 NULL
 */
void *mpool_alloc(mpool_t *pool)
{
    mpool_node_t *node = pool->free_list;
    if (node == NULL) return NULL;

    pool->free_list = node->next;
    pool->used++;

    void *ptr = (uint8_t *)node + sizeof(mpool_node_t);
    memset(ptr, 0, pool->blk_size);
    return ptr;
}

/**
 * @brief  将块归还到内存池
 */
void mpool_free(mpool_t *pool, void *ptr)
{
    if (ptr == NULL) return;

    mpool_node_t *node = (mpool_node_t *)((uint8_t *)ptr - sizeof(mpool_node_t));
    node->next = pool->free_list;
    pool->free_list = node;
    pool->used--;
}
