#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_IMAGE_HEADER_SIZE        64U
#define BOOT_IMAGE_PAYLOAD_OFFSET     BOOT_IMAGE_HEADER_SIZE
#define BOOT_IMAGE_MAGIC              0x314D4245UL /* "EBM1" 小端序 */
#define BOOT_IMAGE_FORMAT_VERSION     1U
#define BOOT_IMAGE_COMMIT_MARKER      0x54494D43UL /* "CMIT" 小端序 */

typedef struct
{
    uint32_t target_address;
    uint32_t firmware_version;
    uint32_t build_date;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t flags;
    uint32_t header_crc32;
} boot_image_info_t;

uint32_t boot_crc32_update(uint32_t state, const uint8_t *data, uint32_t length);
uint32_t boot_crc32_finish(uint32_t state);

/* 镜像提交标记必须最后写入。 */
void boot_image_header_encode(const boot_image_info_t *image,
                              uint8_t raw[BOOT_IMAGE_HEADER_SIZE]);
bool boot_image_header_decode(const uint8_t raw[BOOT_IMAGE_HEADER_SIZE],
                              boot_image_info_t *image);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_IMAGE_H */
