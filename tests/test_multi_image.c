#include "cold_code.h"
#include "cold_code_first_image.h"
#include "cold_code_second_image.h"
#include "port_host.h"
#include "test_util.h"

/*
 * One executable, two independent cold images and two managers.  Switching the
 * current manager must route each stub to the matching pool.
 */

static cold_code_storage_first_t g_storage_first COLD_CODE_STORAGE_SECTION;
static cold_code_storage_second_t g_storage_second COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager_first;
static cold_code_manager_t g_manager_second;

int cold_first_add(int lhs, int rhs);
int cold_second_mul(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t first_source;
    cold_code_file_source_t second_source;
    cold_code_file_source_init(&first_source, COLD_CODE_IMAGE_FIRST_PATH);
    cold_code_file_source_init(&second_source, COLD_CODE_IMAGE_SECOND_PATH);
    cold_code_storage_first_bind(&g_manager_first, &g_storage_first);
    cold_code_storage_second_bind(&g_manager_second, &g_storage_second);

    cold_code_config_t first_config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &first_source,
    };
    cold_code_config_t second_config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &second_source,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager_first, &cold_code_image_first, &first_config),
             COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_second, &cold_code_image_second, &second_config),
             COLD_CODE_OK);

    cold_code_manager_use(&g_manager_first);
    CHECK_EQ(cold_first_add(2, 3), 5);

    cold_code_manager_use(&g_manager_second);
    CHECK_EQ(cold_second_mul(4, 3), 12);

    /* Each manager keeps its own resident slot. */
    cold_code_manager_use(&g_manager_first);
    CHECK_EQ(cold_first_add(10, 20), 30);
    cold_code_manager_use(&g_manager_second);
    CHECK_EQ(cold_second_mul(5, 6), 30);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_first, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 1);

    CHECK_EQ(cold_code_manager_stats(&g_manager_second, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 1);

    cold_code_manager_deinit(&g_manager_first);
    cold_code_manager_deinit(&g_manager_second);
    TEST_OK();
    return 0;
}
