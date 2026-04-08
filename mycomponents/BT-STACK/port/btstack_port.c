#define BTSTACK_FILE__ "btstack_port.c"

#include "btstack_port.h"

#include "bt_host.h"
#include "btstack_chipset_esp32.h"
#include "btstack_debug.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"
#include "btstack_tlv.h"
#include "btstack_tlv_none.h"
#include "btstack_uart_block.h"
#include "btstack_util.h"
#include "hci.h"

#if BT_CFG_ENABLE_CLASSIC
#include "classic/btstack_link_key_db_memory.h"
#endif

#define BTSTACK_PORT_THREAD_NAME       "btstack"
#define BTSTACK_PORT_THREAD_STACK_SIZE 4096
#define BTSTACK_PORT_THREAD_PRIORITY   11
#define BTSTACK_PORT_THREAD_TICK       10

static rt_bool_t btstack_port_inited = RT_FALSE;
static rt_thread_t btstack_port_thread = RT_NULL;
static btstack_context_callback_registration_t btstack_port_power_on_registration;

static void btstack_port_thread_entry(void * parameter){
    UNUSED(parameter);
    // BTstack expects all timers/callbacks to be driven from its run loop thread.
    btstack_run_loop_execute();
}

static void btstack_port_power_on(void * context){
    int err;
    UNUSED(context);

    err = bt_host_start();
    if (err != 0){
        log_error("btstack_port: bt_host_start failed, err %d", err);
    }
}

int btstack_port_init(const btstack_chipset_t * chipset_driver){
    const btstack_chipset_t * effective_chipset = chipset_driver;

    if (btstack_port_inited){
        return RT_EOK;
    }

    if (effective_chipset == NULL){
        // Current board uses an ESP32 controller on H4, so default to that chipset helper.
        effective_chipset = btstack_chipset_esp32_instance();
    }

    // Set up the OS-facing pieces first, then hand BTstack the UART/chipset pair.
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());
    btstack_tlv_set_instance(btstack_tlv_none_init_instance(), NULL);

    if (bt_host_stack_init(btstack_uart_block_embedded_instance(), effective_chipset) != 0){
        return -RT_ERROR;
    }

#if BT_CFG_ENABLE_CLASSIC
    // Link keys are kept in RAM for now; switch this later if persistent storage is needed.
    hci_set_link_key_db(btstack_link_key_db_memory_instance());
#endif

    // This stage only prepares the stack and local device settings. No HCI power-on yet.
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

    // Power-on is queued onto the BTstack run loop so controller startup happens in the same context.
    btstack_port_power_on_registration.item = NULL;
    btstack_port_power_on_registration.callback = btstack_port_power_on;
    btstack_port_power_on_registration.context = NULL;
    btstack_run_loop_execute_on_main_thread(&btstack_port_power_on_registration);

    return RT_EOK;
}

rt_thread_t btstack_port_get_thread(void){
    return btstack_port_thread;
}

