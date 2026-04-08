#ifndef BT_HOST_H
#define BT_HOST_H

#include <stdint.h>
#include "bt_config.h"
#include "btstack_chipset.h"
#include "btstack_uart_block.h"
#include "hci_transport.h"

#if BT_CFG_ENABLE_BLE
#include "ble/att_server.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

const hci_transport_config_uart_t * bt_host_h4_uart_config_get(void);

int bt_host_stack_init(const btstack_uart_block_t *uart_driver, const btstack_chipset_t *chipset_driver);

void bt_host_protocol_init(void);

void bt_host_apply_device_config(void);

#if BT_CFG_ENABLE_BLE
int bt_host_ble_init_att_server(const uint8_t *att_db, att_read_callback_t read_callback, att_write_callback_t write_callback);
void bt_host_ble_setup_advertising(void);
#endif

int bt_host_start(void);

#ifdef __cplusplus
}
#endif

#endif // BT_HOST_H
