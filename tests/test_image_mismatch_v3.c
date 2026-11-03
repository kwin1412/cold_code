#include <stdio.h>
#include <stdlib.h>

#include "cold_code.h"
#include "cold_code_host.h"
#include "test_util.h"

COLD_CODE_DEFINE_STORAGE(first, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager;

int cold_second_mul(int lhs, int rhs);

static void fault_event(void *ctx, const cold_code_fault_info_t *info) {
    (void)ctx;
    fprintf(stderr, "fault code %d: %s\n", (int)info->code, info->message);
    abort();
}

int main(void) {
    cold_code_file_loader_t file;
    cold_code_file_loader_init(&file, COLD_CODE_IMAGE_FIRST_PATH);

    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .loader = cold_code_file_loader(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
        .fault = {.fn = fault_event, .ctx = NULL},
    };

    if (cold_code_manager_init(&g_manager, &config) != COLD_CODE_OK) {
        return 1;
    }

    /* The manager belongs to the first image, but the stub carries the second
       image's descriptor.  The loader must abort before jumping. */
    return cold_second_mul(4, 3);
}
