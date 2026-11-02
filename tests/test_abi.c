#include "cold/abi_types.h"
#include "cold_code.h"
#include "cold_code_image.h"
#include "test_util.h"

double cold_abi_mix(int a, long b, double c, int d, int e, int f, int g, int h, float i, double j);
int cold_abi_sum(int count, ...);
triple_t cold_abi_triple(long a, long b, long c);

int main(void) {
    CHECK(cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH));
    CHECK_EQ(cold_code_total_slots(), 1);
    CHECK_EQ(cold_code_pool_slots(), 1);

    /* Stack GPR + SSE arguments. */
    CHECK_NEAR(cold_abi_mix(1, 2L, 3.5, 4, 5, 6, 7, 8, 9.5f, 10.25), 56.25, 1e-9);

    /* Variadic arguments (AL must survive the stub slow path). */
    CHECK_EQ(cold_abi_sum(4, 10, 20, 30, 40), 100);

    /* 24-byte struct return through the hidden sret pointer. */
    triple_t triple = cold_abi_triple(10, 20, 30);
    CHECK_EQ(triple.a, 10);
    CHECK_EQ(triple.b, 20);
    CHECK_EQ(triple.c, 30);

    CHECK_EQ(cold_code_loaded_slots(), 1);
    TEST_OK();
    return 0;
}
