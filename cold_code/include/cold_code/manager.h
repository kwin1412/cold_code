#ifndef COLD_CODE_MANAGER_H
#define COLD_CODE_MANAGER_H

#include "cold_code/fault.h"
#include "cold_code/image.h"
#include "cold_code/loader.h"
#include "cold_code/port.h"
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
    cold_code_loader_t loader;   /* load_fptr + load_ctx + image_size */
    void *exec_addr;             /* caller-provided executable RAM */
    size_t exec_size;            /* bytes available at exec_addr */
    uint32_t cpuid;              /* CPU index bound by manager_init */
    const cold_code_context_ops_t *context; /* platform arch/fatal/sync */
    uint32_t pool_slots;         /* 0 => image max_pool_slots */
    uint32_t expected_image_id;  /* checked when flags has EXPECT_IMAGE_ID */
    uint32_t flags;              /* COLD_CODE_INIT_VERIFY_CRC | EXPECT_IMAGE_ID */
    cold_code_trace_t trace;
    cold_code_fault_t fault;
} cold_code_manager_config_t;

/*
 * Read metadata from the wire image header without claiming executable memory.
 * This is the sizing phase: use the returned slot_size/slot_count/
 * max_pool_slots/storage_align with COLD_CODE_STORAGE_SIZE/
 * cold_code_storage_required_*, then pass an exec_addr/exec_size to
 * cold_code_manager_init().  The loader must be readable again by init.
 *
 * Probe validates wire form, geometry and payload range.  It does not check
 * the local context arch/ABI, does not verify the payload CRC, and does not
 * reserve storage.  init re-reads and revalidates the header.
 */
cold_code_status_t cold_code_manager_probe(const cold_code_loader_t *loader,
                                           cold_code_image_info_t *out);

/*
 * Initialize a manager by reading metadata through config->loader and binding
 * it to config->cpuid in the core's per-CPU table.  The manager must be
 * zero-initialized.  config->context must outlive the manager; config->loader
 * is copied and its load_ctx must outlive the manager.  exec_addr must point
 * to executable RAM of at least exec_size bytes, aligned per the image header.
 *
 * If flags contains COLD_CODE_INIT_EXPECT_IMAGE_ID, expected_image_id must
 * match the image's build fingerprint.  Returns COLD_CODE_ERR_ARG for an
 * invalid loader/exec/context/cpuid, COLD_CODE_ERR_STATE for a repeated init,
 * an occupied cpuid, or storage owned by another manager, COLD_CODE_ERR_SIZE
 * for too-small/misaligned exec memory, and COLD_CODE_ERR_ARCH/UNSUPPORTED for
 * arch/ABI mismatches.
 */
cold_code_status_t cold_code_manager_init(cold_code_manager_t *manager,
                                          const cold_code_manager_config_t *config);

/*
 * Unbind the manager from its cpuid and release the exec storage.  The loader
 * is caller-owned and is not closed.  Returns ERR_STATE if not initialized.
 */
cold_code_status_t cold_code_manager_deinit(cold_code_manager_t *manager);

/* Unload all slots and clear counters; keep image/loader/exec storage claimed. */
cold_code_status_t cold_code_manager_reset(cold_code_manager_t *manager);

bool cold_code_manager_is_initialized(const cold_code_manager_t *manager);

cold_code_status_t cold_code_manager_stats(const cold_code_manager_t *manager,
                                           cold_code_stats_t *out);

/*
 * Read-only introspection used by diagnostics and tests.  The returned pointer
 * is a manager-owned runtime view populated from the wire header; name is NULL.
 * It is valid until the manager is deinitialized.  Do not write through the
 * returned pointer: it lives inside the writable manager block.
 */
const cold_code_image_t *cold_code_manager_image(const cold_code_manager_t *manager);
void *cold_code_manager_pool_base(const cold_code_manager_t *manager);
uint32_t cold_code_manager_pool_slots(const cold_code_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_MANAGER_H */
