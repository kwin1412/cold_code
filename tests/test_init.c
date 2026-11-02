#include "cold_code.h"
#include "cold_code_image.h"
#include "test_util.h"

int main(void) {
    const cold_code_image_t valid = cold_code_image;
    cold_code_image_t invalid = cold_code_image;
    cold_code_image_t other = cold_code_image;

    CHECK(!cold_code_init(NULL, COLD_CODE_IMAGE_PATH));
    CHECK(!cold_code_init(&valid, NULL));

    invalid = valid;
    invalid.slot_size = 0;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    invalid = valid;
    invalid.slot_count = 0;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    invalid = valid;
    invalid.pool_slot_count = 0;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    invalid = valid;
    invalid.pool_slot_count = COLD_CODE_MAX_POOL_SLOTS + 1;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    invalid = valid;
    invalid.slot_count = 1;
    invalid.pool_slot_count = 2;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    invalid = valid;
    invalid.slot_size = UINT32_MAX;
    invalid.slot_count = 2;
    invalid.pool_slot_count = 1;
    CHECK(!cold_code_init(&invalid, COLD_CODE_IMAGE_PATH));

    CHECK(!cold_code_init(&valid, "/nonexistent/cold-code-test-image.bin"));

    /* None of the failures above may have initialised the loader. */
    CHECK(cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH));
    CHECK_EQ(cold_code_total_slots(), 1);
    CHECK_EQ(cold_code_pool_slots(), 1);
    CHECK_EQ(cold_code_loaded_slots(), 0);

    /* Same descriptor: no-op success. Different address: rejected. */
    CHECK(cold_code_init(&cold_code_image, COLD_CODE_IMAGE_PATH));
    CHECK(!cold_code_init(&other, COLD_CODE_IMAGE_PATH));

    TEST_OK();
    return 0;
}
