#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cold_code_context_ops_valid(const cold_code_context_ops_t *ops) {
    return ops != NULL && ops->abi_version == COLD_CODE_CONTEXT_ABI_VERSION &&
           ops->struct_size >= (uint32_t)sizeof(*ops) && ops->fatal != NULL;
}

bool cold_code_context_arch_valid(const cold_code_context_ops_t *ops) {
    if (ops == NULL) {
        return false;
    }
    switch (ops->arch) {
    case COLD_CODE_ARCH_X86_64:
    case COLD_CODE_ARCH_ARMV7M:
    case COLD_CODE_ARCH_AARCH64:
    case COLD_CODE_ARCH_RISCV32:
    case COLD_CODE_ARCH_RISCV64:
        return true;
    case COLD_CODE_ARCH_UNKNOWN:
        return false;
    }
    return false;
}

_Noreturn void cold_code_core_fatal(struct cold_code_manager_impl *manager,
                                    cold_code_fault_code_t code, uint32_t image_slot,
                                    const char *message) {
    if (manager != NULL) {
        ++manager->faults;
        if (manager->fault.fn != NULL) {
            cold_code_fault_info_t info = {
                .code = code,
                .manager = (const cold_code_manager_t *)manager,
                .image = &manager->image_view,
                .image_slot = image_slot,
                .message = message,
            };
            manager->fault.fn(manager->fault.ctx, &info);
        }
        if (manager->context != NULL && manager->context->fatal != NULL) {
            manager->context->fatal(message != NULL ? message : "cold_code: fatal loader error");
        }
    }

    /* No manager/context is available (for example before init). */
    __builtin_trap();
    for (;;) {
        /* A port violated the fatal() contract; do not return into the loader. */
    }
}
