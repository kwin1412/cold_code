file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
file(GLOB_RECURSE MODULE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(module STATIC ${MODULE_SOURCES} ${MODULE_HEADERS})

target_include_directories(module PUBLIC include)

target_link_libraries(module PRIVATE project_warnings)
