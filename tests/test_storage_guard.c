#include "cold_code.h"
#include "cold_code_host.h"
#include "image_fixture.h"
#include "test_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Internal v1 stub entry, exercised directly until the generator exists. */
unsigned char *cold_code_core_acquire_slot_v1(const cold_code_image_t *image,
                                              uint32_t image_slot);

typedef union {
    max_align_t align;
    unsigned char bytes[4096];
} test_storage_t __attribute__((aligned(64)));

static unsigned trace_loads;
static unsigned trace_evictions;

static void trace_event(void *ctx, cold_code_trace_event_t event, uint32_t image_slot,
                        uint32_t way) {
    (void)ctx;
    (void)image_slot;
    (void)way;
    if (event == COLD_CODE_TRACE_LOAD) {
        ++trace_loads;
    } else {
        ++trace_evictions;
    }
}

static bool fail_open(void *ctx, uint32_t *size_out) {
    (void)ctx;
    (void)size_out;
    return false;
}

static bool fail_read(void *ctx, uint32_t offset, void *dst, size_t len) {
    (void)ctx;
    (void)offset;
    (void)dst;
    (void)len;
    return false;
}

static const cold_code_source_ops_t fail_source_ops = {
    .open = fail_open,
    .close = NULL,
    .read = fail_read,
};

static cold_code_stats_t stats_of(const cold_code_manager_t *manager) {
    cold_code_stats_t stats;
    if (cold_code_manager_stats(manager, &stats) != COLD_CODE_OK) {
        cold_code_stats_t bad = {0};
        return bad;
    }
    return stats;
}

static cold_code_manager_config_t make_config(const cold_code_image_t *image, void *storage,
                                              size_t storage_size, uint32_t pool_slots,
                                              cold_code_source_t source) {
    cold_code_manager_config_t config = {
        .image = image,
        .storage = storage,
        .storage_size = storage_size,
        .pool_slots = pool_slots,
        .source = source,
        .flags = COLD_CODE_INIT_VERIFY_CRC,
        .trace = {.fn = trace_event, .ctx = NULL},
    };
    return config;
}

