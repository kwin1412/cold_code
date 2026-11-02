#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_host.h"
#include "cold_code_slots_image.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(slots, g_storage_a);
COLD_CODE_DEFINE_STORAGE(slots, g_storage_b);
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;
static cold_code_memory_source_t g_source;

uintptr_t cold_slot_address(void);

typedef struct {
    cold_code_manager_t *manager;
    uintptr_t address;
    int ok;
} worker_t;

static void *worker(void *arg) {
    worker_t *work = (worker_t *)arg;
    int ok = 1;

    if (cold_code_manager_activate(work->manager) != COLD_CODE_OK) {
        work->ok = 0;
        return NULL;
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

    cold_code_manager_activate(NULL);
    work->address = address;
    work->ok = ok;
    return NULL;
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

    cold_code_memory_source_init(&g_source, image_data, image_size);
    cold_code_source_t source = cold_code_memory_source(&g_source);
    cold_code_manager_config_t config = {
        .image = &cold_code_image_slots,
        .storage = &g_storage_a,
        .storage_size = sizeof(g_storage_a),
        .pool_slots = 1u,
        .source = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &config), COLD_CODE_OK);
    config.storage = &g_storage_b;
    config.storage_size = sizeof(g_storage_b);
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    worker_t work_a = {.manager = &g_manager_a, .address = 0, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .address = 0, .ok = 0};
    pthread_t thread_a;
    pthread_t thread_b;
    CHECK_EQ(pthread_create(&thread_a, NULL, worker, &work_a), 0);
    CHECK_EQ(pthread_create(&thread_b, NULL, worker, &work_b), 0);
    CHECK_EQ(pthread_join(thread_a, NULL), 0);
    CHECK_EQ(pthread_join(thread_b, NULL), 0);

    CHECK(cold_code_manager_current() == NULL);
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

    CHECK_EQ(cold_code_manager_deinit(&g_manager_a), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_deinit(&g_manager_b), COLD_CODE_OK);
    free(image_data);
    TEST_OK();
    return 0;
}
