cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES C ASM)

add_subdirectory("${COLD_CODE_DIR}" cold_code)
include("${COLD_CODE_HELPER}")

add_executable(consumer main.c port_host.c port_host_fast_x86_64.S)
target_link_libraries(consumer PRIVATE cold_code_core)
target_compile_options(consumer PRIVATE -ftls-model=local-exec)
cold_code_add_image(consumer
    NAME consumer
    SOURCES cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    FAST_PATH ON
)
