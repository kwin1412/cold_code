include("${COLD_CODE_HELPER}")
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(cold_code_test_properties target)
    target_compile_features(${target} PRIVATE c_std_17)
    set_target_properties(${target} PROPERTIES C_EXTENSIONS OFF)
endfunction()

# --- Core/storage/ABI tests (no generated stubs) ---------------------------

add_executable(test_storage_guard test_storage_guard.c image_fixture.c)
target_include_directories(test_storage_guard PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_storage_guard PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_storage_guard)
add_test(NAME cold_code.storage_guard COMMAND test_storage_guard)

add_executable(test_manager_abi test_manager_abi.c image_fixture.c)
target_include_directories(test_manager_abi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_manager_abi PRIVATE cold_code_core project_warnings)
cold_code_test_properties(test_manager_abi)
add_test(NAME cold_code.manager_abi COMMAND test_manager_abi)

# --- Generated-image runtime tests -----------------------------------------

add_executable(test_basic_v3 test_basic_v3.c)
target_link_libraries(test_basic_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_basic_v3)
cold_code_add_image(test_basic_v3
    NAME basic SOURCES cold/basic_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS SLOW DEFINE_PATH ON)
add_test(NAME cold_code.basic COMMAND test_basic_v3)

add_executable(test_init_v3 test_init_v3.c)
target_link_libraries(test_init_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_init_v3)
cold_code_add_image(test_init_v3
    NAME init SOURCES cold/init_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.init COMMAND test_init_v3)

add_executable(test_lru_1way test_lru_v3.c)
target_compile_definitions(test_lru_1way PRIVATE EXPECTED_POOL=1)
target_link_libraries(test_lru_1way PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_lru_1way)
cold_code_add_image(test_lru_1way
    NAME lru1 SOURCES cold/lru_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.lru_1way COMMAND test_lru_1way)
set_tests_properties(cold_code.lru_1way PROPERTIES
    PASS_REGULAR_EXPRESSION "TEST_OK"
    FAIL_REGULAR_EXPRESSION "into way 1")

add_executable(test_lru_2way test_lru_v3.c)
target_compile_definitions(test_lru_2way PRIVATE EXPECTED_POOL=2)
target_link_libraries(test_lru_2way PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_lru_2way)
cold_code_add_image(test_lru_2way
    NAME lru2 SOURCES cold/lru_cold.c SLOT_SIZE 64 POOL_SLOTS 2
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.lru_2way COMMAND test_lru_2way)
set_tests_properties(cold_code.lru_2way PROPERTIES
    PASS_REGULAR_EXPRESSION "TEST_OK"
    FAIL_REGULAR_EXPRESSION "evict image slot 0")

add_executable(test_abi_v3 test_abi_v3.c)
target_include_directories(test_abi_v3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_abi_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_abi_v3)
cold_code_add_image(test_abi_v3
    NAME abi SOURCES cold/abi_cold.c SLOT_SIZE 512 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.abi COMMAND test_abi_v3)

add_executable(test_preinit_v3 test_preinit_v3.c)
target_link_libraries(test_preinit_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_preinit_v3)
cold_code_add_image(test_preinit_v3
    NAME preinit SOURCES cold/preinit_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST)
