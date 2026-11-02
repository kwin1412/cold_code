#ifndef COLD_CODE_PORT_FAST_X86_64_H
#define COLD_CODE_PORT_FAST_X86_64_H

#include "cold_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional x86-64 FAST_PATH accessor implemented by the platform port.
 *
 * Generated stubs built with FAST_PATH ON call this symbol directly using a
 * private ABI, not the SysV C ABI:
 *   - returns the current manager in %r11
 *   - preserves rax/AL, rdi, rsi, rdx, rcx, r8, r9, xmm0-7
 *   - may clobber r10, r11 and flags
 *   - uses no stack or SSE state
 *
 * The host port uses a local-exec TLS load; custom/embedded ports may use a
 * per-core table or task-local pointer.  Ports that only support the slow
 * stubs do not need to provide this symbol.
 */
cold_code_manager_t *cold_code_port_current_fast(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_FAST_X86_64_H */
