#include "cold_code_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool file_open(void *ctx, uint32_t *size_out) {
    cold_code_file_source_t *source = (cold_code_file_source_t *)ctx;
    if (source == NULL || source->path == NULL || size_out == NULL) {
        return false;
    }
    if (source->handle != NULL) {
        return false; /* one file source instance per manager */
    }

    FILE *file = fopen(source->path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0 || (unsigned long)size > UINT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    source->handle = file;
    *size_out = (uint32_t)size;
    return true;
}

static void file_close(void *ctx) {
    cold_code_file_source_t *source = (cold_code_file_source_t *)ctx;
    if (source != NULL && source->handle != NULL) {
        fclose((FILE *)source->handle);
        source->handle = NULL;
    }
}

static bool file_read(void *ctx, uint32_t offset, void *dst, size_t len) {
    cold_code_file_source_t *source = (cold_code_file_source_t *)ctx;
    if (source == NULL || source->handle == NULL || (dst == NULL && len != 0u)) {
        return false;
    }
    if (fseek((FILE *)source->handle, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    return fread(dst, 1, len, (FILE *)source->handle) == len;
}

static const cold_code_source_ops_t file_source_ops = {
    .open = file_open,
    .close = file_close,
    .read = file_read,
};

void cold_code_file_source_init(cold_code_file_source_t *source, const char *path) {
    if (source != NULL) {
        source->path = path;
        source->handle = NULL;
    }
}

cold_code_source_t cold_code_file_source(cold_code_file_source_t *source) {
    cold_code_source_t handle = {.ops = &file_source_ops, .ctx = source};
    return handle;
}
