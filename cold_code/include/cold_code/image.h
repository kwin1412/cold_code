#ifndef COLD_CODE_IMAGE_H
#define COLD_CODE_IMAGE_H

#include "cold_code/status.h"
#include "cold_code/types.h"
#include "cold_code/version.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generated, read-only image descriptor.  The field order and offsets are
 * deliberately stable: the generated fast stubs and the runtime compare the
 * descriptor *address* rather than an image id, so no fast-path field offset
 * is required.  struct_size allows a future descriptor to grow.
 */
struct cold_code_image {
    uint32_t image_id;       /* CRC32(name), diagnostics/header validation */
    uint32_t slot_size;      /* bytes per image slot */
    uint32_t slot_count;     /* number of image slots */
    uint32_t max_pool_slots; /* capacity of the generated storage type */
    uint32_t arch;           /* cold_code_arch_t */
    uint32_t abi_version;    /* COLD_CODE_ABI_VERSION */
    uint32_t struct_size;    /* sizeof(cold_code_image_t) at generation time */
    uint32_t storage_align;  /* required storage/pool alignment (power of two) */
    const char *name;        /* generated name (diagnostics) */
};

/*
 * v3 on-disk header.  This is a wire format: all multi-byte fields are
 * little-endian and the core decodes them byte-wise.  Do not overlay this
 * struct on untrusted image bytes.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t image_id;
    uint32_t arch;
    uint32_t abi_version;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t max_pool_slots;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t flags;
    uint32_t header_crc32; /* CRC32 over the 64-byte header, field zeroed */
    uint32_t reserved[3];  /* must be zero */
} cold_code_image_header_t;

/* Reflected CRC-32 (IEEE 802.3), matching zlib.crc32(). */
uint32_t cold_code_crc32(const void *data, size_t size);

/*
 * Encode a header into exactly COLD_CODE_IMAGE_HEADER_SIZE bytes, computing
 * header_crc32 over the encoded header (with the CRC field zeroed).
 * Returns COLD_CODE_ERR_ARG for NULL/short output or invalid field values.
 */
cold_code_status_t cold_code_image_header_encode(const cold_code_image_header_t *header,
                                                 void *bytes, size_t size);

/*
 * Decode and integrity-check a 64-byte v3 header.  Returns COLD_CODE_ERR_IMAGE
 * for a bad magic/version/size/reserved/flags/CRC.
 */
cold_code_status_t cold_code_image_header_decode(const void *bytes, size_t size,
                                                 cold_code_image_header_t *out);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_IMAGE_H */
