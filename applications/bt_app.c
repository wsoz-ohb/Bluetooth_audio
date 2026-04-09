/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-02     wsoz       the first version
 */
#include "bt_app.h"
#include "btstack_port.h"
#include "bt_a2dp_sink_app.h"

#define DBG_TAG "bt_app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static int bt_profiles_init(void)
{
    // bt_app 只负责统一编排各个 profile 的初始化顺序。
    // 具体协议的服务注册和事件处理已经拆到独立文件里，后面继续加 HID/SPP 时也按这个套路扩展。
    return bt_a2dp_sink_service_init();
}

rt_err_t bt__init(void)
{
    int err;

    // 第一步先把 BTstack 基础栈和本地设备配置准备好。
    // 这一阶段会完成 run loop、HCI 传输层、L2CAP/SDP 等基础协议初始化。
    err = btstack_port_init(NULL);
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_init failed: %d", err);
        return RT_ERROR;
    }

    // 第二步注册当前需要的 profile 服务。
    // 当前先接了 A2DP Sink，后面再按同样方式把其他 profile 拆出去。
    err = bt_profiles_init();
    if (err != RT_EOK)
    {
        LOG_E("bt_profiles_init failed: %d", err);
        return RT_ERROR;
    }

    // 第三步启动 BTstack 专用线程，并在该线程的 run loop 上下文里执行控制器上电。
    // 真正 HCI_POWER_ON 发生在这里之后。
    err = btstack_port_start_thread();
    if (err != RT_EOK)
    {
        LOG_E("btstack_port_start_thread failed: %d", err);
        return RT_ERROR;
    }

    LOG_I("BT-STACK init ok");
    return RT_EOK;
}
