# Runtime tests.  Each scenario is a separate process and each image has a
# unique NAME, so generated symbols/storage never collide.

find_package(Threads REQUIRED)

# The test suite provides its own host port/source implementation; the library
# contains no platform port.  test_custom_port deliberately does not link this.
add_library(test_host_port STATIC
    support/port_host.c
    support/port_host.h
)
target_link_libraries(test_host_port PUBLIC cold_code_port_interface)
target_link_libraries(test_host_port PRIVATE project_warnings)
target_include_directories(test_host_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_compile_features(test_host_port PRIVATE c_std_17)
set_target_properties(test_host_port PROPERTIES C_EXTENSIONS OFF)

add_executable(test_basic test_basic.c)
target_link_libraries(test_basic PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_basic
    NAME basic
    SOURCES cold/basic_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 2
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/basic_image"
)
add_test(NAME cold_code.basic COMMAND test_basic)

add_executable(test_init test_init.c)
target_link_libraries(test_init PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_init
    NAME init
    SOURCES cold/init_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/init_image"
)
add_test(NAME cold_code.init COMMAND test_init)

add_executable(test_lru_1way test_lru.c)
target_compile_definitions(test_lru_1way PRIVATE EXPECTED_POOL=1)
target_link_libraries(test_lru_1way PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_lru_1way
    NAME lru1
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
target_link_libraries(test_lru_2way PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_lru_2way
    NAME lru2
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
target_link_libraries(test_abi PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_abi
    NAME abi
    SOURCES cold/abi_cold.c
    SLOT_SIZE 512
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/abi_image"
)
add_test(NAME cold_code.abi COMMAND test_abi)

add_executable(test_preinit test_preinit.c)
target_link_libraries(test_preinit PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_preinit
    NAME preinit
    SOURCES cold/preinit_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/preinit_image"
)
add_test(NAME cold_code.preinit_abort
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:test_preinit>"
            "-DEXPECT_REGEX=no current manager"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_abort.cmake"
)
set_tests_properties(cold_code.preinit_abort PROPERTIES TIMEOUT 30)

add_executable(test_image_format test_image_format.c)
target_link_libraries(test_image_format PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_image_format
    NAME imagefmt
    SOURCES cold/imagefmt_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/imagefmt_image"
)
add_test(NAME cold_code.image_format COMMAND test_image_format)

add_executable(test_multi_manager test_multi_manager.c)
target_link_libraries(test_multi_manager PRIVATE
    cold_code_core
    test_host_port
    project_warnings
    Threads::Threads
)
cold_code_add_image(test_multi_manager
    NAME multi
    SOURCES cold/multi_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/multi_image"
)
add_test(NAME cold_code.multi_manager COMMAND test_multi_manager)

# The same image loaded into two independent managers/pools; a cold function
# reports its own runtime address to prove the slot copies differ.
add_executable(test_same_image_two_slots test_same_image_two_slots.c)
target_link_libraries(test_same_image_two_slots PRIVATE
    cold_code_core
    test_host_port
    project_warnings
    Threads::Threads
)
cold_code_add_image(test_same_image_two_slots
    NAME slots
    SOURCES cold/slots_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/slots_image"
)
add_test(NAME cold_code.same_image_two_slots COMMAND test_same_image_two_slots)

# Links only cold_code_core and provides its own per-CPU port + memory source.
# It must NOT link test_host_port or any other host port: this is the
# interface-isolation proof and the custom-port example.
add_executable(test_custom_port test_custom_port.c)
target_link_libraries(test_custom_port PRIVATE
    cold_code_core
    project_warnings
    Threads::Threads
)
cold_code_add_image(test_custom_port
    NAME custom
    SOURCES cold/slots_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/custom_image"
)
add_test(NAME cold_code.custom_port COMMAND test_custom_port)

# Two images in one executable, with independent managers.
add_executable(test_multi_image test_multi_image.c)
target_link_libraries(test_multi_image PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_multi_image
    NAME first
    SOURCES cold/first_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/first_image"
)
cold_code_add_image(test_multi_image
    NAME second
    SOURCES cold/second_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/second_image"
)
add_test(NAME cold_code.multi_image COMMAND test_multi_image)

# Stub image ID validation: a manager for one image must reject a stub from
# another image of the same executable.
add_executable(test_image_mismatch test_image_mismatch.c)
target_link_libraries(test_image_mismatch PRIVATE cold_code_core test_host_port project_warnings)
cold_code_add_image(test_image_mismatch
    NAME first
    SOURCES cold/first_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/mismatch_first_image"
)
cold_code_add_image(test_image_mismatch
    NAME second
    SOURCES cold/second_cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/mismatch_second_image"
)
add_test(NAME cold_code.image_mismatch
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:test_image_mismatch>"
            "-DEXPECT_REGEX=different image"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_abort.cmake"
)
set_tests_properties(cold_code.image_mismatch PROPERTIES TIMEOUT 30)

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

add_test(NAME cold_code.reject_duplicate_cold_symbol
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/duplicate"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_duplicate"
            "-DCOLD_CODE_DIR=${PROJECT_SOURCE_DIR}/cold_code"
            "-DEXPECT_REGEX=defined in two images"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake"
)
set_tests_properties(cold_code.reject_duplicate_cold_symbol PROPERTIES TIMEOUT 120)

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
