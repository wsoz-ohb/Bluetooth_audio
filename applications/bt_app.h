/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-02     wsoz       the first version
 */
#ifndef APPLICATIONS_BT_APP_H_
#define APPLICATIONS_BT_APP_H_

#include <rtthread.h>

// 蓝牙应用入口：负责拉起 BT-STACK 端口层和底层控制器启动流程.
rt_err_t bt__init(void);

#endif /* APPLICATIONS_BT_APP_H_ */

