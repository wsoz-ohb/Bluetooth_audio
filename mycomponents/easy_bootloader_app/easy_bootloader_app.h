#ifndef EASY_BOOTLOADER_APP_H
#define EASY_BOOTLOADER_APP_H

#include "boot_config_app.h"
#include "boot_control.h"
#include "boot_image.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BOOT_APP_OK = 0,
    BOOT_APP_ERROR = -1,
    BOOT_APP_INVALID_ARGUMENT = -2,
    BOOT_APP_BUSY = -3,
    BOOT_APP_IO_ERROR = -4,
    BOOT_APP_PROTOCOL_ERROR = -5,
    BOOT_APP_OVERFLOW = -6,
    BOOT_APP_VERIFY_ERROR = -7,
    BOOT_APP_TIMEOUT = -8,
} boot_app_status_t;

typedef enum
{
    BOOT_APP_STATE_IDLE = 0,
    BOOT_APP_STATE_WAIT_DATA,
    BOOT_APP_STATE_RECEIVING,
    BOOT_APP_STATE_WAIT_FINISH,
    BOOT_APP_STATE_READY,
    BOOT_APP_STATE_ERROR,
} boot_app_state_t;

typedef struct
{
    uint32_t target_address;
    uint32_t image_max_size;
    uint32_t slot_a_size;
    uint32_t slot_b_size;
    uint32_t erase_size;
    uint32_t bcb_region_size;
    uint32_t session_timeout_ms;
    uint32_t running_version;
    uint32_t running_build_date;
    uint8_t auto_reset;
} boot_app_config_t;

/* 存储接口使用相对槽偏移，下载时不会选择当前确认槽。 */
typedef struct
{
    void *context;

    uint32_t (*get_time_ms)(void *context);
    uint32_t (*transport_read)(void *context, uint8_t *data, uint32_t capacity);
    boot_app_status_t (*transport_write)(void *context,
                                         const uint8_t *data,
                                         uint32_t length);

    boot_app_status_t (*read_boot_control)(void *context,
                                           boot_control_status_t *status);
    boot_app_status_t (*bcb_read)(void *context,
                                  uint32_t offset,
                                  uint8_t *data,
                                  uint32_t length);
    boot_app_status_t (*bcb_program)(void *context,
                                     uint32_t offset,
                                     const uint8_t *data,
                                     uint32_t length);
    boot_app_status_t (*bcb_erase)(void *context,
                                   uint32_t offset,
                                   uint32_t length);

    boot_app_status_t (*storage_erase)(void *context,
                                       boot_slot_t slot,
                                       uint32_t offset,
                                       uint32_t length);
    boot_app_status_t (*storage_write)(void *context,
                                       boot_slot_t slot,
                                       uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t length);
    boot_app_status_t (*storage_read)(void *context,
                                      boot_slot_t slot,
                                      uint32_t offset,
                                      uint8_t *data,
                                      uint32_t length);

    boot_app_status_t (*mark_update_ready)(void *context,
                                           boot_slot_t pending_slot,
                                           const boot_image_info_t *image);
    boot_app_status_t (*mark_confirmed)(void *context,
                                        const boot_control_status_t *status);
    void (*system_reset)(void *context);
    void (*log)(void *context, const char *format, ...);
} boot_app_ops_t;

typedef struct
{
    boot_app_state_t state;
    boot_app_status_t last_error;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t payload_crc32;
    boot_slot_t target_slot;
} boot_app_progress_t;

void easy_bootloader_app_get_default_config(boot_app_config_t *config);
boot_app_status_t easy_bootloader_app_init(const boot_app_config_t *config,
                                           const boot_app_ops_t *ops);
void easy_bootloader_app_run(void);
void easy_bootloader_app_abort(void);
void easy_bootloader_app_get_progress(boot_app_progress_t *progress);
boot_app_status_t easy_bootloader_app_confirm_running(void);

#ifdef __cplusplus
}
#endif

#endif /* EASY_BOOTLOADER_APP_H */
