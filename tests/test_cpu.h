#ifndef TEST_CPU_H
#define TEST_CPU_H

#include <stdint.h>

/* Test-only strong cold_code_cpu_id() hook. */
void test_set_cpu(uint32_t cpu_id);
uint32_t test_cpu(void);

#endif /* TEST_CPU_H */
