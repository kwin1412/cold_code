#include "cold_code_baremetal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static cold_code_manager_t *g_managers[COLD_CODE_BAREMETAL_MAX_CORES];
static cold_code_baremetal_config_t g_config;

__attribute__((weak)) unsigned cold_code_baremetal_cpu_id(void) {
    return 0u;
}

static void baremetal_fatal(const char *message) {
    if (g_config.fatal != NULL) {
        g_config.fatal(g_config.fatal_ctx, message);
    }
    __builtin_trap();
}

static cold_code_manager_t *baremetal_current(void) {
    unsigned cpu = cold_code_baremetal_cpu_id();
    if (cpu >= g_config.core_count || cpu >= COLD_CODE_BAREMETAL_MAX_CORES) {
        return NULL;
    }
    return g_managers[cpu];
}

static void baremetal_set_current(cold_code_manager_t *manager) {
    unsigned cpu = cold_code_baremetal_cpu_id();
    if (cpu >= g_config.core_count || cpu >= COLD_CODE_BAREMETAL_MAX_CORES) {
        baremetal_fatal("cold_code: baremetal cpu id out of range");
    }
    g_managers[cpu] = manager;
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

const cold_code_context_ops_t cold_code_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_BAREMETAL_ARCH,
    .current = baremetal_current,
    .set_current = baremetal_set_current,
    .fatal = baremetal_fatal,
    .sync_code = baremetal_sync_code,
};

void cold_code_baremetal_init(const cold_code_baremetal_config_t *config) {
    if (config == NULL || config->core_count == 0u ||
        config->core_count > COLD_CODE_BAREMETAL_MAX_CORES) {
        baremetal_fatal("cold_code: invalid baremetal configuration");
    }
    g_config = *config;
    for (uint32_t i = 0; i < COLD_CODE_BAREMETAL_MAX_CORES; ++i) {
        g_managers[i] = NULL;
    }
}