add_test(NAME cold_code.preinit_abort
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:test_preinit_v3>"
            "-DEXPECT_REGEX=no current manager"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_abort.cmake")
set_tests_properties(cold_code.preinit_abort PROPERTIES TIMEOUT 30)

add_executable(test_image_format_v3 test_image_format_v3.c)
target_link_libraries(test_image_format_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_image_format_v3)
cold_code_add_image(test_image_format_v3
    NAME imagefmt SOURCES cold/imagefmt_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.image_format COMMAND test_image_format_v3)

add_executable(test_multi_manager_v3 test_multi_manager_v3.c)
target_link_libraries(test_multi_manager_v3 PRIVATE
    cold_code_host project_warnings Threads::Threads)
cold_code_test_properties(test_multi_manager_v3)
cold_code_add_image(test_multi_manager_v3
    NAME multi SOURCES cold/multi_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.multi_manager COMMAND test_multi_manager_v3)

add_executable(test_same_image_two_slots_v3 test_same_image_two_slots_v3.c)
target_link_libraries(test_same_image_two_slots_v3 PRIVATE
    cold_code_host project_warnings Threads::Threads)
cold_code_test_properties(test_same_image_two_slots_v3)
cold_code_add_image(test_same_image_two_slots_v3
    NAME slots SOURCES cold/slots_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.same_image_two_slots COMMAND test_same_image_two_slots_v3)

add_executable(test_custom_port_v3
    test_custom_port_v3.c test_custom_port_fast_x86_64.S)
target_link_libraries(test_custom_port_v3 PRIVATE
    cold_code_core project_warnings Threads::Threads)
target_compile_options(test_custom_port_v3 PRIVATE -ftls-model=local-exec)
cold_code_test_properties(test_custom_port_v3)
cold_code_add_image(test_custom_port_v3
    NAME custom SOURCES cold/slots_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.custom_port COMMAND test_custom_port_v3)

add_executable(test_baremetal_v3 test_baremetal_v3.c)
target_link_libraries(test_baremetal_v3 PRIVATE
    cold_code_baremetal project_warnings Threads::Threads)
cold_code_test_properties(test_baremetal_v3)
cold_code_add_image(test_baremetal_v3
    NAME baremetal SOURCES cold/slots_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS SLOW DEFINE_PATH ON)
add_test(NAME cold_code.baremetal_port COMMAND test_baremetal_v3)

add_executable(test_multi_image_v3 test_multi_image_v3.c)
target_link_libraries(test_multi_image_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_multi_image_v3)
cold_code_add_image(test_multi_image_v3
    NAME first SOURCES cold/first_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
cold_code_add_image(test_multi_image_v3
    NAME second SOURCES cold/second_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
add_test(NAME cold_code.multi_image COMMAND test_multi_image_v3)

add_executable(test_image_mismatch_v3 test_image_mismatch_v3.c)
target_link_libraries(test_image_mismatch_v3 PRIVATE cold_code_host project_warnings)
cold_code_test_properties(test_image_mismatch_v3)
cold_code_add_image(test_image_mismatch_v3
    NAME first SOURCES cold/first_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)
cold_code_add_image(test_image_mismatch_v3
    NAME second SOURCES cold/second_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST)
add_test(NAME cold_code.image_mismatch
    COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXE=$<TARGET_FILE:test_image_mismatch_v3>"
            "-DEXPECT_REGEX=fault code 1"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_abort.cmake")
set_tests_properties(cold_code.image_mismatch PROPERTIES TIMEOUT 30)

if(TARGET example)
    add_test(NAME cold_code.example COMMAND example)
    set_tests_properties(cold_code.example PROPERTIES
        PASS_REGULAR_EXPRESSION "loaded final: 1/1")
endif()

# --- Benchmarks (not registered with CTest) --------------------------------

add_executable(bench_fast bench_cold_call.c)
target_compile_definitions(bench_fast PRIVATE BENCH_FAST=1)
target_compile_options(bench_fast PRIVATE -O2)
target_link_libraries(bench_fast PRIVATE cold_code_host project_warnings)
cold_code_test_properties(bench_fast)
cold_code_add_image(bench_fast
    NAME bench_fast SOURCES cold/lru_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS FAST DEFINE_PATH ON)

add_executable(bench_slow bench_cold_call.c)
target_compile_definitions(bench_slow PRIVATE BENCH_FAST=0)
target_compile_options(bench_slow PRIVATE -O2)
target_link_libraries(bench_slow PRIVATE cold_code_host project_warnings)
cold_code_test_properties(bench_slow)
cold_code_add_image(bench_slow
    NAME bench_slow SOURCES cold/lru_cold.c SLOT_SIZE 64 POOL_SLOTS 1
    STUBS SLOW DEFINE_PATH ON)

# --- Build-time negative fixtures / positive auto-ASM fixture --------------

set(COLD_CODE_V3_DIR "${PROJECT_SOURCE_DIR}/cold_code")

add_test(NAME cold_code.reject_cross_slot
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cross_slot"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_cross_slot"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=cross-slot cold calls are not supported"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_cross_slot PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_straddle
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/straddle"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_straddle"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=spans image slots"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_straddle PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_self_contained
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/self_contained"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_self_contained"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=discarded section|undefined reference|undefined symbol|cold image has undefined symbols"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_self_contained PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_duplicate_cold_symbol
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/duplicate"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_duplicate"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=defined in two images"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_duplicate_cold_symbol PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_unsupported_arch
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unsupported_arch"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_unsupported_arch"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=interface"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_unsupported_arch PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.reject_dual_port
    COMMAND "${CMAKE_COMMAND}"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dual_port"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/reject_dual_port"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_REGEX=cold_code_context_ops"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_expect_build_failure.cmake")
set_tests_properties(cold_code.reject_dual_port PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.auto_asm
    COMMAND "${CMAKE_COMMAND}"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/auto_asm"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/auto_asm_build"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_OUTPUT=auto asm ok"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_consumer.cmake")
set_tests_properties(cold_code.auto_asm PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.arch_plugin
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_arch_plugin.py"
            --tool-dir "${COLD_CODE_V3_DIR}/tool"
            --include-dir "${COLD_CODE_V3_DIR}/include"
            --arch-include-dir "${COLD_CODE_V3_DIR}/src/arch/x86_64/include")

add_test(NAME cold_code.external_consumer
    COMMAND "${CMAKE_COMMAND}"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
            "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer_build"
            "-DCOLD_CODE_DIR=${COLD_CODE_V3_DIR}"
            "-DEXPECT_OUTPUT=consumer ok"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_consumer.cmake")
set_tests_properties(cold_code.external_consumer PROPERTIES TIMEOUT 120)

add_test(NAME cold_code.install_consumer
    COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/install_consumer"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install_consumer"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_install_consumer.cmake")
set_tests_properties(cold_code.install_consumer PROPERTIES TIMEOUT 180)
