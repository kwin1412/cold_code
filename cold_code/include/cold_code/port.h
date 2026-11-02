#ifndef COLD_CODE_PORT_H
#define COLD_CODE_PORT_H

#include "cold_code/types.h"
#include "cold_code/version.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform context port.  Exactly one implementation is linked into an
 * executable; host/baremetal/custom ports define this symbol.  The backend
 * owns the per-thread/per-core current-manager state.
 *
 * fatal() must not return.  sync_code() is optional and is called after a slot
 * is copied into executable RAM and before it is marked resident; embedded
 * ports use it for D-cache clean + I-cache invalidate.
 */
typedef struct cold_code_context_ops {
    uint32_t abi_version; /* COLD_CODE_CONTEXT_ABI_VERSION */
    uint32_t struct_size;
    uint32_t arch; /* cold_code_arch_t */
    cold_code_manager_t *(*current)(void);
    void (*set_current)(cold_code_manager_t *manager);
    void (*fatal)(const char *message);
    void (*sync_code)(void *addr, size_t size);
} cold_code_context_ops_t;

extern const cold_code_context_ops_t cold_code_context_ops;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_H */
