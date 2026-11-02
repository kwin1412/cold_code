#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "cold_code.h"
#include "port_host.h"

#if BENCH_FAST
#include "cold_code_bench_fast_image.h"
#define BENCH_IMAGE     cold_code_image_bench_fast
#define BENCH_PATH      COLD_CODE_IMAGE_BENCH_FAST_PATH
#define BENCH_STORAGE_T cold_code_storage_bench_fast_t
#define BENCH_BIND      cold_code_storage_bench_fast_bind
#define BENCH_LABEL     "fast"
#else
#include "cold_code_bench_slow_image.h"
#define BENCH_IMAGE     cold_code_image_bench_slow
#define BENCH_PATH      COLD_CODE_IMAGE_BENCH_SLOW_PATH
#define BENCH_STORAGE_T cold_code_storage_bench_slow_t
#define BENCH_BIND      cold_code_storage_bench_slow_bind
#define BENCH_LABEL     "slow"
#endif

static BENCH_STORAGE_T g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;

int cold_lru_add(int lhs, int rhs);
int cold_lru_sub(int lhs, int rhs);
int cold_lru_mul(int lhs, int rhs);
int cold_lru_div(int lhs, int rhs);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : BENCH_PATH;
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, path);
    BENCH_BIND(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };
    if (cold_code_manager_init(&g_manager, &BENCH_IMAGE, &config) != COLD_CODE_OK) {
        fprintf(stderr, "manager init failed\n");
        return 1;
    }
    cold_code_manager_use(&g_manager);

    const int iterations = 2000000;
    volatile long sink = 0;

    /* Warm slot 0. */
    sink += cold_lru_add(1, 2);

    uint64_t start = now_ns();
    for (int i = 0; i < iterations; ++i) {
        sink += cold_lru_add(1, 2);
    }
    uint64_t hit_ns = now_ns() - start;

    /* With POOL_SLOTS=1, alternating slot 0 and slot 2 forces a miss/load. */
    start = now_ns();
    for (int i = 0; i < iterations; ++i) {
        sink += cold_lru_add(1, 2);
        sink += cold_lru_div(8, 2);
    }
    uint64_t miss_ns = now_ns() - start;

    cold_code_stats_t stats;
    cold_code_manager_stats(&g_manager, &stats);
    printf("bench %s: hit %.2f ns/call, miss %.2f ns/call, "
           "hits=%llu misses=%llu loads=%llu evictions=%llu (sink=%ld)\n",
           BENCH_LABEL, (double)hit_ns / (double)iterations,
           (double)miss_ns / (double)(2 * iterations), (unsigned long long)stats.hits,
           (unsigned long long)stats.misses, (unsigned long long)stats.loads,
           (unsigned long long)stats.evictions, sink);

    cold_code_manager_deinit(&g_manager);
    return 0;
}
