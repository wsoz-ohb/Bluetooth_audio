#include "boot_control.h"

#include "boot_image.h"

#include <string.h>

#define BCB_CRC_OFFSET                 52U
#define BCB_COMMIT_OFFSET              56U

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool is_erased(const uint8_t *raw)
{
    uint32_t index;

    for (index = 0U; index < BOOT_CONTROL_RECORD_SIZE; index++)
    {
        if (raw[index] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

bool boot_control_record_decode(const uint8_t raw[BOOT_CONTROL_RECORD_SIZE],
                                boot_control_status_t *status)
{
    uint32_t expected_crc;
    uint32_t actual_crc;

    if ((raw == NULL) || (status == NULL))
    {
        return false;
    }
    if ((get_u32_le(&raw[0]) != BOOT_CONTROL_MAGIC) ||
        (get_u16_le(&raw[4]) != BOOT_CONTROL_FORMAT_VERSION) ||
        (get_u16_le(&raw[6]) != BOOT_CONTROL_RECORD_SIZE) ||
        (get_u32_le(&raw[BCB_COMMIT_OFFSET]) != BOOT_CONTROL_COMMIT_MARKER))
    {
        return false;
    }

    expected_crc = get_u32_le(&raw[BCB_CRC_OFFSET]);
    actual_crc = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, raw, BCB_CRC_OFFSET));
    if (expected_crc != actual_crc)
    {
        return false;
    }

    status->sequence = get_u32_le(&raw[8]);
    status->state = (boot_control_state_t)get_u32_le(&raw[12]);
    status->confirmed_slot = (boot_slot_t)get_u32_le(&raw[16]);
    status->pending_slot = (boot_slot_t)get_u32_le(&raw[20]);
    status->confirmed_version = get_u32_le(&raw[24]);
    status->pending_version = get_u32_le(&raw[28]);
    status->image_size = get_u32_le(&raw[32]);
    status->image_crc32 = get_u32_le(&raw[36]);
    status->boot_attempts = raw[40];
    status->max_boot_attempts = raw[41];
    status->last_error = get_u32_le(&raw[44]);
    status->flags = get_u32_le(&raw[48]);
    return ((uint32_t)status->state <= (uint32_t)BOOT_CONTROL_ERROR) &&
           ((status->confirmed_slot == BOOT_SLOT_NONE) ||
            boot_control_is_slot_valid(status->confirmed_slot)) &&
           ((status->pending_slot == BOOT_SLOT_NONE) ||
            boot_control_is_slot_valid(status->pending_slot));
}

bool boot_control_record_encode(const boot_control_status_t *status,
                                uint32_t sequence,
                                uint8_t raw[BOOT_CONTROL_RECORD_SIZE])
{
    uint32_t crc;

    if ((status == NULL) || (raw == NULL) ||
        ((uint32_t)status->state > (uint32_t)BOOT_CONTROL_ERROR) ||
        ((status->confirmed_slot != BOOT_SLOT_NONE) &&
         !boot_control_is_slot_valid(status->confirmed_slot)) ||
        ((status->pending_slot != BOOT_SLOT_NONE) &&
         !boot_control_is_slot_valid(status->pending_slot)))
    {
        return false;
    }

    memset(raw, 0xFF, BOOT_CONTROL_RECORD_SIZE);
    put_u32_le(&raw[0], BOOT_CONTROL_MAGIC);
    put_u16_le(&raw[4], BOOT_CONTROL_FORMAT_VERSION);
    put_u16_le(&raw[6], BOOT_CONTROL_RECORD_SIZE);
    put_u32_le(&raw[8], sequence);
    put_u32_le(&raw[12], (uint32_t)status->state);
    put_u32_le(&raw[16], (uint32_t)status->confirmed_slot);
    put_u32_le(&raw[20], (uint32_t)status->pending_slot);
    put_u32_le(&raw[24], status->confirmed_version);
    put_u32_le(&raw[28], status->pending_version);
    put_u32_le(&raw[32], status->image_size);
    put_u32_le(&raw[36], status->image_crc32);
    raw[40] = status->boot_attempts;
    raw[41] = status->max_boot_attempts;
    put_u32_le(&raw[44], status->last_error);
    put_u32_le(&raw[48], status->flags);

    crc = boot_crc32_finish(boot_crc32_update(0xFFFFFFFFUL, raw, BCB_CRC_OFFSET));
    put_u32_le(&raw[BCB_CRC_OFFSET], crc);
    put_u32_le(&raw[BCB_COMMIT_OFFSET], BOOT_CONTROL_COMMIT_MARKER);
    return true;
}

bool boot_control_is_slot_valid(boot_slot_t slot)
{
    return (slot == BOOT_SLOT_A) || (slot == BOOT_SLOT_B);
}

boot_slot_t boot_control_other_slot(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return BOOT_SLOT_B;
    }
    if (slot == BOOT_SLOT_B)
    {
        return BOOT_SLOT_A;
    }
    return BOOT_SLOT_NONE;
}

