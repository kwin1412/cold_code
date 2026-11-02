#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cold_code.h"
#include "cold_code_custom_image.h"
#include "cold_code_port.h"
#include "test_util.h"

/*
 * This executable links only cold_code_core (not the host port).  It provides
 * its own per-CPU port and memory source, proving the core is independent of
 * the platform implementation.
 */

#define CUSTOM_CPUS 2u

static cold_code_storage_custom_t g_storage_a COLD_CODE_STORAGE_SECTION;
static cold_code_storage_custom_t g_storage_b COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager_a;
static cold_code_manager_t g_manager_b;

uintptr_t cold_slot_address(void);

typedef struct {
    const unsigned char *data;
    size_t size;
} custom_source_t;

static custom_source_t g_image;

static bool custom_source_open(void *ctx, uint32_t *size_out) {
    custom_source_t *source = (custom_source_t *)ctx;
    if (source == NULL || source->data == NULL || size_out == NULL || source->size > UINT32_MAX) {
        return false;
    }
    *size_out = (uint32_t)source->size;
    return true;
}

static void custom_source_close(void *ctx) {
    (void)ctx;
}

static bool custom_source_read(void *ctx, uint32_t offset, void *dst, size_t len) {
    custom_source_t *source = (custom_source_t *)ctx;
    if (source == NULL || (dst == NULL && len != 0u)) {
        return false;
    }
    if ((uint64_t)offset + (uint64_t)len > (uint64_t)source->size) {
        return false;
    }
    memcpy(dst, source->data + offset, len);
    return true;
}

static const cold_code_source_ops_t g_source_ops = {
    .open = custom_source_open,
    .close = custom_source_close,
    .read = custom_source_read,
};

/* --- Per-CPU port implementation (no host port linked) ------------------ */

static cold_code_manager_t *g_managers[CUSTOM_CPUS];
static _Thread_local unsigned g_cpu;

static void custom_port_select_cpu(unsigned cpu) {
    g_cpu = cpu;
}

void cold_code_port_set_current(cold_code_manager_t *mgr) {
    g_managers[g_cpu] = mgr;
}

cold_code_manager_t *cold_code_port_current(void) {
    return g_managers[g_cpu];
}

COLD_CODE_PORT_NORETURN void cold_code_port_fatal(const char *message) {
    if (message != NULL) {
        fputs(message, stderr);
        fputc('\n', stderr);
    }
    abort();
}

COLD_CODE_PORT_NORETURN void cold_code_port_abort(void) {
    abort();
}

/* --- Test --------------------------------------------------------------- */

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
    cold_code_manager_use(work->manager);

    uintptr_t address = cold_slot_address();
    uintptr_t pool = (uintptr_t)work->manager->pool;
    if (address < pool || address - pool >= work->manager->image->slot_size) {
        ok = 0;
    }

    cold_code_stats_t stats;
    if (cold_code_manager_stats(work->manager, &stats) != COLD_CODE_OK || stats.loaded_slots != 1 ||
        stats.loads != 1 || stats.hits != 0 || stats.misses != 1) {
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
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_CUSTOM_PATH, &image_size);
    CHECK(image_data != NULL);

    g_image.data = image_data;
    g_image.size = image_size;
    cold_code_storage_custom_bind(&g_manager_a, &g_storage_a);
    cold_code_storage_custom_bind(&g_manager_b, &g_storage_b);

    cold_code_config_t config = {
        .source = &g_source_ops,
        .source_ctx = &g_image,
        .verify_crc = true,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager_a, &cold_code_image_custom, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&g_manager_b, &cold_code_image_custom, &config), COLD_CODE_OK);

    worker_t work_a = {.manager = &g_manager_a, .cpu = 0, .address = 0, .ok = 0};
    worker_t work_b = {.manager = &g_manager_b, .cpu = 1, .address = 0, .ok = 0};
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

    uintptr_t offset_a = work_a.address - (uintptr_t)g_manager_a.pool;
    uintptr_t offset_b = work_b.address - (uintptr_t)g_manager_b.pool;
    CHECK_EQ(offset_a, offset_b);
    CHECK(offset_a < (uintptr_t)g_manager_a.image->slot_size);
    CHECK(cold_code_manager_current() == NULL);

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
