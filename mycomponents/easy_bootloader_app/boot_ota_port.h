/*
 * RT-Thread adaptation for the Bluetooth SPP Bootloader APP component.
 *
 * This must only be enabled after the application is linked at 0x08010000
 * and the standalone Bootloader jump path has been verified.
 */
#ifndef APPLICATIONS_BOOT_OTA_PORT_H_
#define APPLICATIONS_BOOT_OTA_PORT_H_

#include <rtthread.h>

#include "easy_bootloader_app.h"

#ifndef BOOT_OTA_ENABLE_INSTALL
#define BOOT_OTA_ENABLE_INSTALL 1
#endif

#if defined(__cplusplus)
extern "C" {
#endif

rt_err_t boot_ota_init(void);
void boot_ota_poll(void);
rt_bool_t boot_ota_is_ready(void);
boot_app_status_t boot_ota_confirm_running(void);
void boot_ota_schedule_confirmation(rt_uint32_t delay_ms);
void boot_ota_get_progress(boot_app_progress_t *progress);

#if defined(__cplusplus)
}
#endif

#endif /* APPLICATIONS_BOOT_OTA_PORT_H_ */
