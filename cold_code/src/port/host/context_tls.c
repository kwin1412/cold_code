#include "cold_code.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Host context port: one current manager per thread.  The variable is hidden
 * but non-static so the x86-64 fast accessor can load it with local-exec TLS.
 * This port is intended for statically linked host executables.
 */
_Thread_local cold_code_manager_t *cold_code_host_current_manager
    __attribute__((visibility("hidden")));

static cold_code_manager_t *host_current(void) {
    return cold_code_host_current_manager;
}

static void host_set_current(cold_code_manager_t *manager) {
    cold_code_host_current_manager = manager;
}

static void host_fatal(const char *message) {
    if (message != NULL) {
        fputs(message, stderr);
        fputc('\n', stderr);
    }
    abort();
}

#if defined(__x86_64__)
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_X86_64
#elif defined(__aarch64__)
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_AARCH64
#elif defined(__arm__)
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_ARMV7M
#elif defined(__riscv) && (__riscv_xlen == 64)
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_RISCV64
#elif defined(__riscv)
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_RISCV32
#else
#define COLD_CODE_HOST_ARCH COLD_CODE_ARCH_UNKNOWN
#endif

const cold_code_context_ops_t cold_code_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_HOST_ARCH,
    .current = host_current,
    .set_current = host_set_current,
    .fatal = host_fatal,
    .sync_code = NULL,
};
