#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool memory_open(void *ctx, uint32_t *size_out) {
    const cold_code_memory_source_t *source = (const cold_code_memory_source_t *)ctx;
    if (source == NULL || size_out == NULL || source->size > (size_t)UINT32_MAX) {
        return false;
    }
    if (source->data == NULL && source->size != 0u) {
        return false;
    }
    *size_out = (uint32_t)source->size;
    return true;
}

static void memory_close(void *ctx) {
    (void)ctx;
}

static bool memory_read(void *ctx, uint32_t offset, void *dst, size_t len) {
    const cold_code_memory_source_t *source = (const cold_code_memory_source_t *)ctx;
    if (source == NULL || (dst == NULL && len != 0u) || source->data == NULL) {
        return false;
    }
    if ((uint64_t)offset + (uint64_t)len > (uint64_t)source->size) {
        return false;
    }

    const unsigned char *src = (const unsigned char *)source->data + offset;
    unsigned char *out = (unsigned char *)dst;
    for (size_t i = 0; i < len; ++i) {
        out[i] = src[i];
    }
    return true;
}

static const cold_code_source_ops_t memory_source_ops = {
    .open = memory_open,
    .close = memory_close,
    .read = memory_read,
};

void cold_code_memory_source_init(cold_code_memory_source_t *source, const void *data,
                                  size_t size) {
    if (source != NULL) {
        source->data = data;
        source->size = size;
    }
}

cold_code_source_t cold_code_memory_source(cold_code_memory_source_t *source) {
    cold_code_source_t handle = {.ops = &memory_source_ops, .ctx = source};
    return handle;
}