bool boot_control_load(const boot_control_storage_t *storage,
                       boot_control_status_t *status)
{
    uint8_t raw[BOOT_CONTROL_RECORD_SIZE];
    boot_control_status_t candidate;
    uint32_t offset;
    bool found = false;

    if ((storage == NULL) || (status == NULL) || (storage->read == NULL) ||
        (storage->region_size < BOOT_CONTROL_RECORD_SIZE) ||
        ((storage->region_size % BOOT_CONTROL_RECORD_SIZE) != 0U))
    {
        return false;
    }

    /* 取序号最大的有效记录。 */
    memset(status, 0, sizeof(*status));
    status->state = BOOT_CONTROL_EMPTY;
    for (offset = 0U;
         offset <= (storage->region_size - BOOT_CONTROL_RECORD_SIZE);
         offset += BOOT_CONTROL_RECORD_SIZE)
    {
        if (storage->read(storage->context, offset, raw, sizeof(raw)) != 0)
        {
            return false;
        }
        if (boot_control_record_decode(raw, &candidate) &&
            (!found || (candidate.sequence > status->sequence)))
        {
            *status = candidate;
            found = true;
        }
    }
    return found;
}

uint32_t boot_control_free_record_count(const boot_control_storage_t *storage)
{
    uint8_t raw[BOOT_CONTROL_RECORD_SIZE];
    uint32_t offset;
    uint32_t count = 0U;

    if ((storage == NULL) || (storage->read == NULL) ||
        (storage->region_size < BOOT_CONTROL_RECORD_SIZE) ||
        ((storage->region_size % BOOT_CONTROL_RECORD_SIZE) != 0U))
    {
        return 0U;
    }

    for (offset = 0U;
         offset <= (storage->region_size - BOOT_CONTROL_RECORD_SIZE);
         offset += BOOT_CONTROL_RECORD_SIZE)
    {
        if (storage->read(storage->context, offset, raw, sizeof(raw)) != 0)
        {
            return 0U;
        }
        if (is_erased(raw))
        {
            count++;
        }
    }
    return count;
}

bool boot_control_append(const boot_control_storage_t *storage,
                         const boot_control_status_t *status)
{
    uint8_t raw[BOOT_CONTROL_RECORD_SIZE];
    boot_control_status_t current;
    uint32_t offset;
    uint32_t sequence = 1U;

    if ((storage == NULL) || (status == NULL) || (storage->read == NULL) ||
        (storage->program == NULL) ||
        (storage->region_size < BOOT_CONTROL_RECORD_SIZE) ||
        ((storage->region_size % BOOT_CONTROL_RECORD_SIZE) != 0U))
    {
        return false;
    }

    if (boot_control_load(storage, &current))
    {
        if (current.sequence == 0xFFFFFFFFUL)
        {
            return false;
        }
        sequence = current.sequence + 1U;
    }

    /* 只追加，不覆盖旧状态。 */
    for (offset = 0U;
         offset <= (storage->region_size - BOOT_CONTROL_RECORD_SIZE);
         offset += BOOT_CONTROL_RECORD_SIZE)
    {
        if (storage->read(storage->context, offset, raw, sizeof(raw)) != 0)
        {
            return false;
        }
        if (is_erased(raw))
        {
            if (!boot_control_record_encode(status, sequence, raw))
            {
                return false;
            }
            /* 提交标记最后写，断电时可回退。 */
            if (storage->program(storage->context, offset, raw, BCB_COMMIT_OFFSET) != 0)
            {
                return false;
            }
            return storage->program(storage->context,
                                    offset + BCB_COMMIT_OFFSET,
                                    &raw[BCB_COMMIT_OFFSET],
                                    sizeof(uint32_t)) == 0;
        }
    }
    return false;
}

bool boot_control_recycle(const boot_control_storage_t *storage,
                          const boot_control_status_t *status)
{
    uint8_t raw[BOOT_CONTROL_RECORD_SIZE];

    if ((storage == NULL) || (status == NULL) || (storage->erase == NULL) ||
        (storage->program == NULL) || (storage->region_size == 0U))
    {
        return false;
    }
    /* 回收前必须确保存在可靠备份。 */
    if (storage->erase(storage->context, 0U, storage->region_size) != 0)
    {
        return false;
    }

    if (!boot_control_record_encode(status, 1U, raw))
    {
        return false;
    }
    if (storage->program(storage->context, 0U, raw, BCB_COMMIT_OFFSET) != 0)
    {
        return false;
    }
    return storage->program(storage->context,
                            BCB_COMMIT_OFFSET,
                            &raw[BCB_COMMIT_OFFSET],
                            sizeof(uint32_t)) == 0;
}
