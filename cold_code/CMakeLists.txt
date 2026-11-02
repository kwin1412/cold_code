set(COLD_CODE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(COLD_CODE_PORT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/include")

# Port contract is header-only.  The library intentionally does not provide a
# port implementation; the executable links exactly one platform port
# (example/port_host.c is the host reference; embedded ports differ).
add_library(cold_code_port_interface INTERFACE)
target_include_directories(cold_code_port_interface INTERFACE
    "${COLD_CODE_INCLUDE_DIR}"
    "${COLD_CODE_PORT_INCLUDE_DIR}"
)

# Platform-neutral loader: manager/image only.  Do not add port or source
# implementations here.
add_library(cold_code_core STATIC
    src/core/manager.c
    src/core/image.c
    src/core/cold_code_internal.h
)
target_link_libraries(cold_code_core PUBLIC cold_code_port_interface)
target_include_directories(cold_code_core PRIVATE "${COLD_CODE_PORT_INCLUDE_DIR}")
target_compile_features(cold_code_core PUBLIC c_std_17)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cold_code_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Werror
    )
endif()

set_target_properties(cold_code_core PROPERTIES C_EXTENSIONS OFF)

add_library(cold_code::core ALIAS cold_code_core)
add_library(cold_code::port_interface ALIAS cold_code_port_interface)

# Consumers include this helper after add_subdirectory(cold_code).  Using a
# cache entry avoids relying on function-definition scope across directories.
set(COLD_CODE_HELPER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ColdCodeImage.cmake"
    CACHE INTERNAL "Path to the cold_code_add_image() CMake helper")
