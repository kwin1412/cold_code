#include "cold_code.h"

COLD_CODE()
int cold_multi_add(int lhs, int rhs) {
    return lhs + rhs;
}

COLD_CODE()
int cold_multi_sub(int lhs, int rhs) {
    return lhs - rhs;
}

/* Aligned to SLOT_SIZE so each function starts at a slot boundary. */
COLD_CODE()
__attribute__((aligned(64))) int cold_multi_mul(int lhs, int rhs) {
    return lhs * rhs;
}
