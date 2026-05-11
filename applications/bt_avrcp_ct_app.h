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

rt_err_t bt_avrcp_ct_service_init(void);
rt_err_t bt_avrcp_ct_connect(const bd_addr_t remote_addr);
rt_err_t bt_avrcp_ct_play(void);
rt_err_t bt_avrcp_ct_pause(void);
rt_err_t bt_avrcp_ct_next(void);
rt_err_t bt_avrcp_ct_previous(void);
rt_err_t bt_avrcp_ct_volume_up(void);
rt_err_t bt_avrcp_ct_volume_down(void);

#endif /* APPLICATIONS_BT_AVRCP_CT_APP_H_ */
