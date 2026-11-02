add_executable(example main.c)
target_include_directories(example PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cold")
target_link_libraries(example PRIVATE cold_code_host project_warnings)
target_compile_features(example PRIVATE c_std_17)
set_target_properties(example PROPERTIES C_EXTENSIONS OFF)

cold_code_add_image(example
    NAME example
    SOURCES cold/cold_functions.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    STUBS FAST
    DEFINE_PATH ON
)
