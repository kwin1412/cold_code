#include "cold_code.h"
#include "cold_code_host.h"
#include "image_fixture.h"
#include "test_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Internal v2 stub entry, exercised directly without generated stubs. */
unsigned char *cold_code_core_acquire_slot_v2(uint32_t image_id, uint32_t image_slot);

typedef union {
    max_align_t align;
    unsigned char bytes[4096];
} test_storage_t __attribute__((aligned(64)));

typedef struct {
    const unsigned char *data;
    size_t size;
    unsigned reads;
} counting_loader_t;

static bool fail_load(void *ctx, uint32_t offset, void *dst, size_t size) {
    (void)ctx;
    (void)offset;
    (void)dst;
    (void)size;
    return false;
}

static bool counting_load(void *ctx, uint32_t offset, void *dst, size_t size) {
    counting_loader_t *loader = (counting_loader_t *)ctx;
    if (loader == NULL || (dst == NULL && size != 0u) ||
        (uint64_t)offset + (uint64_t)size > (uint64_t)loader->size) {
        return false;
    }
    memcpy(dst, loader->data + offset, size);
    ++loader->reads;
    return true;
}

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

static cold_code_stats_t stats_of(const cold_code_manager_t *manager) {
    cold_code_stats_t stats;
    if (cold_code_manager_stats(manager, &stats) != COLD_CODE_OK) {
        cold_code_stats_t bad = {0};
        return bad;
    }
    return stats;
}

static cold_code_manager_config_t make_config(void *storage, size_t storage_size,
                                              uint32_t pool_slots, cold_code_loader_t source,
                                              uint32_t flags, uint32_t expected_image_id) {
    cold_code_manager_config_t config = {
        .exec_addr = storage,
        .exec_size = storage_size,
        .cpuid = 0u,
        .context = &cold_code_host_context_ops,
        .pool_slots = pool_slots,
        .expected_image_id = expected_image_id,
        .loader = source,
        .flags = flags,
        .trace = {.fn = trace_event, .ctx = NULL},
    };
    return config;
}

