#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_host.h"
#include "test_cpu.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(slots, g_storage_a, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
COLD_CODE_DEFINE_STORAGE(slots, g_storage_b, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;
static cold_code_memory_loader_t g_source;

uintptr_t cold_slot_address(void);

typedef struct {
    cold_code_manager_t *manager;
    uint32_t cpu;
    uintptr_t address;
    int ok;
} worker_t;

static void run_worker(worker_t *work) {
    int ok = 1;

    test_set_cpu(work->cpu);
    if (cold_code_manager_current() != work->manager) {
        work->ok = 0;
        return;
    }
    uintptr_t address = cold_slot_address();
    uintptr_t pool = (uintptr_t)cold_code_manager_pool_base(work->manager);
    const cold_code_image_t *image = cold_code_manager_image(work->manager);
    if (image == NULL || address < pool || address - pool >= image->slot_size) {
        ok = 0;
    }

    cold_code_stats_t stats;
    if (ok && cold_code_manager_stats(work->manager, &stats) == COLD_CODE_OK) {
        if (stats.loaded_slots != 1u || stats.loads != 1u || stats.misses != 1u ||
            stats.hits != 0u) {
            ok = 0;
        }
    } else {
        ok = 0;
    }
    work->address = address;
    work->ok = ok;
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
    size_t image_size = 0;
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_SLOTS_PATH, &image_size);
    CHECK(image_data != NULL);

    cold_code_memory_loader_init(&g_source, image_data, image_size);
    cold_code_loader_t loader = cold_code_memory_loader(&g_source);
    cold_code_image_info_t info;
    CHECK(test_probe_ok(loader, 1u, sizeof(g_storage_a), &info));
    cold_code_manager_config_t config = {
        .loader = loader,
        .exec_addr = &g_storage_a,
        .exec_size = sizeof(g_storage_a),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = info.image_id,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    test_set_cpu(0u);
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &config), COLD_CODE_OK);
    config.exec_addr = &g_storage_b;
    config.exec_size = sizeof(g_storage_b);
    config.cpuid = 1u;
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager_a);

    worker_t work_a = {.manager = &g_manager_a, .cpu = 0u, .address = 0, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .cpu = 1u, .address = 0, .ok = 0};
    run_worker(&work_a);
    run_worker(&work_b);

    CHECK(work_a.ok);
    CHECK(work_b.ok);
    CHECK(work_a.address != 0);
    CHECK(work_b.address != 0);
    CHECK(work_a.address != work_b.address);

    uintptr_t pool_a = (uintptr_t)cold_code_manager_pool_base(&g_manager_a);
    uintptr_t pool_b = (uintptr_t)cold_code_manager_pool_base(&g_manager_b);
    uintptr_t offset_a = work_a.address - pool_a;
    uintptr_t offset_b = work_b.address - pool_b;
    CHECK_EQ(offset_a, offset_b);
    CHECK(offset_a < (uintptr_t)cold_code_manager_image(&g_manager_a)->slot_size);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_a, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 0u);

    CHECK_EQ(cold_code_manager_stats(&g_manager_b, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.hits, 0u);

    test_set_cpu(1u);
    CHECK_EQ(cold_code_manager_deinit(&g_manager_a), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager_b);
    test_set_cpu(0u);
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_deinit(&g_manager_b), COLD_CODE_OK);
    test_set_cpu(1u);
    CHECK(cold_code_manager_current() == NULL);

    free(image_data);
    TEST_OK();
    return 0;
}
