#include "cold_code.h"

COLD_CODE()
int cold_fixture_add(int lhs, int rhs) {
    return lhs + rhs;
}

/* Aligned to 64 -> image slot 1; calling cold_fixture_add crosses slots. */
COLD_CODE()
__attribute__((aligned(64))) int cold_fixture_call_add(int lhs, int rhs) {
    return cold_fixture_add(lhs, rhs);
}
