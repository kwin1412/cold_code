# Runtime tests.  Each scenario is a separate process because cold_code_init()
# is process-global and one-shot.

add_executable(test_basic test_basic.c)
target_link_libraries(test_basic PRIVATE cold_code project_warnings)
cold_code_add_image(test_basic
    SOURCES cold/basic_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/basic_image"
)
add_test(NAME cold_code.basic COMMAND test_basic)

add_executable(test_init test_init.c)
target_link_libraries(test_init PRIVATE cold_code project_warnings)
cold_code_add_image(test_init
    SOURCES cold/init_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/init_image"
)
add_test(NAME cold_code.init COMMAND test_init)

add_executable(test_lru_1way test_lru.c)
target_compile_definitions(test_lru_1way PRIVATE EXPECTED_POOL=1)
target_link_libraries(test_lru_1way PRIVATE cold_code project_warnings)
cold_code_add_image(test_lru_1way
    SOURCES cold/lru_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/lru1_image"
)
add_test(NAME cold_code.lru_1way COMMAND test_lru_1way)
set_tests_properties(cold_code.lru_1way PROPERTIES
    PASS_REGULAR_EXPRESSION "TEST_OK"
    FAIL_REGULAR_EXPRESSION "into way 1"
)

add_executable(test_lru_2way test_lru.c)
target_compile_definitions(test_lru_2way PRIVATE EXPECTED_POOL=2)
target_link_libraries(test_lru_2way PRIVATE cold_code project_warnings)
cold_code_add_image(test_lru_2way
    SOURCES cold/lru_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/lru2_image"
)
add_test(NAME cold_code.lru_2way COMMAND test_lru_2way)
# A hit on slot 0 must refresh its LRU age, so div(slot 2) evicts slot 1.
set_tests_properties(cold_code.lru_2way PROPERTIES
    PASS_REGULAR_EXPRESSION "TEST_OK"
    FAIL_REGULAR_EXPRESSION "evict image slot 0"
)

add_executable(test_abi test_abi.c)
target_link_libraries(test_abi PRIVATE cold_code project_warnings)
cold_code_add_image(test_abi
    SOURCES cold/abi_cold.c
    SLOT_SIZE 256
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/abi_image"
)
add_test(NAME cold_code.abi COMMAND test_abi)

add_executable(test_preinit test_preinit.c)
target_link_libraries(test_preinit PRIVATE cold_code project_warnings)
cold_code_add_image(test_preinit
    SOURCES cold/preinit_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/preinit_image"
)
add_test(NAME cold_code.preinit_abort
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:test_preinit>"
            "-DEXPECT_REGEX=cold_code_init.*not called"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_abort.cmake"
)
set_tests_properties(cold_code.preinit_abort PROPERTIES TIMEOUT 30)

# Example smoke test (defined by the example/ subdirectory).
add_test(NAME cold_code.example COMMAND example)
set_tests_properties(cold_code.example PROPERTIES
    PASS_REGULAR_EXPRESSION "loaded final: 1/1"
)

# Negative build fixtures: each configures/builds a tiny independent project
# and requires the expected failure message.

add_test(NAME cold_code.reject_cross_slot
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cross_slot"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_cross_slot"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_REGEX=cross-slot cold calls are not supported"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake"
)
set_tests_properties(cold_code.reject_cross_slot PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_straddle
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/straddle"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_straddle"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_REGEX=spans image slots"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake"
)
set_tests_properties(cold_code.reject_straddle PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_self_contained
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/self_contained"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_self_contained"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_REGEX=discarded section|undefined reference"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake"
)
set_tests_properties(cold_code.reject_self_contained PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_no_asm
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/no_asm"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_no_asm"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_REGEX=requires the ASM language"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake"
)
set_tests_properties(cold_code.reject_no_asm PROPERTIES TIMEOUT 120)

# The external consumer uses add_subdirectory(cold_code) exactly like an
# unrelated project would; this guards the self-contained-library contract.
add_test(NAME cold_code.external_consumer
    COMMAND "${CMAKE_COMMAND}"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer_build"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_OUTPUT=consumer ok"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_consumer.cmake"
)
set_tests_properties(cold_code.external_consumer PROPERTIES TIMEOUT 120)
