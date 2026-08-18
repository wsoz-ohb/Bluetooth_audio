/*
 * Bluetooth SPP OTA port.
 *
 * The generic component owns the download protocol, image CRC and image
 * header. This file only maps it to RT-Thread, SPP, FAL and the BCB sector.
 */
#include "boot_ota_port.h"

#include "bt_spp_app.h"
#include "sfud_app.h"

#include <board.h>
#include <drivers/spi.h>
#include <fal.h>
#include <stdarg.h>
#include <string.h>

#define DBG_TAG "boot_ota"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BOOT_OTA_BCB_ADDRESS         0x0800C000UL
#define BOOT_OTA_BCB_SIZE            (16UL * 1024UL)
#define BOOT_OTA_MAX_BOOT_ATTEMPTS   2U

#define BOOT_OTA_SLOT_A_NAME         "fw_a"
#define BOOT_OTA_SLOT_B_NAME         "fw_b"

typedef struct
{
    const struct fal_partition *slot_a;
    const struct fal_partition *slot_b;
    rt_bool_t initialized;
    rt_bool_t confirmation_pending;
    rt_bool_t confirmation_complete;
    rt_uint32_t confirmation_due_ms;
} boot_ota_context_t;

static boot_ota_context_t boot_ota_ctx;

static rt_bool_t boot_ota_range_valid(rt_uint32_t offset,
                                      rt_uint32_t length,
                                      rt_uint32_t capacity)
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

static const struct fal_partition *boot_ota_partition_for_slot(boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return boot_ota_ctx.slot_a;
    }
    if (slot == BOOT_SLOT_B)
    {
        return boot_ota_ctx.slot_b;
    }
    return RT_NULL;
}

static struct rt_spi_device *boot_ota_take_flash_bus(void)
{
    rt_spi_flash_device_t flash_dev = sfud_app_get_device();
    struct rt_spi_device *flash_spi;

    if ((flash_dev == RT_NULL) || (flash_dev->rt_spi_device == RT_NULL))
    {
        return RT_NULL;
    }

    flash_spi = flash_dev->rt_spi_device;
    return (rt_spi_take_bus(flash_spi) == RT_EOK) ? flash_spi : RT_NULL;
}

static uint32_t boot_ota_get_time_ms(void *context)
{
    (void)context;
    return (uint32_t)rt_tick_get_millisecond();
}

static uint32_t boot_ota_transport_read(void *context,
                                        uint8_t *data,
                                        uint32_t capacity)
{
    (void)context;

    if ((data == RT_NULL) || (capacity == 0U))
    {
        return 0U;
    }
    return (uint32_t)bt_spp_rx_read(data, (rt_size_t)capacity);
}

static boot_app_status_t boot_ota_transport_write(void *context,
                                                  const uint8_t *data,
                                                  uint32_t length)
{
    (void)context;

    if ((data == RT_NULL) || (length == 0U) || (length > 0xFFFFU))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }
    return (bt_spp_tx_write(data, (rt_size_t)length) == (rt_size_t)length) ?
           BOOT_APP_OK : BOOT_APP_IO_ERROR;
}

static void boot_ota_make_empty_control(boot_control_status_t *status)
{
    rt_memset(status, 0, sizeof(*status));
    status->state = BOOT_CONTROL_EMPTY;
    status->confirmed_slot = BOOT_SLOT_NONE;
    status->pending_slot = BOOT_SLOT_NONE;
}

static boot_app_status_t boot_ota_bcb_read(void *context,
                                           uint32_t offset,
                                           uint8_t *data,
                                           uint32_t length)
{
    (void)context;

    if ((data == RT_NULL) ||
        !boot_ota_range_valid(offset, length, BOOT_OTA_BCB_SIZE))
    {
        return BOOT_APP_IO_ERROR;
    }
    rt_memcpy(data, (const void *)(BOOT_OTA_BCB_ADDRESS + offset), length);
    return BOOT_APP_OK;
}

static boot_app_status_t boot_ota_bcb_program(void *context,
                                              uint32_t offset,
                                              const uint8_t *data,
                                              uint32_t length)
{
    uint32_t index;

    (void)context;
    if ((data == RT_NULL) || ((length & 3U) != 0U) ||
        !boot_ota_range_valid(offset, length, BOOT_OTA_BCB_SIZE))
    {
        return BOOT_APP_IO_ERROR;
    }

    HAL_FLASH_Unlock();
    for (index = 0U; index < length; index += 4U)
    {
        uint32_t word = (uint32_t)data[index] |
                        ((uint32_t)data[index + 1U] << 8) |
                        ((uint32_t)data[index + 2U] << 16) |
                        ((uint32_t)data[index + 3U] << 24);
        uint32_t address = BOOT_OTA_BCB_ADDRESS + offset + index;

        if ((HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, word) != HAL_OK) ||
            (*(const volatile uint32_t *)address != word))
        {
            HAL_FLASH_Lock();
            return BOOT_APP_IO_ERROR;
        }
    }
    HAL_FLASH_Lock();
    return BOOT_APP_OK;
}

