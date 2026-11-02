#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool cold_code_image_geometry_valid(const cold_code_image_t *image) {
    if (image == NULL || image->struct_size < (uint32_t)sizeof(*image)) {
        return false;
    }
    if (image->slot_size == 0u || image->slot_count == 0u || image->max_pool_slots == 0u ||
        image->max_pool_slots > COLD_CODE_MAX_POOL_SLOTS ||
        image->max_pool_slots > image->slot_count) {
        return false;
    }
    if (image->storage_align == 0u || !is_power_of_two(image->storage_align) ||
        image->storage_align < _Alignof(cold_code_storage_header_t)) {
        return false;
    }
    if ((image->slot_size % image->storage_align) != 0u) {
        return false;
    }
    return (uint64_t)image->slot_size * (uint64_t)image->slot_count <= (uint64_t)UINT32_MAX;
}

uint32_t cold_code_resolve_pool_slots(const cold_code_image_t *image, uint32_t requested) {
    if (image == NULL || !cold_code_image_geometry_valid(image)) {
        return 0u;
    }
    uint32_t limit = image->max_pool_slots;
    if (limit > image->slot_count) {
        limit = image->slot_count;
    }
    uint32_t pool_slots = requested == 0u ? limit : requested;
    if (pool_slots == 0u || pool_slots > limit) {
        return 0u;
    }
    return pool_slots;
}

cold_code_status_t cold_code_storage_layout(const cold_code_image_t *image, uint32_t pool_slots,
                                            cold_code_storage_layout_t *out) {
    if (image == NULL || out == NULL || !cold_code_image_geometry_valid(image)) {
        return COLD_CODE_ERR_ARG;
    }
    if (pool_slots == 0u || pool_slots > image->max_pool_slots ||
        pool_slots > image->slot_count) {
        return COLD_CODE_ERR_ARG;
    }

    cold_code_storage_layout_t layout;
    layout.pool_offset = COLD_CODE_STORAGE_POOL_OFFSET(image->storage_align);
    layout.slot_map_offset =
        COLD_CODE_STORAGE_SLOT_MAP_OFFSET(image->slot_count, pool_slots, image->slot_size,
                                          image->storage_align);
    layout.way_slot_offset =
        COLD_CODE_STORAGE_WAY_SLOT_OFFSET(image->slot_count, pool_slots, image->slot_size,
                                          image->storage_align);
    layout.way_used_offset =
        COLD_CODE_STORAGE_WAY_USED_OFFSET(image->slot_count, pool_slots, image->slot_size,
                                          image->storage_align);
    layout.total_size =
        COLD_CODE_STORAGE_SIZE(image->slot_count, pool_slots, image->slot_size,
                               image->storage_align);

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

size_t cold_code_storage_required_size(const cold_code_image_t *image, uint32_t pool_slots) {
    cold_code_storage_layout_t layout;
    if (cold_code_storage_layout(image, pool_slots, &layout) != COLD_CODE_OK) {
        return 0u;
    }
    return layout.total_size;
}

size_t cold_code_storage_required_align(const cold_code_image_t *image) {
    if (image == NULL || !cold_code_image_geometry_valid(image)) {
        return 0u;
    }
    return image->storage_align;
}
