#ifndef COLD_CODE_LOADER_H
#define COLD_CODE_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Image loader contract.  load_fptr must support repeated random reads over
 * [0, image_size); it returns false for an out-of-range or short read.  The
 * manager never owns/close the loader: load_ctx and its backing storage must
 * outlive every manager that copies this loader.
 *
 * Several CPUs may share one loader, so load_fptr must be reentrant or the
 * application must provide one loader per manager.
 */
typedef bool (*cold_code_load_fn)(void *ctx, uint32_t offset, void *dst, size_t size);

typedef struct {
    cold_code_load_fn load_fptr;
    void *load_ctx;
    uint32_t image_size;
} cold_code_loader_t;

/* Platform-neutral read-only memory loader; safe to share across managers. */
typedef struct {
    const void *data;
    size_t size;
} cold_code_memory_loader_t;

void cold_code_memory_loader_init(cold_code_memory_loader_t *loader, const void *data, size_t size);
cold_code_loader_t cold_code_memory_loader(cold_code_memory_loader_t *loader);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_LOADER_H */
