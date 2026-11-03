#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "cold_code.h"

#include <stdio.h>

/* Generous static exec buffer for runtime tests. */
#define TEST_STORAGE_BYTES 16384u
#define TEST_STORAGE_ALIGN COLD_CODE_STORAGE_ALIGN_MAX

/* Probe metadata and confirm the static test exec buffer is large enough. */
static inline int test_probe_ok(cold_code_loader_t loader, uint32_t pool_slots,
                                size_t exec_size, cold_code_image_info_t *info) {
    if (cold_code_manager_probe(&loader, info) != COLD_CODE_OK) {
        return 0;
    }
    size_t required = cold_code_storage_required_size(info, pool_slots);
    return required != 0u && required <= exec_size;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        if ((actual) != (expected)) {                                                              \
            fprintf(stderr, "CHECK_EQ failed at %s:%d: %s != %s\n", __FILE__, __LINE__, #actual,   \
                    #expected);                                                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    do {                                                                                           \
        double check_actual_ = (double)(actual);                                                   \
        double check_expected_ = (double)(expected);                                               \
        double check_diff_ = check_actual_ - check_expected_;                                      \
        if (check_diff_ < 0.0) {                                                                   \
            check_diff_ = -check_diff_;                                                            \
        }                                                                                          \
        if (check_diff_ > (double)(tolerance)) {                                                   \
            fprintf(stderr, "CHECK_NEAR failed at %s:%d: %s = %.17g, expected %.17g\n", __FILE__,  \
                    __LINE__, #actual, check_actual_, check_expected_);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define TEST_OK()                                                                                  \
    do {                                                                                           \
        printf("TEST_OK\n");                                                                       \
        fflush(stdout);                                                                            \
    } while (0)

#endif /* TEST_UTIL_H */
