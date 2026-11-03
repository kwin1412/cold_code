#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_baremetal.h"
#include "cold_code_host.h"
#include "test_cpu.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(multi_port, g_storage_host, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
COLD_CODE_DEFINE_STORAGE(multi_port_b, g_storage_bm, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager_host;
static cold_code_manager_t g_manager_bm;
static cold_code_memory_loader_t g_source;

uintptr_t cold_slot_address(void);

static void multi_port_fatal(void *ctx, const char *message) {
    (void)ctx;
    if (message != NULL) {
        fputs(message, stderr);
        fputc('\n', stderr);
    }
    abort();
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *data = malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        free(data);
        return NULL;
    }
    *size_out = (size_t)length;
    return data;
}

int main(void) {
    cold_code_baremetal_config_t bm_config = {
        .fatal = multi_port_fatal,
        .fatal_ctx = NULL,
        .sync_code = NULL,
        .sync_code_ctx = NULL,
    };
    cold_code_baremetal_init(&bm_config);

    size_t image_size = 0;
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_MULTI_PORT_PATH, &image_size);
    CHECK(image_data != NULL);

    cold_code_memory_loader_init(&g_source, image_data, image_size);
    cold_code_loader_t loader = cold_code_memory_loader(&g_source);
    cold_code_image_info_t info;
    CHECK(test_probe_ok(loader, 1u, sizeof(g_storage_host), &info));

    cold_code_manager_config_t host_config = {
        .loader = loader,
        .exec_addr = &g_storage_host,
        .exec_size = sizeof(g_storage_host),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = info.image_id,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    cold_code_manager_config_t bm_config_mgr = {
        .loader = loader,
        .exec_addr = &g_storage_bm,
        .exec_size = sizeof(g_storage_bm),
        .cpuid = 1u,
        .context = &cold_code_baremetal_context_ops,
        .pool_slots = 1u,
        .expected_image_id = info.image_id,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };

    test_set_cpu(0u);
    CHECK_EQ(cold_code_manager_init(&g_manager_host, &host_config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_bm, &bm_config_mgr), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager_host);

    uintptr_t host_addr = cold_slot_address();
    test_set_cpu(1u);
    CHECK(cold_code_manager_current() == &g_manager_bm);
    uintptr_t bm_addr = cold_slot_address();

    CHECK(host_addr != 0);
    CHECK(bm_addr != 0);
    CHECK(host_addr != bm_addr);
    CHECK_EQ(host_addr - (uintptr_t)cold_code_manager_pool_base(&g_manager_host),
             bm_addr - (uintptr_t)cold_code_manager_pool_base(&g_manager_bm));

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_host, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(cold_code_manager_stats(&g_manager_bm, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loads, 1u);

    CHECK_EQ(cold_code_manager_deinit(&g_manager_bm), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_deinit(&g_manager_host), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    free(image_data);
    TEST_OK();
    return 0;
}
