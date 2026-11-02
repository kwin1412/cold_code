#include "image_fixture.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char fixture_name[] = "fixture";

static uint32_t fixture_image_id(void) {
    return cold_code_crc32(fixture_name, sizeof(fixture_name) - 1u);
}

static cold_code_image_header_t fixture_header(const test_image_fixture_t *fixture,
                                               uint32_t slot_count, uint32_t max_pool_slots) {
    uint32_t payload_size = slot_count * TEST_IMAGE_FIXTURE_SLOT_SIZE;
    cold_code_image_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = COLD_CODE_IMAGE_MAGIC;
    header.version = COLD_CODE_IMAGE_VERSION;
    header.header_size = COLD_CODE_IMAGE_HEADER_SIZE;
    header.image_id = fixture_image_id();
    header.arch = COLD_CODE_ARCH_X86_64;
    header.abi_version = COLD_CODE_ABI_VERSION;
    header.slot_size = TEST_IMAGE_FIXTURE_SLOT_SIZE;
    header.slot_count = slot_count;
    header.max_pool_slots = max_pool_slots;
    header.payload_offset = COLD_CODE_IMAGE_HEADER_SIZE;
    header.payload_size = payload_size;
    header.payload_crc32 =
        cold_code_crc32(fixture->bytes + COLD_CODE_IMAGE_HEADER_SIZE, payload_size);
    header.flags = COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32;
    return header;
}

cold_code_status_t test_image_fixture_init(test_image_fixture_t *fixture, uint32_t slot_count,
                                           uint32_t max_pool_slots) {
    if (fixture == NULL || slot_count == 0u || max_pool_slots == 0u ||
        max_pool_slots > slot_count) {
        return COLD_CODE_ERR_ARG;
    }
    uint32_t payload_size = slot_count * TEST_IMAGE_FIXTURE_SLOT_SIZE;
    if (payload_size > TEST_IMAGE_FIXTURE_MAX_BYTES - COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_ARG;
    }

    memset(fixture, 0, sizeof(*fixture));
    for (uint32_t i = 0; i < payload_size; ++i) {
        fixture->bytes[COLD_CODE_IMAGE_HEADER_SIZE + i] = 0x90u; /* NOP padding */
    }

    fixture->image.image_id = fixture_image_id();
    fixture->image.slot_size = TEST_IMAGE_FIXTURE_SLOT_SIZE;
    fixture->image.slot_count = slot_count;
    fixture->image.max_pool_slots = max_pool_slots;
    fixture->image.arch = COLD_CODE_ARCH_X86_64;
    fixture->image.abi_version = COLD_CODE_ABI_VERSION;
    fixture->image.struct_size = (uint32_t)sizeof(cold_code_image_t);
    fixture->image.storage_align = 64u;
    fixture->image.name = fixture_name;

    cold_code_image_header_t header = fixture_header(fixture, slot_count, max_pool_slots);
    cold_code_status_t status =
        cold_code_image_header_encode(&header, fixture->bytes, COLD_CODE_IMAGE_HEADER_SIZE);
    if (status != COLD_CODE_OK) {
        return status;
    }
    fixture->size = COLD_CODE_IMAGE_HEADER_SIZE + payload_size;
    return COLD_CODE_OK;
}

cold_code_status_t test_image_fixture_refresh(test_image_fixture_t *fixture) {
    if (fixture == NULL || fixture->size < COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_ARG;
    }
    cold_code_image_header_t header = fixture_header(
        fixture, fixture->image.slot_count, fixture->image.max_pool_slots);
    return cold_code_image_header_encode(&header, fixture->bytes, COLD_CODE_IMAGE_HEADER_SIZE);
}
