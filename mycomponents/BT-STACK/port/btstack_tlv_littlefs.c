#include "btstack_tlv_littlefs.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bt_config.h"
#include "btstack_debug.h"

#define BTSTACK_TLV_FILE              "/btstack/link_keys.dat"
#define BTSTACK_TLV_TMP_FILE          "/btstack/link_keys.tmp"
#define BTSTACK_TLV_MAGIC             "BTLF"
#define BTSTACK_TLV_VERSION           1u
#define BTSTACK_TLV_MAX_VALUE         64u
#define BTSTACK_TLV_RECORD_CAPACITY   BT_CFG_NVM_NUM_LINK_KEYS

typedef struct __attribute__((packed))
{
    uint8_t magic[4];
    uint8_t version;
    uint8_t reserved;
    uint16_t record_count;
    uint32_t payload_crc;
    uint32_t reserved2;
} btstack_tlv_file_header_t;

typedef struct __attribute__((packed))
{
    uint32_t tag;
    uint16_t length;
    uint16_t reserved;
    uint8_t value[BTSTACK_TLV_MAX_VALUE];
} btstack_tlv_file_record_t;

typedef struct
{
    rt_bool_t initialized;
    struct rt_mutex lock;
    btstack_tlv_file_record_t records[BTSTACK_TLV_RECORD_CAPACITY];
} btstack_tlv_littlefs_context_t;

static btstack_tlv_littlefs_context_t btstack_tlv_littlefs_ctx;

static uint32_t btstack_tlv_crc32(const uint8_t *data, size_t length)
{
    uint32_t state = 0xFFFFFFFFu;
    size_t i;
    int bit;

    for (i = 0; i < length; i++)
    {
        state ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            state = (state >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(state & 1u));
        }
    }
    return ~state;
}

static ssize_t btstack_tlv_read_all(int fd, void *buffer, size_t length)
{
    uint8_t *data = (uint8_t *)buffer;
    size_t offset = 0;
    ssize_t count;

    while (offset < length)
    {
        count = read(fd, data + offset, length - offset);
        if (count <= 0)
        {
            return -1;
        }
        offset += (size_t)count;
    }
    return (ssize_t)offset;
}

