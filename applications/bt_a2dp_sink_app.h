/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_A2DP_SINK_APP_H_
#define APPLICATIONS_BT_A2DP_SINK_APP_H_

#include <rtthread.h>

// A2DP Sink 协议层入口：负责注册 SEP、SDP 和协议事件回调。
rt_err_t bt_a2dp_sink_service_init(void);

#endif /* APPLICATIONS_BT_A2DP_SINK_APP_H_ */
