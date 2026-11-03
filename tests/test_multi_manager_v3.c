#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_host.h"
#include "test_cpu.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(multi, g_storage_a, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
COLD_CODE_DEFINE_STORAGE(multi, g_storage_b, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;
static cold_code_memory_loader_t g_source;

int cold_multi_add(int lhs, int rhs);
int cold_multi_sub(int lhs, int rhs);
int cold_multi_mul(int lhs, int rhs);

typedef struct {
    cold_code_manager_t *manager;
    uint32_t cpu;
    int iterations;
    int ok;
} worker_t;

static void run_worker(worker_t *work) {
    int ok = 1;

    test_set_cpu(work->cpu);
    if (cold_code_manager_current() != work->manager) {
        work->ok = 0;
        return;
    }
    for (int i = 0; i < work->iterations; ++i) {
        if (cold_multi_add(2, 3) != 5 || cold_multi_sub(5, 3) != 2 ||
            cold_multi_mul(4, 3) != 12 || cold_multi_add(2, 3) != 5) {
            ok = 0;
            break;
        }
    }

    if (ok) {
        cold_code_stats_t stats;
        if (cold_code_manager_stats(work->manager, &stats) != COLD_CODE_OK) {
            ok = 0;
        } else if (stats.image_slots != 2u || stats.pool_slots != 1u ||
                   stats.loaded_slots != 1u ||
                   stats.loads != (uint64_t)(2 * work->iterations + 1) ||
                   stats.evictions != (uint64_t)(2 * work->iterations) ||
                   stats.hits != (uint64_t)(2 * work->iterations - 1)) {
            ok = 0;
        }
    }

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
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_MULTI_PATH, &image_size);
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
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager_a);
    config.exec_addr = &g_storage_b;
    config.exec_size = sizeof(g_storage_b);
    config.cpuid = 1u;
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &g_manager_a);
    test_set_cpu(1u);
    CHECK(cold_code_manager_current() == &g_manager_b);

    worker_t work_a = {.manager = &g_manager_a, .cpu = 0u, .iterations = 10, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .cpu = 1u, .iterations = 3, .ok = 0};
    run_worker(&work_a);
    run_worker(&work_b);
    CHECK(work_a.ok);
    CHECK(work_b.ok);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_a, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 2u);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 21u);
    CHECK_EQ(stats.misses, 21u);
    CHECK_EQ(stats.evictions, 20u);
    CHECK_EQ(stats.hits, 19u);

    CHECK_EQ(cold_code_manager_stats(&g_manager_b, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 7u);
    CHECK_EQ(stats.misses, 7u);
    CHECK_EQ(stats.evictions, 6u);
    CHECK_EQ(stats.hits, 5u);

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
