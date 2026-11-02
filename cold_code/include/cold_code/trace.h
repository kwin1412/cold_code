#ifndef COLD_CODE_TRACE_H
#define COLD_CODE_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COLD_CODE_TRACE_LOAD = 0,
    COLD_CODE_TRACE_EVICT = 1,
} cold_code_trace_event_t;

typedef void (*cold_code_trace_fn)(void *ctx, cold_code_trace_event_t event, uint32_t image_slot,
                                   uint32_t way);

typedef struct {
    cold_code_trace_fn fn;
    void *ctx;
} cold_code_trace_t;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_TRACE_H */
