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
 * expression shared by the core and generated headers.
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
 * align_ must be a power of two.  The generated storage type is sized with
 * this macro for image->max_pool_slots.
 */
#define COLD_CODE_STORAGE_SIZE(slot_count_, pool_slots_, slot_size_, align_)                       \
    COLD_CODE_ALIGN_UP(COLD_CODE_STORAGE_WAY_USED_OFFSET(slot_count_, pool_slots_, slot_size_,     \
                                                         align_) +                                 \
                           (size_t)(pool_slots_) * sizeof(uint64_t),                               \
                       (align_))

/* Caller-supplied storage lives in executable RAM. */
#ifndef COLD_CODE_STORAGE_SECTION
#define COLD_CODE_STORAGE_SECTION __attribute__((section(".cold_ram")))
#endif

/* Generated type alias: COLD_CODE_STORAGE(app) -> cold_code_storage_app_t. */
#define COLD_CODE_STORAGE(name_) cold_code_storage_##name_##_t

/*
 * Declare a storage instance and a hidden section marker.  The marker lets the
 * post-build verifier check the storage the caller actually uses, not only the
 * generated marker.  The macro includes "static"; use it at file scope.
 */
#define COLD_CODE_DEFINE_STORAGE(name_, var_)                                                      \
    static cold_code_storage_##name_##_t var_ COLD_CODE_STORAGE_SECTION;                           \
    static unsigned char cold_code_storage_marker_##name_##_##var_[1]                              \
        COLD_CODE_STORAGE_SECTION __attribute__((used))

/* Slot alignment for cold functions; helper injects COLD_CODE_IMAGE_SLOT_SIZE. */
#if defined(COLD_CODE_IMAGE_SLOT_SIZE) && (COLD_CODE_IMAGE_SLOT_SIZE > 0)
#define COLD_CODE_SLOT_ALIGNED __attribute__((aligned(COLD_CODE_IMAGE_SLOT_SIZE)))
#else
#define COLD_CODE_SLOT_ALIGNED
#endif

/* Exact size/alignment required for a given image and pool size. */
size_t cold_code_storage_required_size(const cold_code_image_t *image, uint32_t pool_slots);
size_t cold_code_storage_required_align(const cold_code_image_t *image);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_STORAGE_H */
