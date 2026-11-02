cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME project_tmp)

project(${PROJECT_NAME} VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Werror
)

add_subdirectory(module)
add_subdirectory(example)
