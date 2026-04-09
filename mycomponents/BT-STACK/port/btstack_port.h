#ifndef BTSTACK_PORT_H
#define BTSTACK_PORT_H

#include <rtthread.h>

#include "btstack_chipset.h"

#if defined __cplusplus
extern "C" {
#endif

// 完成 BTstack 基础栈、传输层和本地设备参数的初始化。
int btstack_port_init(const btstack_chipset_t * chipset_driver);
// 启动 BTstack 线程，并在 run loop 上下文里安排 HCI 上电。
int btstack_port_start_thread(void);
rt_thread_t btstack_port_get_thread(void);

#if defined __cplusplus
}
#endif

#endif /* BTSTACK_PORT_H */


