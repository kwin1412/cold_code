#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_multi_image.h"
#include "port_host.h"
#include "test_util.h"

/*
 * Two threads, two managers, two independent pools, one shared read-only
 * image.  Each thread selects its own manager; no manager is shared, so the
 * runtime performs no locking and no slot pinning.
 */

static cold_code_storage_multi_t g_storage_a COLD_CODE_STORAGE_SECTION;
static cold_code_storage_multi_t g_storage_b COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;
static cold_code_memory_source_t g_source;

int cold_multi_add(int lhs, int rhs);
int cold_multi_sub(int lhs, int rhs);
int cold_multi_mul(int lhs, int rhs);

typedef struct {
    cold_code_manager_t *manager;
    int iterations;
    int ok;
} worker_t;

static void *worker(void *arg) {
    worker_t *work = (worker_t *)arg;
    int ok = 1;

    cold_code_manager_use(work->manager);
    for (int i = 0; i < work->iterations; ++i) {
        if (cold_multi_add(2, 3) != 5 || cold_multi_sub(5, 3) != 2 || cold_multi_mul(4, 3) != 12 ||
            cold_multi_add(2, 3) != 5) {
            ok = 0;
            break;
        }
    }

    if (ok) {
        cold_code_stats_t stats;
        if (cold_code_manager_stats(work->manager, &stats) != COLD_CODE_OK) {
            ok = 0;
        } else if (stats.image_slots != 2 || stats.pool_slots != 1 || stats.loaded_slots != 1 ||
                   stats.loads != (uint64_t)(2 * work->iterations + 1) ||
                   stats.evictions != (uint64_t)(2 * work->iterations) ||
                   stats.hits != (uint64_t)(2 * work->iterations - 1)) {
            ok = 0;
        }
    }

    cold_code_manager_use(NULL);
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
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_MULTI_PATH, &image_size);
    CHECK(image_data != NULL);

    cold_code_memory_source_init(&g_source, image_data, image_size);
    cold_code_storage_multi_bind(&g_manager_a, &g_storage_a);
    cold_code_storage_multi_bind(&g_manager_b, &g_storage_b);

    cold_code_config_t config = {
        .source = &cold_code_memory_source_ops,
        .source_ctx = &g_source,
        .verify_crc = true,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &cold_code_image_multi, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &cold_code_image_multi, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    worker_t work_a = {.manager = &g_manager_a, .iterations = 10, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .iterations = 3, .ok = 0};
    pthread_t thread_a;
    pthread_t thread_b;
    CHECK_EQ(pthread_create(&thread_a, NULL, worker, &work_a), 0);
    CHECK_EQ(pthread_create(&thread_b, NULL, worker, &work_b), 0);
    CHECK_EQ(pthread_join(thread_a, NULL), 0);
    CHECK_EQ(pthread_join(thread_b, NULL), 0);

    /* The workers' per-thread selection must not leak into this thread. */
    CHECK(cold_code_manager_current() == NULL);
    CHECK(work_a.ok);
    CHECK(work_b.ok);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager_a, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, 2);
    CHECK_EQ(stats.pool_slots, 1);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 21);
    CHECK_EQ(stats.misses, 21);
    CHECK_EQ(stats.evictions, 20);
    CHECK_EQ(stats.hits, 19);

    CHECK_EQ(cold_code_manager_stats(&g_manager_b, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 7);
    CHECK_EQ(stats.misses, 7);
    CHECK_EQ(stats.evictions, 6);
    CHECK_EQ(stats.hits, 5);

    cold_code_manager_deinit(&g_manager_a);
    cold_code_manager_deinit(&g_manager_b);
    free(image_data);
    TEST_OK();
    return 0;
}
