#include "cold_code.h"
#include "image_fixture.h"
#include "test_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Core-only test: provide a deliberately incompatible context port. */
static cold_code_manager_t *bad_current(void) {
    return NULL;
}

static void bad_set_current(cold_code_manager_t *manager) {
    (void)manager;
}

static void bad_fatal(const char *message) {
    (void)message;
    abort();
}

const cold_code_context_ops_t cold_code_context_ops = {
    .abi_version = 999u,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .current = bad_current,
    .set_current = bad_set_current,
    .fatal = bad_fatal,
    .sync_code = NULL,
};

_Static_assert(sizeof(cold_code_manager_t) == COLD_CODE_MANAGER_BYTES,
               "opaque manager size mismatch");
_Static_assert(sizeof(cold_code_image_header_t) == COLD_CODE_IMAGE_HEADER_SIZE,
               "wire header size mismatch");
_Static_assert(offsetof(cold_code_fast_state_t, state) == 36u, "fast-state state offset");
_Static_assert(offsetof(cold_code_fast_state_t, hits) == 48u, "fast-state hits offset");
_Static_assert(sizeof(cold_code_fast_state_t) == 56u, "fast-state size");

int main(void) {
    test_image_fixture_t fixture;
    CHECK_EQ(test_image_fixture_init(&fixture, 2u, 1u), COLD_CODE_OK);

    static unsigned char storage[4096] __attribute__((aligned(64)));
    static cold_code_memory_source_t memory;
    static cold_code_manager_t manager;

    cold_code_memory_source_init(&memory, fixture.bytes, fixture.size);
    cold_code_manager_config_t config = {
        .image = &fixture.image,
        .storage = storage,
        .storage_size = sizeof(storage),
        .pool_slots = 1u,
        .source = cold_code_memory_source(&memory),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };

    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_UNSUPPORTED);
    CHECK_EQ(cold_code_manager_activate(NULL), COLD_CODE_ERR_UNSUPPORTED);
    CHECK(cold_code_manager_current() == NULL);
    CHECK(!cold_code_manager_is_initialized(&manager));

    TEST_OK();
    return 0;
}
