#include <stdio.h>

#include "cold_code.h"
#include "cold_code_example_image.h"
#include "cold_code_host.h"
#include "cold_functions.h"

COLD_CODE_DEFINE_STORAGE(example, g_storage);
static cold_code_manager_t g_manager;

static void trace_event(void *ctx, cold_code_trace_event_t event, uint32_t image_slot,
                        uint32_t way) {
    (void)ctx;
    if (event == COLD_CODE_TRACE_LOAD) {
        fprintf(stderr, "[cold-code] load image slot %u into way %u\n", (unsigned)image_slot,
                (unsigned)way);
    } else {
        fprintf(stderr, "[cold-code] evict image slot %u from way %u\n", (unsigned)image_slot,
                (unsigned)way);
    }
}

int main(int argc, char **argv) {
    const char *image_path = argc > 1 ? argv[1] : COLD_CODE_IMAGE_EXAMPLE_PATH;
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, image_path);

    cold_code_manager_config_t config = {
        .image = &cold_code_image_example,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_file_source(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
        .trace = {.fn = trace_event, .ctx = NULL},
    };

    cold_code_status_t status = cold_code_manager_init(&g_manager, &config);
    if (status != COLD_CODE_OK) {
        fprintf(stderr, "cold_code_manager_init failed: %s\n", cold_code_status_string(status));
        return 1;
    }
    status = cold_code_manager_activate(&g_manager);
    if (status != COLD_CODE_OK) {
        return 1;
    }

    cold_code_stats_t stats;
    if (cold_code_manager_stats(&g_manager, &stats) != COLD_CODE_OK) {
        return 1;
    }
    printf("image slots: %u, pool slots: %u, loaded: %u\n", (unsigned)stats.image_slots,
           (unsigned)stats.pool_slots, (unsigned)stats.loaded_slots);
    fflush(stdout);

    /* add and sub share image slot 0: the second call is an LRU hit. */
    printf("add(2, 3) = %d\n", cold_code_add(2, 3));
    printf("sub(5, 3) = %d\n", cold_code_sub(5, 3));
    fflush(stdout);

    if (cold_code_manager_stats(&g_manager, &stats) != COLD_CODE_OK) {
        return 1;
    }
    printf("loaded after add/sub: %u/%u\n", (unsigned)stats.loaded_slots,
           (unsigned)stats.pool_slots);
    fflush(stdout);

    printf("mul(4, 3) = %d\n", cold_code_mul(4, 3));
    fflush(stdout);
    printf("add(2, 3) = %d\n", cold_code_add(2, 3));
    fflush(stdout);
    printf("div(8, 2) = %d\n", cold_code_div(8, 2));
    fflush(stdout);

    if (cold_code_manager_stats(&g_manager, &stats) != COLD_CODE_OK) {
        return 1;
    }
    printf("loaded final: %u/%u (image slots: %u)\n", (unsigned)stats.loaded_slots,
           (unsigned)stats.pool_slots, (unsigned)stats.image_slots);

    cold_code_manager_deinit(&g_manager);
    return 0;
}
