#define BTSTACK_FILE__ "btstack_uart_block_embedded.c"

#include "btstack_uart_block.h"

#include "btstack_debug.h"
#include "btstack_run_loop.h"
#include "btstack_util.h"

#include <string.h>

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>
#include "drivers/serial.h"

#define BTSTACK_UART_RTTHREAD_DEFAULT_DEVICE_NAME     "uart2"
#define BTSTACK_UART_RTTHREAD_RX_BUFFER_SIZE          (4 * 1024)
#define BTSTACK_UART_RTTHREAD_RX_TMP_BUFFER_SIZE      256
#define BTSTACK_UART_RTTHREAD_THREAD_STACK_SIZE       2048
#define BTSTACK_UART_RTTHREAD_THREAD_PRIORITY         12
#define BTSTACK_UART_RTTHREAD_THREAD_TICK             10

typedef struct {
    btstack_uart_config_t config;
    rt_device_t device;
    struct rt_ringbuffer rx_ringbuffer;
    rt_uint8_t rx_storage[BTSTACK_UART_RTTHREAD_RX_BUFFER_SIZE];
    rt_uint8_t rx_tmp_storage[BTSTACK_UART_RTTHREAD_RX_TMP_BUFFER_SIZE];
    struct rt_semaphore rx_sem;
    struct rt_mutex lock;
    rt_bool_t rx_sem_inited;
    rt_bool_t lock_inited;
    rt_thread_t rx_thread;
    uint8_t * rx_buffer;
    uint16_t rx_len;
    rt_bool_t rx_active;
    rt_bool_t rx_callback_pending;
    rt_bool_t tx_callback_pending;
    void (*block_received_handler)(void);
    void (*block_sent_handler)(void);
    void (*wakeup_handler)(void);
    btstack_context_callback_registration_t rx_done_registration;
    btstack_context_callback_registration_t tx_done_registration;
} btstack_uart_rtthread_state_t;

static btstack_uart_rtthread_state_t btstack_uart_rtthread_state;

static const char * btstack_uart_rtthread_device_name(void){
    if ((btstack_uart_rtthread_state.config.device_name != NULL) &&
        (btstack_uart_rtthread_state.config.device_name[0] != '\0')){
        return btstack_uart_rtthread_state.config.device_name;
    }
    return BTSTACK_UART_RTTHREAD_DEFAULT_DEVICE_NAME;
}

