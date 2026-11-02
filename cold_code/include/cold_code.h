#ifndef COLD_CODE_H
#define COLD_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum number of physical slots (LRU pool capacity).  The generated
 * descriptor must use a pool_slot_count no larger than this.
 */
#define COLD_CODE_MAX_POOL_SLOTS 16u

/*
 * Mark a function as cold code.
 *
 * In a cold-image build (COLD_CODE_BUILD_IMAGE) the function is emitted into
 * the .cold_text section, which is linked into the separate cold image and
 * copied into a RAM slot on first execution.  In a normal build this macro is
 * only an annotation and does not affect code generation.
 */
#ifdef COLD_CODE_BUILD_IMAGE
#define COLD_CODE() __attribute__((noinline, used, section(".cold_text")))
#else
#define COLD_CODE()
#endif

typedef struct {
    uint32_t slot_size;       /* bytes per slot (also the LRU load unit) */
    uint32_t slot_count;      /* number of slots in the cold image */
    uint32_t pool_slot_count; /* physical slots available in RAM (LRU pool) */
} cold_code_image_t;

/*
 * Open the cold image (simulated flash) and validate the descriptor.
 * Returns true on success.  Calling it again with the same descriptor is a
 * no-op; a different descriptor returns false.
 */
bool cold_code_init(const cold_code_image_t *image, const char *image_path);

/* Enable/disable tracing of actual slot loads and LRU evictions. */
void cold_code_set_trace(bool enabled);

size_t cold_code_total_slots(void);  /* slots in the cold image */
size_t cold_code_loaded_slots(void); /* physical slots currently resident */
size_t cold_code_pool_slots(void);   /* physical slot capacity */

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_H */
