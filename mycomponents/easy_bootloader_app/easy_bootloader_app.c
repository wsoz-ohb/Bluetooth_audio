#include "easy_bootloader_app.h"

#include <stdbool.h>
#include <string.h>

#define FRAME_HEADER0               0x55U
#define FRAME_HEADER1               0xAAU
#define FRAME_TAIL0                 0x55U
#define FRAME_TAIL1                 0x55U
#define FRAME_FIXED_SIZE            11U
#define FRAME_PAYLOAD_MAX_SIZE      (BOOT_APP_PACKET_MAX_SIZE - FRAME_FIXED_SIZE)

#define COMMAND_FRAME_SIZE          6U
#define FINISH_FRAME_SIZE           14U
#define COMMAND_QUERY_VERSION0      0xFFU
#define COMMAND_QUERY_VERSION1      0xDDU
#define COMMAND_QUERY_DATE0         0xFFU
#define COMMAND_QUERY_DATE1         0xCCU
#define COMMAND_START0              0xFFU
#define COMMAND_START1              0xEEU
#define COMMAND_FINISH0             0xFFU
#define COMMAND_FINISH1             0xFDU

#define IMAGE_HEADER_COMMIT_OFFSET  60U

static const uint8_t app_ack[] =
{
    0x55U, 0xAAU, 0xFFU, 0xFEU, 0x55U, 0x55U
};

typedef struct
{
    boot_app_config_t config;
    const boot_app_ops_t *ops;
    boot_app_state_t state;
    boot_app_status_t last_error;
    uint8_t initialized;
    uint8_t storage_erased;
    boot_slot_t target_slot;
    uint32_t target_slot_size;

    uint8_t rx_cache[BOOT_APP_PACKET_MAX_SIZE];
    uint16_t rx_length;
    uint8_t payload[FRAME_PAYLOAD_MAX_SIZE];

    uint32_t expected_size;
    uint32_t received_size;
    uint32_t running_crc_state;
    uint32_t last_activity_ms;
} boot_app_context_t;

static boot_app_context_t app;

static int bcb_read_adapter(void *context,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length)
{
    (void)context;
    return (app.ops->bcb_read(app.ops->context, offset, data, length) == BOOT_APP_OK) ?
           0 : -1;
}

static int bcb_program_adapter(void *context,
                               uint32_t offset,
                               const uint8_t *data,
                               uint32_t length)
{
    (void)context;
    return (app.ops->bcb_program(app.ops->context, offset, data, length) == BOOT_APP_OK) ?
           0 : -1;
}

static int bcb_erase_adapter(void *context, uint32_t offset, uint32_t length)
{
    (void)context;
    return (app.ops->bcb_erase(app.ops->context, offset, length) == BOOT_APP_OK) ?
           0 : -1;
}

static boot_control_storage_t bcb_storage(void)
{
    boot_control_storage_t storage;

    storage.context = NULL;
    storage.read = bcb_read_adapter;
    storage.program = bcb_program_adapter;
    storage.erase = bcb_erase_adapter;
    storage.region_size = app.config.bcb_region_size;
    return storage;
}

#if BOOT_APP_CONFIG_ENABLE_LOG
#define APP_LOG(...)                                                         \
    do                                                                       \
    {                                                                        \
        if ((app.ops != NULL) && (app.ops->log != NULL))                    \
        {                                                                    \
            app.ops->log(app.ops->context, __VA_ARGS__);                    \
        }                                                                    \
    } while (0)
#else
#define APP_LOG(...) ((void)0)
#endif

static uint32_t get_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void put_u32_le(uint8_t data[4], uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    uint32_t remainder = value % alignment;
    return (remainder == 0U) ? value : (value + alignment - remainder);
}

static uint32_t now_ms(void)
{
    if ((app.ops != NULL) && (app.ops->get_time_ms != NULL))
    {
        return app.ops->get_time_ms(app.ops->context);
    }
    return 0U;
}

static void consume(uint16_t length)
{
    uint16_t remaining;

    if (length >= app.rx_length)
    {
        app.rx_length = 0U;
        return;
    }
    remaining = (uint16_t)(app.rx_length - length);
    memmove(app.rx_cache, &app.rx_cache[length], remaining);
    app.rx_length = remaining;
}

