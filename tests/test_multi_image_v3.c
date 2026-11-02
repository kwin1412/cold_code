#include "cold_code.h"
#include "cold_code_first_image.h"
#include "cold_code_host.h"
#include "cold_code_second_image.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(first, g_storage_first);
COLD_CODE_DEFINE_STORAGE(second, g_storage_second);
static cold_code_manager_t g_manager_first;
static cold_code_manager_t g_manager_second;

int cold_first_add(int lhs, int rhs);
int cold_second_mul(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t first_file;
    cold_code_file_source_t second_file;
    cold_code_file_source_init(&first_file, COLD_CODE_IMAGE_FIRST_PATH);
    cold_code_file_source_init(&second_file, COLD_CODE_IMAGE_SECOND_PATH);

    cold_code_manager_config_t first_config = {
        .image = &cold_code_image_first,
        .storage = &g_storage_first,
        .storage_size = sizeof(g_storage_first),
        .pool_slots = 1u,
        .source = cold_code_file_source(&first_file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    cold_code_manager_config_t second_config = {
        .image = &cold_code_image_second,
        .storage = &g_storage_second,
        .storage_size = sizeof(g_storage_second),
        .pool_slots = 1u,
        .source = cold_code_file_source(&second_file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };

    CHECK_EQ(cold_code_manager_init(&g_manager_first, &first_config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_second, &second_config), COLD_CODE_OK);

    CHECK_EQ(cold_code_manager_activate(&g_manager_first), COLD_CODE_OK);
    CHECK_EQ(cold_first_add(2, 3), 5);

    CHECK_EQ(cold_code_manager_activate(&g_manager_second), COLD_CODE_OK);
    CHECK_EQ(cold_second_mul(4, 3), 12);

    CHECK_EQ(cold_code_manager_activate(&g_manager_first), COLD_CODE_OK);
    CHECK_EQ(cold_first_add(10, 20), 30);
    CHECK_EQ(cold_code_manager_activate(&g_manager_second), COLD_CODE_OK);
    CHECK_EQ(cold_second_mul(5, 6), 30);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_first, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 1u);

    CHECK_EQ(cold_code_manager_stats(&g_manager_second, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 1u);

    CHECK_EQ(cold_code_manager_deinit(&g_manager_first), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_deinit(&g_manager_second), COLD_CODE_OK);
    TEST_OK();
    return 0;
}
