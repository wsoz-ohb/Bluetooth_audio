#ifndef HCI_DUMP_UART4_RTTHREAD_H
#define HCI_DUMP_UART4_RTTHREAD_H

#include <rtthread.h>

#include "hci_dump.h"

#if defined __cplusplus
extern "C" {
#endif

rt_err_t hci_dump_uart4_rtthread_open(hci_dump_format_t format);
const hci_dump_t * hci_dump_uart4_rtthread_get_instance(void);

#if defined __cplusplus
}
#endif

#endif /* HCI_DUMP_UART4_RTTHREAD_H */
