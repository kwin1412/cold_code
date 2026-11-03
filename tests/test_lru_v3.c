#include "cold_code.h"
#include "cold_code_host.h"
#include "test_runtime.h"
#include "test_util.h"

#ifndef EXPECTED_POOL
#define EXPECTED_POOL 1
#endif

#if EXPECTED_POOL == 2
#define TEST_IMAGE_PATH COLD_CODE_IMAGE_LRU2_PATH
COLD_CODE_DEFINE_STORAGE(lru2, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
#else
#define TEST_IMAGE_PATH COLD_CODE_IMAGE_LRU1_PATH
COLD_CODE_DEFINE_STORAGE(lru1, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
#endif

static cold_code_manager_t g_manager;

int cold_lru_add(int lhs, int rhs);
int cold_lru_sub(int lhs, int rhs);
int cold_lru_mul(int lhs, int rhs);
int cold_lru_div(int lhs, int rhs);

static cold_code_stats_t stats(void) {
    cold_code_stats_t snapshot;
    if (cold_code_manager_stats(&g_manager, &snapshot) != COLD_CODE_OK) {
        snapshot = (cold_code_stats_t){0};
    }
    return snapshot;
}

int main(void) {
    cold_code_file_loader_t file;
    cold_code_file_loader_init(&file, TEST_IMAGE_PATH);
    cold_code_loader_t source = cold_code_file_loader(&file);
    cold_code_image_info_t info;
    CHECK(test_probe_ok(source, EXPECTED_POOL, sizeof(g_storage), &info));

    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = EXPECTED_POOL,
        .expected_image_id = info.image_id,
        .loader = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
        .trace = {.fn = test_trace_event, .ctx = NULL},
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &config), COLD_CODE_OK);

    CHECK_EQ(stats().image_slots, 3u);
    CHECK_EQ(stats().pool_slots, EXPECTED_POOL);
    CHECK_EQ(stats().loaded_slots, 0u);

    CHECK_EQ(cold_lru_add(2, 3), 5); /* image slot 0 */
    CHECK_EQ(cold_lru_sub(5, 3), 2); /* image slot 0: hit */
    CHECK_EQ(stats().loaded_slots, 1u);

    CHECK_EQ(cold_lru_mul(4, 3), 12); /* image slot 1 */
    CHECK_EQ(cold_lru_add(2, 3), 5);  /* image slot 0: hit refreshes LRU */

#if EXPECTED_POOL == 2
    CHECK_EQ(stats().loaded_slots, 2u);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 1 (true LRU) */
    CHECK_EQ(stats().loaded_slots, 2u);
    CHECK_EQ(stats().loads, 3u);
    CHECK_EQ(stats().misses, 3u);
    CHECK_EQ(stats().hits, 2u);
    CHECK_EQ(stats().evictions, 1u);
#else
    CHECK_EQ(stats().loaded_slots, 1u);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 0 */
    CHECK_EQ(stats().loaded_slots, 1u);
    CHECK_EQ(stats().loads, 4u);
    CHECK_EQ(stats().misses, 4u);
    CHECK_EQ(stats().hits, 1u);
    CHECK_EQ(stats().evictions, 3u);
#endif

    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    cold_code_file_loader_close(&file);
    TEST_OK();
    return 0;
}
