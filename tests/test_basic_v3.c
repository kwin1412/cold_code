#include "cold_code.h"
#include "cold_code_basic_image.h"
#include "cold_code_host.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(basic, g_storage);
static cold_code_manager_t g_manager;

int cold_add(int lhs, int rhs);
int cold_sub(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, COLD_CODE_IMAGE_BASIC_PATH);

    cold_code_manager_config_t config = {
        .image = &cold_code_image_basic,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_file_source(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_activate(&g_manager), COLD_CODE_OK);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1u);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 0u);

    CHECK_EQ(cold_add(2, 3), 5);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.misses, 1u);
    CHECK_EQ(stats.hits, 0u);

    CHECK_EQ(cold_sub(5, 3), 2);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 1u);

    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    TEST_OK();
    return 0;
}
