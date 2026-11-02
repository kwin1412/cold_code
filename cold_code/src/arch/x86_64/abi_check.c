#include "cold_code_internal.h"

#include "cold_code/abi/v1.h"
#include "cold_code/abi/v1/x86_64.h"

#include <stddef.h>

_Static_assert(offsetof(struct cold_code_manager_impl, fast) == 0u,
               "fast-state block must start at manager offset 0");
_Static_assert(offsetof(cold_code_fast_state_t, image) == COLD_CODE_FAST_OFF_IMAGE,
               "fast-state image offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, slot_map) == COLD_CODE_FAST_OFF_SLOT_MAP,
               "fast-state slot_map offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, way_used) == COLD_CODE_FAST_OFF_WAY_USED,
               "fast-state way_used offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, pool) == COLD_CODE_FAST_OFF_POOL,
               "fast-state pool offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, pool_slots) == COLD_CODE_FAST_OFF_POOL_SLOTS,
               "fast-state pool_slots offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, state) == COLD_CODE_FAST_OFF_STATE,
               "fast-state state offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, tick) == COLD_CODE_FAST_OFF_TICK,
               "fast-state tick offset mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, hits) == COLD_CODE_FAST_OFF_HITS,
               "fast-state hits offset mismatch");
_Static_assert(sizeof(cold_code_fast_state_t) == COLD_CODE_FAST_STATE_SIZE,
               "fast-state size mismatch");
_Static_assert(COLD_CODE_MANAGER_STATE_READY == COLD_CODE_FAST_STATE_READY,
               "fast-state ready flag mismatch");
