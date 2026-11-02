#include "cold_code.h"
#include "port_host.h"
#include "test_runtime.h"
#include "test_util.h"

#ifndef EXPECTED_POOL
#define EXPECTED_POOL 1
#endif

#if EXPECTED_POOL == 2
#include "cold_code_lru2_image.h"
#define TEST_IMAGE        cold_code_image_lru2
#define TEST_IMAGE_PATH   COLD_CODE_IMAGE_LRU2_PATH
#define TEST_STORAGE_TYPE cold_code_storage_lru2_t
#define TEST_STORAGE_BIND cold_code_storage_lru2_bind
#else
#include "cold_code_lru1_image.h"
#define TEST_IMAGE        cold_code_image_lru1
#define TEST_IMAGE_PATH   COLD_CODE_IMAGE_LRU1_PATH
#define TEST_STORAGE_TYPE cold_code_storage_lru1_t
#define TEST_STORAGE_BIND cold_code_storage_lru1_bind
#endif

static TEST_STORAGE_TYPE g_storage COLD_CODE_STORAGE_SECTION;
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
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, TEST_IMAGE_PATH);
    TEST_STORAGE_BIND(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
        .trace = test_trace_event,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &TEST_IMAGE, &config), COLD_CODE_OK);
    cold_code_manager_use(&g_manager);

    CHECK_EQ(stats().image_slots, 3);
    CHECK_EQ(stats().pool_slots, EXPECTED_POOL);
    CHECK_EQ(stats().loaded_slots, 0);

    CHECK_EQ(cold_lru_add(2, 3), 5); /* image slot 0 */
    CHECK_EQ(cold_lru_sub(5, 3), 2); /* image slot 0: hit */
    CHECK_EQ(stats().loaded_slots, 1);

    CHECK_EQ(cold_lru_mul(4, 3), 12); /* image slot 1 */
    CHECK_EQ(cold_lru_add(2, 3), 5);  /* image slot 0: hit refreshes LRU */

#if EXPECTED_POOL == 2
    CHECK_EQ(stats().loaded_slots, 2);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 1 (true LRU) */
    CHECK_EQ(stats().loaded_slots, 2);
    CHECK_EQ(stats().loads, 3);
    CHECK_EQ(stats().misses, 3);
    CHECK_EQ(stats().hits, 2);
    CHECK_EQ(stats().evictions, 1);
#else
    CHECK_EQ(stats().loaded_slots, 1);
    CHECK_EQ(cold_lru_div(8, 2), 4); /* image slot 2: evicts slot 0 */
    CHECK_EQ(stats().loaded_slots, 1);
    CHECK_EQ(stats().loads, 4);
    CHECK_EQ(stats().misses, 4);
    CHECK_EQ(stats().hits, 1);
    CHECK_EQ(stats().evictions, 3);
#endif

    cold_code_manager_deinit(&g_manager);
    TEST_OK();
    return 0;
}
