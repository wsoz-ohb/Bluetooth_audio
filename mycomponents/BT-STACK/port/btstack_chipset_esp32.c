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

static const btstack_chipset_t btstack_chipset_esp32 = {    //controller芯片适配板级初始化
    "ESP32-WROOM-32E",      //controller芯片名称
    &btstack_chipset_esp32_init,    //初始化函数
    &btstack_chipset_esp32_next_command,
    NULL,
    NULL,
};  
/*即是板子的厂商的初始化命令,vendor specific initialization commands
如果芯片需要上电初始化序列，则在 next_command 里实现并返回 BTSTACK_CHIPSET_VALID_COMMAND，
协议栈会在发送完 RESET 命令后调用 next_command 获取并发送这些初始化命令。*/

const btstack_chipset_t * btstack_chipset_esp32_instance(void){
    return &btstack_chipset_esp32;
}
