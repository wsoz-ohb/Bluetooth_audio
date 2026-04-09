/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_A2DP_AUDIO_H_
#define APPLICATIONS_BT_A2DP_AUDIO_H_

#include <rtthread.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*bt_a2dp_audio_pcm_callback_t)(const int16_t * pcm,
                                             uint16_t num_samples,
                                             uint8_t num_channels,
                                             uint32_t sample_rate,
                                             void * context);

// 初始化 A2DP 音频层：内部会准备 SBC 解码器，但不会直接操作 I2S。
rt_err_t bt_a2dp_audio_init(void);

// 重置解码器状态。通常在重新协商 codec 或流释放时调用，避免旧流状态残留。
void bt_a2dp_audio_reset(void);

// 注册 PCM 输出回调。
// 你后面接 I2S 时，只需要在自己的播放模块里注册这个回调即可。
void bt_a2dp_audio_register_pcm_callback(bt_a2dp_audio_pcm_callback_t callback, void * context);

// 处理 A2DP Sink 收到的媒体包。
// 这个接口会负责：RTP/SBC 头解析 -> SBC 解码 -> 通过 PCM 回调把 PCM 往外抛。
void bt_a2dp_audio_process_media_packet(uint8_t local_seid, const uint8_t * packet, uint16_t size);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_A2DP_AUDIO_H_ */
