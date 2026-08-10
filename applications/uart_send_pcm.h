/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APPLICATIONS_UART_SEND_PCM_H_
#define APPLICATIONS_UART_SEND_PCM_H_

#include <rtthread.h>

#if defined(__cplusplus)
extern "C" {
#endif

rt_err_t uart_send_pcm_start(void);
void uart_send_pcm_stop(void);
void uart_send_pcm_start_reply_rx(void);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_UART_SEND_PCM_H_ */
