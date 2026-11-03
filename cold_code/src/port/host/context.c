#include "cold_code_host.h"

#include <stdio.h>
#include <stdlib.h>

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

const cold_code_context_ops_t cold_code_host_context_ops = {
    .abi_version = COLD_CODE_CONTEXT_ABI_VERSION,
    .struct_size = (uint32_t)sizeof(cold_code_context_ops_t),
    .arch = COLD_CODE_HOST_ARCH,
    .fatal = host_fatal,
    .sync_code = NULL,
};
