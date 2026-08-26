#define BTSTACK_FILE__ "btstack_port.c"

#include "btstack_port.h"

#include "bt_host.h"
#include "btstack_chipset_esp32.h"
#include "btstack_debug.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"
#include "btstack_tlv.h"
#include "btstack_tlv_none.h"
#include "btstack_tlv_littlefs.h"
#include "btstack_uart_block.h"
#include "btstack_util.h"
#include "hci.h"

#if BT_CFG_ENABLE_CLASSIC
#include "classic/btstack_link_key_db_memory.h"
#include "classic/btstack_link_key_db_tlv.h"
#endif

#define BTSTACK_PORT_THREAD_NAME       "btstack"
#define BTSTACK_PORT_THREAD_STACK_SIZE 4096
#define BTSTACK_PORT_THREAD_PRIORITY   9
#define BTSTACK_PORT_THREAD_TICK       10

static rt_bool_t btstack_port_inited = RT_FALSE;
static rt_thread_t btstack_port_thread = RT_NULL;
static btstack_context_callback_registration_t btstack_port_power_on_registration;

static void btstack_port_thread_entry(void * parameter){
    UNUSED(parameter);
    // BTstack 的定时器、回调和数据源轮询都依赖这个 run loop 线程驱动。
    btstack_run_loop_execute();
}

//启动协议栈
static void btstack_port_power_on(void * context){
    int err;
    UNUSED(context);

    err = bt_host_start();  //依赖
    if (err != 0){
        log_error("btstack_port: bt_host_start failed, err %d", err);
    }
}

int btstack_port_init(const btstack_chipset_t * chipset_driver){
    const btstack_chipset_t * effective_chipset = chipset_driver;
    const btstack_tlv_t * persistent_tlv = NULL;

    if (btstack_port_inited){
        return RT_EOK;
    }

    if (effective_chipset == NULL){
        // 当前板级默认外挂的是 ESP32 控制器，并且走 H4 UART，因此这里使用 ESP32 的 chipset 适配。
        effective_chipset = btstack_chipset_esp32_instance();
    }

    // 先把 OS 相关的 run loop/TLV 准备好，再把 UART 和 chipset 驱动交给 Host 层。
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());
    if (btstack_tlv_littlefs_init() == RT_EOK){
        persistent_tlv = btstack_tlv_littlefs_instance();
        btstack_tlv_set_instance(persistent_tlv, NULL);
        log_info("BTstack Link Key persistence enabled");
    } else {
        // 文件系统不可用时保留蓝牙功能，但配对信息只存于本次运行的 RAM。
        btstack_tlv_set_instance(btstack_tlv_none_init_instance(), NULL);
        log_error("BTstack Link Key persistence unavailable, using RAM only");
    }

    if (bt_host_stack_init(btstack_uart_block_embedded_instance(), effective_chipset) != 0){
        return -RT_ERROR;
    }

#if BT_CFG_ENABLE_CLASSIC
    if (persistent_tlv != NULL){
        hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(persistent_tlv, NULL));
    } else {
        hci_set_link_key_db(btstack_link_key_db_memory_instance());
    }
#endif

    // 这里只做协议栈和本地设备参数准备，还没有真正让控制器上电。
    bt_host_protocol_init();
    bt_host_apply_device_config();
#if BT_CFG_ENABLE_BLE
    bt_host_ble_setup_advertising();
#endif

    btstack_port_inited = RT_TRUE;
    return RT_EOK;
}

int btstack_port_start_thread(void){
    rt_err_t err;

    if (!btstack_port_inited){
        return -RT_ERROR;
    }

    if (btstack_port_thread != RT_NULL){
        return RT_EOK;
    }

    btstack_port_thread = rt_thread_create(BTSTACK_PORT_THREAD_NAME,
                                           btstack_port_thread_entry,
                                           RT_NULL,
                                           BTSTACK_PORT_THREAD_STACK_SIZE,
                                           BTSTACK_PORT_THREAD_PRIORITY,
                                           BTSTACK_PORT_THREAD_TICK);
    if (btstack_port_thread == RT_NULL){
        return -RT_ERROR;
    }

    err = rt_thread_startup(btstack_port_thread);
    if (err != RT_EOK){
        btstack_port_thread = RT_NULL;
        return err;
    }

    // 把上电动作投递到 run loop 线程里执行，避免控制器启动发生在错误的线程上下文。
    btstack_port_power_on_registration.item = NULL;
    btstack_port_power_on_registration.callback = btstack_port_power_on;
    btstack_port_power_on_registration.context = NULL;
    btstack_run_loop_execute_on_main_thread(&btstack_port_power_on_registration);

    return RT_EOK;
}

rt_thread_t btstack_port_get_thread(void){
    return btstack_port_thread;
}


