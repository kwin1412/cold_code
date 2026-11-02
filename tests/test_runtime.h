#ifndef TEST_RUNTIME_H
#define TEST_RUNTIME_H

#include "cold_code.h"

#include <stdio.h>

/* Matches the trace lines used by the v1 tests and the README examples. */
static inline void test_trace_event(void *ctx, cold_code_trace_event_t event, uint32_t image_slot,
                                    uint32_t way) {
    (void)ctx;
    if (event == COLD_CODE_TRACE_LOAD) {
        fprintf(stderr, "[cold-code] load image slot %u into way %u\n", (unsigned)image_slot,
                (unsigned)way);
    } else {
        fprintf(stderr, "[cold-code] evict image slot %u from way %u\n", (unsigned)image_slot,
                (unsigned)way);
    }
}

#endif /* TEST_RUNTIME_H */
