#include <stdio.h>

#include "cold_code.h"

/* Violates the self-contained contract: string literal + libc call. */
COLD_CODE()
int cold_fixture_bad(void) {
    return printf("not self contained\n");
}
