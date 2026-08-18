#include "boot_image.h"

#include <string.h>

#define BOOT_IMAGE_HEADER_CRC_OFFSET     56U
#define BOOT_IMAGE_COMMIT_OFFSET         60U

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

uint32_t boot_crc32_update(uint32_t state, const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bit;

    if (data == NULL)
    {
        return state;
    }

    for (index = 0U; index < length; index++)
    {
        state ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            state = (state >> 1) ^
                    (0xEDB88320UL & (0U - (state & 1U)));
        }
    }
    return state;
}

uint32_t boot_crc32_finish(uint32_t state)
{
    return ~state;
}

void boot_image_header_encode(const boot_image_info_t *image,
                              uint8_t raw[BOOT_IMAGE_HEADER_SIZE])
{
    uint32_t header_crc;

    if ((image == NULL) || (raw == NULL))
    {
        return;
    }

    memset(raw, 0xFF, BOOT_IMAGE_HEADER_SIZE);
    put_u32_le(&raw[0], BOOT_IMAGE_MAGIC);
    put_u16_le(&raw[4], BOOT_IMAGE_FORMAT_VERSION);
    put_u16_le(&raw[6], BOOT_IMAGE_HEADER_SIZE);
    put_u32_le(&raw[8], image->target_address);
    put_u32_le(&raw[12], image->firmware_version);
    put_u32_le(&raw[16], image->build_date);
    put_u32_le(&raw[20], image->payload_offset);
    put_u32_le(&raw[24], image->payload_size);
    put_u32_le(&raw[28], image->payload_crc32);
    put_u32_le(&raw[32], image->flags);

    header_crc = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, raw, BOOT_IMAGE_HEADER_CRC_OFFSET));
    put_u32_le(&raw[BOOT_IMAGE_HEADER_CRC_OFFSET], header_crc);
    /* 提交标记由调用方最后写入。 */
    put_u32_le(&raw[BOOT_IMAGE_COMMIT_OFFSET], BOOT_IMAGE_COMMIT_MARKER);
}

bool boot_image_header_decode(const uint8_t raw[BOOT_IMAGE_HEADER_SIZE],
                              boot_image_info_t *image)
{
    uint32_t expected_crc;
    uint32_t actual_crc;

    if ((raw == NULL) || (image == NULL))
    {
        return false;
    }
    if ((get_u32_le(&raw[0]) != BOOT_IMAGE_MAGIC) ||
        (get_u16_le(&raw[4]) != BOOT_IMAGE_FORMAT_VERSION) ||
        (get_u16_le(&raw[6]) != BOOT_IMAGE_HEADER_SIZE) ||
        (get_u32_le(&raw[BOOT_IMAGE_COMMIT_OFFSET]) != BOOT_IMAGE_COMMIT_MARKER))
    {
        return false;
    }

    expected_crc = get_u32_le(&raw[BOOT_IMAGE_HEADER_CRC_OFFSET]);
    actual_crc = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, raw, BOOT_IMAGE_HEADER_CRC_OFFSET));
    if (expected_crc != actual_crc)
    {
        return false;
    }

    image->target_address = get_u32_le(&raw[8]);
    image->firmware_version = get_u32_le(&raw[12]);
    image->build_date = get_u32_le(&raw[16]);
    image->payload_offset = get_u32_le(&raw[20]);
    image->payload_size = get_u32_le(&raw[24]);
    image->payload_crc32 = get_u32_le(&raw[28]);
    image->flags = get_u32_le(&raw[32]);
    image->header_crc32 = expected_crc;
    return image->payload_offset == BOOT_IMAGE_PAYLOAD_OFFSET;
}
