#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_CRC_CHUNK 512u

static struct cold_code_manager_impl *impl_of(cold_code_manager_t *manager) {
    return (struct cold_code_manager_impl *)manager;
}

static const struct cold_code_manager_impl *impl_of_const(const cold_code_manager_t *manager) {
    return (const struct cold_code_manager_impl *)manager;
}

static bool manager_is_ready(const struct cold_code_manager_impl *manager) {
    return manager != NULL && manager->manager_magic == COLD_CODE_MANAGER_MAGIC &&
           (manager->fast.state & COLD_CODE_MANAGER_STATE_READY) != 0u;
}

static void manager_record_clear(struct cold_code_manager_impl *manager) {
    cold_code_memzero(manager, sizeof(*manager));
}

static void storage_release(struct cold_code_manager_impl *manager) {
    if (manager->storage != NULL &&
        manager->storage->owner == (cold_code_manager_t *)manager) {
        manager->storage->owner = NULL;
        manager->storage->magic = 0u;
    }
}

static void residency_clear(struct cold_code_manager_impl *manager) {
    if (manager->fast.slot_map != NULL) {
        for (uint32_t slot = 0; slot < manager->slot_count; ++slot) {
            manager->fast.slot_map[slot] = 0u;
        }
    }
    if (manager->way_slot != NULL) {
        for (uint32_t way = 0; way < manager->fast.pool_slots; ++way) {
            manager->way_slot[way] = COLD_CODE_INVALID_SLOT;
        }
    }
    if (manager->fast.way_used != NULL) {
        for (uint32_t way = 0; way < manager->fast.pool_slots; ++way) {
            manager->fast.way_used[way] = 0u;
        }
    }
    manager->fast.tick = 0u;
    manager->fast.hits = 0u;
    manager->loads = 0u;
    manager->evictions = 0u;
    manager->misses = 0u;
    manager->faults = 0u;
}

static cold_code_status_t manager_fail_init(struct cold_code_manager_impl *manager,
                                            cold_code_status_t status, bool source_opened) {
    if (source_opened && manager->source_ops != NULL && manager->source_ops->close != NULL) {
        manager->source_ops->close(manager->source_ctx);
    }
    storage_release(manager);
    manager_record_clear(manager);
    return status;
}

static cold_code_status_t verify_payload_crc(struct cold_code_manager_impl *manager,
                                             const cold_code_image_header_t *header,
                                             uint32_t payload_size) {
    unsigned char buffer[COLD_CODE_CRC_CHUNK];
    uint32_t crc = COLD_CODE_CRC32_INIT;
    uint32_t done = 0u;

    while (done < payload_size) {
        uint32_t remaining = payload_size - done;
        size_t chunk = remaining < COLD_CODE_CRC_CHUNK ? (size_t)remaining : COLD_CODE_CRC_CHUNK;
        if (!manager->source_ops->read(manager->source_ctx, header->payload_offset + done, buffer,
                                       chunk)) {
            return COLD_CODE_ERR_SOURCE;
        }
        crc = cold_code_crc32_update(crc, buffer, chunk);
        done += (uint32_t)chunk;
    }

    if ((crc ^ 0xFFFFFFFFu) != header->payload_crc32) {
        return COLD_CODE_ERR_IMAGE;
    }
    return COLD_CODE_OK;
}

static cold_code_status_t validate_header(const cold_code_image_header_t *header,
                                          const cold_code_image_t *image, uint32_t source_size,
                                          bool verify_crc) {
    if (header->image_id != image->image_id || header->arch != image->arch ||
        header->abi_version != image->abi_version || header->slot_size != image->slot_size ||
        header->slot_count != image->slot_count ||
        header->max_pool_slots != image->max_pool_slots ||
        header->payload_offset != COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_IMAGE;
    }

    uint64_t expected_payload = (uint64_t)image->slot_size * (uint64_t)image->slot_count;
    if (expected_payload > (uint64_t)UINT32_MAX ||
        (uint64_t)header->payload_size != expected_payload) {
        return COLD_CODE_ERR_IMAGE;
    }
    if ((uint64_t)header->payload_offset + (uint64_t)header->payload_size >
        (uint64_t)source_size) {
        return COLD_CODE_ERR_IMAGE;
    }
    if (verify_crc && (header->flags & COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32) == 0u) {
        return COLD_CODE_ERR_IMAGE;
    }
    return COLD_CODE_OK;
}

