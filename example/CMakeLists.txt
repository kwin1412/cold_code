add_executable(example main.c)

target_link_libraries(example PRIVATE
    cold_code
    project_warnings
)
