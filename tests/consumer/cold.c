#include "cold_code.h"

COLD_CODE()
int cold_consumer_add(int lhs, int rhs) {
    return lhs + rhs;
}
