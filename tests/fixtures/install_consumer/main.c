#include <stdio.h>

#include "cold_code.h"
#include "cold_code_host.h"

COLD_CODE_DEFINE_STORAGE(consumer, g_storage,
    COLD_CODE_STORAGE_SIZE(64u, COLD_CODE_IMAGE_CONSUMER_MAX_POOL_SLOTS,
                           COLD_CODE_IMAGE_CONSUMER_SLOT_SIZE,
                           COLD_CODE_IMAGE_CONSUMER_STORAGE_ALIGN),
    COLD_CODE_IMAGE_CONSUMER_STORAGE_ALIGN);
static cold_code_manager_t g_manager;

int cold_consumer_add(int lhs, int rhs);

int main(void) {
    cold_code_file_loader_t file;
    cold_code_file_loader_init(&file, COLD_CODE_IMAGE_CONSUMER_PATH);
    cold_code_loader_t source = cold_code_file_loader(&file);
    cold_code_image_info_t info;
    if (cold_code_manager_probe(&source, &info) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_code_storage_required_size(&info, 1u) > sizeof(g_storage)) {
        return 1;
    }
    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = info.image_id,
        .loader = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
    };
    if (cold_code_manager_init(&g_manager, &config) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_consumer_add(2, 3) != 5) {
        return 2;
    }
    if (cold_code_manager_deinit(&g_manager) != COLD_CODE_OK) {
        return 3;
    }
    cold_code_file_loader_close(&file);
    printf("consumer ok\n");
    return 0;
}
