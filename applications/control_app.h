/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_CONTROL_APP_H_
#define APPLICATIONS_CONTROL_APP_H_

#include <rtthread.h>

#if defined(__cplusplus)
extern "C" {
#endif

rt_err_t control_app_init(void);

/* PTT 长按采集是否正在进行（供 GUI 状态栏等只读查询）。 */
rt_bool_t control_app_is_capturing(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_CONTROL_APP_H_ */
