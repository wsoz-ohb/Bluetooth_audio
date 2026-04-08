#ifndef BTSTACK_RUN_LOOP_EMBEDDED_H
#define BTSTACK_RUN_LOOP_EMBEDDED_H

#include "btstack_run_loop.h"

#if defined __cplusplus
extern "C" {
#endif

extern const btstack_run_loop_t btstack_run_loop_embedded;

const btstack_run_loop_t * btstack_run_loop_embedded_get_instance(void);

#if defined __cplusplus
}
#endif

#endif /* BTSTACK_RUN_LOOP_EMBEDDED_H */
