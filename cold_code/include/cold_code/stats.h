#ifndef COLD_CODE_STATS_H
#define COLD_CODE_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t image_slots;
    uint32_t pool_slots;
    uint32_t loaded_slots;
    uint64_t loads;
    uint64_t evictions;
    uint64_t hits;
    uint64_t misses;
    uint64_t faults;
} cold_code_stats_t;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_STATS_H */
