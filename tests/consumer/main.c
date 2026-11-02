#include <stdio.h>

#include "cold_code.h"
#include "cold_code_image.h"

int cold_consumer_add(int lhs, int rhs);

int main(void) {
    if (!cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH)) {
        return 1;
    }

    if (cold_consumer_add(2, 3) != 5) {
        return 2;
    }

    printf("consumer ok\n");
    return 0;
}
