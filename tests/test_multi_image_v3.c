#include "cold_code.h"
#include "cold_code_host.h"
#include "test_cpu.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(first, g_storage_first, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
COLD_CODE_DEFINE_STORAGE(second, g_storage_second, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager_first;
static cold_code_manager_t g_manager_second;

int cold_first_add(int lhs, int rhs);
int cold_second_mul(int lhs, int rhs);

int main(void) {
    cold_code_file_loader_t first_file;
    cold_code_file_loader_t second_file;
    cold_code_file_loader_init(&first_file, COLD_CODE_IMAGE_FIRST_PATH);
    cold_code_file_loader_init(&second_file, COLD_CODE_IMAGE_SECOND_PATH);
    cold_code_loader_t first_source = cold_code_file_loader(&first_file);
    cold_code_loader_t second_source = cold_code_file_loader(&second_file);
    cold_code_image_info_t first_info;
    cold_code_image_info_t second_info;
    CHECK(test_probe_ok(first_source, 1u, sizeof(g_storage_first), &first_info));
    CHECK(test_probe_ok(second_source, 1u, sizeof(g_storage_second), &second_info));

    cold_code_manager_config_t first_config = {
        .exec_addr = &g_storage_first,
        .exec_size = sizeof(g_storage_first),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = first_info.image_id,
        .loader = first_source,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    cold_code_manager_config_t second_config = {
        .exec_addr = &g_storage_second,
        .exec_size = sizeof(g_storage_second),
        .cpuid = 1u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = second_info.image_id,
        .loader = second_source,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };

    CHECK_EQ(cold_code_manager_init(&g_manager_first, &first_config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_second, &second_config), COLD_CODE_OK);

    test_set_cpu(0u);
    CHECK(cold_code_manager_current() == &g_manager_first);
    CHECK_EQ(cold_first_add(2, 3), 5);
    test_set_cpu(1u);
    CHECK(cold_code_manager_current() == &g_manager_second);
    CHECK_EQ(cold_second_mul(4, 3), 12);
    test_set_cpu(0u);
    CHECK_EQ(cold_first_add(10, 20), 30);
    test_set_cpu(1u);
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
    cold_code_file_loader_close(&first_file);
    cold_code_file_loader_close(&second_file);
    TEST_OK();
    return 0;
}
