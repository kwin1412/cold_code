#include "cold_code_internal.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(cold_code_image_header_t) == 36u,
               "cold image header must stay 36 bytes for the generated format");
_Static_assert(offsetof(cold_code_image_header_t, magic) == 0u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, version) == 4u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, header_size) == 6u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, image_id) == 8u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, slot_size) == 12u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, slot_count) == 16u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, payload_offset) == 20u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, payload_crc32) == 24u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, flags) == 28u, "header layout");
_Static_assert(offsetof(cold_code_image_header_t, reserved) == 32u, "header layout");

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

cold_code_status_t cold_code_validate_image_header(const cold_code_image_header_t *header,
                                                   const cold_code_image_t *image,
                                                   uint32_t source_size) {
    if (header == NULL || image == NULL) {
        return COLD_CODE_ERR_ARG;
    }

    if (header->magic != COLD_CODE_IMAGE_MAGIC || header->version != COLD_CODE_IMAGE_VERSION ||
        header->header_size < (uint16_t)sizeof(*header)) {
        return COLD_CODE_ERR_IMAGE;
    }

    if (header->flags != 0u || header->reserved != 0u) {
        return COLD_CODE_ERR_IMAGE;
    }

    if (header->image_id != image->image_id || header->slot_size != image->slot_size ||
        header->slot_count != image->slot_count) {
        return COLD_CODE_ERR_IMAGE;
    }

    if (header->payload_offset < header->header_size) {
        return COLD_CODE_ERR_IMAGE;
    }

    uint64_t payload_size = (uint64_t)header->slot_count * (uint64_t)header->slot_size;
    if (payload_size > UINT32_MAX) {
        return COLD_CODE_ERR_IMAGE;
    }

    if (header->payload_offset > source_size ||
        payload_size > (uint64_t)source_size - (uint64_t)header->payload_offset) {
        return COLD_CODE_ERR_IMAGE;
    }

    return COLD_CODE_OK;
}