int main(void) {
    test_image_fixture_t fixture;
    CHECK_EQ(test_image_fixture_init(&fixture, 3u, 2u), COLD_CODE_OK);
    CHECK_EQ(fixture.size, 64u + 3u * 64u);

    size_t required = cold_code_storage_required_size(&fixture.image, 2u);
    CHECK_EQ(required, (size_t)COLD_CODE_STORAGE_SIZE(3u, 2u, 64u, 64u));
    CHECK(required <= sizeof(test_storage_t));
    CHECK_EQ(cold_code_storage_required_align(&fixture.image), 64u);

    /* v3 header encode/decode and little-endian wire layout. */
    cold_code_image_header_t header;
    CHECK_EQ(cold_code_image_header_decode(fixture.bytes, fixture.size, &header), COLD_CODE_OK);
    CHECK_EQ(header.magic, COLD_CODE_IMAGE_MAGIC);
    CHECK_EQ(header.version, COLD_CODE_IMAGE_VERSION);
    CHECK_EQ(header.header_size, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.payload_offset, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.payload_size, 3u * 64u);
    CHECK_EQ(fixture.bytes[0], (unsigned char)'C');
    CHECK_EQ(fixture.bytes[1], (unsigned char)'O');
    CHECK_EQ(fixture.bytes[2], (unsigned char)'L');
    CHECK_EQ(fixture.bytes[3], (unsigned char)'D');
    CHECK_EQ(cold_code_image_header_decode(fixture.bytes, 63u, &header), COLD_CODE_ERR_IMAGE);

    unsigned char bad_header[COLD_CODE_IMAGE_HEADER_SIZE];
    memcpy(bad_header, fixture.bytes, sizeof(bad_header));
    bad_header[48] ^= 1u; /* header CRC */
    CHECK_EQ(cold_code_image_header_decode(bad_header, sizeof(bad_header), &header),
             COLD_CODE_ERR_IMAGE);

    static test_storage_t storage;
    static test_storage_t storage_b;
    static cold_code_manager_t manager;
    static cold_code_manager_t manager_b;
    static cold_code_manager_t manager_c;
    static cold_code_memory_source_t memory;

    cold_code_memory_source_init(&memory, fixture.bytes, fixture.size);
    cold_code_source_t source = cold_code_memory_source(&memory);
    cold_code_manager_config_t config =
        make_config(&fixture.image, &storage, sizeof(storage), 1u, source);

    trace_loads = 0;
    trace_evictions = 0;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_is_initialized(&manager));
    CHECK_EQ(cold_code_manager_image(&manager), &fixture.image);
    CHECK_EQ(cold_code_manager_pool_slots(&manager), 1u);
    CHECK_EQ(cold_code_manager_pool_base(&manager),
             (void *)((unsigned char *)&storage + COLD_CODE_STORAGE_POOL_OFFSET(64u)));
    CHECK_EQ(cold_code_manager_activate(&manager), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == &manager);

    cold_code_stats_t stats = stats_of(&manager);
    CHECK_EQ(stats.image_slots, 3u);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 0u);

    unsigned char *pool = (unsigned char *)cold_code_manager_pool_base(&manager);
    unsigned char *slot0 = cold_code_core_acquire_slot_v1(&fixture.image, 0u);
    CHECK(slot0 == pool);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.misses, 1u);
    CHECK_EQ(stats.hits, 0u);
    CHECK_EQ(stats.evictions, 0u);
    CHECK_EQ(stats.loaded_slots, 1u);

    CHECK(cold_code_core_acquire_slot_v1(&fixture.image, 0u) == slot0);
    stats = stats_of(&manager);
    CHECK_EQ(stats.hits, 1u);
    CHECK_EQ(stats.misses, 1u);

    unsigned char *slot1 = cold_code_core_acquire_slot_v1(&fixture.image, 1u);
    CHECK(slot1 == pool);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loads, 2u);
    CHECK_EQ(stats.misses, 2u);
    CHECK_EQ(stats.evictions, 1u);
    CHECK_EQ(stats.hits, 1u);

    CHECK(cold_code_core_acquire_slot_v1(&fixture.image, 2u) == pool);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loads, 3u);
    CHECK_EQ(stats.misses, 3u);
    CHECK_EQ(stats.evictions, 2u);
    CHECK_EQ(stats.loaded_slots, 1u);
    CHECK_EQ(trace_loads, 3u);
    CHECK_EQ(trace_evictions, 2u);

    CHECK_EQ(cold_code_manager_reset(&manager), COLD_CODE_OK);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loaded_slots, 0u);
    CHECK_EQ(stats.loads, 0u);
    CHECK_EQ(stats.hits, 0u);
    CHECK(cold_code_manager_is_initialized(&manager));
    CHECK(cold_code_core_acquire_slot_v1(&fixture.image, 0u) == pool);
    CHECK_EQ(stats_of(&manager).loads, 1u);

    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);
    CHECK(!cold_code_manager_is_initialized(&manager));
    CHECK(cold_code_manager_current() == NULL);
    stats = stats_of(&manager);
    CHECK_EQ(stats.image_slots, 0u); /* stats failed and returned a zeroed struct */
    CHECK_EQ(cold_code_manager_stats(&manager, &stats), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_reset(&manager), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_activate(&manager), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_activate(NULL), COLD_CODE_OK);
    CHECK(cold_code_manager_current() == NULL);

    /* Storage is reusable after deinit, with a different pool size. */
    config = make_config(&fixture.image, &storage, sizeof(storage), 2u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_activate(&manager), COLD_CODE_OK);
    CHECK_EQ(stats_of(&manager).pool_slots, 2u);
    CHECK(cold_code_core_acquire_slot_v1(&fixture.image, 0u) == pool);
    CHECK(cold_code_core_acquire_slot_v1(&fixture.image, 1u) == pool + 64u);
    CHECK_EQ(stats_of(&manager).loaded_slots, 2u);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    /* pool_slots == 0 selects the image maximum. */
    config = make_config(&fixture.image, &storage, sizeof(storage), 0u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(stats_of(&manager).pool_slots, 2u);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    /* Too-small and misaligned storage. */
    size_t required_one = cold_code_storage_required_size(&fixture.image, 1u);
    CHECK(required_one > 0u);
    config = make_config(&fixture.image, &storage, required_one - 1u, 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SIZE);
    config = make_config(&fixture.image, (unsigned char *)&storage + 1u, sizeof(storage), 1u,
                         source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SIZE);

    /* One storage serves one manager at a time. */
    config = make_config(&fixture.image, &storage_b, sizeof(storage_b), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&manager_b, &config), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&manager_b, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_deinit(&manager_b), COLD_CODE_OK);

    /* Repeated init requires deinit. */
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    /* Corrupt header/payload and failed source open are rejected cleanly. */
    test_image_fixture_t broken = fixture;
    broken.bytes[0] ^= 1u;
    cold_code_memory_source_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_source(&memory);
    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    broken = fixture;
    broken.bytes[COLD_CODE_IMAGE_HEADER_SIZE] ^= 0xFFu;
    cold_code_memory_source_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_source(&memory);
    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    cold_code_memory_source_init(&memory, broken.bytes, 8u);
    source = cold_code_memory_source(&memory);
    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SOURCE);

    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    config.source.ops = &fail_source_ops;
    config.source.ctx = NULL;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SOURCE);

    /* Argument and descriptor validation. */
    cold_code_memory_source_init(&memory, fixture.bytes, fixture.size);
    source = cold_code_memory_source(&memory);
    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(NULL, &config), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&manager, NULL), COLD_CODE_ERR_ARG);
    config.image = NULL;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&fixture.image, NULL, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&fixture.image, &storage, sizeof(storage), 1u, source);
    config.source.ops = NULL;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&fixture.image, &storage, sizeof(storage), 3u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);

    cold_code_image_t bad_image = fixture.image;
    bad_image.abi_version = 99u;
    config = make_config(&bad_image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_UNSUPPORTED);

    bad_image = fixture.image;
    bad_image.arch = COLD_CODE_ARCH_ARMV7M;
    config = make_config(&bad_image, &storage, sizeof(storage), 1u, source);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARCH);

    CHECK_EQ(cold_code_manager_activate(&manager_c), COLD_CODE_ERR_STATE);

    TEST_OK();
    return 0;
}