static void btstack_uart_rtthread_rx_done(void * context){
    void (*handler)(void);
    UNUSED(context);

    handler = NULL;
    if (btstack_uart_rtthread_state.lock_inited){
        (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
        btstack_uart_rtthread_state.rx_callback_pending = RT_FALSE;
        handler = btstack_uart_rtthread_state.block_received_handler;
        rt_mutex_release(&btstack_uart_rtthread_state.lock);
    }

    if (handler != NULL){
        handler();
    }
}

static void btstack_uart_rtthread_tx_done(void * context){
    void (*handler)(void);
    UNUSED(context);

    handler = NULL;
    if (btstack_uart_rtthread_state.lock_inited){
        (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
        btstack_uart_rtthread_state.tx_callback_pending = RT_FALSE;
        handler = btstack_uart_rtthread_state.block_sent_handler;
        rt_mutex_release(&btstack_uart_rtthread_state.lock);
    }

    if (handler != NULL){
        handler();
    }
}

static void btstack_uart_rtthread_schedule_rx_done_locked(void){
    if (btstack_uart_rtthread_state.rx_callback_pending){
        return;
    }
    if (btstack_uart_rtthread_state.block_received_handler == NULL){
        return;
    }

    btstack_uart_rtthread_state.rx_callback_pending = RT_TRUE;
    btstack_uart_rtthread_state.rx_done_registration.item = NULL;
    btstack_uart_rtthread_state.rx_done_registration.callback = btstack_uart_rtthread_rx_done;
    btstack_uart_rtthread_state.rx_done_registration.context = NULL;
    btstack_run_loop_execute_on_main_thread(&btstack_uart_rtthread_state.rx_done_registration);
}

static void btstack_uart_rtthread_schedule_tx_done_locked(void){
    if (btstack_uart_rtthread_state.tx_callback_pending){
        return;
    }
    if (btstack_uart_rtthread_state.block_sent_handler == NULL){
        return;
    }

    btstack_uart_rtthread_state.tx_callback_pending = RT_TRUE;
    btstack_uart_rtthread_state.tx_done_registration.item = NULL;
    btstack_uart_rtthread_state.tx_done_registration.callback = btstack_uart_rtthread_tx_done;
    btstack_uart_rtthread_state.tx_done_registration.context = NULL;
    btstack_run_loop_execute_on_main_thread(&btstack_uart_rtthread_state.tx_done_registration);
}

static void btstack_uart_rtthread_try_deliver_rx_locked(void){
    if (!btstack_uart_rtthread_state.rx_active){
        return;
    }
    if (btstack_uart_rtthread_state.rx_callback_pending){
        return;
    }
    if (btstack_uart_rtthread_state.rx_buffer == NULL){
        return;
    }
    if (rt_ringbuffer_data_len(&btstack_uart_rtthread_state.rx_ringbuffer) < btstack_uart_rtthread_state.rx_len){
        return;
    }

    (void) rt_ringbuffer_get(&btstack_uart_rtthread_state.rx_ringbuffer,
                             btstack_uart_rtthread_state.rx_buffer,
                             btstack_uart_rtthread_state.rx_len);
    btstack_uart_rtthread_state.rx_active = RT_FALSE;
    btstack_uart_rtthread_schedule_rx_done_locked();
}

static int btstack_uart_rtthread_apply_config(void){
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    if (btstack_uart_rtthread_state.device == RT_NULL){
        return -1;
    }

    cfg.baud_rate = btstack_uart_rtthread_state.config.baudrate;
    cfg.flowcontrol = (btstack_uart_rtthread_state.config.flowcontrol == BTSTACK_UART_FLOWCONTROL_ON) ?
        RT_SERIAL_FLOWCONTROL_CTSRTS : RT_SERIAL_FLOWCONTROL_NONE;

    switch (btstack_uart_rtthread_state.config.parity){
        case BTSTACK_UART_PARITY_EVEN:
            cfg.parity = PARITY_EVEN;
            break;
        case BTSTACK_UART_PARITY_ODD:
            cfg.parity = PARITY_ODD;
            break;
        case BTSTACK_UART_PARITY_OFF:
        default:
            cfg.parity = PARITY_NONE;
            break;
    }

    return (int) rt_device_control(btstack_uart_rtthread_state.device, RT_DEVICE_CTRL_CONFIG, &cfg);
}

static void btstack_uart_rtthread_configure_uart2_flowcontrol_pins(void){
#if defined(GPIO_PIN_0) && defined(GPIO_PIN_1) && defined(GPIO_AF7_USART2)
    if (strcmp(btstack_uart_rtthread_device_name(), "uart2") != 0){
        return;
    }
    if (btstack_uart_rtthread_state.config.flowcontrol != BTSTACK_UART_FLOWCONTROL_ON){
        return;
    }

    {
        GPIO_InitTypeDef gpio = {0};

        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin       = GPIO_PIN_0 | GPIO_PIN_1;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
#endif
}

static rt_err_t btstack_uart_rtthread_rx_indicate(rt_device_t dev, rt_size_t size){
    UNUSED(dev);

    if ((size > 0) && btstack_uart_rtthread_state.rx_sem_inited){
        (void) rt_sem_release(&btstack_uart_rtthread_state.rx_sem);
    }

    return RT_EOK;
}

static void btstack_uart_rtthread_rx_thread(void * parameter){
    UNUSED(parameter);

    while (1){
        (void) rt_sem_take(&btstack_uart_rtthread_state.rx_sem, RT_WAITING_FOREVER);

        if (btstack_uart_rtthread_state.device == RT_NULL){
            continue;
        }

        while (1){
            rt_size_t read_len = rt_device_read(btstack_uart_rtthread_state.device,
                                                0,
                                                btstack_uart_rtthread_state.rx_tmp_storage,
                                                sizeof(btstack_uart_rtthread_state.rx_tmp_storage));
            if (read_len == 0){
                break;
            }

            (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
            {
                rt_size_t stored_len = rt_ringbuffer_put(&btstack_uart_rtthread_state.rx_ringbuffer,
                                                         btstack_uart_rtthread_state.rx_tmp_storage,
                                                         (rt_uint16_t) read_len);
                if (stored_len < read_len){
                    log_error("btstack_uart: ringbuffer overflow, drop %u bytes",
                              (unsigned int) (read_len - stored_len));
                }
                btstack_uart_rtthread_try_deliver_rx_locked();
            }
            rt_mutex_release(&btstack_uart_rtthread_state.lock);
        }
    }
}

static int btstack_uart_rtthread_init(const btstack_uart_config_t * uart_config){
    if (uart_config == NULL){
        return -1;
    }

    memset(&btstack_uart_rtthread_state.config, 0, sizeof(btstack_uart_rtthread_state.config));
    btstack_uart_rtthread_state.config = *uart_config;
    btstack_uart_rtthread_state.rx_buffer = NULL;
    btstack_uart_rtthread_state.rx_len = 0;
    btstack_uart_rtthread_state.rx_active = RT_FALSE;
    btstack_uart_rtthread_state.rx_callback_pending = RT_FALSE;
    btstack_uart_rtthread_state.tx_callback_pending = RT_FALSE;
    btstack_uart_rtthread_state.wakeup_handler = NULL;

    rt_ringbuffer_init(&btstack_uart_rtthread_state.rx_ringbuffer,
                       btstack_uart_rtthread_state.rx_storage,
                       sizeof(btstack_uart_rtthread_state.rx_storage));

    if (!btstack_uart_rtthread_state.lock_inited){
        rt_mutex_init(&btstack_uart_rtthread_state.lock, "btuart", RT_IPC_FLAG_FIFO);
        btstack_uart_rtthread_state.lock_inited = RT_TRUE;
    }

    if (!btstack_uart_rtthread_state.rx_sem_inited){
        rt_sem_init(&btstack_uart_rtthread_state.rx_sem, "btrx", 0, RT_IPC_FLAG_FIFO);
        btstack_uart_rtthread_state.rx_sem_inited = RT_TRUE;
    }

    if (btstack_uart_rtthread_state.rx_thread == RT_NULL){
        btstack_uart_rtthread_state.rx_thread = rt_thread_create("bt_h4_rx",
                                                                 btstack_uart_rtthread_rx_thread,
                                                                 RT_NULL,
                                                                 BTSTACK_UART_RTTHREAD_THREAD_STACK_SIZE,
                                                                 BTSTACK_UART_RTTHREAD_THREAD_PRIORITY,
                                                                 BTSTACK_UART_RTTHREAD_THREAD_TICK);
        if (btstack_uart_rtthread_state.rx_thread == RT_NULL){
            log_error("btstack_uart: create rx thread failed");
            return -1;
        }
        rt_thread_startup(btstack_uart_rtthread_state.rx_thread);
    }

    return 0;
}

static int btstack_uart_rtthread_open(void){
    int err;
    const char * device_name = btstack_uart_rtthread_device_name();

    if (btstack_uart_rtthread_state.device != RT_NULL){
        return 0;
    }

    btstack_uart_rtthread_state.device = rt_device_find(device_name);
    if (btstack_uart_rtthread_state.device == RT_NULL){
        log_error("btstack_uart: device %s not found", device_name);
        return -1;
    }

    btstack_uart_rtthread_configure_uart2_flowcontrol_pins();

    err = (int) rt_device_open(btstack_uart_rtthread_state.device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (err != RT_EOK){
        log_error("btstack_uart: open %s failed, err %d", device_name, err);
        btstack_uart_rtthread_state.device = RT_NULL;
        return -1;
    }

    err = btstack_uart_rtthread_apply_config();
    if (err != RT_EOK){
        log_error("btstack_uart: configure %s failed, err %d", device_name, err);
        rt_device_close(btstack_uart_rtthread_state.device);
        btstack_uart_rtthread_state.device = RT_NULL;
        return -1;
    }

    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    rt_ringbuffer_reset(&btstack_uart_rtthread_state.rx_ringbuffer);
    btstack_uart_rtthread_state.rx_buffer = NULL;
    btstack_uart_rtthread_state.rx_len = 0;
    btstack_uart_rtthread_state.rx_active = RT_FALSE;
    btstack_uart_rtthread_state.rx_callback_pending = RT_FALSE;
    btstack_uart_rtthread_state.tx_callback_pending = RT_FALSE;
    rt_mutex_release(&btstack_uart_rtthread_state.lock);

    rt_device_set_rx_indicate(btstack_uart_rtthread_state.device, btstack_uart_rtthread_rx_indicate);
    return 0;
}

static int btstack_uart_rtthread_close(void){
    if (btstack_uart_rtthread_state.device == RT_NULL){
        return 0;
    }

    rt_device_set_rx_indicate(btstack_uart_rtthread_state.device, RT_NULL);
    (void) rt_device_close(btstack_uart_rtthread_state.device);
    btstack_uart_rtthread_state.device = RT_NULL;

    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    rt_ringbuffer_reset(&btstack_uart_rtthread_state.rx_ringbuffer);
    btstack_uart_rtthread_state.rx_buffer = NULL;
    btstack_uart_rtthread_state.rx_len = 0;
    btstack_uart_rtthread_state.rx_active = RT_FALSE;
    btstack_uart_rtthread_state.rx_callback_pending = RT_FALSE;
    btstack_uart_rtthread_state.tx_callback_pending = RT_FALSE;
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
    return 0;
}

static void btstack_uart_rtthread_set_block_received(void (*block_handler)(void)){
    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    btstack_uart_rtthread_state.block_received_handler = block_handler;
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
}

static void btstack_uart_rtthread_set_block_sent(void (*block_handler)(void)){
    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    btstack_uart_rtthread_state.block_sent_handler = block_handler;
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
}

static int btstack_uart_rtthread_set_baudrate(uint32_t baudrate){
    btstack_uart_rtthread_state.config.baudrate = baudrate;
    if (btstack_uart_rtthread_state.device == RT_NULL){
        return 0;
    }
    return (btstack_uart_rtthread_apply_config() == RT_EOK) ? 0 : -1;
}

static int btstack_uart_rtthread_set_parity(int parity){
    btstack_uart_rtthread_state.config.parity = parity;
    if (btstack_uart_rtthread_state.device == RT_NULL){
        return 0;
    }
    return (btstack_uart_rtthread_apply_config() == RT_EOK) ? 0 : -1;
}

static int btstack_uart_rtthread_set_flowcontrol(int flowcontrol){
    btstack_uart_rtthread_state.config.flowcontrol = flowcontrol;
    if (btstack_uart_rtthread_state.device == RT_NULL){
        return 0;
    }
    return (btstack_uart_rtthread_apply_config() == RT_EOK) ? 0 : -1;
}

static void btstack_uart_rtthread_receive_block(uint8_t * buffer, uint16_t len){
    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    btstack_uart_rtthread_state.rx_buffer = buffer;
    btstack_uart_rtthread_state.rx_len = len;
    btstack_uart_rtthread_state.rx_active = RT_TRUE;
    btstack_uart_rtthread_try_deliver_rx_locked();
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
}

static void btstack_uart_rtthread_send_block(const uint8_t * buffer, uint16_t length){
    rt_size_t written;

    if ((btstack_uart_rtthread_state.device == RT_NULL) || (buffer == NULL) || (length == 0)){
        return;
    }

    written = rt_device_write(btstack_uart_rtthread_state.device, 0, (void *) buffer, length);
    if (written != length){
        log_error("btstack_uart: write truncated, expect %u got %u",
                  (unsigned int) length,
                  (unsigned int) written);
    }

    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    btstack_uart_rtthread_schedule_tx_done_locked();
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
}

static int btstack_uart_rtthread_get_supported_sleep_modes(void){
    return 0;
}

static void btstack_uart_rtthread_set_sleep(btstack_uart_sleep_mode_t sleep_mode){
    UNUSED(sleep_mode);
}

static void btstack_uart_rtthread_set_wakeup_handler(void (*wakeup_handler)(void)){
    (void) rt_mutex_take(&btstack_uart_rtthread_state.lock, RT_WAITING_FOREVER);
    btstack_uart_rtthread_state.wakeup_handler = wakeup_handler;
    rt_mutex_release(&btstack_uart_rtthread_state.lock);
}

static const btstack_uart_block_t btstack_uart_rtthread_block = {
    &btstack_uart_rtthread_init,
    &btstack_uart_rtthread_open,
    &btstack_uart_rtthread_close,
    &btstack_uart_rtthread_set_block_received,
    &btstack_uart_rtthread_set_block_sent,
    &btstack_uart_rtthread_set_baudrate,
    &btstack_uart_rtthread_set_parity,
    &btstack_uart_rtthread_set_flowcontrol,
    &btstack_uart_rtthread_receive_block,
    &btstack_uart_rtthread_send_block,
    &btstack_uart_rtthread_get_supported_sleep_modes,
    &btstack_uart_rtthread_set_sleep,
    &btstack_uart_rtthread_set_wakeup_handler,
    NULL,
    NULL,
    NULL,
    NULL,
};

const btstack_uart_block_t * btstack_uart_block_embedded_instance(void){
    return &btstack_uart_rtthread_block;
}
