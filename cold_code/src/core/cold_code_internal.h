#ifndef COLD_CODE_INTERNAL_H
#define COLD_CODE_INTERNAL_H

#include "cold_code.h"

#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_INVALID_SLOT UINT32_MAX
#define COLD_CODE_INVALID_WAY  UINT32_MAX

#define COLD_CODE_CRC32_INIT 0xFFFFFFFFu

uint32_t cold_code_crc32_update(uint32_t crc, const void *data, size_t size);

/*
 * Validate an on-disk header against the generated descriptor and the actual
 * source size.  The descriptor itself is validated by manager_init().
 */
cold_code_status_t cold_code_validate_image_header(const cold_code_image_header_t *header,
                                                   const cold_code_image_t *image,
                                                   uint32_t source_size);

/*
 * Report a fatal loader error through the manager's callback (if any) and
 * then abort through the port.  Never returns.
 */
_Noreturn void cold_code_manager_fatal(const cold_code_manager_t *mgr, const char *message);

/* Called by generated stubs.  Not part of the consumer-facing API. */
unsigned char *cold_code_acquire_slot(uint32_t image_id, uint32_t image_slot);

#endif /* COLD_CODE_INTERNAL_H */
