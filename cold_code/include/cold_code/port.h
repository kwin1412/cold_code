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
 * Platform context operations.  The caller passes one of these tables through
 * cold_code_manager_config_t.context; the manager stores the pointer and uses
 * it for architecture checks, fatal reporting and cache maintenance.  Multiple
 * ports may be linked into one executable and selected per manager.
 *
 * fatal() must not return.  sync_code() is optional and is called after a slot
 * is copied into executable RAM and before it is marked resident; embedded
 * ports use it for D-cache clean + I-cache invalidate.  Both callbacks may be
 * invoked from more than one CPU, so implementations must be reentrant.
 */
typedef struct cold_code_context_ops {
    uint32_t abi_version; /* COLD_CODE_CONTEXT_ABI_VERSION */
    uint32_t struct_size;
    uint32_t arch; /* cold_code_arch_t */
    void (*fatal)(const char *message);
    void (*sync_code)(void *addr, size_t size);
} cold_code_context_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_H */
