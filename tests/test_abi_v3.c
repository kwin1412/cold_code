#include "cold/abi_types.h"
#include "cold_code.h"
#include "cold_code_host.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(abi, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager;

double cold_abi_mix(int a, long b, double c, int d, int e, int f, int g, int h, float i, double j);
int cold_abi_sum(int count, ...);
triple_t cold_abi_triple(long a, long b, long c);

int main(void) {
    cold_code_file_loader_t file;
    cold_code_file_loader_init(&file, COLD_CODE_IMAGE_ABI_PATH);
    cold_code_loader_t source = cold_code_file_loader(&file);
    cold_code_image_info_t info;
    CHECK(test_probe_ok(source, 1u, sizeof(g_storage), &info));

    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = info.image_id,
        .loader = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &config), COLD_CODE_OK);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1u);
    CHECK_EQ(stats.pool_slots, 1u);

    CHECK_NEAR(cold_abi_mix(1, 2L, 3.5, 4, 5, 6, 7, 8, 9.5f, 10.25), 56.25, 1e-9);
    CHECK_EQ(cold_abi_sum(4, 10, 20, 30, 40), 100);
    triple_t triple = cold_abi_triple(10, 20, 30);
    CHECK_EQ(triple.a, 10);
    CHECK_EQ(triple.b, 20);
    CHECK_EQ(triple.c, 30);

    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 2u);

    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    cold_code_file_loader_close(&file);
    TEST_OK();
    return 0;
}
