#ifndef COLD_CODE_CONTEXT_H
#define COLD_CODE_CONTEXT_H

#include "cold_code/types.h"
#include "cold_code/version.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Current executing CPU index.  The core defines a weak default returning 0;
 * multi-CPU applications/targets provide a strong per-core definition (for
 * example by reading MPIDR/COREID).  The value must be less than
 * COLD_CODE_MAX_CPUS.
 *
 * The core owns the per-CPU manager table.  Every CPU may access its own entry
 * concurrently; concurrent init/deinit of the same cpuid is not supported.
 */
uint32_t cold_code_cpu_id(void);

/* Manager bound to the executing CPU, or NULL.  Read-only introspection. */
cold_code_manager_t *cold_code_manager_current(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_CONTEXT_H */