int main(void) {
    test_image_fixture_t fixture;
    CHECK_EQ(test_image_fixture_init(&fixture, 3u, 2u), COLD_CODE_OK);
    CHECK_EQ(fixture.size, 64u + 3u * 64u);

    static test_storage_t storage;
    static test_storage_t storage_b;
    static cold_code_manager_t manager;
    static cold_code_manager_t manager_b;
    static cold_code_memory_loader_t memory;

    cold_code_memory_loader_init(&memory, fixture.bytes, fixture.size);
    cold_code_loader_t source = cold_code_memory_loader(&memory);
    cold_code_image_info_t info;
    CHECK_EQ(cold_code_manager_probe(&source, &info), COLD_CODE_OK);
    CHECK_EQ(info.slot_size, 64u);
    CHECK_EQ(info.slot_count, 3u);
    CHECK_EQ(info.max_pool_slots, 2u);
    CHECK_EQ(info.arch, COLD_CODE_ARCH_X86_64);
    CHECK_EQ(info.abi_version, COLD_CODE_ABI_VERSION);
    CHECK_EQ(info.storage_align, 64u);
    CHECK_EQ(info.payload_offset, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(info.payload_size, 3u * 64u);
    CHECK_EQ(info.reserved, 0u);

    size_t required = cold_code_storage_required_size(&info, 2u);
    CHECK_EQ(required, (size_t)COLD_CODE_STORAGE_SIZE(3u, 2u, 64u, 64u));
    CHECK(required <= sizeof(test_storage_t));
    CHECK_EQ(cold_code_storage_required_align(&info), 64u);
    CHECK_EQ(cold_code_storage_required_size(&info, 0u),
             (size_t)COLD_CODE_STORAGE_SIZE(3u, 2u, 64u, 64u));

    /* v4 header encode/decode and little-endian wire layout. */
    cold_code_image_header_t header;
    CHECK_EQ(cold_code_image_header_decode(fixture.bytes, fixture.size, &header), COLD_CODE_OK);
    CHECK_EQ(header.magic, COLD_CODE_IMAGE_MAGIC);
    CHECK_EQ(header.version, COLD_CODE_IMAGE_VERSION);
    CHECK_EQ(header.header_size, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.payload_offset, COLD_CODE_IMAGE_HEADER_SIZE);
    CHECK_EQ(header.payload_size, 3u * 64u);
    CHECK_EQ(header.storage_align, 64u);
    CHECK_EQ(header.reserved[0], 0u);
    CHECK_EQ(header.reserved[1], 0u);
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

    trace_loads = 0;
    trace_evictions = 0;
    cold_code_manager_config_t config =
        make_config(&storage, sizeof(storage), 1u, source,
                    COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID, info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK(cold_code_manager_is_initialized(&manager));
    const cold_code_image_t *view = cold_code_manager_image(&manager);
    CHECK(view != NULL);
    CHECK_EQ(view->image_id, info.image_id);
    CHECK_EQ(view->slot_size, info.slot_size);
    CHECK_EQ(view->slot_count, info.slot_count);
    CHECK_EQ(view->max_pool_slots, info.max_pool_slots);
    CHECK_EQ(view->arch, info.arch);
    CHECK_EQ(view->abi_version, info.abi_version);
    CHECK_EQ(view->struct_size, sizeof(cold_code_image_t));
    CHECK_EQ(view->storage_align, info.storage_align);
    CHECK(view->name == NULL);
    CHECK_EQ(cold_code_manager_pool_slots(&manager), 1u);
    CHECK_EQ(cold_code_manager_pool_base(&manager),
             (void *)((unsigned char *)&storage + COLD_CODE_STORAGE_POOL_OFFSET(64u)));
    CHECK(cold_code_manager_current() == &manager);

    cold_code_stats_t stats = stats_of(&manager);
    CHECK_EQ(stats.image_slots, 3u);
    CHECK_EQ(stats.pool_slots, 1u);
    CHECK_EQ(stats.loaded_slots, 0u);

    unsigned char *pool = (unsigned char *)cold_code_manager_pool_base(&manager);
    unsigned char *slot0 = cold_code_core_acquire_slot_v2(info.image_id, 0u);
    CHECK(slot0 == pool);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loads, 1u);
    CHECK_EQ(stats.misses, 1u);
    CHECK_EQ(stats.hits, 0u);
    CHECK_EQ(stats.evictions, 0u);
    CHECK_EQ(stats.loaded_slots, 1u);

    CHECK(cold_code_core_acquire_slot_v2(info.image_id, 0u) == slot0);
    stats = stats_of(&manager);
    CHECK_EQ(stats.hits, 1u);
    CHECK_EQ(stats.misses, 1u);

    unsigned char *slot1 = cold_code_core_acquire_slot_v2(info.image_id, 1u);
    CHECK(slot1 == pool);
    stats = stats_of(&manager);
    CHECK_EQ(stats.loads, 2u);
    CHECK_EQ(stats.misses, 2u);
    CHECK_EQ(stats.evictions, 1u);
    CHECK_EQ(stats.hits, 1u);

    CHECK(cold_code_core_acquire_slot_v2(info.image_id, 2u) == pool);
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
    CHECK(cold_code_core_acquire_slot_v2(info.image_id, 0u) == pool);
    CHECK_EQ(stats_of(&manager).loads, 1u);

    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);
    CHECK(!cold_code_manager_is_initialized(&manager));
    CHECK(cold_code_manager_current() == NULL);
    stats = stats_of(&manager);
    CHECK_EQ(stats.image_slots, 0u); /* stats failed and returned a zeroed struct */
    CHECK_EQ(cold_code_manager_stats(&manager, &stats), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_ERR_STATE);
    CHECK_EQ(cold_code_manager_reset(&manager), COLD_CODE_ERR_STATE);
    CHECK(cold_code_manager_current() == NULL);

    /* Storage is reusable after deinit, with a different pool size. */
    config = make_config(&storage, sizeof(storage), 2u, source,
                         COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(stats_of(&manager).pool_slots, 2u);
    CHECK(cold_code_core_acquire_slot_v2(info.image_id, 0u) == pool);
    CHECK(cold_code_core_acquire_slot_v2(info.image_id, 1u) == pool + 64u);
    CHECK_EQ(stats_of(&manager).loaded_slots, 2u);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    /* pool_slots == 0 selects the image maximum. */
    config = make_config(&storage, sizeof(storage), 0u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(stats_of(&manager).pool_slots, 2u);
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    /* Too-small and misaligned storage. */
    size_t required_one = cold_code_storage_required_size(&info, 1u);
    CHECK(required_one > 0u);
    config = make_config(&storage, required_one - 1u, 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SIZE);
    config = make_config((unsigned char *)&storage + 1u, sizeof(storage), 1u, source,
                         COLD_CODE_INIT_VERIFY_CRC, info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SIZE);

    /* One storage serves one manager at a time. */
    config = make_config(&storage_b, sizeof(storage_b), 1u, source,
                         COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(cold_code_manager_init(&manager_b, &config), COLD_CODE_ERR_STATE);
    CHECK(cold_code_manager_current() == &manager);
    CHECK(!cold_code_manager_is_initialized(&manager_b));
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
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    broken = fixture;
    broken.bytes[COLD_CODE_IMAGE_HEADER_SIZE] ^= 0xFFu;
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    cold_code_memory_loader_init(&memory, broken.bytes, 8u);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);

    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    config.loader.load_fptr = fail_load;
    config.loader.load_ctx = NULL;
    config.loader.image_size = COLD_CODE_IMAGE_HEADER_SIZE;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SOURCE);

    /* Argument and metadata validation. */
    cold_code_memory_loader_init(&memory, fixture.bytes, fixture.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source,
                         COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(NULL, &config), COLD_CODE_ERR_ARG);
    CHECK_EQ(cold_code_manager_init(&manager, NULL), COLD_CODE_ERR_ARG);
    config = make_config(NULL, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&storage, 0u, 1u, source, COLD_CODE_INIT_VERIFY_CRC, info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    config.loader.load_fptr = NULL;
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);
    config = make_config(&storage, sizeof(storage), 3u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARG);

    /* Expected image id mismatch. */
    config = make_config(&storage, sizeof(storage), 1u, source,
                         COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                         info.image_id ^ 1u);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    /* Wire geometry/ABI/arch mutations are rejected as image errors. */
    broken = fixture;
    test_image_fixture_put_u32(&broken, 20u, 0u); /* slot_size */
    CHECK_EQ(test_image_fixture_refresh(&broken), COLD_CODE_OK);
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    broken = fixture;
    test_image_fixture_put_u32(&broken, 52u, 0u); /* storage_align */
    CHECK_EQ(test_image_fixture_refresh(&broken), COLD_CODE_OK);
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_IMAGE);

    broken = fixture;
    test_image_fixture_put_u32(&broken, 16u, 99u); /* abi_version */
    CHECK_EQ(test_image_fixture_refresh(&broken), COLD_CODE_OK);
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_UNSUPPORTED);

    broken = fixture;
    test_image_fixture_put_u32(&broken, 12u, COLD_CODE_ARCH_ARMV7M);
    CHECK_EQ(test_image_fixture_refresh(&broken), COLD_CODE_OK);
    cold_code_memory_loader_init(&memory, broken.bytes, broken.size);
    source = cold_code_memory_loader(&memory);
    config = make_config(&storage, sizeof(storage), 1u, source, COLD_CODE_INIT_VERIFY_CRC,
                         info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_ARCH);

    /* Loader usage and the early storage check. */
    counting_loader_t counting = {.data = fixture.bytes, .size = fixture.size};
    cold_code_loader_t counting_loader = {
        .load_fptr = counting_load,
        .load_ctx = &counting,
        .image_size = (uint32_t)fixture.size,
    };
    cold_code_image_info_t counting_info;
    CHECK_EQ(cold_code_manager_probe(&counting_loader, &counting_info), COLD_CODE_OK);
    CHECK_EQ(counting.reads, 1u);
    counting.reads = 0u;
    config = make_config(&storage, sizeof(storage), 1u, counting_loader,
                         COLD_CODE_INIT_VERIFY_CRC | COLD_CODE_INIT_EXPECT_IMAGE_ID,
                         counting_info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_OK);
    CHECK_EQ(counting.reads, 2u); /* header + payload CRC */
    CHECK_EQ(cold_code_manager_deinit(&manager), COLD_CODE_OK);

    counting.reads = 0u;
    config = make_config(&storage, sizeof(cold_code_storage_header_t) - 1u, 1u, counting_loader,
                         COLD_CODE_INIT_VERIFY_CRC, counting_info.image_id);
    CHECK_EQ(cold_code_manager_init(&manager, &config), COLD_CODE_ERR_SIZE);
    CHECK_EQ(counting.reads, 0u); /* early check happens before any loader read */

    CHECK_EQ(cold_code_storage_required_size(NULL, 1u), 0u);
    CHECK_EQ(cold_code_storage_required_align(NULL), 0u);

    TEST_OK();
    return 0;
}
