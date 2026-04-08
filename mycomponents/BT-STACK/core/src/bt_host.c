#include "bt_host.h"

#include <string.h>

#include "bluetooth_data_types.h"
#include "hci.h"
#include "gap.h"
#include "btstack_memory.h"
#include "hci_transport_h4.h"
#include "l2cap.h"

#if BT_CFG_ENABLE_CLASSIC
#include "classic/rfcomm.h"
#include "classic/sdp_server.h"
#endif

#if BT_CFG_ENABLE_BLE
#include "ble/sm.h"
#endif

static const hci_transport_config_uart_t bt_host_h4_uart_config = {
    .type = HCI_TRANSPORT_CONFIG_UART,
    .baudrate_init = BT_CFG_UART_BAUDRATE_INIT,
    .baudrate_main = BT_CFG_UART_BAUDRATE_MAIN,
    .flowcontrol = BT_CFG_UART_FLOWCONTROL,
    .device_name = BT_CFG_UART_DEVICE_NAME,
    .parity = BT_CFG_UART_PARITY,
};

#if BT_CFG_ENABLE_BLE
static uint8_t bt_host_adv_data[31];
static uint8_t bt_host_scan_response_data[31];

static uint8_t bt_host_store_name_ad(uint8_t *buffer, uint8_t buffer_size, const char *name, uint8_t data_type){
    uint8_t name_len = (uint8_t) strlen(name);
    if (buffer_size < 2u){
        return 0;
    }
    if (name_len > (uint8_t) (buffer_size - 2u)){
        name_len = (uint8_t) (buffer_size - 2u);
    }
    buffer[0] = (uint8_t) (name_len + 1u);
    buffer[1] = data_type;
    if (name_len > 0u){
        memcpy(&buffer[2], name, name_len);
    }
    return (uint8_t) (name_len + 2u);
}
#endif

const hci_transport_config_uart_t * bt_host_h4_uart_config_get(void){
    return &bt_host_h4_uart_config;
}

int bt_host_stack_init(const btstack_uart_block_t *uart_driver, const btstack_chipset_t *chipset_driver){
    if (uart_driver == NULL){
        return -1;
    }

    btstack_memory_init();
    hci_init(hci_transport_h4_instance(uart_driver), &bt_host_h4_uart_config);

    if (chipset_driver != NULL){
        hci_set_chipset(chipset_driver);
    }

    return 0;
}

void bt_host_protocol_init(void){
    l2cap_init();

#if BT_CFG_ENABLE_CLASSIC
#if BT_CFG_CLASSIC_ENABLE_SDP
    sdp_init();
#endif

#if BT_CFG_CLASSIC_ENABLE_RFCOMM
    rfcomm_init();
#endif
#endif

#if BT_CFG_ENABLE_BLE
#if BT_CFG_BLE_ENABLE_SM
    sm_init();
    sm_set_io_capabilities((io_capability_t) BT_CFG_BLE_SM_IO_CAPABILITY);
    sm_set_authentication_requirements(BT_CFG_BLE_SM_AUTH_REQ);
#endif
#endif
}

void bt_host_apply_device_config(void){
#if BT_CFG_ENABLE_CLASSIC
    gap_set_local_name(BT_CFG_LOCAL_NAME);
    if (BT_CFG_CLASS_OF_DEVICE != 0u){
        gap_set_class_of_device(BT_CFG_CLASS_OF_DEVICE);
    }
    gap_ssp_set_io_capability(BT_CFG_CLASSIC_SSP_IO_CAPABILITY);
    gap_ssp_set_authentication_requirement(BT_CFG_CLASSIC_SSP_AUTHREQ);
    gap_discoverable_control(BT_CFG_CLASSIC_DISCOVERABLE);
#endif
}

#if BT_CFG_ENABLE_BLE
int bt_host_ble_init_att_server(const uint8_t *att_db, att_read_callback_t read_callback, att_write_callback_t write_callback){
    if (att_db == NULL){
        return -1;
    }

    att_server_init(att_db, read_callback, write_callback);
    return 0;
}

void bt_host_ble_setup_advertising(void){
    uint8_t adv_len = 0u;
    uint8_t scan_len = 0u;
    bd_addr_t null_addr = {0};
    size_t adv_name_len = strlen(BT_CFG_BLE_ADV_NAME);
    uint8_t adv_name_room;

    memset(bt_host_adv_data, 0, sizeof(bt_host_adv_data));
    memset(bt_host_scan_response_data, 0, sizeof(bt_host_scan_response_data));

    bt_host_adv_data[adv_len++] = 2u;
    bt_host_adv_data[adv_len++] = BLUETOOTH_DATA_TYPE_FLAGS;
    bt_host_adv_data[adv_len++] = BT_CFG_BLE_ADV_FLAGS;

    adv_name_room = (uint8_t) (sizeof(bt_host_adv_data) - adv_len);
    if (adv_name_len + 2u <= adv_name_room){
        adv_len += bt_host_store_name_ad(&bt_host_adv_data[adv_len], adv_name_room, BT_CFG_BLE_ADV_NAME, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME);
    } else {
        if (adv_name_room > 2u){
            adv_len += bt_host_store_name_ad(&bt_host_adv_data[adv_len], adv_name_room, BT_CFG_BLE_ADV_NAME, BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME);
        }
        scan_len = bt_host_store_name_ad(bt_host_scan_response_data, sizeof(bt_host_scan_response_data), BT_CFG_BLE_ADV_NAME, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME);
    }

    gap_advertisements_set_params(BT_CFG_BLE_ADV_INTERVAL_MIN, BT_CFG_BLE_ADV_INTERVAL_MAX, BT_CFG_BLE_ADV_TYPE, 0, null_addr, BT_CFG_BLE_ADV_CHANNEL_MAP, BT_CFG_BLE_ADV_FILTER_POLICY);
    gap_advertisements_set_data(adv_len, bt_host_adv_data);
    gap_scan_response_set_data(scan_len, bt_host_scan_response_data);
    gap_advertisements_enable(BT_CFG_BLE_ADV_ENABLE);
}
#endif

int bt_host_start(void){
    return hci_power_control(HCI_POWER_ON);
}

