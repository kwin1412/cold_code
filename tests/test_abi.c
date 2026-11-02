#include "cold/abi_types.h"
#include "cold_code.h"
#include "cold_code_abi_image.h"
#include "port_host.h"
#include "test_util.h"

static cold_code_storage_abi_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;

double cold_abi_mix(int a, long b, double c, int d, int e, int f, int g, int h, float i, double j);
int cold_abi_sum(int count, ...);
triple_t cold_abi_triple(long a, long b, long c);

int main(void) {
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, COLD_CODE_IMAGE_ABI_PATH);
    cold_code_storage_abi_bind(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &cold_code_image_abi, &config), COLD_CODE_OK);
    cold_code_manager_use(&g_manager);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 1);
    CHECK_EQ(stats.pool_slots, 1);

    /* Stack GPR + SSE arguments. */
    CHECK_NEAR(cold_abi_mix(1, 2L, 3.5, 4, 5, 6, 7, 8, 9.5f, 10.25), 56.25, 1e-9);

    /* Variadic arguments (AL must survive the stub slow path). */
    CHECK_EQ(cold_abi_sum(4, 10, 20, 30, 40), 100);

    /* 24-byte struct return through the hidden sret pointer. */
    triple_t triple = cold_abi_triple(10, 20, 30);
    CHECK_EQ(triple.a, 10);
    CHECK_EQ(triple.b, 20);
    CHECK_EQ(triple.c, 30);

    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 2);

    cold_code_manager_deinit(&g_manager);
    TEST_OK();
    return 0;
}
