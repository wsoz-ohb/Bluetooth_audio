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

// 初始化 A2DP 音频层：内部会准备 SBC 解码器和 PCM stream，但不会直接操作具体播放后端。
rt_err_t bt_a2dp_audio_init(void);

// 重置解码器状态。通常在重新协商 codec 或流释放时调用，避免旧流状态残留。
void bt_a2dp_audio_reset(void);

// 处理 A2DP Sink 收到的媒体包。
// 这个接口会负责：RTP/SBC 头解析 -> SBC 解码 -> PCM 写入 bt_pcm_stream。
void bt_a2dp_audio_process_media_packet(uint8_t local_seid, const uint8_t * packet, uint16_t size);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_A2DP_AUDIO_H_ */
