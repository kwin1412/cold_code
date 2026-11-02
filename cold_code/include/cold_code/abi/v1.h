#ifndef COLD_CODE_ABI_V1_H
#define COLD_CODE_ABI_V1_H

#include "cold_code/types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v1 fast-path ABI block.  This is the only fixed-layout part of the opaque
 * manager: it starts at manager offset 0 and is read/written by generated
 * x86-64 fast stubs.  The numeric offsets live in the arch-specific header
 * cold_code/abi/v1/x86_64.h and are checked by abi_check.c.
 */
typedef struct {
    const cold_code_image_t *image;
    uint32_t *slot_map;
    uint64_t *way_used;
    unsigned char *pool;
    uint32_t pool_slots;
    uint32_t state;
    uint64_t tick;
    uint64_t hits;
} cold_code_fast_state_t;

#define COLD_CODE_MANAGER_STATE_READY 0x00000001u

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_ABI_V1_H */
