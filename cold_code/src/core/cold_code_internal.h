#ifndef COLD_CODE_INTERNAL_H
#define COLD_CODE_INTERNAL_H

#include "cold_code.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_INVALID_SLOT UINT32_MAX
#define COLD_CODE_INVALID_WAY UINT32_MAX

#define COLD_CODE_CRC32_INIT 0xFFFFFFFFu
#define COLD_CODE_MANAGER_MAGIC 0x434d4c43u /* bytes "CLMC" (little-endian) */

typedef struct {
    size_t pool_offset;
    size_t slot_map_offset;
    size_t way_slot_offset;
    size_t way_used_offset;
    size_t total_size;
} cold_code_storage_layout_t;

/*
 * The implementation starts with the versioned fast-state ABI block.  Nothing
 * after it is stable across versions.
 */
struct cold_code_manager_impl {
    cold_code_fast_state_t fast;

    cold_code_storage_header_t *storage;
    uint32_t *way_slot;

    const cold_code_source_ops_t *source_ops;
    void *source_ctx;

    uint32_t slot_count;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t flags;
    uint32_t manager_magic;

    cold_code_trace_t trace;
    cold_code_fault_t fault;

    uint64_t loads;
    uint64_t evictions;
    uint64_t misses;
    uint64_t faults;
};

_Static_assert(offsetof(struct cold_code_manager_impl, fast) == 0u,
               "fast-state ABI must start at manager offset 0");
_Static_assert(sizeof(struct cold_code_manager_impl) <= COLD_CODE_MANAGER_BYTES,
               "COLD_CODE_MANAGER_BYTES is too small for the manager implementation");
_Static_assert(_Alignof(struct cold_code_manager_impl) <= COLD_CODE_MANAGER_ALIGN,
               "COLD_CODE_MANAGER_ALIGN is too small");

uint32_t cold_code_crc32_update(uint32_t crc, const void *data, size_t size);

/* Freestanding helpers: the core must not reference libc mem* symbols. */
static inline void cold_code_memzero(void *dst, size_t size) {
    unsigned char *bytes = (unsigned char *)dst;
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0u;
    }
}

static inline void cold_code_memcopy(void *dst, const void *src, size_t size) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    for (size_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
}

bool cold_code_image_geometry_valid(const cold_code_image_t *image);

/* Resolve pool_slots==0 and validate an explicit pool size.  Returns 0 on error. */
uint32_t cold_code_resolve_pool_slots(const cold_code_image_t *image, uint32_t requested);

cold_code_status_t cold_code_storage_layout(const cold_code_image_t *image, uint32_t pool_slots,
                                            cold_code_storage_layout_t *out);

const cold_code_context_ops_t *cold_code_context_ops_checked(void);
bool cold_code_context_arch_valid(const cold_code_context_ops_t *ops);

/*
 * Report a loader fault.  Calls the manager fault callback (if any) and then
 * the context port fatal handler.  Never returns.
 */
_Noreturn void cold_code_core_fatal(struct cold_code_manager_impl *manager,
                                    cold_code_fault_code_t code, uint32_t image_slot,
                                    const char *message);

/* Called by generated slow stubs.  The symbol name encodes the stub ABI. */
unsigned char *cold_code_core_acquire_slot_v1(const cold_code_image_t *image,
                                              uint32_t image_slot);

#endif /* COLD_CODE_INTERNAL_H */
