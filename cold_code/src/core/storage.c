#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint32_t cold_code_resolve_pool_slots(const cold_code_image_info_t *info, uint32_t requested) {
    if (!cold_code_image_info_valid(info)) {
        return 0u;
    }
    uint32_t limit = info->max_pool_slots;
    if (limit > info->slot_count) {
        limit = info->slot_count;
    }
    uint32_t pool_slots = requested == 0u ? limit : requested;
    if (pool_slots == 0u || pool_slots > limit) {
        return 0u;
    }
    return pool_slots;
}

cold_code_status_t cold_code_storage_layout(const cold_code_image_info_t *info, uint32_t pool_slots,
                                            cold_code_storage_layout_t *out) {
    if (info == NULL || out == NULL || !cold_code_image_info_valid(info)) {
        return COLD_CODE_ERR_ARG;
    }
    if (pool_slots == 0u || pool_slots > info->max_pool_slots ||
        pool_slots > info->slot_count) {
        return COLD_CODE_ERR_ARG;
    }

    cold_code_storage_layout_t layout;
    layout.pool_offset = COLD_CODE_STORAGE_POOL_OFFSET(info->storage_align);
    layout.slot_map_offset =
        COLD_CODE_STORAGE_SLOT_MAP_OFFSET(info->slot_count, pool_slots, info->slot_size,
                                          info->storage_align);
    layout.way_slot_offset =
        COLD_CODE_STORAGE_WAY_SLOT_OFFSET(info->slot_count, pool_slots, info->slot_size,
                                          info->storage_align);
    layout.way_used_offset =
        COLD_CODE_STORAGE_WAY_USED_OFFSET(info->slot_count, pool_slots, info->slot_size,
                                          info->storage_align);
    layout.total_size =
        COLD_CODE_STORAGE_SIZE(info->slot_count, pool_slots, info->slot_size,
                               info->storage_align);

    if (layout.pool_offset > layout.slot_map_offset ||
        layout.slot_map_offset > layout.way_slot_offset ||
        layout.way_slot_offset > layout.way_used_offset ||
        layout.way_used_offset > layout.total_size ||
        layout.total_size > (size_t)UINT32_MAX) {
        return COLD_CODE_ERR_ARG;
    }
    out->pool_offset = layout.pool_offset;
    out->slot_map_offset = layout.slot_map_offset;
    out->way_slot_offset = layout.way_slot_offset;
    out->way_used_offset = layout.way_used_offset;
    out->total_size = layout.total_size;
    return COLD_CODE_OK;
}

size_t cold_code_storage_required_size(const cold_code_image_info_t *info, uint32_t pool_slots) {
    if (!cold_code_image_info_valid(info)) {
        return 0u;
    }
    uint32_t resolved = cold_code_resolve_pool_slots(info, pool_slots);
    if (resolved == 0u) {
        return 0u;
    }
    cold_code_storage_layout_t layout;
    if (cold_code_storage_layout(info, resolved, &layout) != COLD_CODE_OK) {
        return 0u;
    }
    return layout.total_size;
}

size_t cold_code_storage_required_align(const cold_code_image_info_t *info) {
    if (!cold_code_image_info_valid(info)) {
        return 0u;
    }
    return info->storage_align;
}
