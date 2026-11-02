#ifndef COLD_CODE_PORT_HOST_H
#define COLD_CODE_PORT_HOST_H

#include "cold_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reference host port/source implementation for the example (simulated flash).
 *
 * This file is not part of the library: the platform-neutral cold_code_core
 * does not provide a port or file/memory sources.  An embedded consumer
 * implements its own cold_code_port.h hooks and cold_code_source_ops_t.
 */

/* Host file-backed source.  One instance per manager. */
typedef struct {
    const char *path;
    void *handle;
} cold_code_file_source_t;

/* Read-only memory-backed source.  May be shared by several managers. */
typedef struct {
    const void *data;
    size_t size;
} cold_code_memory_source_t;

extern const cold_code_source_ops_t cold_code_file_source_ops;
extern const cold_code_source_ops_t cold_code_memory_source_ops;

void cold_code_file_source_init(cold_code_file_source_t *source, const char *path);
void cold_code_memory_source_init(cold_code_memory_source_t *source, const void *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_HOST_H */
