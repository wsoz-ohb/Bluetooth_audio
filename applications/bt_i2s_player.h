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

/* A2DP PCM -> I2S -> WM8978 播放层。 */
rt_err_t bt_i2s_player_init(void);

/* 按协商结果准备播放参数。channels 当前支持 1 或 2。 */
rt_err_t bt_i2s_player_prepare(rt_uint32_t sample_rate, rt_uint8_t channels);

/* 兼容按采样率单独配置的调用方式。 */
rt_err_t bt_i2s_player_set_sample_rate(rt_uint32_t sample_rate);

/* 启动/停止 I2S DMA 和 WM8978 播放路径。 */
rt_err_t bt_i2s_player_start(void);
rt_err_t bt_i2s_player_stop(void);

/* 清空软件 PCM 缓冲。 */
void bt_i2s_player_reset_buffer(void);
void bt_i2s_player_flush(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_I2S_PLAYER_H_ */
