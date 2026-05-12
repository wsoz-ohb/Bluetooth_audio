/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_A2DP_SINK_APP_H_
#define APPLICATIONS_BT_A2DP_SINK_APP_H_

#include <rtthread.h>

typedef enum
{
    BT_A2DP_SINK_SUSPEND_NOT_NEEDED = 0,
    BT_A2DP_SINK_SUSPEND_PENDING,
    BT_A2DP_SINK_SUSPEND_FAILED,
} bt_a2dp_sink_suspend_result_t;

// A2DP Sink 协议层入口：负责注册 SEP、SDP 和协议事件回调。
rt_err_t bt_a2dp_sink_service_init(void);
rt_err_t bt_a2dp_sink_restore_local_playback(void);
rt_err_t bt_a2dp_sink_set_local_media_enabled(rt_bool_t enabled);
bt_a2dp_sink_suspend_result_t bt_a2dp_sink_request_media_suspend(void);
rt_err_t bt_a2dp_sink_resume_media_stream(void);
rt_bool_t bt_a2dp_sink_is_stream_active(void);
rt_bool_t bt_a2dp_sink_is_suspend_in_progress(void);

#endif /* APPLICATIONS_BT_A2DP_SINK_APP_H_ */
