/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_BT_SPP_APP_H_
#define APPLICATIONS_BT_SPP_APP_H_

#include <rtthread.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* 注册 Classic SPP 服务，并准备 1 KB 接收环形缓存。 */
rt_err_t bt_spp_service_init(void);

/* 从 SPP 接收缓存读取数据；该接口不应在中断上下文调用。 */
rt_size_t bt_spp_rx_read(rt_uint8_t *buffer, rt_size_t size);

/* 查询当前接收缓存中的字节数。 */
rt_size_t bt_spp_rx_data_len(void);

/* 查询当前连接会话因缓存满而丢弃的字节数。 */
rt_size_t bt_spp_rx_dropped_bytes(void);

/* 查询 SPP RFCOMM 通道是否已经建立。 */
rt_bool_t bt_spp_is_connected(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BT_SPP_APP_H_ */
