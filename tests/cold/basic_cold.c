#include "cold_code.h"

COLD_CODE()
int cold_add(int lhs, int rhs) {
    return lhs + rhs;
}

COLD_CODE()
int cold_sub(int lhs, int rhs) {
    return lhs - rhs;
}
