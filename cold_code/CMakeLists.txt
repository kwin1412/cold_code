file(GLOB_RECURSE COLD_CODE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
file(GLOB_RECURSE COLD_CODE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(cold_code STATIC ${COLD_CODE_SOURCES} ${COLD_CODE_HEADERS})

target_include_directories(cold_code PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(cold_code PUBLIC c_std_17)
set_target_properties(cold_code PROPERTIES C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cold_code PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Werror
    )
endif()

# Consumers include this helper after add_subdirectory(cold_code).  Using a
# cache entry avoids relying on function-definition scope across directories.
set(COLD_CODE_HELPER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ColdCodeImage.cmake"
    CACHE INTERNAL "Path to the cold_code_add_image() CMake helper")