static void fail(boot_app_status_t error)
{
    app.state = BOOT_APP_STATE_ERROR;
    app.last_error = error;
    APP_LOG("update session failed: %d\r\n", (int)error);
}

static void reset_session(boot_app_state_t state)
{
    app.state = state;
    app.last_error = BOOT_APP_OK;
    app.storage_erased = 0U;
    app.target_slot = BOOT_SLOT_NONE;
    app.target_slot_size = 0U;
    app.expected_size = 0U;
    app.received_size = 0U;
    app.running_crc_state = 0xFFFFFFFFUL;
    app.last_activity_ms = now_ms();
}

static bool command_at_front(uint8_t command0, uint8_t command1)
{
    return (app.rx_length >= COMMAND_FRAME_SIZE) &&
           (app.rx_cache[0] == FRAME_HEADER0) &&
           (app.rx_cache[1] == FRAME_HEADER1) &&
           (app.rx_cache[2] == command0) &&
           (app.rx_cache[3] == command1) &&
           (app.rx_cache[4] == FRAME_TAIL0) &&
           (app.rx_cache[5] == FRAME_TAIL1);
}

static void send_text_u32(const char *prefix, uint32_t value)
{
    char output[32];
    char digits[10];
    uint32_t count = 0U;
    uint32_t index = 0U;

    while (prefix[index] != '\0')
    {
        output[index] = prefix[index];
        index++;
    }
    do
    {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));
    while (count > 0U)
    {
        output[index++] = digits[--count];
    }
    output[index++] = '\r';
    output[index++] = '\n';
    (void)app.ops->transport_write(app.ops->context,
                                   (const uint8_t *)output,
                                   index);
}

static void send_date(uint32_t date)
{
    uint8_t output[12];

    output[0] = (uint8_t)('0' + ((date >> 28) & 0x0FU));
    output[1] = (uint8_t)('0' + ((date >> 24) & 0x0FU));
    output[2] = (uint8_t)('0' + ((date >> 20) & 0x0FU));
    output[3] = (uint8_t)('0' + ((date >> 16) & 0x0FU));
    output[4] = '-';
    output[5] = (uint8_t)('0' + ((date >> 12) & 0x0FU));
    output[6] = (uint8_t)('0' + ((date >> 8) & 0x0FU));
    output[7] = '-';
    output[8] = (uint8_t)('0' + ((date >> 4) & 0x0FU));
    output[9] = (uint8_t)('0' + (date & 0x0FU));
    output[10] = '\r';
    output[11] = '\n';
    (void)app.ops->transport_write(app.ops->context, output, sizeof(output));
}

static bool extract_data_frame(uint32_t *remaining, uint16_t *payload_length)
{
    uint16_t length;
    uint32_t frame_size;
    uint32_t checksum_offset;
    uint16_t expected_checksum;
    uint16_t actual_checksum = 0U;
    uint32_t index;

    if (app.rx_length < FRAME_FIXED_SIZE)
    {
        return false;
    }
    if ((app.rx_cache[0] != FRAME_HEADER0) ||
        (app.rx_cache[1] != FRAME_HEADER1))
    {
        consume(1U);
        return false;
    }

    length = (uint16_t)(((uint16_t)app.rx_cache[5] << 8) |
                        (uint16_t)app.rx_cache[6]);
    if ((length == 0U) || (length > FRAME_PAYLOAD_MAX_SIZE))
    {
        consume(2U);
        fail(BOOT_APP_PROTOCOL_ERROR);
        return false;
    }
    frame_size = FRAME_FIXED_SIZE + length;
    if (app.rx_length < frame_size)
    {
        return false;
    }

    checksum_offset = 7U + length;
    expected_checksum = (uint16_t)(((uint16_t)app.rx_cache[checksum_offset] << 8) |
                                   app.rx_cache[checksum_offset + 1U]);
    /* 兼容原协议：校验和只覆盖长度与数据，不包含 remaining。 */
    for (index = 5U; index < checksum_offset; index++)
    {
        actual_checksum = (uint16_t)(actual_checksum + app.rx_cache[index]);
    }
    if ((actual_checksum != expected_checksum) ||
        (app.rx_cache[checksum_offset + 2U] != FRAME_TAIL0) ||
        (app.rx_cache[checksum_offset + 3U] != FRAME_TAIL1))
    {
        consume((uint16_t)frame_size);
        fail(BOOT_APP_PROTOCOL_ERROR);
        return false;
    }

    *remaining = ((uint32_t)app.rx_cache[2] << 16) |
                 ((uint32_t)app.rx_cache[3] << 8) |
                 app.rx_cache[4];
    *payload_length = length;
    memcpy(app.payload, &app.rx_cache[7], length);
    consume((uint16_t)frame_size);
    return true;
}

