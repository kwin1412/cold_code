#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_custom_image.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(custom, g_storage_a);
COLD_CODE_DEFINE_STORAGE(custom, g_storage_b);
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;
static cold_code_memory_source_t g_image;

uintptr_t cold_slot_address(void);

#define CUSTOM_CPUS 2u

static cold_code_manager_t *g_managers[CUSTOM_CPUS];
static _Thread_local unsigned g_cpu;

/* TLS mirror used by the private-ABI fast accessor. */
_Thread_local cold_code_manager_t *cold_code_custom_current_manager
    __attribute__((visibility("hidden")));

static void custom_port_select_cpu(unsigned cpu) {
    g_cpu = cpu;
}

static cold_code_manager_t *custom_current(void) {
    return g_managers[g_cpu];
}

static void custom_set_current(cold_code_manager_t *manager) {
    g_managers[g_cpu] = manager;
    cold_code_custom_current_manager = manager;
}

static void custom_fatal(const char *message) {
    if (message != NULL) {
        fputs(message, stderr);
        fputc('\n', stderr);
    }
    abort();
}

const cold_code_context_ops_t cold_code_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .current = custom_current,
    .set_current = custom_set_current,
    .fatal = custom_fatal,
    .sync_code = NULL,
};

typedef struct {
    cold_code_manager_t *manager;
    unsigned cpu;
    uintptr_t address;
    int ok;
} worker_t;

static void *worker(void *arg) {
    worker_t *work = (worker_t *)arg;
    int ok = 1;

    custom_port_select_cpu(work->cpu);
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
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_CUSTOM_PATH, &image_size);
    CHECK(image_data != NULL);

    cold_code_memory_source_init(&g_image, image_data, image_size);
    cold_code_source_t source = cold_code_memory_source(&g_image);
    cold_code_manager_config_t config = {
        .image = &cold_code_image_custom,
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

    worker_t work_a = {.manager = &g_manager_a, .cpu = 0u, .address = 0, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .cpu = 1u, .address = 0, .ok = 0};
    pthread_t thread_a;
    pthread_t thread_b;
    CHECK_EQ(pthread_create(&thread_a, NULL, worker, &work_a), 0);
    CHECK_EQ(pthread_create(&thread_b, NULL, worker, &work_b), 0);
    CHECK_EQ(pthread_join(thread_a, NULL), 0);
    CHECK_EQ(pthread_join(thread_b, NULL), 0);

    CHECK(work_a.ok);
    CHECK(work_b.ok);
    CHECK(work_a.address != 0);
    CHECK(work_b.address != 0);
    CHECK(work_a.address != work_b.address);

    uintptr_t offset_a = work_a.address - (uintptr_t)cold_code_manager_pool_base(&g_manager_a);
    uintptr_t offset_b = work_b.address - (uintptr_t)cold_code_manager_pool_base(&g_manager_b);
    CHECK_EQ(offset_a, offset_b);
    CHECK(offset_a < (uintptr_t)cold_code_manager_image(&g_manager_a)->slot_size);
    CHECK(cold_code_manager_current() == NULL);

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
