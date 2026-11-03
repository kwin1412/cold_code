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
 * Read-only runtime view of the metadata loaded from the wire image header.
 * cold_code_manager_image() returns a manager-owned view.  The wire format
 * does not carry a name, so `name` is always NULL; use `image_id` for
 * diagnostics and identity.
 */
struct cold_code_image {
    uint32_t image_id;       /* build fingerprint, also embedded in stubs */
    uint32_t slot_size;      /* bytes per image slot */
    uint32_t slot_count;     /* number of image slots */
    uint32_t max_pool_slots; /* maximum pool capacity */
    uint32_t arch;           /* cold_code_arch_t */
    uint32_t abi_version;    /* COLD_CODE_ABI_VERSION */
    uint32_t struct_size;    /* sizeof(cold_code_image_t) */
    uint32_t storage_align;  /* required storage/pool alignment */
    const char *name;        /* always NULL; use image_id instead */
};

/*
 * Fixed-layout metadata returned by cold_code_manager_probe().  probe reads
 * the wire header and fills this structure; it does not reserve storage and
 * does not validate the local context port or payload CRC.  Changing this
 * layout requires an ABI bump.
 */
typedef struct {
    uint32_t image_id;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t max_pool_slots;
    uint32_t arch;
    uint32_t abi_version;
    uint32_t storage_align;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t flags;
    uint32_t reserved; /* must be zero */
} cold_code_image_info_t;

/*
 * v4 on-disk header.  This is a wire format: all multi-byte fields are
 * little-endian and the core decodes them byte-wise.  Do not overlay this
 * struct on untrusted image bytes.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t image_id; /* build fingerprint: name + geometry + payload */
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
    uint32_t storage_align;
    uint32_t reserved[2]; /* must be zero */
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
 * Decode and integrity-check a 64-byte v4 header.  Returns COLD_CODE_ERR_IMAGE
 * for a bad magic/version/size/reserved/flags/CRC.
 */
cold_code_status_t cold_code_image_header_decode(const void *bytes, size_t size,
                                                 cold_code_image_header_t *out);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_IMAGE_H */