static boot_app_status_t select_target_slot(void)
{
    boot_control_status_t control;
    boot_app_status_t status;

    memset(&control, 0, sizeof(control));
    status = app.ops->read_boot_control(app.ops->context, &control);
    if (status != BOOT_APP_OK)
    {
        return status;
    }

    if ((control.state != BOOT_CONTROL_EMPTY) &&
        (control.state != BOOT_CONTROL_CONFIRMED))
    {
        return BOOT_APP_BUSY;
    }

    /* 新固件始终写入确认槽的另一槽，保留可回滚镜像。 */
    if (control.confirmed_slot == BOOT_SLOT_A)
    {
        app.target_slot = BOOT_SLOT_B;
        app.target_slot_size = app.config.slot_b_size;
    }
    else if (control.confirmed_slot == BOOT_SLOT_B)
    {
        app.target_slot = BOOT_SLOT_A;
        app.target_slot_size = app.config.slot_a_size;
    }
    else if (control.confirmed_slot == BOOT_SLOT_NONE)
    {
        /* 首次升级先下载 A，Bootloader 会把当前 APP 备份到 B。 */
        app.target_slot = BOOT_SLOT_A;
        app.target_slot_size = app.config.slot_a_size;
    }
    else
    {
        return BOOT_APP_PROTOCOL_ERROR;
    }

    return BOOT_APP_OK;
}

