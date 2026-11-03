#include "cold_code.h"
#include "cold_code_host.h"
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

COLD_CODE_DEFINE_STORAGE(imagefmt, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager;
static cold_code_memory_loader_t g_source;

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

static cold_code_status_t init_image(const void *data, size_t size, uint32_t flags,
                                     uint32_t expected_image_id) {
    if (cold_code_manager_is_initialized(&g_manager)) {
        cold_code_manager_deinit(&g_manager);
    }
    cold_code_memory_loader_init(&g_source, data, size);
    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = expected_image_id,
        .loader = cold_code_memory_loader(&g_source),
        .flags = flags,
    };
    return cold_code_manager_init(&g_manager, &config);
}

static bool fail_load(void *ctx, uint32_t offset, void *dst, size_t size) {
    (void)ctx;
    (void)offset;
    (void)dst;
    (void)size;
    return false;
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
    CHECK_EQ(header.payload_offset, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.storage_align, 64u);
    CHECK_EQ(header.reserved[0], 0u);
    CHECK_EQ(header.reserved[1], 0u);

    cold_code_memory_loader_t memory;
    cold_code_memory_loader_init(&memory, image_data, image_size);
    cold_code_loader_t source = cold_code_memory_loader(&memory);
    cold_code_image_info_t info;
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_OK);
    CHECK_EQ(info.image_id, header.image_id);
    CHECK_EQ(info.slot_size, header.slot_size);
    CHECK_EQ(info.slot_count, header.slot_count);
    CHECK_EQ(info.max_pool_slots, header.max_pool_slots);
    CHECK_EQ(info.arch, header.arch);
    CHECK_EQ(info.abi_version, header.abi_version);
    CHECK_EQ(info.storage_align, header.storage_align);
    CHECK_EQ(info.payload_offset, header.payload_offset);
    CHECK_EQ(info.payload_size, header.payload_size);
    CHECK_EQ(info.payload_crc32, header.payload_crc32);
    CHECK_EQ(info.reserved, 0u);
    CHECK(cold_code_storage_required_size(&info, 1u) <= sizeof(g_storage));

    unsigned char *scratch = malloc(image_size);
    CHECK(scratch != NULL);

    memcpy(scratch, image_data, image_size);
    scratch[0] ^= 1u;
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[4] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[6] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[56] ^= 1u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[44] = 0x80u;
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 20, header.slot_size + 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 24, header.slot_count + 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 52, 0u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 52, 65u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 40, header.payload_crc32 ^ 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[48] ^= 1u;
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    scratch[header.payload_offset] ^= 0xFFu;
    CHECK_EQ(init_image(scratch, image_size, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    CHECK_EQ(init_image(image_data, image_size - 1u, COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);
    CHECK_EQ(init_image(image_data, 8u, COLD_CODE_INIT_VERIFY_CRC, 0u), COLD_CODE_ERR_ARG);

    CHECK_EQ(init_image(image_data, image_size,
                        COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                        info.image_id ^ 1u),
             COLD_CODE_ERR_IMAGE);

    /* Probe is metadata-only; loader failures are reported without state changes. */
    cold_code_memory_loader_init(&memory, image_data, 8u);
    source = cold_code_memory_loader(&memory);
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_ERR_ARG);

    cold_code_memory_loader_init(&memory, image_data, image_size);
    source = cold_code_memory_loader(&memory);
    cold_code_loader_t bad_source = source;
    bad_source.load_fptr = fail_load;
    bad_source.load_ctx = NULL;
    CHECK_EQ(cold_code_manager_probe(&bad_source, &info), COLD_CODE_ERR_SOURCE);

    memcpy(scratch, image_data, image_size);
    scratch[48] ^= 1u;
    cold_code_memory_loader_init(&memory, scratch, image_size);
    source = cold_code_memory_loader(&memory);
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 52, 0u);
    refresh_header_crc(scratch);
    cold_code_memory_loader_init(&memory, scratch, image_size);
    source = cold_code_memory_loader(&memory);
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_ERR_IMAGE);

    CHECK_EQ(init_image(image_data, image_size,
                        COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                        info.image_id),
             COLD_CODE_OK);
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
