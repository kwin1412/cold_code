#include "cold_code_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *g_image;
static size_t g_image_size;

bool cold_code_port_open(const char *path) {
    if (path == NULL) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    g_image = file;
    g_image_size = (size_t)size;
    return true;
}

size_t cold_code_port_size(void) {
    return g_image_size;
}

void cold_code_port_read(uint32_t offset, void *dst, size_t len) {
    if (g_image == NULL) {
        cold_code_port_fatal("cold_code: image is not open");
    }

    if (fseek(g_image, (long)offset, SEEK_SET) != 0) {
        cold_code_port_fatal("cold_code: seek to image offset 0x%08x failed", (unsigned)offset);
    }

    size_t read_len = fread(dst, 1, len, g_image);
    if (read_len != len) {
        cold_code_port_fatal("cold_code: short read at image offset 0x%08x "
                             "(wanted %zu bytes, got %zu)",
                             (unsigned)offset, len, read_len);
    }
}

void cold_code_port_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

COLD_CODE_NORETURN void cold_code_port_fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}
