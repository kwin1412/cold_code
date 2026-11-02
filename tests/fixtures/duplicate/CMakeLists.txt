cmake_minimum_required(VERSION 3.20)
project(fixture_duplicate LANGUAGES C ASM)

add_subdirectory("${COLD_CODE_DIR}" cold_code)
include("${COLD_CODE_HELPER}")

add_executable(fixture main.c)
target_link_libraries(fixture PRIVATE cold_code_core)
cold_code_add_image(fixture
    NAME first
    SOURCES cold_a.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/first_image"
)
cold_code_add_image(fixture
    NAME second
    SOURCES cold_b.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/second_image"
)
