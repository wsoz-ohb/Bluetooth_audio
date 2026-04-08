#define BTSTACK_FILE__ "btstack_chipset_esp32.c"

#include "btstack_chipset_esp32.h"

#include "btstack_util.h"

static void btstack_chipset_esp32_init(const void * transport_config){
    UNUSED(transport_config);
}

static btstack_chipset_result_t btstack_chipset_esp32_next_command(uint8_t * hci_cmd_buffer){
    UNUSED(hci_cmd_buffer);
    return BTSTACK_CHIPSET_NO_INIT_SCRIPT;
}

static const btstack_chipset_t btstack_chipset_esp32 = {
    "ESP32-WROOM-32E",
    &btstack_chipset_esp32_init,
    &btstack_chipset_esp32_next_command,
    NULL,
    NULL,
};

const btstack_chipset_t * btstack_chipset_esp32_instance(void){
    return &btstack_chipset_esp32;
}
