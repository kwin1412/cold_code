#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_slots_image.h"
#include "port_host.h"
#include "test_util.h"

/*
 * One image, two managers, two independent pools, two threads.  The same cold
 * function must be copied into different physical slots; its own runtime
 * address proves it.
 */

static cold_code_storage_slots_t g_storage_a COLD_CODE_STORAGE_SECTION;
static cold_code_storage_slots_t g_storage_b COLD_CODE_STORAGE_SECTION;
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

    cold_code_manager_use(work->manager);
    uintptr_t address = cold_slot_address();
    uintptr_t pool = (uintptr_t)work->manager->pool;
    if (address < pool || address - pool >= work->manager->image->slot_size) {
        ok = 0;
    }

    cold_code_stats_t stats;
    if (ok && cold_code_manager_stats(work->manager, &stats) == COLD_CODE_OK) {
        if (stats.loaded_slots != 1 || stats.loads != 1 || stats.misses != 1 || stats.hits != 0) {
            ok = 0;
        }
    } else {
        ok = 0;
    }

    cold_code_manager_use(NULL);
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
    cold_code_storage_slots_bind(&g_manager_a, &g_storage_a);
    cold_code_storage_slots_bind(&g_manager_b, &g_storage_b);

    cold_code_config_t config = {
        .source = &cold_code_memory_source_ops,
        .source_ctx = &g_source,
        .verify_crc = true,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &cold_code_image_slots, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &cold_code_image_slots, &config), COLD_CODE_OK);
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

    uintptr_t offset_a = work_a.address - (uintptr_t)g_manager_a.pool;
    uintptr_t offset_b = work_b.address - (uintptr_t)g_manager_b.pool;
    CHECK_EQ(offset_a, offset_b);
    CHECK(offset_a < (uintptr_t)g_manager_a.image->slot_size);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_a, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 0);

    CHECK_EQ(cold_code_manager_stats(&g_manager_b, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);
    CHECK_EQ(stats.hits, 0);

    cold_code_manager_deinit(&g_manager_a);
    cold_code_manager_deinit(&g_manager_b);
    free(image_data);
    TEST_OK();
    return 0;
}
