#ifndef COLD_CODE_FAST_LAYOUT_X86_64_H
#define COLD_CODE_FAST_LAYOUT_X86_64_H

/*
 * Layout of cold_code_manager_t/cold_code_image_t fields used by the
 * generated x86-64 FAST_PATH stubs.  gen_cold_code.py parses these defines,
 * and fast_layout_check.c verifies them against offsetof().  Keep the two in
 * sync; changing manager/image fields requires updating this header (and the
 * generated stubs).
 */
#define COLD_CODE_FAST_LAYOUT_VERSION 1

#define COLD_CODE_FAST_OFF_MGR_IMAGE       0
#define COLD_CODE_FAST_OFF_MGR_SLOT_MAP    32
#define COLD_CODE_FAST_OFF_MGR_WAY_USED    48
#define COLD_CODE_FAST_OFF_MGR_POOL        56
#define COLD_CODE_FAST_OFF_MGR_POOL_SLOTS  64
#define COLD_CODE_FAST_OFF_MGR_TICK        80
#define COLD_CODE_FAST_OFF_MGR_STATS_HITS  120
#define COLD_CODE_FAST_OFF_MGR_INITIALIZED 136

#define COLD_CODE_FAST_OFF_IMAGE_ID         8
#define COLD_CODE_FAST_OFF_IMAGE_SLOT_SIZE  12
#define COLD_CODE_FAST_OFF_IMAGE_SLOT_COUNT 16

#endif /* COLD_CODE_FAST_LAYOUT_X86_64_H */
