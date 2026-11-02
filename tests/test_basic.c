#include "cold_code.h"
#include "cold_code_image.h"
#include "test_util.h"

int cold_add(int lhs, int rhs);
int cold_sub(int lhs, int rhs);

int main(void) {
    CHECK(cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH));

    CHECK_EQ(cold_code_total_slots(), 1);
    CHECK_EQ(cold_code_pool_slots(), 1);
    CHECK_EQ(cold_code_loaded_slots(), 0);

    CHECK_EQ(cold_add(2, 3), 5);
    CHECK_EQ(cold_code_loaded_slots(), 1);

    /* add and sub share slot 0: the second call is a hit. */
    CHECK_EQ(cold_sub(5, 3), 2);
    CHECK_EQ(cold_code_loaded_slots(), 1);

    TEST_OK();
    return 0;
}
