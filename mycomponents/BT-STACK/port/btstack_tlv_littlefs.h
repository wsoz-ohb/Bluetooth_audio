/* Persistent BTstack TLV storage backed by the mounted littlefs volume. */
#ifndef BTSTACK_TLV_LITTLEFS_H
#define BTSTACK_TLV_LITTLEFS_H

#include <rtthread.h>

#include "btstack_tlv.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Loads /btstack/link_keys.dat. The filesystem must already be mounted. */
rt_err_t btstack_tlv_littlefs_init(void);
const btstack_tlv_t *btstack_tlv_littlefs_instance(void);

#if defined(__cplusplus)
}
#endif

#endif /* BTSTACK_TLV_LITTLEFS_H */