cold_code_status_t cold_code_manager_init(cold_code_manager_t *manager,
                                          const cold_code_manager_config_t *config) {
    if (manager == NULL || config == NULL || config->image == NULL ||
        config->storage == NULL || config->storage_size == 0u) {
        return COLD_CODE_ERR_ARG;
    }

    struct cold_code_manager_impl *impl = impl_of(manager);
    if (impl->manager_magic != 0u) {
        return COLD_CODE_ERR_STATE;
    }

    const cold_code_image_t *image = config->image;
    if (!cold_code_image_geometry_valid(image)) {
        return COLD_CODE_ERR_ARG;
    }
    if (image->abi_version != COLD_CODE_ABI_VERSION) {
        return COLD_CODE_ERR_UNSUPPORTED;
    }

    const cold_code_context_ops_t *ops = cold_code_context_ops_checked();
    if (ops == NULL || !cold_code_context_arch_valid(ops)) {
        return COLD_CODE_ERR_UNSUPPORTED;
    }
    if (ops->arch != image->arch) {
        return COLD_CODE_ERR_ARCH;
    }

    if (config->source.ops == NULL || config->source.ops->open == NULL ||
        config->source.ops->read == NULL) {
        return COLD_CODE_ERR_ARG;
    }

    uint32_t pool_slots = cold_code_resolve_pool_slots(image, config->pool_slots);
    if (pool_slots == 0u) {
        return COLD_CODE_ERR_ARG;
    }

    cold_code_storage_layout_t layout;
    if (cold_code_storage_layout(image, pool_slots, &layout) != COLD_CODE_OK) {
        return COLD_CODE_ERR_ARG;
    }
    if (config->storage_size < layout.total_size) {
        return COLD_CODE_ERR_SIZE;
    }
    if ((uintptr_t)config->storage % (uintptr_t)image->storage_align != 0u) {
        return COLD_CODE_ERR_SIZE;
    }

    cold_code_storage_header_t *storage = (cold_code_storage_header_t *)config->storage;
    if (storage->magic == 0u) {
        if (storage->owner != NULL) {
            return COLD_CODE_ERR_STATE;
        }
    } else if (storage->magic != COLD_CODE_STORAGE_MAGIC) {
        return COLD_CODE_ERR_STATE;
    } else {
        if (storage->abi_version != (uint16_t)COLD_CODE_ABI_VERSION ||
            storage->struct_size < (uint16_t)sizeof(*storage)) {
            return COLD_CODE_ERR_STATE;
        }
        if (storage->owner != NULL && storage->owner != (void *)manager) {
            return COLD_CODE_ERR_STATE;
        }
    }

    unsigned char *base = (unsigned char *)config->storage;
    storage->owner = manager;
    storage->magic = COLD_CODE_STORAGE_MAGIC;
    storage->abi_version = (uint16_t)COLD_CODE_ABI_VERSION;
    storage->struct_size = (uint16_t)sizeof(*storage);
    storage->image_id = image->image_id;
    storage->slot_count = image->slot_count;
    storage->pool_slots = pool_slots;
    storage->total_size = (uint32_t)layout.total_size;

    impl->fast.image = image;
    impl->fast.slot_map = (uint32_t *)(base + layout.slot_map_offset);
    impl->fast.way_used = (uint64_t *)(base + layout.way_used_offset);
    impl->fast.pool = base + layout.pool_offset;
    impl->fast.pool_slots = pool_slots;
    impl->fast.state = 0u;
    impl->way_slot = (uint32_t *)(base + layout.way_slot_offset);
    impl->storage = storage;
    impl->source_ops = config->source.ops;
    impl->source_ctx = config->source.ctx;
    impl->slot_count = image->slot_count;
    impl->trace = config->trace;
    impl->fault = config->fault;
    impl->flags = config->flags;
    residency_clear(impl);

    uint32_t source_size = 0u;
    if (!impl->source_ops->open(impl->source_ctx, &source_size)) {
        return manager_fail_init(impl, COLD_CODE_ERR_SOURCE, false);
    }

    unsigned char header_bytes[COLD_CODE_IMAGE_HEADER_SIZE];
    if (!impl->source_ops->read(impl->source_ctx, 0u, header_bytes, sizeof(header_bytes))) {
        return manager_fail_init(impl, COLD_CODE_ERR_SOURCE, true);
    }

    cold_code_image_header_t header;
    cold_code_status_t status =
        cold_code_image_header_decode(header_bytes, sizeof(header_bytes), &header);
    if (status != COLD_CODE_OK) {
        return manager_fail_init(impl, COLD_CODE_ERR_IMAGE, true);
    }

    bool verify_crc = (config->flags & COLD_CODE_INIT_VERIFY_CRC) != 0u;
    status = validate_header(&header, image, source_size, verify_crc);
    if (status != COLD_CODE_OK) {
        return manager_fail_init(impl, status, true);
    }

    if (verify_crc) {
        status = verify_payload_crc(impl, &header, header.payload_size);
        if (status != COLD_CODE_OK) {
            return manager_fail_init(impl, status, true);
        }
    }

    impl->payload_offset = header.payload_offset;
    impl->payload_size = header.payload_size;
    impl->manager_magic = COLD_CODE_MANAGER_MAGIC;
    impl->fast.state = COLD_CODE_MANAGER_STATE_READY;
    return COLD_CODE_OK;
}