static int btstack_tlv_write_all(int fd, const void *buffer, size_t length)
{
    const uint8_t *data = (const uint8_t *)buffer;
    size_t offset = 0;
    ssize_t count;

    while (offset < length)
    {
        count = write(fd, data + offset, length - offset);
        if (count <= 0)
        {
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

static uint16_t btstack_tlv_record_count(void)
{
    uint16_t count = 0;
    uint32_t i;

    for (i = 0; i < BTSTACK_TLV_RECORD_CAPACITY; i++)
    {
        if (btstack_tlv_littlefs_ctx.records[i].length != 0u)
        {
            count++;
        }
    }
    return count;
}

static int btstack_tlv_littlefs_save_locked(void)
{
    btstack_tlv_file_header_t header;
    int fd;
    int result;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, BTSTACK_TLV_MAGIC, sizeof(header.magic));
    header.version = BTSTACK_TLV_VERSION;
    header.record_count = btstack_tlv_record_count();
    header.payload_crc = btstack_tlv_crc32(
        (const uint8_t *)btstack_tlv_littlefs_ctx.records,
        sizeof(btstack_tlv_littlefs_ctx.records));

    fd = open(BTSTACK_TLV_TMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
    {
        log_error("BTstack TLV open temp failed, errno=%d", rt_get_errno());
        return -1;
    }

    result = btstack_tlv_write_all(fd, &header, sizeof(header));
    if (result == 0)
    {
        result = btstack_tlv_write_all(fd,
                                       btstack_tlv_littlefs_ctx.records,
                                       sizeof(btstack_tlv_littlefs_ctx.records));
    }
    if (close(fd) != 0)
    {
        result = -1;
    }
    if (result != 0)
    {
        (void)unlink(BTSTACK_TLV_TMP_FILE);
        log_error("BTstack TLV write temp failed, errno=%d", rt_get_errno());
        return -1;
    }

    if (rename(BTSTACK_TLV_TMP_FILE, BTSTACK_TLV_FILE) != 0)
    {
        (void)unlink(BTSTACK_TLV_TMP_FILE);
        log_error("BTstack TLV rename failed, errno=%d", rt_get_errno());
        return -1;
    }
    return 0;
}

static int btstack_tlv_littlefs_load(void)
{
    btstack_tlv_file_header_t header;
    int fd;
    struct stat st;
    uint32_t i;
    rt_bool_t valid = RT_TRUE;

    /* RT-Thread DFS stores errno as a negative error code. */
    if (mkdir("/btstack", 0700) != 0 && rt_get_errno() != -EEXIST)
    {
        log_error("BTstack TLV mkdir failed, errno=%d", rt_get_errno());
        return -1;
    }

    fd = open(BTSTACK_TLV_FILE, O_RDONLY, 0);
    if (fd < 0)
    {
        /* A missing file is the normal first-boot case. */
        if (rt_get_errno() == -ENOENT)
        {
            log_info("BTstack TLV file not found, starting empty link-key store");
            return 0;
        }
        log_error("BTstack TLV open failed, errno=%d", rt_get_errno());
        return -1;
    }

    if (fstat(fd, &st) != 0 ||
        (size_t)st.st_size != sizeof(header) + sizeof(btstack_tlv_littlefs_ctx.records) ||
        btstack_tlv_read_all(fd, &header, sizeof(header)) < 0 ||
        btstack_tlv_read_all(fd,
                             btstack_tlv_littlefs_ctx.records,
                             sizeof(btstack_tlv_littlefs_ctx.records)) < 0)
    {
        log_error("BTstack TLV read file failed, errno=%d", rt_get_errno());
        close(fd);
        return -1;
    }
    close(fd);

    if (memcmp(header.magic, BTSTACK_TLV_MAGIC, sizeof(header.magic)) != 0 ||
        header.version != BTSTACK_TLV_VERSION ||
        header.record_count > BTSTACK_TLV_RECORD_CAPACITY ||
        header.payload_crc != btstack_tlv_crc32(
            (const uint8_t *)btstack_tlv_littlefs_ctx.records,
            sizeof(btstack_tlv_littlefs_ctx.records)) ||
        header.record_count != btstack_tlv_record_count())
    {
        valid = RT_FALSE;
    }
    for (i = 0; valid && i < BTSTACK_TLV_RECORD_CAPACITY; i++)
    {
        if (btstack_tlv_littlefs_ctx.records[i].length > BTSTACK_TLV_MAX_VALUE)
        {
            valid = RT_FALSE;
        }
    }
    if (!valid)
    {
        /* Ignore a torn/old file and start with an empty persistent store. */
        memset(btstack_tlv_littlefs_ctx.records, 0,
               sizeof(btstack_tlv_littlefs_ctx.records));
        log_error("BTstack TLV file invalid, starting with empty link-key store");
    }
    else
    {
        log_info("BTstack TLV loaded %u link-key record(s)",
                 (unsigned int)btstack_tlv_record_count());
    }
    return 0;
}

static int btstack_tlv_littlefs_get_tag(void *context,
                                        uint32_t tag,
                                        uint8_t *buffer,
                                        uint32_t buffer_size)
{
    uint32_t i;
    (void)context;

    if (!btstack_tlv_littlefs_ctx.initialized || buffer == NULL)
    {
        return 0;
    }

    rt_mutex_take(&btstack_tlv_littlefs_ctx.lock, RT_WAITING_FOREVER);
    for (i = 0; i < BTSTACK_TLV_RECORD_CAPACITY; i++)
    {
        btstack_tlv_file_record_t *record = &btstack_tlv_littlefs_ctx.records[i];
        if (record->length != 0u && record->tag == tag)
        {
            if (buffer_size < record->length)
            {
                rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
                return 0;
            }
            memcpy(buffer, record->value, record->length);
            buffer_size = record->length;
            rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
            log_info("BTstack TLV get tag %08x hit, len %u", (unsigned int)tag,
                     (unsigned int)buffer_size);
            return (int)buffer_size;
        }
    }
    rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
    log_info("BTstack TLV get tag %08x miss", (unsigned int)tag);
    return 0;
}

static int btstack_tlv_littlefs_store_tag(void *context,
                                          uint32_t tag,
                                          const uint8_t *data,
                                          uint32_t data_size)
{
    uint32_t i;
    uint32_t slot = BTSTACK_TLV_RECORD_CAPACITY;
    int result;
    (void)context;

    if (!btstack_tlv_littlefs_ctx.initialized || data == NULL ||
        data_size == 0u || data_size > BTSTACK_TLV_MAX_VALUE)
    {
        return -1;
    }

    rt_mutex_take(&btstack_tlv_littlefs_ctx.lock, RT_WAITING_FOREVER);
    for (i = 0; i < BTSTACK_TLV_RECORD_CAPACITY; i++)
    {
        if (btstack_tlv_littlefs_ctx.records[i].length != 0u &&
            btstack_tlv_littlefs_ctx.records[i].tag == tag)
        {
            slot = i;
            break;
        }
        if (slot == BTSTACK_TLV_RECORD_CAPACITY &&
            btstack_tlv_littlefs_ctx.records[i].length == 0u)
        {
            slot = i;
        }
    }

    if (slot == BTSTACK_TLV_RECORD_CAPACITY)
    {
        rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
        log_error("BTstack TLV store tag %08x failed: store full", (unsigned int)tag);
        return -1;
    }

    memset(&btstack_tlv_littlefs_ctx.records[slot], 0,
           sizeof(btstack_tlv_littlefs_ctx.records[slot]));
    btstack_tlv_littlefs_ctx.records[slot].tag = tag;
    btstack_tlv_littlefs_ctx.records[slot].length = (uint16_t)data_size;
    memcpy(btstack_tlv_littlefs_ctx.records[slot].value, data, data_size);
    result = btstack_tlv_littlefs_save_locked();
    rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
    if (result != 0)
    {
        log_error("BTstack TLV store tag %08x failed, errno=%d", (unsigned int)tag,
                  rt_get_errno());
    }
    else
    {
        log_info("BTstack TLV store tag %08x ok, len %u", (unsigned int)tag,
                 (unsigned int)data_size);
    }
    return result;
}

static void btstack_tlv_littlefs_delete_tag(void *context, uint32_t tag)
{
    uint32_t i;
    (void)context;

    if (!btstack_tlv_littlefs_ctx.initialized)
    {
        return;
    }

    rt_mutex_take(&btstack_tlv_littlefs_ctx.lock, RT_WAITING_FOREVER);
    for (i = 0; i < BTSTACK_TLV_RECORD_CAPACITY; i++)
    {
        if (btstack_tlv_littlefs_ctx.records[i].length != 0u &&
            btstack_tlv_littlefs_ctx.records[i].tag == tag)
        {
            memset(&btstack_tlv_littlefs_ctx.records[i], 0,
                   sizeof(btstack_tlv_littlefs_ctx.records[i]));
            if (btstack_tlv_littlefs_save_locked() != 0)
            {
                log_error("BTstack TLV delete tag %08x failed, errno=%d", (unsigned int)tag,
                          rt_get_errno());
            }
            else
            {
                log_info("BTstack TLV delete tag %08x ok", (unsigned int)tag);
            }
            break;
        }
    }
    rt_mutex_release(&btstack_tlv_littlefs_ctx.lock);
}

static const btstack_tlv_t btstack_tlv_littlefs = {
    btstack_tlv_littlefs_get_tag,
    btstack_tlv_littlefs_store_tag,
    btstack_tlv_littlefs_delete_tag,
};

rt_err_t btstack_tlv_littlefs_init(void)
{
    rt_err_t err;

    if (btstack_tlv_littlefs_ctx.initialized)
    {
        return RT_EOK;
    }

    memset(&btstack_tlv_littlefs_ctx, 0, sizeof(btstack_tlv_littlefs_ctx));
    err = rt_mutex_init(&btstack_tlv_littlefs_ctx.lock, "bttlv", RT_IPC_FLAG_FIFO);
    if (err != RT_EOK)
    {
        return err;
    }

    if (btstack_tlv_littlefs_load() != 0)
    {
        rt_mutex_detach(&btstack_tlv_littlefs_ctx.lock);
        memset(&btstack_tlv_littlefs_ctx, 0, sizeof(btstack_tlv_littlefs_ctx));
        return -RT_ERROR;
    }

    btstack_tlv_littlefs_ctx.initialized = RT_TRUE;
    return RT_EOK;
}

const btstack_tlv_t *btstack_tlv_littlefs_instance(void)
{
    return &btstack_tlv_littlefs;
}
