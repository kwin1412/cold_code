#include "cold_code_internal.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(cold_code_image_header_t) == COLD_CODE_IMAGE_HEADER_SIZE,
               "v3 image header must be exactly 64 bytes");
_Static_assert(offsetof(cold_code_image_header_t, magic) == 0u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, version) == 4u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, header_size) == 6u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, image_id) == 8u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, arch) == 12u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, abi_version) == 16u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, slot_size) == 20u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, slot_count) == 24u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, max_pool_slots) == 28u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, payload_offset) == 32u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, payload_size) == 36u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, payload_crc32) == 40u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, flags) == 44u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, header_crc32) == 48u, "header layout");

#define COLD_CODE_HEADER_CRC_OFFSET offsetof(cold_code_image_header_t, header_crc32)

static void put_le16(unsigned char *dst, uint16_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8u) & 0xFFu);
}

static void put_le32(unsigned char *dst, uint32_t value) {
    dst[0] = (unsigned char)(value & 0xFFu);
    dst[1] = (unsigned char)((value >> 8u) & 0xFFu);
    dst[2] = (unsigned char)((value >> 16u) & 0xFFu);
    dst[3] = (unsigned char)((value >> 24u) & 0xFFu);
}

static uint16_t get_le16(const unsigned char *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8u));
}

static uint32_t get_le32(const unsigned char *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

const char *cold_code_status_string(cold_code_status_t status) {
    switch (status) {
    case COLD_CODE_OK:
        return "ok";
    case COLD_CODE_ERR_ARG:
        return "invalid argument";
    case COLD_CODE_ERR_IMAGE:
        return "invalid cold image";
    case COLD_CODE_ERR_SOURCE:
        return "image source error";
    case COLD_CODE_ERR_STATE:
        return "invalid manager state";
    case COLD_CODE_ERR_SIZE:
        return "storage too small or misaligned";
    case COLD_CODE_ERR_ARCH:
        return "image architecture mismatch";
    case COLD_CODE_ERR_UNSUPPORTED:
        return "unsupported ABI or architecture";
    }
    return "unknown cold_code status";
}

/* Reflected CRC-32 (IEEE 802.3), matching zlib.crc32() in the generator. */
uint32_t cold_code_crc32_update(uint32_t crc, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t cold_code_crc32(const void *data, size_t size) {
    return cold_code_crc32_update(COLD_CODE_CRC32_INIT, data, size) ^ 0xFFFFFFFFu;
}

static void encode_header(const cold_code_image_header_t *header, unsigned char *bytes) {
    cold_code_memzero(bytes, COLD_CODE_IMAGE_HEADER_SIZE);
    put_le32(bytes + 0, header->magic);
    put_le16(bytes + 4, header->version);
    put_le16(bytes + 6, header->header_size);
    put_le32(bytes + 8, header->image_id);
    put_le32(bytes + 12, header->arch);
    put_le32(bytes + 16, header->abi_version);
    put_le32(bytes + 20, header->slot_size);
    put_le32(bytes + 24, header->slot_count);
    put_le32(bytes + 28, header->max_pool_slots);
    put_le32(bytes + 32, header->payload_offset);
    put_le32(bytes + 36, header->payload_size);
    put_le32(bytes + 40, header->payload_crc32);
    put_le32(bytes + 44, header->flags);
}

cold_code_status_t cold_code_image_header_encode(const cold_code_image_header_t *header,
                                                 void *bytes, size_t size) {
    if (header == NULL || bytes == NULL || size < COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_ARG;
    }
    if (header->magic != COLD_CODE_IMAGE_MAGIC ||
        header->version != COLD_CODE_IMAGE_VERSION ||
        header->header_size != COLD_CODE_IMAGE_HEADER_SIZE ||
        (header->flags & ~COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32) != 0u) {
        return COLD_CODE_ERR_ARG;
    }

    unsigned char encoded[COLD_CODE_IMAGE_HEADER_SIZE];
    encode_header(header, encoded);
    uint32_t crc = cold_code_crc32(encoded, sizeof(encoded));
    put_le32(encoded + COLD_CODE_HEADER_CRC_OFFSET, crc);
    cold_code_memcopy(bytes, encoded, sizeof(encoded));
    return COLD_CODE_OK;
}

cold_code_status_t cold_code_image_header_decode(const void *bytes, size_t size,
                                                 cold_code_image_header_t *out) {
    if (bytes == NULL || out == NULL || size < COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_IMAGE;
    }

    const unsigned char *src = (const unsigned char *)bytes;
    cold_code_image_header_t header;
    header.magic = get_le32(src + 0);
    header.version = get_le16(src + 4);
    header.header_size = get_le16(src + 6);
    header.image_id = get_le32(src + 8);
    header.arch = get_le32(src + 12);
    header.abi_version = get_le32(src + 16);
    header.slot_size = get_le32(src + 20);
    header.slot_count = get_le32(src + 24);
    header.max_pool_slots = get_le32(src + 28);
    header.payload_offset = get_le32(src + 32);
    header.payload_size = get_le32(src + 36);
    header.payload_crc32 = get_le32(src + 40);
    header.flags = get_le32(src + 44);
    header.header_crc32 = get_le32(src + COLD_CODE_HEADER_CRC_OFFSET);
    header.reserved[0] = get_le32(src + 52);
    header.reserved[1] = get_le32(src + 56);
    header.reserved[2] = get_le32(src + 60);

    if (header.magic != COLD_CODE_IMAGE_MAGIC || header.version != COLD_CODE_IMAGE_VERSION ||
        header.header_size != COLD_CODE_IMAGE_HEADER_SIZE || header.reserved[0] != 0u ||
        header.reserved[1] != 0u || header.reserved[2] != 0u ||
        (header.flags & ~COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32) != 0u) {
        return COLD_CODE_ERR_IMAGE;
    }

    unsigned char copy[COLD_CODE_IMAGE_HEADER_SIZE];
    cold_code_memcopy(copy, src, sizeof(copy));
    put_le32(copy + COLD_CODE_HEADER_CRC_OFFSET, 0u);
    if (cold_code_crc32(copy, sizeof(copy)) != header.header_crc32) {
        return COLD_CODE_ERR_IMAGE;
    }

    *out = header;
    return COLD_CODE_OK;
}
