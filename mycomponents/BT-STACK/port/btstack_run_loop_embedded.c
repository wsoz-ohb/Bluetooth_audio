#define BTSTACK_FILE__ "btstack_run_loop_embedded.c"

#include "btstack_run_loop_embedded.h"

#include "btstack_debug.h"

#include <rtthread.h>

static struct rt_semaphore btstack_run_loop_sem;
static rt_bool_t btstack_run_loop_sem_inited = RT_FALSE;
static volatile rt_bool_t btstack_run_loop_exit_requested = RT_FALSE;

static void btstack_run_loop_embedded_notify(void){
    // Wake the run loop whenever a timer, callback, or data source state changes.
    if (btstack_run_loop_sem_inited){
        (void) rt_sem_release(&btstack_run_loop_sem);
    }
}

static void btstack_run_loop_embedded_init(void){
    // Initialize the common BTstack lists first, then the RT-Thread wakeup primitive.
    btstack_run_loop_base_init();

    if (!btstack_run_loop_sem_inited){
        rt_sem_init(&btstack_run_loop_sem, "btloop", 0, RT_IPC_FLAG_FIFO);
        btstack_run_loop_sem_inited = RT_TRUE;
    }

    btstack_run_loop_exit_requested = RT_FALSE;
}

static void btstack_run_loop_embedded_add_data_source(btstack_data_source_t * data_source){
    btstack_run_loop_base_add_data_source(data_source);
    btstack_run_loop_embedded_notify();
}

static bool btstack_run_loop_embedded_remove_data_source(btstack_data_source_t * data_source){
    bool removed = btstack_run_loop_base_remove_data_source(data_source);
    if (removed){
        btstack_run_loop_embedded_notify();
    }
    return removed;
}

static void btstack_run_loop_embedded_enable_data_source_callbacks(btstack_data_source_t * data_source, uint16_t callbacks){
    btstack_run_loop_base_enable_data_source_callbacks(data_source, callbacks);
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_disable_data_source_callbacks(btstack_data_source_t * data_source, uint16_t callbacks){
    btstack_run_loop_base_disable_data_source_callbacks(data_source, callbacks);
}

static uint32_t btstack_run_loop_embedded_get_time_ms(void){
    return (uint32_t) rt_tick_get_millisecond();
}

static void btstack_run_loop_embedded_set_timer(btstack_timer_source_t * timer, uint32_t timeout_in_ms){
    timer->timeout = btstack_run_loop_embedded_get_time_ms() + timeout_in_ms;
}

static void btstack_run_loop_embedded_add_timer(btstack_timer_source_t * timer){
    btstack_run_loop_base_add_timer(timer);
    btstack_run_loop_embedded_notify();
}

static bool btstack_run_loop_embedded_remove_timer(btstack_timer_source_t * timer){
    bool removed = btstack_run_loop_base_remove_timer(timer);
    if (removed){
        btstack_run_loop_embedded_notify();
    }
    return removed;
}

static void btstack_run_loop_embedded_poll_data_sources_from_irq(void){
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_execute_on_main_thread(btstack_context_callback_registration_t * callback_registration){
    btstack_run_loop_base_add_callback(callback_registration);
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_trigger_exit(void){
    btstack_run_loop_exit_requested = RT_TRUE;
    btstack_run_loop_embedded_notify();
}

static void btstack_run_loop_embedded_execute(void){
    while (!btstack_run_loop_exit_requested){
        int32_t timeout_ms;
        rt_tick_t timeout_tick;

        // Keep the same order as the reference embedded run loop: callbacks, I/O, then timers.
        btstack_run_loop_base_execute_callbacks();
        btstack_run_loop_base_poll_data_sources();
        btstack_run_loop_base_process_timers(btstack_run_loop_embedded_get_time_ms());

        if (btstack_run_loop_exit_requested){
            break;
        }

        timeout_ms = btstack_run_loop_base_get_time_until_timeout(btstack_run_loop_embedded_get_time_ms());
        if (timeout_ms == 0){
            // A timer is already due, so spin once more without sleeping.
            continue;
        }

        if (timeout_ms < 0){
            // No timer is pending; sleep until some other part of the stack wakes us.
            (void) rt_sem_take(&btstack_run_loop_sem, RT_WAITING_FOREVER);
            continue;
        }

        // Sleep until the next timer expires or an external wakeup arrives first.
        timeout_tick = rt_tick_from_millisecond(timeout_ms);
        if (timeout_tick == 0){
            timeout_tick = 1;
        }
        (void) rt_sem_take(&btstack_run_loop_sem, timeout_tick);
    }
}

const btstack_run_loop_t btstack_run_loop_embedded = {
    &btstack_run_loop_embedded_init,
    &btstack_run_loop_embedded_add_data_source,
    &btstack_run_loop_embedded_remove_data_source,
    &btstack_run_loop_embedded_enable_data_source_callbacks,
    &btstack_run_loop_embedded_disable_data_source_callbacks,
    &btstack_run_loop_embedded_set_timer,
    &btstack_run_loop_embedded_add_timer,
    &btstack_run_loop_embedded_remove_timer,
    &btstack_run_loop_embedded_execute,
    &btstack_run_loop_base_dump_timer,
    &btstack_run_loop_embedded_get_time_ms,
    &btstack_run_loop_embedded_poll_data_sources_from_irq,
    &btstack_run_loop_embedded_execute_on_main_thread,
    &btstack_run_loop_embedded_trigger_exit,
};

const btstack_run_loop_t * btstack_run_loop_embedded_get_instance(void){
    return &btstack_run_loop_embedded;
}

