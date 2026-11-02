#include "cold_code.h"
#include "cold_code_init_image.h"
#include "port_host.h"
#include "test_util.h"

static cold_code_storage_init_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;
static cold_code_manager_t g_second_manager;
static cold_code_manager_t g_unbound_manager;

int cold_ping(void);

int main(void) {
    cold_code_storage_init_bind(&g_manager, &g_storage);
    cold_code_storage_init_bind(&g_second_manager, &g_storage);

    cold_code_file_source_t source;
    cold_code_file_source_init(&source, COLD_CODE_IMAGE_INIT_PATH);
    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };

    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_init(NULL, &cold_code_image_init, &config), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&g_manager, NULL, &config), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_init, NULL), COLD_CODE_ERR_ARG);

    cold_code_config_t bad_config = config;
    bad_config.source = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_init, &bad_config),
             COLD_CODE_ERR_ARG);

    /* No bind: the storage pointers are missing. */
    CHECK_EQ(cold_code_manager_init(&g_unbound_manager, &cold_code_image_init, &config),
             COLD_CODE_ERR_ARG);

    cold_code_image_t invalid = cold_code_image_init;
    invalid.slot_size = 0;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_count = 0;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.pool_slot_count = 0;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.pool_slot_count = COLD_CODE_MAX_POOL_SLOTS + 1u;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_count = 1;
    invalid.pool_slot_count = 2;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_size = UINT32_MAX;
    invalid.slot_count = 2;
    invalid.pool_slot_count = 1;
    CHECK_EQ(cold_code_manager_init(&g_manager, &invalid, &config), COLD_CODE_ERR_ARG);

    cold_code_file_source_t missing_source;
    cold_code_file_source_init(&missing_source, "/nonexistent/cold-code-test-image.bin");
    cold_code_config_t missing_config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &missing_source,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_init, &missing_config),
             COLD_CODE_ERR_SOURCE);

    /* None of the failures above may have initialised the manager. */
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_init, &config), COLD_CODE_OK);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1);
    CHECK_EQ(stats.pool_slots, 1);
    CHECK_EQ(stats.loaded_slots, 0);

    /* Repeated init requires an explicit deinit. */
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_init, &config),
             COLD_CODE_ERR_STATE);

    /* A storage serves one manager at a time. */
    CHECK_EQ(cold_code_manager_init(&g_second_manager, &cold_code_image_init, &config),
             COLD_CODE_ERR_STATE);

    cold_code_manager_use(&g_manager);
    CHECK(cold_code_manager_current() == &g_manager);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);

    /* Reset unloads the slot and clears counters while keeping the source. */
    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 0);
    CHECK_EQ(stats.loads, 0);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);

    cold_code_manager_use(NULL);
    CHECK(cold_code_manager_current() == NULL);

    cold_code_manager_use(&g_manager);
    cold_code_manager_deinit(&g_manager);
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_ERR_ARG);

    /* Deinit releases the storage for another manager. */
    CHECK_EQ(cold_code_manager_init(&g_second_manager, &cold_code_image_init, &config),
             COLD_CODE_OK);
    cold_code_manager_use(&g_second_manager);
    CHECK_EQ(cold_ping(), 42);
    cold_code_manager_deinit(&g_second_manager);
    CHECK(cold_code_manager_current() == NULL);

    /* A second init may use a different descriptor object with the same values. */
    cold_code_image_t other = cold_code_image_init;
    CHECK_EQ(cold_code_manager_init(&g_manager, &other, &config), COLD_CODE_OK);
    cold_code_manager_deinit(&g_manager);

    TEST_OK();
    return 0;
}
