#ifndef BTSTACK_PORT_H
#define BTSTACK_PORT_H

#include <rtthread.h>

#include "btstack_chipset.h"

#if defined __cplusplus
extern "C" {
#endif

int btstack_port_init(const btstack_chipset_t * chipset_driver);
int btstack_port_start_thread(void);
rt_thread_t btstack_port_get_thread(void);

#if defined __cplusplus
}
#endif

#endif /* BTSTACK_PORT_H */
