#include "cold_code.h"

COLD_CODE()
int cold_code_add(int lhs, int rhs) {
    return lhs + rhs;
}

COLD_CODE()
int cold_code_sub(int lhs, int rhs) {
    return lhs - rhs;
}

/* Align to the slot size so the function does not straddle a slot boundary. */
COLD_CODE()
__attribute__((aligned(64))) int cold_code_mul(int lhs, int rhs) {
    return lhs * rhs;
}

COLD_CODE()
__attribute__((aligned(64))) int cold_code_div(int lhs, int rhs) {
    return lhs / rhs;
}