static boot_app_status_t boot_ota_bcb_erase(void *context,
                                            uint32_t offset,
                                            uint32_t length)
{
    (void)context;
    (void)offset;
    (void)length;

    /* BCB recycling belongs to the Bootloader, never to a running APP. */
    return BOOT_APP_IO_ERROR;
}

static int boot_ota_bcb_storage_read(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length)
{
    return (boot_ota_bcb_read(context, offset, data, length) == BOOT_APP_OK) ? 0 : -1;
}

static int boot_ota_bcb_storage_program(void *context,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t length)
{
    return (boot_ota_bcb_program(context, offset, data, length) == BOOT_APP_OK) ? 0 : -1;
}

static int boot_ota_bcb_storage_erase(void *context, uint32_t offset, uint32_t length)
{
    return (boot_ota_bcb_erase(context, offset, length) == BOOT_APP_OK) ? 0 : -1;
}

static boot_control_storage_t boot_ota_bcb_storage(void)
{
    boot_control_storage_t storage;

    storage.context = RT_NULL;
    storage.read = boot_ota_bcb_storage_read;
    storage.program = boot_ota_bcb_storage_program;
    storage.erase = boot_ota_bcb_storage_erase;
    storage.region_size = BOOT_OTA_BCB_SIZE;
    return storage;
}

static boot_app_status_t boot_ota_read_boot_control(void *context,
                                                    boot_control_status_t *status)
{
    boot_control_storage_t storage;

    (void)context;
    if (status == RT_NULL)
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    storage = boot_ota_bcb_storage();
    if (!boot_control_load(&storage, status))
    {
        boot_ota_make_empty_control(status);
    }
    return BOOT_APP_OK;
}

static boot_app_status_t boot_ota_storage_erase(void *context,
                                                boot_slot_t slot,
                                                uint32_t offset,
                                                uint32_t length)
{
    const struct fal_partition *part;
    struct rt_spi_device *flash_spi;
    int result;

    (void)context;
    part = boot_ota_partition_for_slot(slot);
    if ((part == RT_NULL) || !boot_ota_range_valid(offset, length, part->len))
    {
        return BOOT_APP_IO_ERROR;
    }
    flash_spi = boot_ota_take_flash_bus();
    if (flash_spi == RT_NULL)
    {
        return BOOT_APP_IO_ERROR;
    }
    result = fal_partition_erase(part, offset, length);
    rt_spi_release_bus(flash_spi);
    if (result != (int)length)
    {
        return BOOT_APP_IO_ERROR;
    }
    return BOOT_APP_OK;
}

static boot_app_status_t boot_ota_storage_write(void *context,
                                                boot_slot_t slot,
                                                uint32_t offset,
                                                const uint8_t *data,
                                                uint32_t length)
{
    const struct fal_partition *part;
    struct rt_spi_device *flash_spi;
    int result;

    (void)context;
    part = boot_ota_partition_for_slot(slot);
    if ((part == RT_NULL) || (data == RT_NULL) ||
        !boot_ota_range_valid(offset, length, part->len))
    {
        return BOOT_APP_IO_ERROR;
    }
    flash_spi = boot_ota_take_flash_bus();
    if (flash_spi == RT_NULL)
    {
        return BOOT_APP_IO_ERROR;
    }
    result = fal_partition_write(part, offset, data, length);
    rt_spi_release_bus(flash_spi);
    if (result != (int)length)
    {
        return BOOT_APP_IO_ERROR;
    }
    return BOOT_APP_OK;
}

static boot_app_status_t boot_ota_storage_read(void *context,
                                               boot_slot_t slot,
                                               uint32_t offset,
                                               uint8_t *data,
                                               uint32_t length)
{
    const struct fal_partition *part;
    struct rt_spi_device *flash_spi;
    int result;

    (void)context;
    part = boot_ota_partition_for_slot(slot);
    if ((part == RT_NULL) || (data == RT_NULL) ||
        !boot_ota_range_valid(offset, length, part->len))
    {
        return BOOT_APP_IO_ERROR;
    }
    flash_spi = boot_ota_take_flash_bus();
    if (flash_spi == RT_NULL)
    {
        return BOOT_APP_IO_ERROR;
    }
    result = fal_partition_read(part, offset, data, length);
    rt_spi_release_bus(flash_spi);
    if (result != (int)length)
    {
        return BOOT_APP_IO_ERROR;
    }
    return BOOT_APP_OK;
}

