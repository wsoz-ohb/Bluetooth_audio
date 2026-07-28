/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 把 FAL 分区 "filesystem" 做成 littlefs。
 *
 * 注意: RT-Thread dfs_mount() 在挂载点不是 "/" 或 "/dev" 时，会先
 * dfs_file_open(O_DIRECTORY) 检查挂载点是否已存在。工程没有额外的
 * 根文件系统时，"/fs" 这类路径会得到 ENOTDIR(-20)。
 * 因此这里直接挂到根 "/"，录音目录用 /pcm。
 *
 * 依赖: sfud_app_init() 已把 W25Q128 注册好。
 *
 * 用法:
 *   msh> ls /
 *   msh> ls /pcm
 *   msh> cat /pcm/last.txt
 *   msh> fs_app_info
 */
#include "fs_app.h"

#include <fal.h>
#include <dfs_fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define DBG_TAG "fs_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define FS_FAL_PART_NAME       "filesystem"
#define FS_MOUNT_PATH          "/"
#define FS_PCM_DIR             "/pcm"
#define FS_TYPE_LFS            "lfs"

static rt_bool_t g_fs_ready = RT_FALSE;

const char *fs_app_mount_path(void)
{
    return FS_MOUNT_PATH;
}

const char *fs_app_pcm_dir(void)
{
    return FS_PCM_DIR;
}

rt_bool_t fs_app_is_ready(void)
{
    return g_fs_ready;
}

static rt_err_t fs_app_ensure_pcm_dir(void)
{
    struct stat st;

    if (stat(FS_PCM_DIR, &st) == 0)
    {
        return RT_EOK;
    }

    if (mkdir(FS_PCM_DIR, 0x777) == 0)
    {
        return RT_EOK;
    }

    if (stat(FS_PCM_DIR, &st) == 0)
    {
        return RT_EOK;
    }

    LOG_W("mkdir %s failed, errno=%d", FS_PCM_DIR, rt_get_errno());
    return -RT_ERROR;
}

static rt_err_t fs_app_do_mount(void)
{
    int ret;

    ret = dfs_mount(FS_FAL_PART_NAME, FS_MOUNT_PATH, FS_TYPE_LFS, 0, 0);
    if (ret == 0)
    {
        return RT_EOK;
    }

    LOG_W("mount / failed (errno=%d), try mkfs then remount", rt_get_errno());

    ret = dfs_mkfs(FS_TYPE_LFS, FS_FAL_PART_NAME);
    if (ret != 0)
    {
        LOG_E("dfs_mkfs(lfs, %s) failed: %d errno=%d",
              FS_FAL_PART_NAME, ret, rt_get_errno());
        return -RT_ERROR;
    }
    LOG_I("littlefs mkfs ok on partition \"%s\"", FS_FAL_PART_NAME);

    ret = dfs_mount(FS_FAL_PART_NAME, FS_MOUNT_PATH, FS_TYPE_LFS, 0, 0);
    if (ret != 0)
    {
        LOG_E("dfs_mount after mkfs failed: %d errno=%d", ret, rt_get_errno());
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t fs_app_init(void)
{
    struct rt_device *mtd;

    if (g_fs_ready)
    {
        return RT_EOK;
    }

#if !defined(RT_USING_DFS) || !defined(PKG_USING_LITTLEFS) || !defined(RT_USING_FAL) || !defined(RT_USING_MTD_NOR)
    LOG_E("fs_app needs DFS + littlefs + FAL + MTD_NOR");
    return -RT_ERROR;
#else

    if (fal_init() <= 0)
    {
        LOG_E("fal_init failed");
        return -RT_ERROR;
    }

    /* 设备名 = 分区名 "filesystem" */
    mtd = fal_mtd_nor_device_create(FS_FAL_PART_NAME);
    if (mtd == RT_NULL)
    {
        if (rt_device_find(FS_FAL_PART_NAME) == RT_NULL)
        {
            LOG_E("fal_mtd_nor_device_create(%s) failed", FS_FAL_PART_NAME);
            return -RT_ERROR;
        }
        LOG_I("MTD device \"%s\" already exists, reuse", FS_FAL_PART_NAME);
    }
    else
    {
        LOG_I("MTD device \"%s\" created", FS_FAL_PART_NAME);
    }

    if (fs_app_do_mount() != RT_EOK)
    {
        return -RT_ERROR;
    }

    LOG_I("littlefs mounted at / (partition \"%s\")", FS_FAL_PART_NAME);

    if (fs_app_ensure_pcm_dir() != RT_EOK)
    {
        LOG_W("pcm dir not ready, record-to-file may fail");
    }
    else
    {
        LOG_I("pcm dir ready: %s", FS_PCM_DIR);
    }

    g_fs_ready = RT_TRUE;
    return RT_EOK;
#endif
}

#ifdef RT_USING_FINSH
#include <finsh.h>

static void fs_app_info(int argc, char **argv)
{
    DIR *dir;
    struct dirent *ent;
    struct stat st;

    (void)argc;
    (void)argv;

    if (fs_app_init() != RT_EOK)
    {
        rt_kprintf("fs_app not ready\n");
        return;
    }

    rt_kprintf("mount : %s (lfs on FAL \"%s\")\n", FS_MOUNT_PATH, FS_FAL_PART_NAME);
    rt_kprintf("pcm   : %s\n", FS_PCM_DIR);

    dir = opendir(FS_PCM_DIR);
    if (dir == RT_NULL)
    {
        rt_kprintf("opendir %s failed, errno=%d\n", FS_PCM_DIR, rt_get_errno());
        return;
    }

    rt_kprintf("---- %s ----\n", FS_PCM_DIR);
    while ((ent = readdir(dir)) != RT_NULL)
    {
        char path[64];

        if ((rt_strcmp(ent->d_name, ".") == 0) || (rt_strcmp(ent->d_name, "..") == 0))
        {
            continue;
        }

        rt_snprintf(path, sizeof(path), "%s/%s", FS_PCM_DIR, ent->d_name);
        if (stat(path, &st) == 0)
        {
            rt_kprintf("  %s  %lu bytes\n", ent->d_name, (unsigned long)st.st_size);
        }
        else
        {
            rt_kprintf("  %s\n", ent->d_name);
        }
    }
    closedir(dir);
}
MSH_CMD_EXPORT(fs_app_info, show littlefs mount and /pcm files);
#endif
