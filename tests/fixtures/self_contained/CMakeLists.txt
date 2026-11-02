cmake_minimum_required(VERSION 3.20)
project(fixture_self_contained LANGUAGES C ASM)

add_subdirectory("${COLD_CODE_DIR}" cold_code)
include("${COLD_CODE_HELPER}")

add_executable(fixture main.c)
target_link_libraries(fixture PRIVATE cold_code)
cold_code_add_image(fixture
    SOURCES cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
