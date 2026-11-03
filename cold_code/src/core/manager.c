#include "cold_code_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_CRC_CHUNK 512u

_Static_assert(COLD_CODE_MAX_CPUS > 0u, "at least one CPU slot is required");

static cold_code_manager_t *g_managers[COLD_CODE_MAX_CPUS];

__attribute__((weak)) uint32_t cold_code_cpu_id(void) {
    return 0u;
}

static struct cold_code_manager_impl *impl_of(cold_code_manager_t *manager) {
    return (struct cold_code_manager_impl *)manager;
}

static const struct cold_code_manager_impl *impl_of_const(const cold_code_manager_t *manager) {
    return (const struct cold_code_manager_impl *)manager;
}

static bool manager_is_ready(const struct cold_code_manager_impl *manager) {
    return manager != NULL && manager->manager_magic == COLD_CODE_MANAGER_MAGIC &&
           (manager->state.state & COLD_CODE_MANAGER_STATE_READY) != 0u;
}

static void manager_record_clear(struct cold_code_manager_impl *manager) {
    cold_code_memzero(manager, sizeof(*manager));
}

static bool loader_read(const struct cold_code_manager_impl *manager, uint32_t offset, void *dst,
                        size_t size) {
    return manager->loader.load_fptr != NULL &&
           manager->loader.load_fptr(manager->loader.load_ctx, offset, dst, size);
}

static void storage_release(struct cold_code_manager_impl *manager) {
    if (manager->storage != NULL &&
        manager->storage->owner == (cold_code_manager_t *)manager) {
        manager->storage->owner = NULL;
        manager->storage->magic = 0u;
    }
}

static void residency_clear(struct cold_code_manager_impl *manager) {
    if (manager->state.slot_map != NULL) {
        for (uint32_t slot = 0; slot < manager->slot_count; ++slot) {
            manager->state.slot_map[slot] = 0u;
        }
    }
    if (manager->way_slot != NULL) {
        for (uint32_t way = 0; way < manager->state.pool_slots; ++way) {
            manager->way_slot[way] = COLD_CODE_INVALID_SLOT;
        }
    }
    if (manager->state.way_used != NULL) {
        for (uint32_t way = 0; way < manager->state.pool_slots; ++way) {
            manager->state.way_used[way] = 0u;
        }
    }
    manager->state.tick = 0u;
    manager->state.hits = 0u;
    manager->loads = 0u;
    manager->evictions = 0u;
    manager->misses = 0u;
    manager->faults = 0u;
}

