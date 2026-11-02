#include "port_host.h"
#include "cold_code_port.h"
#include "cold_code_port_fast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The host port keeps the current manager per thread.  Managers never share
 * mutable state, so concurrent threads using different managers need no
 * locking.  Using one manager from two threads at once is a caller error.
 *
 * The variable is hidden but non-static so the tiny FAST_PATH assembly
 * accessor can load it with a local-exec TLS access.  This port is intended
 * for STATIC executables (Phase 2).
 */
_Thread_local cold_code_manager_t *cold_code_port_current_manager
    __attribute__((visibility("hidden")));

void cold_code_port_set_current(cold_code_manager_t *mgr) {
    cold_code_port_current_manager = mgr;
}

cold_code_manager_t *cold_code_port_current(void) {
    return cold_code_port_current_manager;
}

_Noreturn void cold_code_port_fatal(const char *message) {
    if (message != NULL) {
        fputs(message, stderr);
        fputc('\n', stderr);
    }
    abort();
}

_Noreturn void cold_code_port_abort(void) {
    abort();
}

/* --- Host file source (simulated flash) -------------------------------- */

static bool file_open(void *ctx, uint32_t *size_out) {
    cold_code_file_source_t *source = (cold_code_file_source_t *)ctx;
    if (source == NULL || source->path == NULL || size_out == NULL) {
        return false;
    }
    /* A file source owns one FILE handle; do not share it between managers. */
    if (source->handle != NULL) {
        return false;
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

const cold_code_source_ops_t cold_code_file_source_ops = {
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

/* --- Read-only memory source (flash mapping / shared image) ------------- */

static bool memory_open(void *ctx, uint32_t *size_out) {
    cold_code_memory_source_t *source = (cold_code_memory_source_t *)ctx;
    if (source == NULL || source->data == NULL || size_out == NULL || source->size > UINT32_MAX) {
        return false;
    }
    *size_out = (uint32_t)source->size;
    return true;
}

static void memory_close(void *ctx) {
    (void)ctx;
}

static bool memory_read(void *ctx, uint32_t offset, void *dst, size_t len) {
    cold_code_memory_source_t *source = (cold_code_memory_source_t *)ctx;
    if (source == NULL || (dst == NULL && len != 0u)) {
        return false;
    }
    if ((uint64_t)offset + (uint64_t)len > (uint64_t)source->size) {
        return false;
    }

    memcpy(dst, (const unsigned char *)source->data + offset, len);
    return true;
}

const cold_code_source_ops_t cold_code_memory_source_ops = {
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
