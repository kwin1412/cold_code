#include <stdio.h>

#include "cold_code.h"
#include "cold_code_image.h"
#include "cold_functions.h"

int main(void) {
    if (!cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH)) {
        fprintf(stderr, "cold_code_init failed\n");
        return 1;
    }

    cold_code_set_trace(true);

    printf("image slots: %zu, pool slots: %zu, loaded: %zu\n", cold_code_total_slots(),
           cold_code_pool_slots(), cold_code_loaded_slots());
    fflush(stdout);

    /* add and sub share image slot 0: the second call is an LRU hit. */
    printf("add(2, 3) = %d\n", cold_code_add(2, 3));
    printf("sub(5, 3) = %d\n", cold_code_sub(5, 3));
    fflush(stdout);
    printf("loaded after add/sub: %zu/%zu\n", cold_code_loaded_slots(), cold_code_pool_slots());
    fflush(stdout);

    /* mul lives in image slot 1: this evicts slot 0 with POOL_SLOTS=1. */
    printf("mul(4, 3) = %d\n", cold_code_mul(4, 3));
    fflush(stdout);

    /* Reloading slot 0 evicts slot 1. */
    printf("add(2, 3) = %d\n", cold_code_add(2, 3));
    fflush(stdout);

    /* div lives in image slot 2: this evicts slot 0 again. */
    printf("div(8, 2) = %d\n", cold_code_div(8, 2));
    fflush(stdout);

    printf("loaded final: %zu/%zu (image slots: %zu)\n", cold_code_loaded_slots(),
           cold_code_pool_slots(), cold_code_total_slots());
    return 0;
}
