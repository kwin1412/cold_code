#ifndef COLD_CODE_FAULT_H
#define COLD_CODE_FAULT_H

#include "cold_code/types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COLD_CODE_FAULT_NO_CURRENT = 0,
    COLD_CODE_FAULT_WRONG_IMAGE = 1,
    COLD_CODE_FAULT_BAD_SLOT = 2,
    COLD_CODE_FAULT_CORRUPT_STATE = 3,
    COLD_CODE_FAULT_SOURCE_READ = 4,
    COLD_CODE_FAULT_NO_VICTIM = 5,
} cold_code_fault_code_t;

typedef struct {
    cold_code_fault_code_t code;
    const cold_code_manager_t *manager;
    const cold_code_image_t *image;
    uint32_t image_slot;
    const char *message;
} cold_code_fault_info_t;

/*
 * Called before the context port's fatal handler.  The callback must return
 * normally or terminate; longjmp-ing out of the loader leaves residency state
 * inconsistent.  If it returns, the core calls cold_code_context_ops.fatal().
 */
typedef void (*cold_code_fault_fn)(void *ctx, const cold_code_fault_info_t *fault);

typedef struct {
    cold_code_fault_fn fn;
    void *ctx;
} cold_code_fault_t;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_FAULT_H */
