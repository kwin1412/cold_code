#ifndef COLD_CODE_MANAGER_H
#define COLD_CODE_MANAGER_H

#include "cold_code/fault.h"
#include "cold_code/image.h"
#include "cold_code/source.h"
#include "cold_code/stats.h"
#include "cold_code/status.h"
#include "cold_code/trace.h"
#include "cold_code/types.h"
#include "cold_code/version.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque, statically allocatable manager storage.  Declare it static (zero
 * initialized) and treat the bytes as private to the core.
 */
struct cold_code_manager {
    unsigned char _opaque[COLD_CODE_MANAGER_BYTES];
} __attribute__((aligned(COLD_CODE_MANAGER_ALIGN)));

typedef struct {
    const cold_code_image_t *image;
    void *storage;       /* caller-provided executable RAM storage */
    size_t storage_size; /* bytes available at storage */
    uint32_t pool_slots; /* 0 => image->max_pool_slots; must not exceed it */
    cold_code_source_t source;
    uint32_t flags; /* COLD_CODE_INIT_VERIFY_CRC */
    cold_code_trace_t trace;
    cold_code_fault_t fault;
} cold_code_manager_config_t;

/*
 * Initialize a manager.  The manager must be zero-initialized and the storage
 * must be large enough and aligned as required by the image.  Returns
 * COLD_CODE_ERR_STATE for a repeated init or a storage owned by another
 * manager, COLD_CODE_ERR_SIZE for a too-small/misaligned storage, and
 * COLD_CODE_ERR_ARCH/UNSUPPORTED for arch/ABI mismatches.
 */
cold_code_status_t cold_code_manager_init(cold_code_manager_t *manager,
                                          const cold_code_manager_config_t *config);

/* Close the source and release the storage.  Returns ERR_STATE if not initialized. */
cold_code_status_t cold_code_manager_deinit(cold_code_manager_t *manager);

/* Unload all slots and clear counters; keep image/source/storage claimed. */
cold_code_status_t cold_code_manager_reset(cold_code_manager_t *manager);

bool cold_code_manager_is_initialized(const cold_code_manager_t *manager);

cold_code_status_t cold_code_manager_stats(const cold_code_manager_t *manager,
                                           cold_code_stats_t *out);

/* Read-only introspection used by diagnostics and tests. */
const cold_code_image_t *cold_code_manager_image(const cold_code_manager_t *manager);
void *cold_code_manager_pool_base(const cold_code_manager_t *manager);
uint32_t cold_code_manager_pool_slots(const cold_code_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_MANAGER_H */