static cold_code_status_t storage_header_check(void *storage, size_t storage_size,
                                               cold_code_manager_t *manager) {
    if (storage == NULL || storage_size < sizeof(cold_code_storage_header_t)) {
        return COLD_CODE_ERR_SIZE;
    }
    if ((uintptr_t)storage % _Alignof(cold_code_storage_header_t) != 0u) {
        return COLD_CODE_ERR_SIZE;
    }

    cold_code_storage_header_t *header = (cold_code_storage_header_t *)storage;
    if (header->magic == 0u) {
        return header->owner == NULL ? COLD_CODE_OK : COLD_CODE_ERR_STATE;
    }
    if (header->magic != COLD_CODE_STORAGE_MAGIC) {
        return COLD_CODE_ERR_STATE;
    }
    if (header->abi_version != (uint16_t)COLD_CODE_ABI_VERSION ||
        header->struct_size < (uint16_t)sizeof(*header)) {
        return COLD_CODE_ERR_STATE;
    }
    if (header->owner != NULL && header->owner != (void *)manager) {
        return COLD_CODE_ERR_STATE;
    }
    return COLD_CODE_OK;
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
        if (!loader_read(manager, header->payload_offset + done, buffer, chunk)) {
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

cold_code_status_t cold_code_manager_probe(const cold_code_loader_t *loader,
                                           cold_code_image_info_t *out) {
    if (loader == NULL || loader->load_fptr == NULL || out == NULL ||
        loader->image_size < COLD_CODE_IMAGE_HEADER_SIZE) {
        return COLD_CODE_ERR_ARG;
    }

    unsigned char header_bytes[COLD_CODE_IMAGE_HEADER_SIZE];
    if (!loader->load_fptr(loader->load_ctx, 0u, header_bytes, sizeof(header_bytes))) {
        return COLD_CODE_ERR_SOURCE;
    }

    cold_code_image_header_t header;
    if (cold_code_image_header_decode(header_bytes, sizeof(header_bytes), &header) !=
        COLD_CODE_OK) {
        return COLD_CODE_ERR_IMAGE;
    }

    cold_code_image_info_t info;
    if (!cold_code_image_info_from_header(&header, loader->image_size, &info)) {
        return COLD_CODE_ERR_IMAGE;
    }
    *out = info;
    return COLD_CODE_OK;
}

cold_code_status_t cold_code_manager_init(cold_code_manager_t *manager,
                                          const cold_code_manager_config_t *config) {
    if (manager == NULL || config == NULL || config->loader.load_fptr == NULL ||
        config->loader.image_size < COLD_CODE_IMAGE_HEADER_SIZE || config->exec_addr == NULL ||
        config->exec_size == 0u || config->context == NULL ||
        config->cpuid >= COLD_CODE_MAX_CPUS) {
        return COLD_CODE_ERR_ARG;
    }

    struct cold_code_manager_impl *impl = impl_of(manager);
    if (impl->manager_magic != 0u) {
        return COLD_CODE_ERR_STATE;
    }

    if (!cold_code_context_ops_valid(config->context) ||
        !cold_code_context_arch_valid(config->context)) {
        return COLD_CODE_ERR_UNSUPPORTED;
    }

    cold_code_status_t status =
        storage_header_check(config->exec_addr, config->exec_size, manager);
    if (status != COLD_CODE_OK) {
        return status;
    }

    impl->loader = config->loader;
    impl->context = config->context;
    impl->exec_addr = config->exec_addr;
    impl->exec_size = config->exec_size;
    impl->trace = config->trace;
    impl->fault = config->fault;
    impl->flags = config->flags;

    bool owner_claimed = false;
    cold_code_storage_header_t *storage = (cold_code_storage_header_t *)config->exec_addr;

    unsigned char header_bytes[COLD_CODE_IMAGE_HEADER_SIZE];
    if (!loader_read(impl, 0u, header_bytes, sizeof(header_bytes))) {
        status = COLD_CODE_ERR_SOURCE;
        goto fail;
    }

    cold_code_image_header_t header;
    if (cold_code_image_header_decode(header_bytes, sizeof(header_bytes), &header) !=
        COLD_CODE_OK) {
        status = COLD_CODE_ERR_IMAGE;
        goto fail;
    }

    cold_code_image_info_t info;
    if (!cold_code_image_info_from_header(&header, config->loader.image_size, &info)) {
        status = COLD_CODE_ERR_IMAGE;
        goto fail;
    }

    if ((config->flags & COLD_CODE_INIT_EXPECT_IMAGE_ID) != 0u &&
        config->expected_image_id != info.image_id) {
        status = COLD_CODE_ERR_IMAGE;
        goto fail;
    }
    if (info.abi_version != COLD_CODE_ABI_VERSION) {
        status = COLD_CODE_ERR_UNSUPPORTED;
        goto fail;
    }
    if (info.arch != config->context->arch) {
        status = COLD_CODE_ERR_ARCH;
        goto fail;
    }

    uint32_t pool_slots = cold_code_resolve_pool_slots(&info, config->pool_slots);
    if (pool_slots == 0u) {
        status = COLD_CODE_ERR_ARG;
        goto fail;
    }

    cold_code_storage_layout_t layout;
    if (cold_code_storage_layout(&info, pool_slots, &layout) != COLD_CODE_OK) {
        status = COLD_CODE_ERR_ARG;
        goto fail;
    }
    if (config->exec_size < layout.total_size) {
        status = COLD_CODE_ERR_SIZE;
        goto fail;
    }
    if ((uintptr_t)config->exec_addr % (uintptr_t)info.storage_align != 0u) {
        status = COLD_CODE_ERR_SIZE;
        goto fail;
    }

    storage->owner = manager;
    storage->magic = COLD_CODE_STORAGE_MAGIC;
    storage->abi_version = (uint16_t)COLD_CODE_ABI_VERSION;
    storage->struct_size = (uint16_t)sizeof(*storage);
    storage->image_id = info.image_id;
    storage->slot_count = info.slot_count;
    storage->pool_slots = pool_slots;
    storage->total_size = (uint32_t)layout.total_size;
    impl->storage = storage;
    owner_claimed = true;

    if ((config->flags & COLD_CODE_INIT_VERIFY_CRC) != 0u) {
        if ((info.flags & COLD_CODE_IMAGE_FLAG_PAYLOAD_CRC32) == 0u) {
            status = COLD_CODE_ERR_IMAGE;
            goto fail;
        }
        status = verify_payload_crc(impl, &header, info.payload_size);
        if (status != COLD_CODE_OK) {
            goto fail;
        }
    }

    unsigned char *base = (unsigned char *)config->exec_addr;
    impl->state.image_id = info.image_id;
    impl->image_view = cold_code_image_view_from_info(&info);
    impl->state.slot_map = (uint32_t *)(base + layout.slot_map_offset);
    impl->state.way_used = (uint64_t *)(base + layout.way_used_offset);
    impl->state.pool = base + layout.pool_offset;
    impl->state.pool_slots = pool_slots;
    impl->state.state = 0u;
    impl->way_slot = (uint32_t *)(base + layout.way_slot_offset);
    impl->slot_count = info.slot_count;
    impl->payload_offset = info.payload_offset;
    impl->payload_size = info.payload_size;
    residency_clear(impl);

    impl->manager_magic = COLD_CODE_MANAGER_MAGIC;
    impl->state.state = COLD_CODE_MANAGER_STATE_READY;

    /* Bind last: this is the final fallible step. */
    if (g_managers[config->cpuid] != NULL) {
        status = COLD_CODE_ERR_STATE;
        goto fail;
    }
    g_managers[config->cpuid] = manager;
    impl->cpuid = config->cpuid;
    return COLD_CODE_OK;

fail:
    if (owner_claimed) {
        storage->owner = NULL;
        storage->magic = 0u;
    }
    manager_record_clear(impl);
    return status;
}

cold_code_status_t cold_code_manager_deinit(cold_code_manager_t *manager) {
    if (manager == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    struct cold_code_manager_impl *impl = impl_of(manager);
    if (!manager_is_ready(impl)) {
        return COLD_CODE_ERR_STATE;
    }

    if (impl->cpuid < COLD_CODE_MAX_CPUS && g_managers[impl->cpuid] == manager) {
        g_managers[impl->cpuid] = NULL;
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
    for (uint32_t way = 0; way < impl->state.pool_slots; ++way) {
        if (impl->way_slot[way] != COLD_CODE_INVALID_SLOT) {
            ++loaded;
        }
    }

    out->image_slots = impl->image_view.slot_count;
    out->pool_slots = impl->state.pool_slots;
    out->loaded_slots = loaded;
    out->loads = impl->loads;
    out->evictions = impl->evictions;
    out->hits = impl->state.hits;
    out->misses = impl->misses;
    out->faults = impl->faults;
    return COLD_CODE_OK;
}

const cold_code_image_t *cold_code_manager_image(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return NULL;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? &impl->image_view : NULL;
}

void *cold_code_manager_pool_base(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return NULL;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? impl->state.pool : NULL;
}

uint32_t cold_code_manager_pool_slots(const cold_code_manager_t *manager) {
    if (manager == NULL) {
        return 0u;
    }
    const struct cold_code_manager_impl *impl = impl_of_const(manager);
    return manager_is_ready(impl) ? impl->state.pool_slots : 0u;
}

cold_code_manager_t *cold_code_manager_current(void) {
    uint32_t cpu = cold_code_cpu_id();
    if (cpu >= COLD_CODE_MAX_CPUS) {
        return NULL;
    }
    return g_managers[cpu];
}

unsigned char *cold_code_core_acquire_slot_v2(uint32_t image_id, uint32_t image_slot) {
    uint32_t cpu = cold_code_cpu_id();
    struct cold_code_manager_impl *manager = NULL;
    if (cpu < COLD_CODE_MAX_CPUS) {
        manager = (struct cold_code_manager_impl *)g_managers[cpu];
    }
    if (manager == NULL || !manager_is_ready(manager)) {
        cold_code_core_fatal(NULL, COLD_CODE_FAULT_NO_CURRENT, image_slot,
                             "cold_code: no current manager for this CPU; initialize a "
                             "manager with matching config.cpuid");
    }
    if (manager->state.image_id != image_id) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_WRONG_IMAGE, image_slot,
                             "cold_code: current manager belongs to a different image");
    }
    if (image_slot >= manager->slot_count) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_BAD_SLOT, image_slot,
                             "cold_code: invalid image slot");
    }

    uint32_t slot_size = manager->image_view.slot_size;
    uint32_t way_plus = manager->state.slot_map[image_slot];
    if (way_plus != 0u) {
        if (way_plus > manager->state.pool_slots) {
            cold_code_core_fatal(manager, COLD_CODE_FAULT_CORRUPT_STATE, image_slot,
                                 "cold_code: corrupt slot map");
        }
        uint32_t way = way_plus - 1u;
        manager->state.way_used[way] = ++manager->state.tick;
        ++manager->state.hits;
        return manager->state.pool + (size_t)way * slot_size;
    }

    ++manager->misses;

    uint32_t free_way = COLD_CODE_INVALID_WAY;
    for (uint32_t way = 0; way < manager->state.pool_slots; ++way) {
        if (manager->way_slot[way] == COLD_CODE_INVALID_SLOT) {
            free_way = way;
            break;
        }
    }

    uint32_t way = free_way;
    if (way == COLD_CODE_INVALID_WAY) {
        uint64_t oldest = UINT64_MAX;
        for (uint32_t candidate = 0; candidate < manager->state.pool_slots; ++candidate) {
            if (manager->state.way_used[candidate] < oldest) {
                oldest = manager->state.way_used[candidate];
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
        manager->state.slot_map[old_slot] = 0u;
        ++manager->evictions;
        if (manager->trace.fn != NULL) {
            manager->trace.fn(manager->trace.ctx, COLD_CODE_TRACE_EVICT, old_slot, way);
        }
    }

    unsigned char *target = manager->state.pool + (size_t)way * slot_size;
    uint64_t source_offset =
        (uint64_t)manager->payload_offset + (uint64_t)image_slot * (uint64_t)slot_size;
    if (source_offset > (uint64_t)UINT32_MAX ||
        !loader_read(manager, (uint32_t)source_offset, target, slot_size)) {
        cold_code_core_fatal(manager, COLD_CODE_FAULT_SOURCE_READ, image_slot,
                             "cold_code: failed to read image slot");
    }

    if (manager->context->sync_code != NULL) {
        manager->context->sync_code(target, slot_size);
    }

    manager->way_slot[way] = image_slot;
    manager->state.slot_map[image_slot] = way + 1u;
    manager->state.way_used[way] = ++manager->state.tick;
    ++manager->loads;
    if (manager->trace.fn != NULL) {
        manager->trace.fn(manager->trace.ctx, COLD_CODE_TRACE_LOAD, image_slot, way);
    }

    return target;
}
