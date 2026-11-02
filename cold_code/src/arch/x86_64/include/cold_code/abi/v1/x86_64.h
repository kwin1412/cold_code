#ifndef COLD_CODE_ABI_V1_X86_64_H
#define COLD_CODE_ABI_V1_X86_64_H

/*
 * Numeric offsets for the v1 fast-state block on x86-64.  Generated .S files
 * include this header and use these macros directly (the assembler is
 * preprocessed).  abi_check.c verifies them against offsetof().
 */

#define COLD_CODE_FAST_ABI_VERSION 1
#define COLD_CODE_FAST_STATE_SIZE 56

#define COLD_CODE_FAST_OFF_IMAGE      0
#define COLD_CODE_FAST_OFF_SLOT_MAP   8
#define COLD_CODE_FAST_OFF_WAY_USED   16
#define COLD_CODE_FAST_OFF_POOL       24
#define COLD_CODE_FAST_OFF_POOL_SLOTS 32
#define COLD_CODE_FAST_OFF_STATE      36
#define COLD_CODE_FAST_OFF_TICK       40
#define COLD_CODE_FAST_OFF_HITS       48

#define COLD_CODE_FAST_STATE_READY 1

#endif /* COLD_CODE_ABI_V1_X86_64_H */
