#include <stdio.h>

#include "cold_code.h"

int main(void)
{
    const int lhs = 2;
    const int rhs = 3;

    printf("cold_code version: %s\n", cold_code_version());
    printf("%d + %d = %d\n", lhs, rhs, cold_code_add(lhs, rhs));

    return 0;
}
