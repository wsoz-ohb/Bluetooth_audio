/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_AVRCP_CT_APP_H_
#define APPLICATIONS_BT_AVRCP_CT_APP_H_
#include <rtthread.h>
#include <stdint.h>
#include "bluetooth.h"

typedef enum
{
    BT_AVRCP_CT_LINK_STATE_DISCONNECTED = 0,
    BT_AVRCP_CT_LINK_STATE_CONNECTED,
} bt_avrcp_ct_link_state_t;

typedef enum
{
    BT_AVRCP_CT_PLAYBACK_STATE_UNKNOWN = 0,
    BT_AVRCP_CT_PLAYBACK_STATE_STOPPED,
    BT_AVRCP_CT_PLAYBACK_STATE_PLAYING,
    BT_AVRCP_CT_PLAYBACK_STATE_PAUSED,
} bt_avrcp_ct_playback_state_t;

typedef enum
{
    BT_AVRCP_CT_OP_STATE_IDLE = 0,
    BT_AVRCP_CT_OP_STATE_WAIT_INITIAL_STATUS,
    BT_AVRCP_CT_OP_STATE_WAIT_PLAY_ACK,
    BT_AVRCP_CT_OP_STATE_WAIT_PAUSE_ACK,
} bt_avrcp_ct_op_state_t;

rt_err_t bt_avrcp_ct_service_init(void);
rt_err_t bt_avrcp_ct_connect(const bd_addr_t remote_addr);
bt_avrcp_ct_link_state_t bt_avrcp_ct_get_link_state(void);
bt_avrcp_ct_playback_state_t bt_avrcp_ct_get_playback_state(void);
bt_avrcp_ct_op_state_t bt_avrcp_ct_get_op_state(void);
rt_bool_t bt_avrcp_ct_is_connected(void);
const char * bt_avrcp_ct_link_state_name(bt_avrcp_ct_link_state_t state);
const char * bt_avrcp_ct_playback_state_name(bt_avrcp_ct_playback_state_t state);
const char * bt_avrcp_ct_op_state_name(bt_avrcp_ct_op_state_t state);
rt_err_t bt_avrcp_ct_play(void);
rt_err_t bt_avrcp_ct_pause(void);
rt_err_t bt_avrcp_ct_next(void);
rt_err_t bt_avrcp_ct_previous(void);
rt_err_t bt_avrcp_ct_volume_up(void);
rt_err_t bt_avrcp_ct_volume_down(void);

/* 对端是否已启用绝对音量（由 SetAbsoluteVolume / VOLUME_CHANGED 推断）。 */
rt_bool_t bt_avrcp_ct_is_absolute_volume_active(void);

/* Now Playing 元数据只读接口（供 GUI 刷新）。
 * title/artist 可能为空串；断开或切歌瞬间会被清空。 */
const char *bt_avrcp_ct_get_title(void);
const char *bt_avrcp_ct_get_artist(void);
uint32_t bt_avrcp_ct_get_song_length_ms(void);
uint32_t bt_avrcp_ct_get_song_position_ms(void);

#endif /* APPLICATIONS_BT_AVRCP_CT_APP_H_ */
