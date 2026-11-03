#ifndef TEST_IMAGE_FIXTURE_H
#define TEST_IMAGE_FIXTURE_H

#include "cold_code.h"

#include <stddef.h>
#include <stdint.h>

#define TEST_IMAGE_FIXTURE_MAX_BYTES 4096u
#define TEST_IMAGE_FIXTURE_SLOT_SIZE 64u
#define TEST_IMAGE_FIXTURE_STORAGE_ALIGN 64u

typedef struct {
    unsigned char bytes[TEST_IMAGE_FIXTURE_MAX_BYTES];
    size_t size;
} test_image_fixture_t;

/* Build a valid v4 image with slot_count slots and max_pool_slots capacity. */
cold_code_status_t test_image_fixture_init(test_image_fixture_t *fixture, uint32_t slot_count,
                                           uint32_t max_pool_slots);

/* Recompute the header CRC after the caller tweaks fixture->bytes. */
cold_code_status_t test_image_fixture_refresh(test_image_fixture_t *fixture);

/* Write a little-endian uint32 at a wire-header offset for negative tests. */
void test_image_fixture_put_u32(test_image_fixture_t *fixture, size_t offset, uint32_t value);

#endif /* TEST_IMAGE_FIXTURE_H */
