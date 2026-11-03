cmake_minimum_required(VERSION 3.20)
project(installed_consumer LANGUAGES C)

find_package(cold_code 5 CONFIG REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE cold_code::host)
cold_code_add_image(consumer
    NAME consumer
    SOURCES cold.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    DEFINE_PATH ON
)
