#include <stdio.h>

#include "module.h"

int main(void)
{
    const int lhs = 2;
    const int rhs = 3;

    printf("module version: %s\n", module_version());
    printf("%d + %d = %d\n", lhs, rhs, module_add(lhs, rhs));

    return 0;
}
