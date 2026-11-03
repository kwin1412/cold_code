#include "cold_code.h"
#include "cold_code_host.h"
#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

COLD_CODE_DEFINE_STORAGE(init, g_storage, TEST_STORAGE_BYTES, TEST_STORAGE_ALIGN);
static cold_code_manager_t g_manager;
static cold_code_memory_loader_t g_source;

int cold_ping(void);

static void put_le32(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8u) & 0xFFu);
    dst[2] = (unsigned char)((value >> 16u) & 0xFFu);
    dst[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static void refresh_header_crc(unsigned char *bytes) {
    put_le32(bytes + 48u, 0u);
    put_le32(bytes + 48u, cold_code_crc32(bytes, COLD_CODE_IMAGE_HEADER_SIZE));
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

static bool failing_load(void *ctx, uint32_t offset, void *dst, size_t size) {
    (void)ctx;
    (void)offset;
    (void)dst;
    (void)size;
    return false;
}

static cold_code_status_t init_image(const void *data, size_t size, size_t storage_size,
                                     uint32_t flags, uint32_t expected_image_id) {
    cold_code_memory_loader_init(&g_source, data, size);
    cold_code_manager_config_t config = {
        .exec_addr = &g_storage,
        .exec_size = storage_size,
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .expected_image_id = expected_image_id,
        .loader = cold_code_memory_loader(&g_source),
        .flags = flags,
    };
    return cold_code_manager_init(&g_manager, &config);
}

int main(void) {
    size_t image_size = 0;
    unsigned char *image_data = read_file(COLD_CODE_IMAGE_INIT_PATH, &image_size);
    CHECK(image_data != NULL);
    CHECK(image_size > COLD_CODE_IMAGE_HEADER_SIZE);

    cold_code_memory_loader_init(&g_source, image_data, image_size);
    cold_code_loader_t source = cold_code_memory_loader(&g_source);
    cold_code_image_info_t info;
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_OK);
    size_t required = cold_code_storage_required_size(&info, 1u);
    CHECK(required > 0u);
    CHECK(required <= sizeof(g_storage));

    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_init(NULL, NULL), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&g_manager, NULL), COLD_CODE_ERR_ARG);

    cold_code_manager_config_t bad = {
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .loader = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    bad.exec_addr = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);
    bad.exec_addr = &g_storage;
    bad.exec_size = 0u;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);
    bad.exec_size = sizeof(g_storage);
    bad.loader.load_fptr = NULL;
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    bad = (cold_code_manager_config_t){
        .exec_addr = &g_storage,
        .exec_size = required - 1u,
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .loader = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_SIZE);

    bad = (cold_code_manager_config_t){
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .loader = {.load_fptr = NULL, .load_ctx = NULL, .image_size = 64u},
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_ARG);

    bad.loader.load_fptr = source.load_fptr;
    bad.loader.load_ctx = source.load_ctx;
    bad.loader.image_size = source.image_size;
    bad.context = &(cold_code_context_ops_t){0};
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_UNSUPPORTED);

    bad = (cold_code_manager_config_t){
        .exec_addr = &g_storage,
        .exec_size = sizeof(g_storage),
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = 1u,
        .loader = {.load_fptr = failing_load, .load_ctx = NULL,
                   .image_size = COLD_CODE_IMAGE_HEADER_SIZE},
        .flags = COLD_CODE_INIT_VERIFY_CRC,
    };
    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_SOURCE);

    CHECK_EQ(init_image(image_data, image_size, sizeof(g_storage),
                        COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                        info.image_id),
             COLD_CODE_OK);
    CHECK(cold_code_manager_is_initialized(&g_manager));

    const cold_code_image_t *view = cold_code_manager_image(&g_manager);
    CHECK(view != NULL);
    CHECK_EQ(view->image_id, info.image_id);
    CHECK_EQ(view->slot_size, info.slot_size);
    CHECK_EQ(view->slot_count, info.slot_count);
    CHECK_EQ(view->struct_size, sizeof(cold_code_image_t));
    CHECK_EQ(view->storage_align, info.storage_align);
    CHECK(view->name == NULL);

    cold_code_stats_t stats;
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.image_slots, info.slot_count);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 0u);

    CHECK_EQ(cold_code_manager_init(&g_manager, &bad), COLD_CODE_ERR_STATE);
    CHECK(cold_code_manager_current() == &g_manager);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);

    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 0u);
    CHECK_EQ(stats.loads, 0u);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_OK);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);
    CHECK_EQ(cold_code_manager_stats(&g_manager, &stats), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_reset(&g_manager), COLD_CODE_ERR_STATE);
    CHECK(cold_code_manager_image(&g_manager) == NULL);
    CHECK(cold_code_manager_pool_base(&g_manager) == NULL);

    CHECK_EQ(init_image(image_data, image_size, sizeof(g_storage),
                        COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                        info.image_id),
             COLD_CODE_OK);
    CHECK_EQ(cold_ping(), 42);
    CHECK_EQ(cold_code_manager_deinit(&g_manager), COLD_CODE_OK);

    CHECK_EQ(init_image(image_data, image_size, sizeof(g_storage),
                        COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                        info.image_id ^ 1u),
             COLD_CODE_ERR_IMAGE);

    /* Invalid geometry is rejected from wire metadata, not a descriptor. */
    unsigned char *scratch = malloc(image_size);
    CHECK(scratch != NULL);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 20u, 0u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 24u, 0u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 28u, 0u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 28u, COLD_CODE_MAX_POOL_SLOTS + 1u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 24u, 1u);
    put_le32(scratch + 28u, 2u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 20u, UINT32_MAX);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    memcpy(scratch, image_data, image_size);
    put_le32(scratch + 52u, 0u);
    refresh_header_crc(scratch);
    CHECK_EQ(init_image(scratch, image_size, sizeof(g_storage), COLD_CODE_INIT_VERIFY_CRC, 0u),
             COLD_CODE_ERR_IMAGE);

    free(scratch);
    free(image_data);
    TEST_OK();
    return 0;
}
