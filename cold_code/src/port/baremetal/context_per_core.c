#include "cold_code_baremetal.h"

#include <stddef.h>

static cold_code_baremetal_config_t g_config;

static void baremetal_fatal(const char *message) {
    if (g_config.fatal != NULL) {
        g_config.fatal(g_config.fatal_ctx, message);
    }
    __builtin_trap();
}

static void baremetal_sync_code(void *addr, size_t size) {
    if (g_config.sync_code != NULL) {
        g_config.sync_code(g_config.sync_code_ctx, addr, size);
    }
}

#if defined(__x86_64__)
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_X86_64
#elif defined(__aarch64__)
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_AARCH64
#elif defined(__arm__)
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_ARMV7M
#elif defined(__riscv) && (__riscv_xlen == 64)
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_RISCV64
#elif defined(__riscv)
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_RISCV32
#else
#define COLD_CODE_BAREMETAL_ARCH COLD_CODE_ARCH_UNKNOWN
#endif

const cold_code_context_ops_t cold_code_baremetal_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_BAREMETAL_ARCH,
    .fatal = baremetal_fatal,
    .sync_code = baremetal_sync_code,
};

void cold_code_baremetal_init(const cold_code_baremetal_config_t *config) {
    if (config == NULL) {
        baremetal_fatal("cold_code: invalid baremetal configuration");
    }
    g_config = *config;
}
