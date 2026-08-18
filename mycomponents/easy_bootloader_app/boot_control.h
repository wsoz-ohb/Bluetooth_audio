#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_CONTROL_RECORD_SIZE       64U
#define BOOT_CONTROL_MAGIC             0x31424342UL /* "BCB1" 小端序 */
#define BOOT_CONTROL_FORMAT_VERSION    1U
#define BOOT_CONTROL_COMMIT_MARKER     0x54494D43UL /* "CMIT" 小端序 */
#define BOOT_CONTROL_UPDATE_RECORD_RESERVE 8U

typedef enum
{
    BOOT_SLOT_NONE = 0,
    BOOT_SLOT_A = 1,
    BOOT_SLOT_B = 2,
} boot_slot_t;

typedef enum
{
    BOOT_CONTROL_EMPTY = 0,
    BOOT_CONTROL_CONFIRMED,
    BOOT_CONTROL_UPDATE_READY,
    BOOT_CONTROL_BACKUP_READY,
    BOOT_CONTROL_INSTALLING,
    BOOT_CONTROL_TRIAL,
    BOOT_CONTROL_ROLLBACK,
    BOOT_CONTROL_ERROR,
} boot_control_state_t;

typedef struct
{
    boot_control_state_t state;
    boot_slot_t confirmed_slot;
    boot_slot_t pending_slot;
    uint32_t confirmed_version;
    uint32_t pending_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t boot_attempts;
    uint8_t max_boot_attempts;
    uint32_t last_error;
    uint32_t flags;
    uint32_t sequence;
} boot_control_status_t;

/* 所有 offset 都相对 BCB 区域，返回 0 表示成功。 */
typedef struct
{
    void *context;
    int (*read)(void *context, uint32_t offset, uint8_t *data, uint32_t length);
    int (*program)(void *context,
                   uint32_t offset,
                   const uint8_t *data,
                   uint32_t length);
    int (*erase)(void *context, uint32_t offset, uint32_t length);
    uint32_t region_size;
} boot_control_storage_t;

bool boot_control_is_slot_valid(boot_slot_t slot);
boot_slot_t boot_control_other_slot(boot_slot_t slot);

/* 编码后的记录含提交标记，写 Flash 时必须最后提交。 */
bool boot_control_record_encode(const boot_control_status_t *status,
                                uint32_t sequence,
                                uint8_t raw[BOOT_CONTROL_RECORD_SIZE]);
bool boot_control_record_decode(const uint8_t raw[BOOT_CONTROL_RECORD_SIZE],
                                boot_control_status_t *status);

bool boot_control_load(const boot_control_storage_t *storage,
                       boot_control_status_t *status);
uint32_t boot_control_free_record_count(const boot_control_storage_t *storage);
bool boot_control_append(const boot_control_storage_t *storage,
                         const boot_control_status_t *status);
bool boot_control_recycle(const boot_control_storage_t *storage,
                          const boot_control_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_CONTROL_H */
