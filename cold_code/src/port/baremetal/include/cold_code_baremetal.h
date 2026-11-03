#ifndef COLD_CODE_BAREMETAL_H
#define COLD_CODE_BAREMETAL_H

#include "cold_code.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*fatal)(void *ctx, const char *message);
    void *fatal_ctx;
    void (*sync_code)(void *ctx, void *addr, size_t size);
    void *sync_code_ctx;
} cold_code_baremetal_config_t;

/*
 * Install fatal/cache-sync hooks used by cold_code_baremetal_context_ops.
 * Call before initializing managers.  The target must also provide a strong
 * cold_code_cpu_id() (for example reading MPIDR/COREID); the core weak default
 * is 0.  A CPU must return a stable id below COLD_CODE_MAX_CPUS.
 */
void cold_code_baremetal_init(const cold_code_baremetal_config_t *config);

/* Pass &cold_code_baremetal_context_ops in manager_config.context. */
extern const cold_code_context_ops_t cold_code_baremetal_context_ops;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_BAREMETAL_H */
