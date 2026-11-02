#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_first_image.h"
#include "cold_code_host.h"

COLD_CODE_DEFINE_STORAGE(first, g_storage);
static cold_code_manager_t g_manager;

int cold_second_mul(int lhs, int rhs);

static void fault_event(void *ctx, const cold_code_fault_info_t *info) {
    (void)ctx;
    fprintf(stderr, "fault code %d: %s\n", (int)info->code, info->message);
    abort();
}

int main(void) {
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, COLD_CODE_IMAGE_FIRST_PATH);

    cold_code_manager_config_t config = {
        .image = &cold_code_image_first,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_file_source(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
        .fault = {.fn = fault_event, .ctx = NULL},
    };

    if (cold_code_manager_init(&g_manager, &config) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_code_manager_activate(&g_manager) != COLD_CODE_OK) {
        return 1;
    }

    /* The manager belongs to the first image, but the stub carries the second
       image's descriptor.  The loader must abort before jumping. */
    return cold_second_mul(4, 3);
}
