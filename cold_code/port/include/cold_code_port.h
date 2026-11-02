#ifndef COLD_CODE_PORT_H
#define COLD_CODE_PORT_H

#include "cold_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COLD_CODE_PORT_API_VERSION 1u

#if defined(__cplusplus)
#define COLD_CODE_PORT_NORETURN [[noreturn]]
#else
#define COLD_CODE_PORT_NORETURN _Noreturn
#endif

/*
 * Port contract implemented by exactly one port library per executable.
 *
 * cold_code_core only calls these hooks; it does not define them.  Link either
 * the default cold_code convenience target (core + host port) or
 * cold_code_core plus a custom/embedded port implementation.
 *
 * The current manager is execution-context-local.  The host port uses
 * _Thread_local (one manager per thread).  An embedded port may instead use a
 * per-core manager table or RTOS task-local storage; the core loader never
 * reads a thread/CPU ID itself.
 */

void cold_code_port_set_current(cold_code_manager_t *mgr);
cold_code_manager_t *cold_code_port_current(void);

COLD_CODE_PORT_NORETURN void cold_code_port_fatal(const char *message);
COLD_CODE_PORT_NORETURN void cold_code_port_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_H */
