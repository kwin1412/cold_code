add_executable(example main.c)

target_link_libraries(example PRIVATE
    module
    project_warnings
)
