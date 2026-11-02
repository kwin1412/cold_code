add_executable(example main.c port_host.c port_host_fast_x86_64.S)

target_include_directories(example PRIVATE cold)
set_source_files_properties(port_host.c PROPERTIES COMPILE_OPTIONS "-ftls-model=local-exec")

target_link_libraries(example PRIVATE
    cold_code_core
    project_warnings
)

cold_code_add_image(example
    NAME example
    SOURCES cold/cold_functions.c
    SLOT_SIZE 64
    POOL_SLOTS 1
    FAST_PATH ON
)
