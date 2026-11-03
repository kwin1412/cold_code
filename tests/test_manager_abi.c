#include "cold_code.h"
#include "image_fixture.h"
#include "test_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Core-only test: provide a deliberately incompatible context ops table. */
static void bad_fatal(const char *message) {
    (void)message;
    abort();
}

static const cold_code_context_ops_t bad_context = {
    .abi_version = 999u,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_ARCH_X86_64,
    .fatal = bad_fatal,
    .sync_code = NULL,
};

_Static_assert(COLD_CODE_VERSION_MAJOR == 5, "project major version mismatch");
_Static_assert(COLD_CODE_IMAGE_VERSION == 4u, "image wire version mismatch");
_Static_assert(COLD_CODE_ABI_VERSION == 2u, "image ABI version mismatch");
_Static_assert(COLD_CODE_CONTEXT_ABI_VERSION == 2u, "context ABI version mismatch");
_Static_assert(sizeof(cold_code_manager_t) == COLD_CODE_MANAGER_BYTES,
               "opaque manager size mismatch");
_Static_assert(sizeof(cold_code_image_header_t) == COLD_CODE_IMAGE_HEADER_SIZE,
               "wire header size mismatch");
_Static_assert(sizeof(cold_code_image_info_t) == 48u, "info size");
_Static_assert(offsetof(cold_code_image_info_t, storage_align) == 24u, "info layout");
_Static_assert(offsetof(cold_code_image_info_t, payload_offset) == 28u, "info layout");
_Static_assert(offsetof(cold_code_image_info_t, flags) == 40u, "info layout");
_Static_assert(offsetof(cold_code_image_info_t, reserved) == 44u, "info layout");
_Static_assert(offsetof(cold_code_image_t, image_id) == 0u, "runtime view layout");
_Static_assert(offsetof(cold_code_image_t, struct_size) == 24u, "runtime view layout");
_Static_assert(offsetof(cold_code_image_t, storage_align) == 28u, "runtime view layout");
_Static_assert(offsetof(cold_code_image_t, name) == 32u, "runtime view layout");
_Static_assert(sizeof(cold_code_image_t) == 40u, "runtime view size");

_Static_assert(offsetof(cold_code_loader_t, load_fptr) == 0u, "loader layout");
_Static_assert(offsetof(cold_code_loader_t, load_ctx) == sizeof(void *), "loader layout");
_Static_assert(offsetof(cold_code_loader_t, image_size) == 2u * sizeof(void *), "loader layout");
_Static_assert(sizeof(cold_code_loader_t) == 3u * sizeof(void *), "loader size");
_Static_assert(offsetof(cold_code_context_ops_t, abi_version) == 0u, "context layout");
_Static_assert(offsetof(cold_code_context_ops_t, struct_size) == 4u, "context layout");
_Static_assert(offsetof(cold_code_context_ops_t, arch) == 8u, "context layout");
_Static_assert(offsetof(cold_code_context_ops_t, fatal) == 16u, "context layout");
_Static_assert(offsetof(cold_code_context_ops_t, sync_code) == 24u, "context layout");
_Static_assert(sizeof(cold_code_context_ops_t) == 32u, "context size");

_Static_assert(offsetof(cold_code_manager_config_t, loader) == 0u, "config layout");
_Static_assert(offsetof(cold_code_manager_config_t, exec_addr) == sizeof(cold_code_loader_t),
               "config layout");
_Static_assert(offsetof(cold_code_manager_config_t, cpuid) > 0u, "config layout");
_Static_assert(offsetof(cold_code_manager_config_t, context) > 0u, "config layout");

int main(void) {
    test_image_fixture_t fixture;
    CHECK_EQ(test_image_fixture_init(&fixture, 2u, 1u), COLD_CODE_OK);

    static unsigned char storage[4096] __attribute__((aligned(64)));
    static cold_code_memory_loader_t memory;
    static cold_code_manager_t manager;

    cold_code_memory_loader_init(&memory, fixture.bytes, fixture.size);
    cold_code_loader_t loader = cold_code_memory_loader(&memory);
    cold_code_image_info_t info;
    CHECK_EQ(cold_code_manager_probe(&loader, &info), COLD_CODE_OK);
    CHECK_EQ(info.slot_count, 2u);
    CHECK_EQ(info.max_pool_slots, 1u);
    CHECK_EQ(info.storage_align, 64u);

    cold_code_manager_config_t config = {
        .loader = loader,
        .exec_addr = storage,
        .exec_size = sizeof(storage),
        .cpuid = 0u,
        .context = &bad_context,
        .pool_slots = 1u,
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };

    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_UNSUPPORTED);
    CHECK(cold_code_manager_current() == NULL);
    CHECK(!cold_code_manager_is_initialized(&manager));

    TEST_OK();
    return 0;
}
