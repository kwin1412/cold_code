#include "cold_code.h"

COLD_CODE()
int cold_fixture_add(int lhs, int rhs) {
    return lhs + rhs;
}

/* Aligned to SLOT_SIZE -> image slot 1; calling cold_fixture_add crosses slots. */
COLD_CODE()
COLD_CODE_SLOT_ALIGNED int cold_fixture_call_add(int lhs, int rhs) {
    return cold_fixture_add(lhs, rhs);
}
