#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>

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
