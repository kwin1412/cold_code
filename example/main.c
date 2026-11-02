#include <stdio.h>

#include "cold_code.h"
#include "cold_code_example_image.h"
#include "cold_functions.h"
#include "port_host.h"

static cold_code_storage_example_t g_storage COLD_CODE_STORAGE_SECTION;
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
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, image_path);

    cold_code_storage_example_bind(&g_manager, &g_storage);
    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
        .verify_crc = true,
        .trace = trace_event,
    };

    cold_code_status_t status =
        cold_code_manager_init(&g_manager, &cold_code_image_example, &config);
    if (status != COLD_CODE_OK) {
        fprintf(stderr, "cold_code_manager_init failed: %s\n", cold_code_status_string(status));
        return 1;
    }
    cold_code_manager_use(&g_manager);

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

    /* mul lives in image slot 1: this evicts slot 0 with POOL_SLOTS=1. */
    printf("mul(4, 3) = %d\n", cold_code_mul(4, 3));
    fflush(stdout);

    /* Reloading slot 0 evicts slot 1. */
    printf("add(2, 3) = %d\n", cold_code_add(2, 3));
    fflush(stdout);

    /* div lives in image slot 2: this evicts slot 0 again. */
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
