#define BTSTACK_FILE__ "hci_dump_uart4_rtthread.c"

#include "hci_dump_uart4_rtthread.h"

#include <board.h>
#include <ipc/ringbuffer.h>

#include "bluetooth.h"
#include "btstack_run_loop.h"

#define HCI_DUMP_UART4_BAUD_RATE          2000000u
#define HCI_DUMP_UART4_BUFFER_SIZE        (4u * 1024u)
#define HCI_DUMP_UART4_TX_BLOCK_SIZE      256u
#define HCI_DUMP_UART4_THREAD_STACK_SIZE  2048u
#define HCI_DUMP_UART4_THREAD_PRIORITY    16u
#define HCI_DUMP_UART4_THREAD_TICK        10u
#define HCI_DUMP_BTSNOOP_EPOCH_DELTA      0x00dcddb30f2f8000ULL

static const uint8_t hci_dump_btsnoop_file_header[] = {
    'b', 't', 's', 'n', 'o', 'o', 'p', 0x00,
    0x00, 0x00, 0x00, 0x01,             /* Version 1 */
    0x00, 0x00, 0x03, 0xea,             /* HCI UART (H4), DLT 1002 */
};

typedef struct {
    UART_HandleTypeDef uart;
    struct rt_ringbuffer ringbuffer;
    rt_uint8_t storage[HCI_DUMP_UART4_BUFFER_SIZE];
    struct rt_mutex lock;
    struct rt_semaphore data_sem;
    rt_thread_t thread;
    rt_uint32_t dropped_records;
    rt_bool_t opened;
} hci_dump_uart4_rtthread_state_t;

static hci_dump_uart4_rtthread_state_t hci_dump_uart4_state;

static rt_bool_t hci_dump_uart4_packet_type_supported(uint8_t packet_type){
    switch (packet_type){
        case HCI_COMMAND_DATA_PACKET:
        case HCI_ACL_DATA_PACKET:
        case HCI_SCO_DATA_PACKET:
        case HCI_ISO_DATA_PACKET:
        case HCI_EVENT_PACKET:
            return RT_TRUE;
        default:
            return RT_FALSE;
    }
}

static void hci_dump_uart4_log_packet(uint8_t packet_type,
                                      uint8_t in,
                                      uint8_t * packet,
                                      uint16_t len){
    uint8_t record_prefix[HCI_DUMP_HEADER_SIZE_BTSNOOP + 1u];
    uint64_t timestamp_us;
    uint32_t time_ms;
    rt_size_t record_len;
    rt_bool_t wake_thread = RT_FALSE;

    if ((!hci_dump_uart4_state.opened) ||
        (!hci_dump_uart4_packet_type_supported(packet_type)) ||
        ((packet == NULL) && (len != 0u))){
        return;
    }

    time_ms = btstack_run_loop_get_time_ms();
    timestamp_us = HCI_DUMP_BTSNOOP_EPOCH_DELTA + ((uint64_t) time_ms * 1000u);
    record_len = sizeof(record_prefix) + len;

    (void) rt_mutex_take(&hci_dump_uart4_state.lock, RT_WAITING_FOREVER);
    if (rt_ringbuffer_space_len(&hci_dump_uart4_state.ringbuffer) >= record_len){
        wake_thread = (rt_ringbuffer_data_len(&hci_dump_uart4_state.ringbuffer) == 0u) ? RT_TRUE : RT_FALSE;
        hci_dump_setup_header_btsnoop(record_prefix,
                                      (uint32_t) (timestamp_us >> 32),
                                      (uint32_t) timestamp_us,
                                      hci_dump_uart4_state.dropped_records,
                                      packet_type,
                                      in,
                                      (uint16_t) (len + 1u));
        record_prefix[HCI_DUMP_HEADER_SIZE_BTSNOOP] = packet_type;
        (void) rt_ringbuffer_put(&hci_dump_uart4_state.ringbuffer,
                                 record_prefix,
                                 sizeof(record_prefix));
        if (len != 0u){
            (void) rt_ringbuffer_put(&hci_dump_uart4_state.ringbuffer, packet, len);
        }
    } else {
        hci_dump_uart4_state.dropped_records++;
    }
    rt_mutex_release(&hci_dump_uart4_state.lock);

    if (wake_thread){
        (void) rt_sem_release(&hci_dump_uart4_state.data_sem);
    }
}

static void hci_dump_uart4_log_message(int log_level,
                                       const char * format,
                                       va_list argptr){
    (void) log_level;
    (void) format;
    (void) argptr;
}

