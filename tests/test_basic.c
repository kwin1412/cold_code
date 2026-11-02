#include "cold_code.h"
#include "cold_code_basic_image.h"
#include "port_host.h"
#include "test_util.h"

static cold_code_storage_basic_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;

int cold_add(int lhs, int rhs);
int cold_sub(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, COLD_CODE_IMAGE_BASIC_PATH);
    cold_code_storage_basic_bind(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_basic, &config), COLD_CODE_OK);
    cold_code_manager_use(&g_manager);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1);
    CHECK_EQ(stats.pool_slots, 1);
    CHECK_EQ(stats.loaded_slots, 0);
    CHECK_EQ(stats.loads, 0);
    CHECK_EQ(stats.hits, 0);

    CHECK_EQ(cold_add(2, 3), 5);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.misses, 1);
    CHECK_EQ(stats.hits, 0);

    /* add and sub share slot 0: the second call is a hit. */
    CHECK_EQ(cold_sub(5, 3), 2);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 1);

    cold_code_manager_deinit(&g_manager);
    TEST_OK();
    return 0;
}
