#ifndef COLD_CODE_SOURCE_H
#define COLD_CODE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Image source contract.  The core never opens files itself; a platform port
 * or the consumer provides these operations.  open() is required and reports
 * the total source size; close() is optional; read() must return false on a
 * short or out-of-range read.
 */
typedef struct cold_code_source_ops {
    bool (*open)(void *ctx, uint32_t *size_out);
    void (*close)(void *ctx);
    bool (*read)(void *ctx, uint32_t offset, void *dst, size_t len);
} cold_code_source_ops_t;

typedef struct {
    const cold_code_source_ops_t *ops;
    void *ctx;
} cold_code_source_t;

/*
 * Platform-neutral, read-only memory source.  Works for host buffers and
 * memory-mapped flash.  It may be shared by several managers.
 */
typedef struct {
    const void *data;
    size_t size;
} cold_code_memory_source_t;

void cold_code_memory_source_init(cold_code_memory_source_t *source, const void *data, size_t size);
cold_code_source_t cold_code_memory_source(cold_code_memory_source_t *source);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_SOURCE_H */