static void hci_dump_uart4_thread_entry(void * parameter){
    uint8_t tx_buffer[HCI_DUMP_UART4_TX_BLOCK_SIZE];

    (void) parameter;
    while (1){
        (void) rt_sem_take(&hci_dump_uart4_state.data_sem, RT_WAITING_FOREVER);

        while (1){
            rt_size_t tx_len;

            (void) rt_mutex_take(&hci_dump_uart4_state.lock, RT_WAITING_FOREVER);
            tx_len = rt_ringbuffer_get(&hci_dump_uart4_state.ringbuffer,
                                       tx_buffer,
                                       sizeof(tx_buffer));
            rt_mutex_release(&hci_dump_uart4_state.lock);

            if (tx_len == 0u){
                break;
            }

            (void) HAL_UART_Transmit(&hci_dump_uart4_state.uart,
                                     tx_buffer,
                                     (uint16_t) tx_len,
                                     HAL_MAX_DELAY);
        }
    }
}

static rt_err_t hci_dump_uart4_hardware_init(void){
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOC, &gpio);

    hci_dump_uart4_state.uart.Instance = UART4;
    hci_dump_uart4_state.uart.Init.BaudRate = HCI_DUMP_UART4_BAUD_RATE;
    hci_dump_uart4_state.uart.Init.WordLength = UART_WORDLENGTH_8B;
    hci_dump_uart4_state.uart.Init.StopBits = UART_STOPBITS_1;
    hci_dump_uart4_state.uart.Init.Parity = UART_PARITY_NONE;
    hci_dump_uart4_state.uart.Init.Mode = UART_MODE_TX;
    hci_dump_uart4_state.uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hci_dump_uart4_state.uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&hci_dump_uart4_state.uart) != HAL_OK){
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10);
        __HAL_RCC_UART4_CLK_DISABLE();
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void hci_dump_uart4_hardware_deinit(void){
    (void) HAL_UART_DeInit(&hci_dump_uart4_state.uart);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10);
    __HAL_RCC_UART4_CLK_DISABLE();
}

rt_err_t hci_dump_uart4_rtthread_open(hci_dump_format_t format){
    rt_err_t err;

    if (format != HCI_DUMP_BTSNOOP){
        return -RT_EINVAL;
    }
    if (hci_dump_uart4_state.opened){
        return RT_EOK;
    }

    err = hci_dump_uart4_hardware_init();
    if (err != RT_EOK){
        return err;
    }

    rt_ringbuffer_init(&hci_dump_uart4_state.ringbuffer,
                       hci_dump_uart4_state.storage,
                       sizeof(hci_dump_uart4_state.storage));
    (void) rt_ringbuffer_put(&hci_dump_uart4_state.ringbuffer,
                             hci_dump_btsnoop_file_header,
                             sizeof(hci_dump_btsnoop_file_header));

    err = rt_mutex_init(&hci_dump_uart4_state.lock, "hcidump", RT_IPC_FLAG_FIFO);
    if (err != RT_EOK){
        hci_dump_uart4_hardware_deinit();
        return err;
    }

    err = rt_sem_init(&hci_dump_uart4_state.data_sem, "hcilog", 0u, RT_IPC_FLAG_FIFO);
    if (err != RT_EOK){
        (void) rt_mutex_detach(&hci_dump_uart4_state.lock);
        hci_dump_uart4_hardware_deinit();
        return err;
    }

    hci_dump_uart4_state.thread = rt_thread_create("hci_log",
                                                   hci_dump_uart4_thread_entry,
                                                   RT_NULL,
                                                   HCI_DUMP_UART4_THREAD_STACK_SIZE,
                                                   HCI_DUMP_UART4_THREAD_PRIORITY,
                                                   HCI_DUMP_UART4_THREAD_TICK);
    if (hci_dump_uart4_state.thread == RT_NULL){
        (void) rt_sem_detach(&hci_dump_uart4_state.data_sem);
        (void) rt_mutex_detach(&hci_dump_uart4_state.lock);
        hci_dump_uart4_hardware_deinit();
        return -RT_ENOMEM;
    }

    err = rt_thread_startup(hci_dump_uart4_state.thread);
    if (err != RT_EOK){
        (void) rt_thread_delete(hci_dump_uart4_state.thread);
        hci_dump_uart4_state.thread = RT_NULL;
        (void) rt_sem_detach(&hci_dump_uart4_state.data_sem);
        (void) rt_mutex_detach(&hci_dump_uart4_state.lock);
        hci_dump_uart4_hardware_deinit();
        return err;
    }

    hci_dump_uart4_state.dropped_records = 0u;
    hci_dump_uart4_state.opened = RT_TRUE;
    (void) rt_sem_release(&hci_dump_uart4_state.data_sem);
    return RT_EOK;
}

const hci_dump_t * hci_dump_uart4_rtthread_get_instance(void){
    static const hci_dump_t hci_dump_instance = {
        RT_NULL,
        hci_dump_uart4_log_packet,
        hci_dump_uart4_log_message,
    };

    return &hci_dump_instance;
}
