#include <stdio.h>

#include "cold_code.h"
#include "cold_code_consumer_image.h"
#include "cold_code_host.h"

COLD_CODE_DEFINE_STORAGE(consumer, g_storage);
static cold_code_manager_t g_manager;

int cold_consumer_add(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t file;
    cold_code_file_source_init(&file, COLD_CODE_IMAGE_CONSUMER_PATH);
    cold_code_manager_config_t config = {
        .image = &cold_code_image_consumer,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_file_source(&file),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    if (cold_code_manager_init(&g_manager, &config) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_code_manager_activate(&g_manager) != COLD_CODE_OK) {
        return 1;
    }
    if (cold_consumer_add(2, 3) != 5) {
        return 2;
    }
    if (cold_code_manager_deinit(&g_manager) != COLD_CODE_OK) {
        return 3;
    }
    printf("consumer ok\n");
    return 0;
}
