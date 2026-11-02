#include "cold_code.h"

COLD_CODE()
int cold_fixture_no_asm(int lhs, int rhs) {
    return lhs + rhs;
}
