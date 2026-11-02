#include "cold_code.h"
#include "cold_code_port.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define COLD_CODE_INVALID_SLOT UINT32_MAX

/* Defined by the generated cold_code_stubs.S. */
extern unsigned char cold_code_slot_pool[] __attribute__((visibility("hidden")));

/* Called by the generated stubs; not part of the public API. */
unsigned char *cold_code_acquire_slot(uint32_t image_slot);

typedef struct {
    uint32_t image_slot; /* COLD_CODE_INVALID_SLOT when the way is free */
    uint64_t last_used;  /* monotonic LRU tick */
} cold_code_way_t;

static const cold_code_image_t *g_image;
static cold_code_way_t g_ways[COLD_CODE_MAX_POOL_SLOTS];
static uint64_t g_tick;
static bool g_trace;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void cold_code_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}

static void cold_code_unlock(void) {
    atomic_flag_clear_explicit(&g_lock, memory_order_release);
}

bool cold_code_init(const cold_code_image_t *image, const char *image_path) {
    if (image == NULL || image_path == NULL) {
        return false;
    }

    /* Initialisation is a one-shot operation; the same descriptor is a no-op. */
    if (g_image != NULL) {
        return g_image == image;
    }

    if (image->slot_size == 0 || image->slot_count == 0 || image->pool_slot_count == 0 ||
        image->pool_slot_count > COLD_CODE_MAX_POOL_SLOTS ||
        image->pool_slot_count > image->slot_count) {
        return false;
    }

    uint64_t image_size = (uint64_t)image->slot_count * image->slot_size;
    if (image_size == 0 || image_size > UINT32_MAX) {
        return false;
    }

    if (!cold_code_port_open(image_path)) {
        return false;
    }

    if (cold_code_port_size() < (size_t)image_size) {
        return false;
    }

    for (size_t i = 0; i < COLD_CODE_MAX_POOL_SLOTS; ++i) {
        g_ways[i].image_slot = COLD_CODE_INVALID_SLOT;
        g_ways[i].last_used = 0;
    }
    g_tick = 0;
    g_image = image;
    return true;
}

void cold_code_set_trace(bool enabled) {
    g_trace = enabled;
}

size_t cold_code_total_slots(void) {
    return g_image != NULL ? (size_t)g_image->slot_count : 0;
}

size_t cold_code_pool_slots(void) {
    return g_image != NULL ? (size_t)g_image->pool_slot_count : 0;
}

size_t cold_code_loaded_slots(void) {
    const cold_code_image_t *image = g_image;
    if (image == NULL) {
        return 0;
    }

    size_t loaded = 0;
    cold_code_lock();
    for (size_t i = 0; i < image->pool_slot_count; ++i) {
        if (g_ways[i].image_slot != COLD_CODE_INVALID_SLOT) {
            ++loaded;
        }
    }
    cold_code_unlock();
    return loaded;
}

/*
 * Make the image slot resident and return the base address of its physical
 * slot.  Every generated stub calls this before tail-jumping into the cold
 * function, so a hit also refreshes the LRU timestamp.
 *
 * Eviction safety: the current model assumes single-threaded/cooperative use.
 * A cold function cannot cause a cross-slot load because cross-slot direct
 * calls are rejected at build time, so it cannot evict its own slot.
 */
unsigned char *cold_code_acquire_slot(uint32_t image_slot) {
    const cold_code_image_t *image = g_image;
    if (image == NULL) {
        cold_code_port_fatal("cold_code: cold_code_init() was not called");
    }

    if (image_slot >= image->slot_count) {
        cold_code_port_fatal("cold_code: invalid image slot %u", (unsigned)image_slot);
    }

    size_t slot_size = image->slot_size;
    size_t way = image->pool_slot_count;
    size_t free_way = image->pool_slot_count;
    size_t lru_way = image->pool_slot_count;
    uint64_t oldest = UINT64_MAX;

    cold_code_lock();

    for (size_t i = 0; i < image->pool_slot_count; ++i) {
        if (g_ways[i].image_slot == image_slot) {
            g_ways[i].last_used = ++g_tick;
            way = i;
            break;
        }
        if (g_ways[i].image_slot == COLD_CODE_INVALID_SLOT) {
            if (free_way == image->pool_slot_count) {
                free_way = i;
            }
        } else if (g_ways[i].last_used < oldest) {
            oldest = g_ways[i].last_used;
            lru_way = i;
        }
    }

    if (way == image->pool_slot_count) {
        if (free_way != image->pool_slot_count) {
            way = free_way;
        } else {
            if (lru_way == image->pool_slot_count) {
                cold_code_unlock();
                cold_code_port_fatal("cold_code: no LRU victim available");
            }
            way = lru_way;
            if (g_trace) {
                cold_code_port_log("[cold-code] evict image slot %u from way %zu\n",
                                   (unsigned)g_ways[way].image_slot, way);
            }
        }

        g_ways[way].image_slot = image_slot;
        cold_code_port_read((uint32_t)((size_t)image_slot * slot_size),
                            cold_code_slot_pool + way * slot_size, slot_size);
        g_ways[way].last_used = ++g_tick;

        if (g_trace) {
            cold_code_port_log("[cold-code] load image slot %u into way %zu\n",
                               (unsigned)image_slot, way);
        }
    }

    unsigned char *base = cold_code_slot_pool + way * slot_size;
    cold_code_unlock();
    return base;
}
