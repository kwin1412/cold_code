#include "cold_code.h"
#include "cold_code_init_image.h"
#include "cold_code_host.h"
#include "test_util.h"

#include <stdint.h>

COLD_CODE_DEFINE_STORAGE(init, g_storage);
static cold_code_manager_t g_manager;
static cold_code_manager_t g_second_manager;

int cold_ping(void);

static cold_code_manager_config_t make_config(cold_code_file_source_t *file) {
    cold_code_manager_config_t config = {
        .image = &cold_code_image_init,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_file_source(file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    return config;
}

int main(void) {
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, COLD_CODE_IMAGE_INIT_PATH);
    cold_code_manager_config_t config = make_config(&file);

    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_init(NULL, &config), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&g_manager, NULL), COLD_CODE_ERR_ARG);

    cold_code_manager_config_t bad = config;
    bad.image = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);
    bad = config;
    bad.storage = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);
    bad = config;
    bad.source.ops = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    cold_code_image_t invalid = cold_code_image_init;
    invalid.slot_size = 0;
    bad = config;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_count = 0;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.max_pool_slots = 0;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.max_pool_slots = COLD_CODE_MAX_POOL_SLOTS + 1u;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_count = 1u;
    invalid.max_pool_slots = 2u;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    invalid = cold_code_image_init;
    invalid.slot_size = UINT32_MAX;
    invalid.slot_count = 2u;
    invalid.max_pool_slots = 1u;
    bad.image = &invalid;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    size_t required = cold_code_storage_required_size(&cold_code_image_init, 1u);
    CHECK(required > 0u);
    bad = config;
    bad.storage_size = required - 1u;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_SIZE);

    cold_code_file_source_t missing;
    cold_code_file_source_init(&missing, "/nonexistent/cold-code-test-image.bin");
    bad = make_config(&missing);
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_SOURCE);

    CHECK_EQ(cold_code_manager_init(&g_manager, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_is_initialized(&g_manager));

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1u);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 0u);

    CHECK_EQ(cold_code_manager_init(&g_manager, &config), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_init(&g_second_manager, &config), COLD_CODE_ERR_STATE);

    CHECK_EQ(cold_code_manager_activate(&g_manager), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);

    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 0u);
    CHECK_EQ(stats.loads, 0u);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);

    CHECK_EQ(cold_code_manager_activate(NULL), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    CHECK_EQ(cold_code_manager_activate(&g_manager), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_ERR_STATE);
    CHECK(cold_code_manager_image(&g_manager) == NULL);
    CHECK(cold_code_manager_pool_base(&g_manager) == NULL);

    CHECK_EQ(cold_code_manager_init(&g_second_manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_activate(&g_second_manager), COLD_CODE_OK);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_deinit(&g_second_manager), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    /* A copied descriptor is accepted by init, but cold calls require the
       generated descriptor object (pointer identity). */
    cold_code_image_t other = cold_code_image_init;
    bad = config;
    bad.image = &other;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);

    TEST_OK();
    return 0;
}