/*
 * The query commands use the running APP configuration.  Populate it from
 * the image header selected by BCB; the generic downloader intentionally does
 * not know which external slot is currently running.
 */
static void boot_ota_load_running_image_metadata(boot_app_config_t *config)
{
    uint8_t raw_header[BOOT_IMAGE_HEADER_SIZE];
    boot_control_status_t control;
    boot_image_info_t image;
    boot_slot_t running_slot = BOOT_SLOT_NONE;
    uint32_t expected_version;

    if ((config == RT_NULL) ||
        (boot_ota_read_boot_control(RT_NULL, &control) != BOOT_APP_OK))
    {
        return;
    }

    if (control.state == BOOT_CONTROL_TRIAL)
    {
        running_slot = control.pending_slot;
    }
    else if (control.state == BOOT_CONTROL_CONFIRMED)
    {
        running_slot = control.confirmed_slot;
    }
    else
    {
        /* EMPTY and unfinished states have no stable running metadata. */
        return;
    }

    expected_version = (control.state == BOOT_CONTROL_TRIAL) ?
                       control.pending_version : control.confirmed_version;

    if (!boot_control_is_slot_valid(running_slot) ||
        (boot_ota_storage_read(RT_NULL,
                               running_slot,
                               0U,
                               raw_header,
                               sizeof(raw_header)) != BOOT_APP_OK) ||
        !boot_image_header_decode(raw_header, &image) ||
        (image.target_address != config->target_address) ||
        (image.payload_size == 0U) ||
        (image.payload_size > config->image_max_size))
    {
        LOG_W("running image metadata unavailable, state=%u slot=%u",
              (unsigned)control.state,
              (unsigned)running_slot);
        return;
    }

    /* If BCB carries image identity, reject a mismatched/corrupt header. */
    if (((control.image_size != 0U) &&
         ((control.image_size != image.payload_size) ||
          (control.image_crc32 != image.payload_crc32))) ||
        ((expected_version != 0U) &&
         (expected_version != image.firmware_version)))
    {
        LOG_W("running image metadata mismatch, state=%u slot=%u",
              (unsigned)control.state,
              (unsigned)running_slot);
        return;
    }

    config->running_version = image.firmware_version;
    config->running_build_date = image.build_date;
    LOG_I("running image: slot=%u version=%lu date=0x%08lX size=%lu crc=0x%08lX",
          (unsigned)running_slot,
          (unsigned long)image.firmware_version,
          (unsigned long)image.build_date,
          (unsigned long)image.payload_size,
          (unsigned long)image.payload_crc32);
}

static boot_app_status_t boot_ota_mark_update_ready(void *context,
                                                     boot_slot_t pending_slot,
                                                     const boot_image_info_t *image)
{
    boot_control_status_t control;
    boot_control_storage_t storage;

    (void)context;
    if ((image == RT_NULL) || !boot_control_is_slot_valid(pending_slot))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    if (boot_ota_read_boot_control(RT_NULL, &control) != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }
    control.state = BOOT_CONTROL_UPDATE_READY;
    control.pending_slot = pending_slot;
    control.pending_version = image->firmware_version;
    control.image_size = image->payload_size;
    control.image_crc32 = image->payload_crc32;
    control.boot_attempts = 0U;
    control.max_boot_attempts = BOOT_OTA_MAX_BOOT_ATTEMPTS;
    control.last_error = 0U;

    storage = boot_ota_bcb_storage();
    return boot_control_append(&storage, &control) ? BOOT_APP_OK : BOOT_APP_IO_ERROR;
}

static boot_app_status_t boot_ota_mark_confirmed(void *context,
                                                  const boot_control_status_t *status)
{
    boot_control_status_t control;
    boot_control_storage_t storage;

    (void)context;
    /* generic app 已将 pending_slot 转移到 confirmed_slot 后再调用回调。 */
    if ((status == RT_NULL) || !boot_control_is_slot_valid(status->confirmed_slot))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    control = *status;
    control.state = BOOT_CONTROL_CONFIRMED;
    control.pending_slot = BOOT_SLOT_NONE;
    control.pending_version = 0U;
    control.boot_attempts = 0U;
    control.last_error = 0U;

    storage = boot_ota_bcb_storage();
    return boot_control_append(&storage, &control) ? BOOT_APP_OK : BOOT_APP_IO_ERROR;
}

static void boot_ota_system_reset(void *context)
{
    (void)context;
    NVIC_SystemReset();
}

