#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool memory_load(void *ctx, uint32_t offset, void *dst, size_t size) {
    const cold_code_memory_loader_t *loader = (const cold_code_memory_loader_t *)ctx;
    if (loader == NULL || (dst == NULL && size != 0u) || loader->size > (size_t)UINT32_MAX) {
        return false;
    }
    if (loader->data == NULL && loader->size != 0u) {
        return false;
    }
    if ((uint64_t)offset + (uint64_t)size > (uint64_t)loader->size) {
        return false;
    }

    const unsigned char *src = (const unsigned char *)loader->data + offset;
    unsigned char *out = (unsigned char *)dst;
    for (size_t i = 0; i < size; ++i) {
        out[i] = src[i];
    }
    return true;
}

void cold_code_memory_loader_init(cold_code_memory_loader_t *loader, const void *data,
                                  size_t size) {
    if (loader != NULL) {
        loader->data = data;
        loader->size = size;
    }
}

cold_code_loader_t cold_code_memory_loader(cold_code_memory_loader_t *loader) {
    cold_code_loader_t result = {
        .load_fptr = memory_load,
        .load_ctx = loader,
        .image_size = (loader != NULL && loader->size <= (size_t)UINT32_MAX)
                          ? (uint32_t)loader->size
                          : 0u,
    };
    return result;
}
