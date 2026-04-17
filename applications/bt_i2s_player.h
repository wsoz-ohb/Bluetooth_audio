/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-10     26410       the first version
 */
#ifndef APPLICATIONS_BT_I2S_PLAYER_H_
#define APPLICATIONS_BT_I2S_PLAYER_H_

#include <rtthread.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* I2S 输出后端：初始化后会把自己注册为 bt_pcm_stream 的当前消费者。 */
rt_err_t bt_i2s_player_init(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_I2S_PLAYER_H_ */