static void boot_ota_log(void *context, const char *format, ...)
{
    char buffer[160];
    va_list args;

    (void)context;
    va_start(args, format);
    rt_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    LOG_I("%s", buffer);
}

static const boot_app_ops_t boot_ota_ops =
{
    RT_NULL,
    boot_ota_get_time_ms,
    boot_ota_transport_read,
    boot_ota_transport_write,
    boot_ota_read_boot_control,
    boot_ota_bcb_read,
    boot_ota_bcb_program,
    boot_ota_bcb_erase,
    boot_ota_storage_erase,
    boot_ota_storage_write,
    boot_ota_storage_read,
    boot_ota_mark_update_ready,
    boot_ota_mark_confirmed,
    boot_ota_system_reset,
    boot_ota_log,
};

void boot_ota_poll(void)
{
    boot_app_status_t status;

    if (!boot_ota_ctx.initialized)
    {
        return;
    }

    easy_bootloader_app_run();

    if (boot_ota_ctx.confirmation_pending &&
        !boot_ota_ctx.confirmation_complete &&
        ((rt_int32_t)(boot_ota_get_time_ms(RT_NULL) -
                      boot_ota_ctx.confirmation_due_ms) >= 0))
    {
        status = easy_bootloader_app_confirm_running();
        if ((status == BOOT_APP_OK) || (status == BOOT_APP_BUSY))
        {
            /* 无待确认版本时返回 BUSY，也不需要继续尝试。 */
            boot_ota_ctx.confirmation_complete = RT_TRUE;
        }
        else
        {
            boot_ota_ctx.confirmation_due_ms =
                boot_ota_get_time_ms(RT_NULL) + 1000U;
            LOG_W("running image confirmation failed: %d", (int)status);
        }
    }
}

rt_err_t boot_ota_init(void)
{
    boot_app_config_t config;
    boot_app_status_t status;

#if !BOOT_OTA_ENABLE_INSTALL
    LOG_W("OTA component is compiled but disabled until the Bootloader/app relocation is complete");
    return -RT_ENOSYS;
#endif

    if (boot_ota_ctx.initialized)
    {
        return RT_EOK;
    }

    if (fal_init() <= 0)
    {
        LOG_E("fal_init failed");
        return -RT_ERROR;
    }
    boot_ota_ctx.slot_a = fal_partition_find(BOOT_OTA_SLOT_A_NAME);
    boot_ota_ctx.slot_b = fal_partition_find(BOOT_OTA_SLOT_B_NAME);
    if ((boot_ota_ctx.slot_a == RT_NULL) || (boot_ota_ctx.slot_b == RT_NULL))
    {
        LOG_E("missing FAL partitions: %s/%s", BOOT_OTA_SLOT_A_NAME, BOOT_OTA_SLOT_B_NAME);
        return -RT_ERROR;
    }

    easy_bootloader_app_get_default_config(&config);
    config.auto_reset = 1U;
    boot_ota_load_running_image_metadata(&config);
    status = easy_bootloader_app_init(&config, &boot_ota_ops);
    if (status != BOOT_APP_OK)
    {
        LOG_E("easy_bootloader_app_init failed: %d", (int)status);
        return -RT_ERROR;
    }

    boot_ota_ctx.initialized = RT_TRUE;
    LOG_I("SPP OTA service ready: %s/%s", BOOT_OTA_SLOT_A_NAME, BOOT_OTA_SLOT_B_NAME);
    return RT_EOK;
}

rt_bool_t boot_ota_is_ready(void)
{
    return boot_ota_ctx.initialized;
}

boot_app_status_t boot_ota_confirm_running(void)
{
    if (!boot_ota_ctx.initialized)
    {
        return BOOT_APP_BUSY;
    }
    return easy_bootloader_app_confirm_running();
}

void boot_ota_schedule_confirmation(rt_uint32_t delay_ms)
{
    if (!boot_ota_ctx.initialized)
    {
        return;
    }

    boot_ota_ctx.confirmation_due_ms = boot_ota_get_time_ms(RT_NULL) + delay_ms;
    boot_ota_ctx.confirmation_complete = RT_FALSE;
    boot_ota_ctx.confirmation_pending = RT_TRUE;
}

void boot_ota_get_progress(boot_app_progress_t *progress)
{
    if (progress == RT_NULL)
    {
        return;
    }
    if (!boot_ota_ctx.initialized)
    {
        rt_memset(progress, 0, sizeof(*progress));
        progress->state = BOOT_APP_STATE_IDLE;
        progress->last_error = BOOT_APP_OK;
        progress->target_slot = BOOT_SLOT_NONE;
        return;
    }
    easy_bootloader_app_get_progress(progress);
}
