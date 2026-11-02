#include "cold_code.h"
#include "cold_code_host.h"
#include "cold_code_imagefmt_image.h"
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

COLD_CODE_DEFINE_STORAGE(imagefmt, g_storage);
static cold_code_manager_t g_manager;
static cold_code_memory_source_t g_source;

int cold_image_ping(void);

static void put_le32(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8u) & 0xFFu);
    dst[2] = (unsigned char)((value >> 16u) & 0xFFu);
    dst[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static void refresh_header_crc(unsigned char *bytes) {
    put_le32(bytes + 48, 0u);
    put_le32(bytes + 48, cold_code_crc32(bytes, COLD_CODE_IMAGE_HEADER_SIZE));
}

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

static cold_code_status_t init_image(const cold_code_image_t *image, const void *data, size_t size) {
    if (cold_code_manager_is_initialized(&g_manager)) {
        cold_code_manager_deinit(&g_manager);
    }
    cold_code_memory_source_init(&g_source, data, size);
    cold_code_manager_config_t config = {
        .image = image,
        .storage = &g_storage,
        .storage_size = sizeof(g_storage),
        .pool_slots = 1u,
        .source = cold_code_memory_source(&g_source),
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    return cold_code_manager_init(&g_manager, &config);
}

int main(void) {
    size_t image_size = 0;
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_IMAGEFMT_PATH, &image_size);
    CHECK(image_data != NULL);
    CHECK(image_size > COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(sizeof(cold_code_image_header_t), COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(image_data[0], (unsigned char)'C');
    CHECK_EQ(image_data[1], (unsigned char)'O');
    CHECK_EQ(image_data[2], (unsigned char)'L');
    CHECK_EQ(image_data[3], (unsigned char)'D');

    cold_code_image_header_t header;
    CHECK_EQ(cold_code_image_header_decode(image_data, image_size, &header), COLD_CODE_OK);
    CHECK_EQ(header.magic, COLD_CODE_IMAGE_MAGIC);
    CHECK_EQ(header.version, COLD_CODE_IMAGE_VERSION);
    CHECK_EQ(header.header_size, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.image_id, cold_code_image_imagefmt.image_id);
    CHECK_EQ(header.slot_size, cold_code_image_imagefmt.slot_size);
    CHECK_EQ(header.slot_count, cold_code_image_imagefmt.slot_count);
    CHECK_EQ(header.payload_offset, COLD_CODE_IMAGE_HEADER_SIZE);

    unsigned char *scratch = malloc(image_size);
    CHECK(scratch != NULL);

    memcpy(scratch, image_data, image_size);
    scratch[0] ^= 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[4] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[6] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[52] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[44] = 0x80u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[8] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 12, COLD_CODE_ARCH_ARMV7M);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 16, 99u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 20, header.slot_size + 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 24, header.slot_count + 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 40, header.payload_crc32 ^ 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[48] ^= 1u;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[header.payload_offset] ^= 0xFFu;
    CHECK_EQ(init_image(&cold_code_image_imagefmt, scratch, image_size), COLD_CODE_ERR_IMAGE);

    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, image_size - 1u),
             COLD_CODE_ERR_IMAGE);
    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, 8u), COLD_CODE_ERR_SOURCE);

    cold_code_image_t wrong_id = cold_code_image_imagefmt;
    wrong_id.image_id ^= 1u;
    CHECK_EQ(init_image(&wrong_id, image_data, image_size), COLD_CODE_ERR_IMAGE);

    cold_code_image_t wrong_arch = cold_code_image_imagefmt;
    wrong_arch.arch = COLD_CODE_ARCH_ARMV7M;
    CHECK_EQ(init_image(&wrong_arch, image_data, image_size), COLD_CODE_ERR_ARCH);

    cold_code_image_t wrong_abi = cold_code_image_imagefmt;
    wrong_abi.abi_version = 99u;
    CHECK_EQ(init_image(&wrong_abi, image_data, image_size), COLD_CODE_ERR_UNSUPPORTED);

    CHECK_EQ(init_image(&cold_code_image_imagefmt, image_data, image_size), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_activate(&g_manager), COLD_CODE_OK);
    CHECK_EQ(cold_image_ping(), 42);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);

    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    free(scratch);
    free(image_data);
    TEST_OK();
    return 0;
}
