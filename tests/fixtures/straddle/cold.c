#include "cold_code.h"

COLD_CODE()
int cold_fixture_add(int lhs, int rhs) {
    return lhs + rhs;
}

/* With SLOT_SIZE=16, this function starts at offset 8 and spans slots 0..1. */
COLD_CODE()
int cold_fixture_sub(int lhs, int rhs) {
    return lhs - rhs;
}
