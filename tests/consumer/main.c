#include <stdio.h>

#include "cold_code.h"
#include "cold_code_consumer_image.h"
#include "port_host.h"

static cold_code_storage_consumer_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;

int cold_consumer_add(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, COLD_CODE_IMAGE_CONSUMER_PATH);
    cold_code_storage_consumer_bind(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };
    if (cold_code_manager_init(&g_manager, &cold_code_image_consumer, &config) != COLD_CODE_OK) {
        return 1;
    }
    cold_code_manager_use(&g_manager);

    if (cold_consumer_add(2, 3) != 5) {
        return 2;
    }

    cold_code_manager_deinit(&g_manager);
    printf("consumer ok\n");
    return 0;
}
