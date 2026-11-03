#ifndef COLD_CODE_HOST_H
#define COLD_CODE_HOST_H

#include "cold_code.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reference host context operations.  Pass &cold_code_host_context_ops in
 * cold_code_manager_config_t.context.  This is a single-threaded host port:
 * the current CPU comes from the application's strong cold_code_cpu_id()
 * definition (the core weak default is 0).
 */
extern const cold_code_context_ops_t cold_code_host_context_ops;

/*
 * Convenience file loader.  The file is read once into memory; the loader is
 * caller-owned and must outlive the manager.  Call cold_code_file_loader_close
 * after cold_code_manager_deinit().
 */
typedef struct {
    const char *path;
    unsigned char *data;
    size_t size;
    cold_code_memory_loader_t memory;
} cold_code_file_loader_t;

bool cold_code_file_loader_init(cold_code_file_loader_t *loader, const char *path);
void cold_code_file_loader_close(cold_code_file_loader_t *loader);
cold_code_loader_t cold_code_file_loader(cold_code_file_loader_t *loader);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_HOST_H */
