#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cold_code.h"
#include "cold_code_imagefmt_image.h"
#include "port_host.h"
#include "test_util.h"

static cold_code_storage_imagefmt_t g_storage COLD_CODE_STORAGE_SECTION;
static cold_code_manager_t g_manager;
static cold_code_memory_source_t g_source;

int cold_image_ping(void);

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    unsigned char *data = malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        free(data);
        return NULL;
    }

    *size_out = (size_t)length;
    return data;
}

static cold_code_status_t init_image(const cold_code_image_t *image, const void *data,
                                     size_t size) {
    cold_code_memory_source_init(&g_source, data, size);
    cold_code_config_t config = {
        .source = &cold_code_memory_source_ops,
        .source_ctx = &g_source,
        .verify_crc = true,
    };
    cold_code_manager_deinit(&g_manager);
    return cold_code_manager_init(&g_manager, image, &config);
}

int main(void) {
    cold_code_storage_imagefmt_bind(&g_manager, &g_storage);

    size_t image_size = 0;
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_IMAGEFMT_PATH, &image_size);
    CHECK(image_data != NULL);
    CHECK(image_size > sizeof(cold_code_image_header_t));
    CHECK_EQ(sizeof(cold_code_image_header_t), 36);

    const cold_code_image_header_t *header = (const cold_code_image_header_t *)image_data;
    CHECK_EQ(header->magic, COLD_CODE_IMAGE_MAGIC);
    CHECK_EQ(header->version, COLD_CODE_IMAGE_VERSION);
    CHECK_EQ(header->header_size, sizeof(cold_code_image_header_t));
    CHECK_EQ(header->image_id, cold_code_image_imagefmt.image_id);
    CHECK_EQ(header->slot_size, cold_code_image_imagefmt.slot_size);
    CHECK_EQ(header->slot_count, cold_code_image_imagefmt.slot_count);
    CHECK_EQ(header->payload_offset, sizeof(cold_code_image_header_t));

    unsigned char *scratch = malloc(image_size);
    CHECK(scratch != NULL);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->magic ^= 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->version += 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->image_id ^= 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->slot_size += 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->slot_count += 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    ((cold_code_image_header_t *)scratch)->payload_crc32 ^= 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[header->payload_offset] ^= 0xFFu;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    /* Truncated payload and truncated header. */
    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, image_size - 1u),
             COLD_CODE_ERR_IMAGE);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, 8u), COLD_CODE_ERR_SOURCE);

    /* Descriptor/header image_id mismatch. */
    cold_code_image_t wrong_id = cold_code_image_imagefmt;
    wrong_id.image_id ^= 1u;
    CHECK_EQ(init_image(&wrong_id, image_data, image_size), COLD_CODE_ERR_IMAGE);

    /* A valid image still initialises and runs after all failed attempts. */
    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, image_size), COLD_CODE_OK);
    cold_code_manager_use(&g_manager);
    CHECK_EQ(cold_image_ping(), 42);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1);
    CHECK_EQ(stats.loads, 1);

    cold_code_manager_deinit(&g_manager);
    free(scratch);
    free(image_data);
    TEST_OK();
    return 0;
}