cold_code_status_t cold_code_manager_deinit(cold_code_manager_t *manager) {
    if (manager == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    struct cold_code_manager_impl *impl = impl_of(manager);
    if (!manager_is_ready(impl)) {
        return COLD_CODE_ERR_STATE;
    }

    const cold_code_context_ops_t *ops = cold_code_context_ops_checked();
    if (ops != NULL && ops->current() == manager) {
        ops->set_current(NULL);
    }

    if (impl->source_ops != NULL && impl->source_ops->close != NULL) {
        impl->source_ops->close(impl->source_ctx);
    }
    storage_release(impl);
    manager_record_clear(impl);
    return COLD_CODE_OK;
}

cold_code_status_t cold_code_manager_reset(cold_code_manager_t *manager) {
    if (manager == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    struct cold_code_manager_impl *impl = impl_of(manager);
    if (!manager_is_ready(impl)) {
        return COLD_CODE_ERR_STATE;
    }
    residency_clear(impl);
    return COLD_CODE_OK;
}

bool cold_code_manager_is_initialized(const cold_code_manager_t *manager) {
    return manager != NULL && manager_is_ready(impl_of_const(manager));
}

cold_code_status_t cold_code_manager_stats(const cold_code_manager_t *manager,
                                           cold_code_stats_t *out) {
    if (manager == NULL || out == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    if (!manager_is_ready(impl)) {
        return COLD_CODE_ERR_STATE;
    }

    uint32_t loaded = 0u;
    for (uint32_t way = 0; way < impl->fast.pool_slots; ++way) {
        if (impl->way_slot[way] != COLD_CODE_INVALID_SLOT) {
            ++loaded;
        }
    }

    out->image_slots = impl->fast.image->slot_count;
    out->pool_slots = impl->fast.pool_slots;
    out->loaded_slots = loaded;
    out->loads = impl->loads;
    out->evictions = impl->evictions;
    out->hits = impl->fast.hits;
    out->misses = impl->misses;
    out->faults = impl->faults;
    return COLD_CODE_OK;
}

const cold_code_image_t *cold_code_manager_image(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return NULL;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? impl->fast.image : NULL;
}

void *cold_code_manager_pool_base(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return NULL;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? impl->fast.pool : NULL;
}

uint32_t cold_code_manager_pool_slots(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return 0u;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? impl->fast.pool_slots : 0u;
}

cold_code_status_t cold_code_manager_activate(cold_code_manager_t *manager) {
    const cold_code_context_ops_t *ops = cold_code_context_ops_checked();
    if (ops == NULL || !cold_code_context_arch_valid(ops)) {
        return COLD_CODE_ERR_UNSUPPORTED;
    }
    if (manager != NULL && !manager_is_ready(impl_of(manager))) {
        return COLD_CODE_ERR_STATE;
    }
    ops->set_current(manager);
    return COLD_CODE_OK;
}

cold_code_manager_t *cold_code_manager_current(void) {
    const cold_code_context_ops_t *ops = cold_code_context_ops_checked();
    return ops != NULL ? ops->current() : NULL;
}

unsigned char *cold_code_core_acquire_slot_v1(const cold_code_image_t *image,
                                              uint32_t image_slot) {
    const cold_code_context_ops_t *ops = cold_code_context_ops_checked();
    if (ops == NULL) {
        cold_code_core_fatal(NULL, COLD_CODE_FAULT_CORRUPT_STATE, image_slot,
                             "cold_code: context port is not available");
    }

    struct cold_code_manager_impl *manager =
        (struct cold_code_manager_impl *)ops->current();
    if (manager == NULL || !manager_is_ready(manager)) {
        cold_code_core_fatal(NULL, COLD_CODE_FAULT_NO_CURRENT, image_slot,
                             "cold_code: no current manager; call "
                             "cold_code_manager_activate() before calling cold code");
    }
    if (manager->fast.image != image) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_WRONG_IMAGE, image_slot,
                             "cold_code: current manager belongs to a different image");
    }
    if (image_slot >= manager->slot_count) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_BAD_SLOT, image_slot,
                             "cold_code: invalid image slot");
    }

    uint32_t slot_size = image->slot_size;
    uint32_t way_plus = manager->fast.slot_map[image_slot];
    if (way_plus != 0u) {
        if (way_plus > manager->fast.pool_slots) {
            cold_code_core_fatal(manager, COLD_CODE_FAULT_CORRUPT_STATE, image_slot,
                                 "cold_code: corrupt slot map");
        }
        uint32_t way = way_plus - 1u;
        manager->fast.way_used[way] = ++manager->fast.tick;
        ++manager->fast.hits;
        return manager->fast.pool + (size_t)way * slot_size;
    }

    ++manager->misses;

    uint32_t free_way = COLD_CODE_INVALID_WAY;
    for (uint32_t way = 0; way < manager->fast.pool_slots; ++way) {
        if (manager->way_slot[way] == COLD_CODE_INVALID_SLOT) {
            free_way = way;
            break;
        }
    }

    uint32_t way = free_way;
    if (way == COLD_CODE_INVALID_WAY) {
        uint64_t oldest = UINT64_MAX;
        for (uint32_t candidate = 0; candidate < manager->fast.pool_slots; ++candidate) {
            if (manager->fast.way_used[candidate] < oldest) {
                oldest = manager->fast.way_used[candidate];
                way = candidate;
            }
        }
        if (way == COLD_CODE_INVALID_WAY) {
            cold_code_core_fatal(manager, COLD_CODE_FAULT_NO_VICTIM, image_slot,
                                 "cold_code: no LRU victim available");
        }

        uint32_t old_slot = manager->way_slot[way];
        if (old_slot >= manager->slot_count) {
            cold_code_core_fatal(manager, COLD_CODE_FAULT_CORRUPT_STATE, image_slot,
                                 "cold_code: corrupt way table");
        }
        manager->fast.slot_map[old_slot] = 0u;
        ++manager->evictions;
        if (manager->trace.fn != NULL) {
            manager->trace.fn(manager->trace.ctx, COLD_CODE_TRACE_EVICT, old_slot, way);
        }
    }

    unsigned char *target = manager->fast.pool + (size_t)way * slot_size;
    uint64_t source_offset =
        (uint64_t)manager->payload_offset + (uint64_t)image_slot * (uint64_t)slot_size;
    if (source_offset > (uint64_t)UINT32_MAX ||
        !manager->source_ops->read(manager->source_ctx, (uint32_t)source_offset, target,
                                   slot_size)) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_SOURCE_READ, image_slot,
                             "cold_code: failed to read image slot");
    }

    if (ops->sync_code != NULL) {
        ops->sync_code(target, slot_size);
    }

    manager->way_slot[way] = image_slot;
    manager->fast.slot_map[image_slot] = way + 1u;
    manager->fast.way_used[way] = ++manager->fast.tick;
    ++manager->loads;
    if (manager->trace.fn != NULL) {
        manager->trace.fn(manager->trace.ctx, COLD_CODE_TRACE_LOAD, image_slot, way);
    }

    return target;
}
