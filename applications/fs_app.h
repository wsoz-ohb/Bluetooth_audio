/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_FS_APP_H_
#define APPLICATIONS_FS_APP_H_

#include <rtthread.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* 挂载 FAL "filesystem" 分区上的 littlefs 到根 "/" */
rt_err_t fs_app_init(void);
rt_bool_t fs_app_is_ready(void);

/* 挂载点与录音目录（只读常量；当前 mount="/"，pcm="/pcm"） */
const char *fs_app_mount_path(void);
const char *fs_app_pcm_dir(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_FS_APP_H_ */
