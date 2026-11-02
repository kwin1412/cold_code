cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME cold-code)

# ASM is intentionally not listed here: the cold_code library/helper enables it
# when an image or the fast accessor needs it.
project(${PROJECT_NAME} VERSION 3.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Werror
)

option(COLD_CODE_BUILD_EXAMPLE "Build the example consumer" ON)

add_subdirectory(cold_code)
include("${COLD_CODE_HELPER}")

if(COLD_CODE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
