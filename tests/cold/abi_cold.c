#include <stdarg.h>

#include "abi_types.h"
#include "cold_code.h"

/* Covers stack GPR arguments plus SSE (float/double) arguments. */
COLD_CODE()
double cold_abi_mix(int a, long b, double c, int d, int e, int f, int g, int h, float i, double j) {
    return (double)(a + b + d + e + f + g + h) + c + (double)i + j;
}

/* Covers variadic arguments and the AL register on the stub slow path. */
COLD_CODE()
int cold_abi_sum(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int sum = 0;
    for (int k = 0; k < count; ++k) {
        sum += va_arg(ap, int);
    }
    va_end(ap);
    return sum;
}

/* 24-byte return value: hidden sret pointer in RDI. */
COLD_CODE()
triple_t cold_abi_triple(long a, long b, long c) {
    triple_t value = {a, b, c};
    return value;
}
