#include "cold_code.h"
#include "cold_code_image.h"
#include "test_util.h"

#ifndef EXPECTED_POOL
#define EXPECTED_POOL 1
#endif

int cold_lru_add(int lhs, int rhs);
int cold_lru_sub(int lhs, int rhs);
int cold_lru_mul(int lhs, int rhs);
int cold_lru_div(int lhs, int rhs);

int main(void) {
    CHECK(cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH));
    CHECK_EQ(cold_code_total_slots(), 3);
    CHECK_EQ(cold_code_pool_slots(), EXPECTED_POOL);
    CHECK_EQ(cold_code_loaded_slots(), 0);

    cold_code_set_trace(true);

    CHECK_EQ(cold_lru_add(2, 3), 5); /* image slot 0 */
    CHECK_EQ(cold_lru_sub(5, 3), 2); /* image slot 0: hit */
    CHECK_EQ(cold_code_loaded_slots(), 1);

    CHECK_EQ(cold_lru_mul(4, 3), 12); /* image slot 1 */
    CHECK_EQ(cold_lru_add(2, 3), 5);  /* image slot 0: hit refreshes LRU */

#if EXPECTED_POOL == 2
    CHECK_EQ(cold_code_loaded_slots(), 2);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 1 (true LRU) */
    CHECK_EQ(cold_code_loaded_slots(), 2);
#else
    CHECK_EQ(cold_code_loaded_slots(), 1);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 0 */
    CHECK_EQ(cold_code_loaded_slots(), 1);
#endif

    TEST_OK();
    return 0;
}
