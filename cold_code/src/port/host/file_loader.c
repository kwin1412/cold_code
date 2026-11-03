#include "cold_code_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cold_code_file_loader_init(cold_code_file_loader_t *loader, const char *path) {
    if (loader == NULL || path == NULL) {
        return false;
    }
    memset(loader, 0, sizeof(*loader));
    loader->path = path;

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length < 0 || (unsigned long)length > 0xFFFFFFFFul || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    size_t size = (size_t)length;
    unsigned char *data = size == 0u ? NULL : (unsigned char *)malloc(size);
    if (size != 0u && data == NULL) {
        fclose(file);
        return false;
    }
    if (size != 0u && fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);

    loader->data = data;
    loader->size = size;
    cold_code_memory_loader_init(&loader->memory, data, size);
    return true;
}

void cold_code_file_loader_close(cold_code_file_loader_t *loader) {
    if (loader == NULL) {
        return;
    }
    free(loader->data);
    memset(loader, 0, sizeof(*loader));
}

cold_code_loader_t cold_code_file_loader(cold_code_file_loader_t *loader) {
    if (loader == NULL) {
        cold_code_loader_t empty = {0};
        return empty;
    }
    return cold_code_memory_loader(&loader->memory);
}