static boot_app_status_t prepare_storage(uint32_t total_size)
{
    uint32_t erase_length;

    if ((app.target_slot == BOOT_SLOT_NONE) ||
        (app.target_slot_size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (total_size == 0U) || (total_size > app.config.image_max_size) ||
        (total_size > (app.target_slot_size - BOOT_IMAGE_PAYLOAD_OFFSET)))
    {
        return BOOT_APP_OVERFLOW;
    }

    erase_length = align_up(BOOT_IMAGE_PAYLOAD_OFFSET + total_size,
                            app.config.erase_size);
    if ((erase_length < total_size) || (erase_length > app.target_slot_size))
    {
        return BOOT_APP_OVERFLOW;
    }
    if (app.ops->storage_erase(app.ops->context,
                               app.target_slot,
                               0U,
                               erase_length) != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }

    app.expected_size = total_size;
    app.storage_erased = 1U;
    APP_LOG("slot %u erased: %lu bytes\r\n",
            (unsigned)app.target_slot,
            (unsigned long)erase_length);
    return BOOT_APP_OK;
}

static boot_app_status_t handle_data_frame(uint32_t remaining,
                                           uint16_t payload_length)
{
    uint32_t total_from_frame;
    boot_app_status_t status;

    if ((app.state != BOOT_APP_STATE_WAIT_DATA) &&
        (app.state != BOOT_APP_STATE_RECEIVING))
    {
        return BOOT_APP_PROTOCOL_ERROR;
    }

    /* 每一包推导出的固件总长度都必须与首包一致。 */
    total_from_frame = app.received_size + payload_length + remaining;
    if (!app.storage_erased)
    {
        status = prepare_storage(total_from_frame);
        if (status != BOOT_APP_OK)
        {
            return status;
        }
    }
    if ((total_from_frame != app.expected_size) ||
        ((app.received_size + payload_length) > app.expected_size))
    {
        return BOOT_APP_PROTOCOL_ERROR;
    }

    status = app.ops->storage_write(app.ops->context,
                                    app.target_slot,
                                    BOOT_IMAGE_PAYLOAD_OFFSET + app.received_size,
                                    app.payload,
                                    payload_length);
    if (status != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }

    app.running_crc_state = boot_crc32_update(app.running_crc_state,
                                              app.payload,
                                              payload_length);
    app.received_size += payload_length;
    app.last_activity_ms = now_ms();
    app.state = (remaining == 0U) ? BOOT_APP_STATE_WAIT_FINISH
                                  : BOOT_APP_STATE_RECEIVING;

    status = app.ops->transport_write(app.ops->context, app_ack, sizeof(app_ack));
    return (status == BOOT_APP_OK) ? BOOT_APP_OK : BOOT_APP_IO_ERROR;
}

static boot_app_status_t verify_payload(uint32_t expected_crc)
{
    uint32_t offset = 0U;
    uint32_t crc_state = 0xFFFFFFFFUL;

    /* 从外部 Flash 回读整包，不能只相信接收过程中的 CRC。 */
    while (offset < app.received_size)
    {
        uint32_t chunk = app.received_size - offset;
        if (chunk > sizeof(app.payload))
        {
            chunk = sizeof(app.payload);
        }
        if (app.ops->storage_read(app.ops->context,
                                  app.target_slot,
                                  BOOT_IMAGE_PAYLOAD_OFFSET + offset,
                                  app.payload,
                                  chunk) != BOOT_APP_OK)
        {
            return BOOT_APP_IO_ERROR;
        }
        crc_state = boot_crc32_update(crc_state, app.payload, chunk);
        offset += chunk;
    }
    return (boot_crc32_finish(crc_state) == expected_crc)
               ? BOOT_APP_OK
               : BOOT_APP_VERIFY_ERROR;
}

static boot_app_status_t commit_image(uint32_t version, uint32_t build_date)
{
    uint8_t raw_header[BOOT_IMAGE_HEADER_SIZE];
    uint8_t commit_word[4];
    boot_image_info_t image;
    boot_image_info_t decoded;
    boot_control_storage_t control_storage;
    boot_app_status_t status;

    memset(&image, 0, sizeof(image));
    image.target_address = app.config.target_address;
    image.firmware_version = version;
    image.build_date = build_date;
    image.payload_offset = BOOT_IMAGE_PAYLOAD_OFFSET;
    image.payload_size = app.received_size;
    image.payload_crc32 = boot_crc32_finish(app.running_crc_state);

    status = verify_payload(image.payload_crc32);
    if (status != BOOT_APP_OK)
    {
        return status;
    }

    boot_image_header_encode(&image, raw_header);
    /* 镜像头提交标记最后写，写到一半的镜像不会被采用。 */
    status = app.ops->storage_write(app.ops->context,
                                    app.target_slot,
                                    0U,
                                    raw_header,
                                    IMAGE_HEADER_COMMIT_OFFSET);
    if (status != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }

    put_u32_le(commit_word, BOOT_IMAGE_COMMIT_MARKER);
    status = app.ops->storage_write(app.ops->context,
                                    app.target_slot,
                                    IMAGE_HEADER_COMMIT_OFFSET,
                                    commit_word,
                                    sizeof(commit_word));
    if (status != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }

    if ((app.ops->storage_read(app.ops->context,
                               app.target_slot,
                               0U,
                               raw_header,
                               sizeof(raw_header)) != BOOT_APP_OK) ||
        !boot_image_header_decode(raw_header, &decoded) ||
        (decoded.target_address != image.target_address) ||
        (decoded.payload_size != image.payload_size) ||
        (decoded.payload_crc32 != image.payload_crc32))
    {
        return BOOT_APP_VERIFY_ERROR;
    }

    image.header_crc32 = decoded.header_crc32;
    control_storage = bcb_storage();
    if (boot_control_free_record_count(&control_storage) <
        BOOT_CONTROL_UPDATE_RECORD_RESERVE)
    {
        return BOOT_APP_BUSY;
    }
    status = app.ops->mark_update_ready(app.ops->context,
                                        app.target_slot,
                                        &image);
    if (status != BOOT_APP_OK)
    {
        return status;
    }

    app.state = BOOT_APP_STATE_READY;
    app.last_error = BOOT_APP_OK;
    status = app.ops->transport_write(app.ops->context, app_ack, sizeof(app_ack));
    if (status != BOOT_APP_OK)
    {
        return BOOT_APP_IO_ERROR;
    }

    APP_LOG("slot %u ready: size=%lu crc=0x%08lX version=%lu\r\n",
            (unsigned)app.target_slot,
            (unsigned long)image.payload_size,
            (unsigned long)image.payload_crc32,
            (unsigned long)image.firmware_version);
    if (app.config.auto_reset && (app.ops->system_reset != NULL))
    {
        app.ops->system_reset(app.ops->context);
    }
    return BOOT_APP_OK;
}

static bool extract_finish_frame(uint32_t *version, uint32_t *build_date)
{
    if (app.rx_length < FINISH_FRAME_SIZE)
    {
        return false;
    }
    if ((app.rx_cache[0] != FRAME_HEADER0) ||
        (app.rx_cache[1] != FRAME_HEADER1) ||
        (app.rx_cache[10] != COMMAND_FINISH0) ||
        (app.rx_cache[11] != COMMAND_FINISH1) ||
        (app.rx_cache[12] != FRAME_TAIL0) ||
        (app.rx_cache[13] != FRAME_TAIL1))
    {
        consume(1U);
        return false;
    }
    *version = get_u32_be(&app.rx_cache[2]);
    *build_date = get_u32_be(&app.rx_cache[6]);
    consume(FINISH_FRAME_SIZE);
    return true;
}

static void process_idle_commands(void)
{
    while (app.rx_length >= COMMAND_FRAME_SIZE)
    {
        if (command_at_front(COMMAND_QUERY_VERSION0, COMMAND_QUERY_VERSION1))
        {
            consume(COMMAND_FRAME_SIZE);
            send_text_u32("version:", app.config.running_version);
        }
        else if (command_at_front(COMMAND_QUERY_DATE0, COMMAND_QUERY_DATE1))
        {
            consume(COMMAND_FRAME_SIZE);
            send_date(app.config.running_build_date);
        }
        else if (command_at_front(COMMAND_START0, COMMAND_START1))
        {
            boot_app_status_t status;

            consume(COMMAND_FRAME_SIZE);
            reset_session(BOOT_APP_STATE_WAIT_DATA);
            status = select_target_slot();
            if (status != BOOT_APP_OK)
            {
                fail(status);
                return;
            }
            (void)app.ops->transport_write(app.ops->context,
                                           app_ack,
                                           sizeof(app_ack));
            APP_LOG("update session started, target slot=%u\r\n",
                    (unsigned)app.target_slot);
            return;
        }
        else
        {
            consume(1U);
        }
    }
}

void easy_bootloader_app_get_default_config(boot_app_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->target_address = BOOT_APP_DEFAULT_TARGET_ADDRESS;
    config->image_max_size = BOOT_APP_DEFAULT_IMAGE_MAX_SIZE;
    config->slot_a_size = BOOT_APP_DEFAULT_SLOT_A_SIZE;
    config->slot_b_size = BOOT_APP_DEFAULT_SLOT_B_SIZE;
    config->erase_size = BOOT_APP_DEFAULT_ERASE_SIZE;
    config->bcb_region_size = BOOT_APP_DEFAULT_BCB_REGION_SIZE;
    config->session_timeout_ms = BOOT_APP_DEFAULT_TIMEOUT_MS;
    config->auto_reset = 1U;
}

boot_app_status_t easy_bootloader_app_init(const boot_app_config_t *config,
                                           const boot_app_ops_t *ops)
{
    if ((config == NULL) || (ops == NULL) ||
        (ops->transport_read == NULL) || (ops->transport_write == NULL) ||
        (ops->read_boot_control == NULL) ||
        (ops->bcb_read == NULL) || (ops->bcb_program == NULL) ||
        (ops->bcb_erase == NULL) ||
        (ops->storage_erase == NULL) || (ops->storage_write == NULL) ||
        (ops->storage_read == NULL) || (ops->mark_update_ready == NULL) ||
        (ops->mark_confirmed == NULL) ||
        (config->erase_size == 0U) || (config->image_max_size == 0U) ||
        (config->bcb_region_size < BOOT_CONTROL_RECORD_SIZE) ||
        ((config->bcb_region_size % BOOT_CONTROL_RECORD_SIZE) != 0U) ||
        (config->slot_a_size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (config->slot_b_size <= BOOT_IMAGE_PAYLOAD_OFFSET) ||
        (config->image_max_size >
         (config->slot_a_size - BOOT_IMAGE_PAYLOAD_OFFSET)) ||
        (config->image_max_size >
         (config->slot_b_size - BOOT_IMAGE_PAYLOAD_OFFSET)))
    {
        return BOOT_APP_INVALID_ARGUMENT;
    }

    memset(&app, 0, sizeof(app));
    app.config = *config;
    app.ops = ops;
    app.initialized = 1U;
    reset_session(BOOT_APP_STATE_IDLE);
    APP_LOG("easy bootloader app downloader ready\r\n");
    return BOOT_APP_OK;
}

void easy_bootloader_app_run(void)
{
    uint32_t space;
    uint32_t received;

    if (!app.initialized)
    {
        return;
    }

    space = sizeof(app.rx_cache) - app.rx_length;
    if (space > 0U)
    {
        received = app.ops->transport_read(app.ops->context,
                                           &app.rx_cache[app.rx_length],
                                           space);
        if (received > space)
        {
            fail(BOOT_APP_OVERFLOW);
            return;
        }
        if (received > 0U)
        {
            app.rx_length = (uint16_t)(app.rx_length + received);
            app.last_activity_ms = now_ms();
        }
    }

    if ((app.state == BOOT_APP_STATE_IDLE) ||
        (app.state == BOOT_APP_STATE_READY) ||
        (app.state == BOOT_APP_STATE_ERROR))
    {
        process_idle_commands();
        return;
    }

    if ((app.config.session_timeout_ms != 0U) &&
        (app.ops->get_time_ms != NULL) &&
        ((uint32_t)(now_ms() - app.last_activity_ms) >=
         app.config.session_timeout_ms))
    {
        fail(BOOT_APP_TIMEOUT);
        return;
    }

    if (app.state == BOOT_APP_STATE_WAIT_FINISH)
    {
        uint32_t version;
        uint32_t build_date;
        if (extract_finish_frame(&version, &build_date))
        {
            boot_app_status_t status = commit_image(version, build_date);
            if (status != BOOT_APP_OK)
            {
                fail(status);
            }
        }
        return;
    }

    while ((app.state == BOOT_APP_STATE_WAIT_DATA) ||
           (app.state == BOOT_APP_STATE_RECEIVING))
    {
        uint32_t remaining;
        uint16_t payload_length;
        boot_app_status_t status;

        if (!extract_data_frame(&remaining, &payload_length))
        {
            break;
        }
        status = handle_data_frame(remaining, payload_length);
        if (status != BOOT_APP_OK)
        {
            fail(status);
            break;
        }
    }
}

void easy_bootloader_app_abort(void)
{
    if (app.initialized)
    {
        app.rx_length = 0U;
        reset_session(BOOT_APP_STATE_IDLE);
    }
}

void easy_bootloader_app_get_progress(boot_app_progress_t *progress)
{
    if (progress == NULL)
    {
        return;
    }
    progress->state = app.state;
    progress->last_error = app.last_error;
    progress->received_size = app.received_size;
    progress->expected_size = app.expected_size;
    progress->payload_crc32 = boot_crc32_finish(app.running_crc_state);
    progress->target_slot = app.target_slot;
}

boot_app_status_t easy_bootloader_app_confirm_running(void)
{
    boot_control_status_t control;
    boot_app_status_t status;

    if ((!app.initialized) || (app.ops == NULL))
    {
        return BOOT_APP_ERROR;
    }

    memset(&control, 0, sizeof(control));
    status = app.ops->read_boot_control(app.ops->context, &control);
    if (status != BOOT_APP_OK)
    {
        return status;
    }
    if (control.state == BOOT_CONTROL_CONFIRMED)
    {
        return BOOT_APP_OK;
    }
    if ((control.state != BOOT_CONTROL_TRIAL) ||
        !boot_control_is_slot_valid(control.pending_slot) ||
        (control.image_size == 0U))
    {
        return BOOT_APP_BUSY;
    }

    /* 试运行确认后，pending_slot 才成为新的 confirmed_slot。 */
    control.state = BOOT_CONTROL_CONFIRMED;
    control.confirmed_slot = control.pending_slot;
    control.confirmed_version = control.pending_version;
    control.pending_slot = BOOT_SLOT_NONE;
    control.pending_version = 0U;
    control.boot_attempts = 0U;
    control.max_boot_attempts = 0U;
    control.last_error = 0U;
    return app.ops->mark_confirmed(app.ops->context, &control);
}
