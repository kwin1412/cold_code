#include "cold_code.h"

COLD_CODE()
int cold_lru_add(int lhs, int rhs) {
    return lhs + rhs;
}

COLD_CODE()
int cold_lru_sub(int lhs, int rhs) {
    return lhs - rhs;
}

/* Align to SLOT_SIZE so each function starts at a slot boundary. */
COLD_CODE()
COLD_CODE_SLOT_ALIGNED int cold_lru_mul(int lhs, int rhs) {
    return lhs * rhs;
}

COLD_CODE()
COLD_CODE_SLOT_ALIGNED int cold_lru_div(int lhs, int rhs) {
    return lhs / rhs;
}
