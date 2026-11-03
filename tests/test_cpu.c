#include "test_cpu.h"

static uint32_t g_test_cpu;

void test_set_cpu(uint32_t cpu_id) {
    g_test_cpu = cpu_id;
}

uint32_t test_cpu(void) {
    return g_test_cpu;
}

uint32_t cold_code_cpu_id(void) {
    return g_test_cpu;
}
