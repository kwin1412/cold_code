file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
file(GLOB_RECURSE MODULE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(cold_code STATIC ${MODULE_SOURCES} ${MODULE_HEADERS})

target_include_directories(cold_code PUBLIC include)

target_link_libraries(cold_code PRIVATE project_warnings)
