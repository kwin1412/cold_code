cmake_minimum_required(VERSION 3.20)
project(fixture_dual_port LANGUAGES C)

add_subdirectory("${COLD_CODE_DIR}" cold_code)
include("${COLD_CODE_HELPER}")

add_executable(fixture main.c)
target_link_libraries(fixture PRIVATE cold_code_host cold_code_baremetal)
cold_code_add_image(fixture
    NAME fixture
    SOURCES cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    STUBS SLOW
)
