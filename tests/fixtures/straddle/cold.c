#include "cold_code.h"

COLD_CODE()
int cold_fixture_add(int lhs, int rhs) {
    return lhs + rhs;
}

/* Inflated past SLOT_SIZE (64) so it starts in one slot and spans the next. */
COLD_CODE()
int cold_fixture_sub(int lhs, int rhs) {
    __asm__ volatile(".rept 80\n\tnop\n\t.endr");
    return lhs - rhs;
}
