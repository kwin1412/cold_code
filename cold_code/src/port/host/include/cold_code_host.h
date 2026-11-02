#ifndef COLD_CODE_HOST_H
#define COLD_CODE_HOST_H

#include "cold_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reference host port.  Link cold_code::host (core + TLS context + file
 * source + x86-64 fast accessor).  The current manager is thread-local.
 */

typedef struct {
    const char *path;
    void *handle;
} cold_code_file_source_t;

void cold_code_file_source_init(cold_code_file_source_t *source, const char *path);
cold_code_source_t cold_code_file_source(cold_code_file_source_t *source);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_HOST_H */
