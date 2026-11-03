#ifndef COLD_CODE_VERSION_H
#define COLD_CODE_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COLD_CODE_VERSION_MAJOR 5
#define COLD_CODE_VERSION_MINOR 0

/* Version of the in-process image metadata/manager ABI. */
#define COLD_CODE_ABI_VERSION 2u

/* Version of the platform context operations passed through manager config. */
#define COLD_CODE_CONTEXT_ABI_VERSION 2u

/* Version of the on-disk (wire) cold image format. */
#define COLD_CODE_IMAGE_VERSION 4u

#define COLD_CODE_IMAGE_MAGIC   0x444c4f43u /* bytes "COLD" (little-endian) */
#define COLD_CODE_IMAGE_HEADER_SIZE 64u

#define COLD_CODE_MAX_POOL_SLOTS 16u

/* Number of per-CPU manager slots owned by the core. */
#define COLD_CODE_MAX_CPUS 64u

/*
 * Opaque, statically allocatable manager.  The size is private to the core;
 * bump COLD_CODE_ABI_VERSION if the implementation ever needs more space.
 */
#define COLD_CODE_MANAGER_BYTES 256u
#define COLD_CODE_MANAGER_ALIGN 16u

/* cold_code_manager_config_t.flags */
#define COLD_CODE_INIT_VERIFY_CRC (1u << 0)
#define COLD_CODE_INIT_EXPECT_IMAGE_ID (1u << 1)

/* cold_code_image_header_t.flags */
#define COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32 (1u << 0)

typedef enum {
    COLD_CODE_ARCH_UNKNOWN = 0,
    COLD_CODE_ARCH_X86_64 = 1,
    COLD_CODE_ARCH_ARMV7M = 2,
    COLD_CODE_ARCH_AARCH64 = 3,
    COLD_CODE_ARCH_RISCV32 = 4,
    COLD_CODE_ARCH_RISCV64 = 5,
} cold_code_arch_t;

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_VERSION_H */
