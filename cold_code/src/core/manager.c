#include "cold_code_internal.h"
#include "cold_code_port.h"

#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_CRC_CHUNK 512u

static bool valid_image_descriptor(const cold_code_image_t *image) {
    if (image == NULL || image->slot_size == 0u || image->slot_count == 0u) {
        return false;
    }
    if (image->pool_slot_count == 0u || image->pool_slot_count > COLD_CODE_MAX_POOL_SLOTS ||
        image->pool_slot_count > image->slot_count) {
        return false;
    }
    return (uint64_t)image->slot_size * (uint64_t)image->slot_count <= UINT32_MAX;
}

static bool manager_storage_is_bound(const cold_code_manager_t *mgr) {
    return mgr->storage != NULL && mgr->slot_map != NULL && mgr->way_slot != NULL &&
           mgr->way_used != NULL && mgr->pool != NULL && mgr->pool_slots != 0u;
}

static void clear_residency(cold_code_manager_t *mgr) {
    for (uint32_t i = 0; i < mgr->image->slot_count; ++i) {
        mgr->slot_map[i] = 0u;
    }
    for (uint32_t way = 0; way < mgr->pool_slots; ++way) {
        mgr->way_slot[way] = COLD_CODE_INVALID_SLOT;
        mgr->way_used[way] = 0u;
    }
    mgr->tick = 0u;
    mgr->stats = (cold_code_stats_t){0};
}

