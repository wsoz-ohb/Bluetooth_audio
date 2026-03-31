/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-02-28     wsoz       the first version
 */
#ifndef MYCOMPONENTS_KEYBOARD_INC_MYPOOL_H_
#define MYCOMPONENTS_KEYBOARD_INC_MYPOOL_H_

#include <stdint.h>
#include <string.h>

/* 4字节对齐 */
#define MPOOL_ALIGN_UP(s)  (((s) + 3u) & ~3u)   //保证编译的要求

/* 空闲链表节点（嵌入在每个块头部） */
typedef struct mpool_node {
    struct mpool_node *next;
} mpool_node_t;

/* 内存池控制结构 */
typedef struct {
    mpool_node_t *free_list;   /* 空闲链表头 */
    uint16_t      blk_size;    /* 用户数据块大小 */
    uint16_t      total;       /* 总块数 */
    uint16_t      used;        /* 已使用块数 */
} mpool_t;

/*--- 核心 API ---*/
void  mpool_init (mpool_t *pool, void *buf, uint16_t blk_size, uint16_t count);
void *mpool_alloc(mpool_t *pool);
void  mpool_free (mpool_t *pool, void *ptr);

/*--- 查询 ---*/
static inline uint16_t mpool_used_count(mpool_t *p) { return p->used; }
static inline uint16_t mpool_free_count(mpool_t *p) { return p->total - p->used; }

/*--- 便捷宏 ---*/

/**
 * 定义一个内存池（放在 .c 文件全局作用域）
 * @param name   池变量名
 * @param type   存储的结构体类型
 * @param count  块数量
 *
 * 展开后自动生成: 静态缓冲数组 + mpool_t 控制结构
 */
#define MPOOL_DEFINE(name, type, count)                                     \
    static uint8_t name##_buf[(count) *                                     \
        MPOOL_ALIGN_UP(sizeof(type) + sizeof(mpool_node_t))];               \
    mpool_t name = { .free_list = NULL, .blk_size = sizeof(type),           \
                     .total = (count), .used = 0 }

/**
 * 初始化内存池（在 main 或 xxx_init 中调用一次）
 */
#define MPOOL_INIT(name)  \
    mpool_init(&(name), (name##_buf), (name).blk_size, (name).total)


#endif /* MYCOMPONENTS_KEYBOARD_INC_MYPOOL_H_ */
