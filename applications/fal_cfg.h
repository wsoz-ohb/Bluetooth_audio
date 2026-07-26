/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-07-26     26410       FAL config for onboard W25Q128 (SFUD port)
 */
#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <rtconfig.h>

/* ===================== Flash device Configuration ========================= */
/* nor_flash0 由 fal_flash_sfud_port.c 提供，底层通过
 * rt_sfud_flash_find_by_dev_name(FAL_USING_NOR_FLASH_DEV_NAME) 找到
 * sfud_app.c probe 出来的 "W25Q128"，容量/擦除粒度初始化时自动更新。 */
extern struct fal_flash_dev nor_flash0;

/* flash device table：只有板载这一颗 W25Q128 */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &nor_flash0,                                                     \
}

/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG
/* W25Q128 共 16MB，布局：
 *   font       0        ~ 2MB  : 汉字点阵字库（GB2312 16x16 约 260KB），裸分区地址直读，
 *                                供 LVGL 自定义字体显示曲名等中文
 *   fw_a       2MB      ~ 4MB  : OTA 新固件下载区（A 区）。应用运行时把新版本写到这里，
 *                                bootloader 校验通过后搬运到片内 Flash 运行
 *   fw_b       4MB      ~ 5MB  : 回退备份区（B 区）。保存当前能正常工作的固件副本，
 *                                新版本启动失败时 bootloader 用它回滚（F407 app 最大 1MB）
 *   filesystem 5MB      ~ 16MB : littlefs，存配置、封面等零散文件
 *
 * 注意：调整 filesystem 的偏移/大小后需要重新格式化（dfs_mkfs）。 */
#define FAL_PART_TABLE                                                                                     \
{                                                                                                          \
    {FAL_PART_MAGIC_WORD, "font",       FAL_USING_NOR_FLASH_DEV_NAME,               0,  2 * 1024 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD, "fw_a",       FAL_USING_NOR_FLASH_DEV_NAME, 2 * 1024 * 1024,  2 * 1024 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD, "fw_b",       FAL_USING_NOR_FLASH_DEV_NAME, 4 * 1024 * 1024,  1 * 1024 * 1024, 0}, \
    {FAL_PART_MAGIC_WORD, "filesystem", FAL_USING_NOR_FLASH_DEV_NAME, 5 * 1024 * 1024, 11 * 1024 * 1024, 0}, \
}
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* _FAL_CFG_H_ */