static cold_code_status_t verify_payload_crc(cold_code_manager_t *mgr,
                                             const cold_code_image_header_t *header,
                                             uint32_t payload_size) {
    unsigned char buffer[COLD_CODE_CRC_CHUNK];
    uint32_t crc = COLD_CODE_CRC32_INIT;
    uint32_t done = 0u;

    while (done < payload_size) {
        uint32_t remaining = payload_size - done;
        size_t chunk = remaining < COLD_CODE_CRC_CHUNK ? (size_t)remaining : COLD_CODE_CRC_CHUNK;
        if (!mgr->source->read(mgr->source_ctx, header->payload_offset + done, buffer, chunk)) {
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

cold_code_status_t cold_code_manager_init(cold_code_manager_t *mgr, const cold_code_image_t *image,
                                          const cold_code_config_t *cfg) {
    if (mgr == NULL || image == NULL || cfg == NULL || cfg->source == NULL ||
        cfg->source->open == NULL || cfg->source->close == NULL || cfg->source->read == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    if (mgr->initialized) {
        return COLD_CODE_ERR_STATE;
    }
    if (!valid_image_descriptor(image) || !manager_storage_is_bound(mgr) ||
        mgr->pool_slots != image->pool_slot_count || mgr->storage_slot_count != image->slot_count) {
        return COLD_CODE_ERR_ARG;
    }
    if (mgr->storage->owner != NULL && mgr->storage->owner != mgr) {
        return COLD_CODE_ERR_STATE;
    }

    uint32_t source_size = 0u;
    if (!cfg->source->open(cfg->source_ctx, &source_size)) {
        return COLD_CODE_ERR_SOURCE;
    }

    cold_code_image_header_t header;
    if (!cfg->source->read(cfg->source_ctx, 0u, &header, sizeof(header))) {
        cfg->source->close(cfg->source_ctx);
        return COLD_CODE_ERR_SOURCE;
    }

    cold_code_status_t status = cold_code_validate_image_header(&header, image, source_size);
    if (status != COLD_CODE_OK) {
        cfg->source->close(cfg->source_ctx);
        return status;
    }

    uint32_t payload_size = header.slot_count * header.slot_size;
    mgr->source = cfg->source;
    mgr->source_ctx = cfg->source_ctx;
    if (cfg->verify_crc) {
        status = verify_payload_crc(mgr, &header, payload_size);
        if (status != COLD_CODE_OK) {
            cfg->source->close(cfg->source_ctx);
            mgr->source = NULL;
            mgr->source_ctx = NULL;
            return status;
        }
    }

    mgr->image = image;
    mgr->payload_offset = header.payload_offset;
    mgr->payload_size = payload_size;
    mgr->verify_crc = cfg->verify_crc;
    mgr->trace = cfg->trace;
    mgr->trace_ctx = cfg->trace_ctx;
    mgr->fatal = cfg->fatal;
    mgr->fatal_ctx = cfg->fatal_ctx;
    clear_residency(mgr);
    mgr->storage->owner = mgr;
    mgr->initialized = true;
    return COLD_CODE_OK;
}

void cold_code_manager_deinit(cold_code_manager_t *mgr) {
    if (mgr == NULL || !mgr->initialized) {
        return;
    }

    if (mgr->source != NULL && mgr->source->close != NULL) {
        mgr->source->close(mgr->source_ctx);
    }
    if (mgr->storage != NULL && mgr->storage->owner == mgr) {
        mgr->storage->owner = NULL;
    }
    if (cold_code_port_current() == mgr) {
        cold_code_port_set_current(NULL);
    }

    mgr->initialized = false;
    mgr->image = NULL;
    mgr->source = NULL;
    mgr->source_ctx = NULL;
    mgr->payload_offset = 0u;
    mgr->payload_size = 0u;
    mgr->trace = NULL;
    mgr->trace_ctx = NULL;
    mgr->fatal = NULL;
    mgr->fatal_ctx = NULL;
}

cold_code_status_t cold_code_manager_reset(cold_code_manager_t *mgr) {
    if (mgr == NULL || !mgr->initialized || mgr->image == NULL) {
        return COLD_CODE_ERR_ARG;
    }
    clear_residency(mgr);
    return COLD_CODE_OK;
}

void cold_code_manager_use(cold_code_manager_t *mgr) {
    if (mgr != NULL && !mgr->initialized) {
        cold_code_port_fatal("cold_code: cold_code_manager_use() got an uninitialised manager");
    }
    cold_code_port_set_current(mgr);
}

cold_code_manager_t *cold_code_manager_current(void) {
    return cold_code_port_current();
}

cold_code_status_t cold_code_manager_stats(const cold_code_manager_t *mgr, cold_code_stats_t *out) {
    if (mgr == NULL || out == NULL || !mgr->initialized || mgr->image == NULL) {
        return COLD_CODE_ERR_ARG;
    }

    uint32_t loaded = 0u;
    for (uint32_t way = 0; way < mgr->pool_slots; ++way) {
        if (mgr->way_slot[way] != COLD_CODE_INVALID_SLOT) {
            ++loaded;
        }
    }

    cold_code_stats_t stats = mgr->stats;
    stats.image_slots = mgr->image->slot_count;
    stats.pool_slots = mgr->image->pool_slot_count;
    stats.loaded_slots = loaded;
    *out = stats;
    return COLD_CODE_OK;
}

_Noreturn void cold_code_manager_fatal(const cold_code_manager_t *mgr, const char *message) {
    if (mgr != NULL && mgr->fatal != NULL) {
        mgr->fatal(mgr->fatal_ctx, message);
        cold_code_port_abort();
    }
    cold_code_port_fatal(message);
}

/*
 * Make the image slot resident and return the base address of its physical
 * slot.  Every generated stub calls this before tail-jumping into the cold
 * function, so a hit also refreshes the LRU timestamp.
 *
 * The current manager is thread-local.  Managers never share mutable state,
 * so no lock or pin is needed; a cold function cannot evict its own slot
 * because cross-slot direct calls are rejected at build time.
 */
unsigned char *cold_code_acquire_slot(uint32_t image_id, uint32_t image_slot) {
    cold_code_manager_t *mgr = cold_code_port_current();
    if (mgr == NULL || !mgr->initialized || mgr->image == NULL) {
        cold_code_port_fatal("cold_code: no current manager; call "
                             "cold_code_manager_use() before calling cold code");
    }

    if (mgr->image->image_id != image_id) {
        cold_code_manager_fatal(mgr, "cold_code: current manager belongs to a different image");
    }
    if (image_slot >= mgr->image->slot_count) {
        cold_code_manager_fatal(mgr, "cold_code: invalid image slot");
    }

    uint32_t slot_size = mgr->image->slot_size;
    uint32_t way_plus = mgr->slot_map[image_slot];

    if (way_plus != 0u) {
        if (way_plus > mgr->pool_slots) {
            cold_code_manager_fatal(mgr, "cold_code: corrupt slot map");
        }
        uint32_t way = way_plus - 1u;
        mgr->way_used[way] = ++mgr->tick;
        ++mgr->stats.hits;
        return mgr->pool + (size_t)way * slot_size;
    }

    ++mgr->stats.misses;

    uint32_t free_way = COLD_CODE_INVALID_WAY;
    for (uint32_t way = 0; way < mgr->pool_slots; ++way) {
        if (mgr->way_slot[way] == COLD_CODE_INVALID_SLOT) {
            free_way = way;
            break;
        }
    }

    uint32_t way = free_way;
    if (way == COLD_CODE_INVALID_WAY) {
        uint64_t oldest = UINT64_MAX;
        for (uint32_t candidate = 0; candidate < mgr->pool_slots; ++candidate) {
            if (mgr->way_used[candidate] < oldest) {
                oldest = mgr->way_used[candidate];
                way = candidate;
            }
        }
        if (way == COLD_CODE_INVALID_WAY) {
            cold_code_manager_fatal(mgr, "cold_code: no LRU victim available");
        }

        uint32_t old_slot = mgr->way_slot[way];
        mgr->slot_map[old_slot] = 0u;
        ++mgr->stats.evictions;
        if (mgr->trace != NULL) {
            mgr->trace(mgr->trace_ctx, COLD_CODE_TRACE_EVICT, old_slot, way);
        }
    }

    if (!mgr->source->read(mgr->source_ctx, mgr->payload_offset + image_slot * slot_size,
                           mgr->pool + (size_t)way * slot_size, slot_size)) {
        cold_code_manager_fatal(mgr, "cold_code: failed to read image slot");
    }

    mgr->way_slot[way] = image_slot;
    mgr->slot_map[image_slot] = way + 1u;
    mgr->way_used[way] = ++mgr->tick;
    ++mgr->stats.loads;
    if (mgr->trace != NULL) {
        mgr->trace(mgr->trace_ctx, COLD_CODE_TRACE_LOAD, image_slot, way);
    }

    return mgr->pool + (size_t)way * slot_size;
}
