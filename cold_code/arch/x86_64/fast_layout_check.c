#include "cold_code.h"
#include "cold_code_fast_layout.h"

#include <stddef.h>

_Static_assert(sizeof(void *) == 8u, "FAST_PATH x86-64 layout expects 64-bit pointers");
_Static_assert(offsetof(cold_code_image_t, image_id) == COLD_CODE_FAST_OFF_IMAGE_ID,
               "FAST_PATH image_id offset");
_Static_assert(offsetof(cold_code_image_t, slot_size) == COLD_CODE_FAST_OFF_IMAGE_SLOT_SIZE,
               "FAST_PATH slot_size offset");
_Static_assert(offsetof(cold_code_image_t, slot_count) == COLD_CODE_FAST_OFF_IMAGE_SLOT_COUNT,
               "FAST_PATH slot_count offset");
_Static_assert(offsetof(cold_code_manager_t, image) == COLD_CODE_FAST_OFF_MGR_IMAGE,
               "FAST_PATH manager image offset");
_Static_assert(offsetof(cold_code_manager_t, slot_map) == COLD_CODE_FAST_OFF_MGR_SLOT_MAP,
               "FAST_PATH manager slot_map offset");
_Static_assert(offsetof(cold_code_manager_t, way_used) == COLD_CODE_FAST_OFF_MGR_WAY_USED,
               "FAST_PATH manager way_used offset");
_Static_assert(offsetof(cold_code_manager_t, pool) == COLD_CODE_FAST_OFF_MGR_POOL,
               "FAST_PATH manager pool offset");
_Static_assert(offsetof(cold_code_manager_t, pool_slots) == COLD_CODE_FAST_OFF_MGR_POOL_SLOTS,
               "FAST_PATH manager pool_slots offset");
_Static_assert(offsetof(cold_code_manager_t, tick) == COLD_CODE_FAST_OFF_MGR_TICK,
               "FAST_PATH manager tick offset");
_Static_assert(offsetof(cold_code_manager_t, stats) + offsetof(cold_code_stats_t, hits) ==
                   COLD_CODE_FAST_OFF_MGR_STATS_HITS,
               "FAST_PATH manager stats.hits offset");
_Static_assert(offsetof(cold_code_manager_t, initialized) == COLD_CODE_FAST_OFF_MGR_INITIALIZED,
               "FAST_PATH manager initialized offset");
