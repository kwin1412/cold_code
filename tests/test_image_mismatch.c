#include "cold_code.h"
#include "cold_code_first_image.h"
#include "port_host.h"

static cold_code_storage_first_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;

int cold_second_mul(int lhs, int rhs);

int main(void) {
    cold_code_file_source_t source;
    cold_code_file_source_init(&source, COLD_CODE_IMAGE_FIRST_PATH);
    cold_code_storage_first_bind(&g_manager, &g_storage);

    cold_code_config_t config = {
        .source = &cold_code_file_source_ops,
        .source_ctx = &source,
    };

    /* The manager belongs to the first image, but the stub carries the second
       image's ID.  The loader must abort before jumping into the wrong image. */
    if (cold_code_manager_init(&g_manager, &cold_code_image_first, &config) != COLD_CODE_OK) {
        return 1;
    }
    cold_code_manager_use(&g_manager);
    return cold_second_mul(4, 3);
}
