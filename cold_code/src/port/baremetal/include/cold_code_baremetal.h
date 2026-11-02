#ifndef COLD_CODE_BAREMETAL_H
#define COLD_CODE_BAREMETAL_H

#include "cold_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef COLD_CODE_BAREMETAL_MAX_CORES
#define COLD_CODE_BAREMETAL_MAX_CORES 8u
#endif

typedef struct {
    uint32_t core_count;
    void (*fatal)(void *ctx, const char *message);
    void *fatal_ctx;
    void (*sync_code)(void *ctx, void *addr, size_t size);
    void *sync_code_ctx;
} cold_code_baremetal_config_t;

/*
 * Install the per-core context backend.  Call before initializing managers.
 * cpu_id() is a weak symbol returning 0; provide a strong definition when the
 * target has more than one core.
 */
void cold_code_baremetal_init(const cold_code_baremetal_config_t *config);
unsigned cold_code_baremetal_cpu_id(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_BAREMETAL_H */
