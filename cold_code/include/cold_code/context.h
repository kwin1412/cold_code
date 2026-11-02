#ifndef COLD_CODE_CONTEXT_H
#define COLD_CODE_CONTEXT_H

#include "cold_code/status.h"
#include "cold_code/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Select the manager used by cold calls in the calling execution context.
 * NULL clears the selection.  Returns ERR_STATE for an uninitialized manager.
 */
cold_code_status_t cold_code_manager_activate(cold_code_manager_t *manager);

/* Manager selected in this execution context, or NULL. */
cold_code_manager_t *cold_code_manager_current(void);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_CONTEXT_H */
