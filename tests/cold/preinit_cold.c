#include "cold_code.h"

COLD_CODE()
int cold_preinit_ping(void) {
    return 42;
}
