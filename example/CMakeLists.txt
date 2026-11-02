add_executable(example main.c)

target_include_directories(example PRIVATE cold)

target_link_libraries(example PRIVATE
    cold_code
    project_warnings
)

cold_code_add_image(example
    SOURCES cold/cold_functions.c
    SLOT_SIZE 64
    POOL_SLOTS 1
)
