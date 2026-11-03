cmake_minimum_required(VERSION 3.20)
project(fixture_auto_asm LANGUAGES C)

add_subdirectory("${COLD_CODE_DIR}" cold_code)
include("${COLD_CODE_HELPER}")

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE cold_code_host)
cold_code_add_image(consumer
    NAME fixture
    SOURCES cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    DEFINE_PATH ON
)
