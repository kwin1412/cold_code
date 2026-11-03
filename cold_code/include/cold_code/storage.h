#ifndef COLD_CODE_STORAGE_H
#define COLD_CODE_STORAGE_H

#include "cold_code/image.h"
#include "cold_code/version.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLD_CODE_STORAGE_MAGIC 0x54534343u /* bytes "CCST" (little-endian) */

/*
 * Runtime state placed at the beginning of caller-provided storage.  These
 * fields are internal to the loader; consumers must not read or write them.
 * The struct is public only so COLD_CODE_STORAGE_SIZE() can be a compile-time
 * expression shared by the core and callers.
 */
typedef struct cold_code_storage_header {
    void *owner; /* cold_code_manager_t * while claimed */
    uint32_t magic;
    uint16_t abi_version;
    uint16_t struct_size;
    uint32_t image_id;
    uint32_t slot_count;
    uint32_t pool_slots;
    uint32_t total_size;
} cold_code_storage_header_t;

#define COLD_CODE_ALIGN_UP(value, align_) \
    (((size_t)(value) + (size_t)(align_) - 1u) & ~((size_t)(align_) - 1u))

#define COLD_CODE_STORAGE_POOL_OFFSET(align_) \
    COLD_CODE_ALIGN_UP(sizeof(cold_code_storage_header_t), (align_))

#define COLD_CODE_STORAGE_SLOT_MAP_OFFSET(slot_count_, pool_slots_, slot_size_, align_)            \
    COLD_CODE_ALIGN_UP(COLD_CODE_STORAGE_POOL_OFFSET(align_) +                                    \
                           (size_t)(pool_slots_) * (size_t)(slot_size_),                           \
                       8u)

#define COLD_CODE_STORAGE_WAY_SLOT_OFFSET(slot_count_, pool_slots_, slot_size_, align_)            \
    (COLD_CODE_STORAGE_SLOT_MAP_OFFSET(slot_count_, pool_slots_, slot_size_, align_) +             \
     (size_t)(slot_count_) * sizeof(uint32_t))

#define COLD_CODE_STORAGE_WAY_USED_OFFSET(slot_count_, pool_slots_, slot_size_, align_)            \
    COLD_CODE_ALIGN_UP(COLD_CODE_STORAGE_WAY_SLOT_OFFSET(slot_count_, pool_slots_, slot_size_,     \
                                                         align_) +                                 \
                           (size_t)(pool_slots_) * sizeof(uint32_t),                               \
                       8u)

/*
 * Exact bytes required for a storage serving pool_slots physical slots.
 * align_ must be a power of two.  Runtime callers compute this from the
 * metadata returned by cold_code_manager_probe().
 */
#define COLD_CODE_STORAGE_SIZE(slot_count_, pool_slots_, slot_size_, align_)                       \
    COLD_CODE_ALIGN_UP(COLD_CODE_STORAGE_WAY_USED_OFFSET(slot_count_, pool_slots_, slot_size_,     \
                                                         align_) +                                 \
                           (size_t)(pool_slots_) * sizeof(uint64_t),                               \
                       (align_))

/*
 * Maximum static-buffer alignment for current arch backends.  A caller that
 * sizes a static max buffer before probing can use this value; the manager
 * validates the actual image storage_align from the wire header.
 */
#define COLD_CODE_STORAGE_ALIGN_MAX 64u

/* Caller-supplied storage lives in executable RAM. */
#ifndef COLD_CODE_STORAGE_SECTION
#define COLD_CODE_STORAGE_SECTION __attribute__((section(".cold_ram")))
#endif

/*
 * Declare a raw storage buffer plus the hidden section marker used by the
 * post-build RWX verifier.  `bytes_` and `align_` must be file-scope integer
 * constant expressions.  The macro includes "static"; use it at file scope.
 * The marker name keeps the cold_code_storage_marker_<name>_<var> scheme.
 */
#define COLD_CODE_DEFINE_STORAGE(name_, var_, bytes_, align_)                                      \
    static unsigned char var_[(bytes_)]                                                            \
        COLD_CODE_STORAGE_SECTION __attribute__((aligned(align_)));                                \
    static unsigned char cold_code_storage_marker_##name_##_##var_[1]                              \
        COLD_CODE_STORAGE_SECTION __attribute__((used))

/* Slot alignment for cold functions; helper injects COLD_CODE_IMAGE_SLOT_SIZE. */
#if defined(COLD_CODE_IMAGE_SLOT_SIZE) && (COLD_CODE_IMAGE_SLOT_SIZE > 0)
#define COLD_CODE_SLOT_ALIGNED __attribute__((aligned(COLD_CODE_IMAGE_SLOT_SIZE)))
#else
#define COLD_CODE_SLOT_ALIGNED
#endif

/*
 * Exact size/alignment implied by probe metadata.  pool_slots == 0 resolves to
 * info->max_pool_slots, matching cold_code_manager_init().  Returns 0 for
 * NULL/invalid metadata.
 */
size_t cold_code_storage_required_size(const cold_code_image_info_t *info, uint32_t pool_slots);
size_t cold_code_storage_required_align(const cold_code_image_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_STORAGE_H */
