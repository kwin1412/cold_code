if(NOT DEFINED FIXTURE_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED COLD_CODE_DIR
   OR NOT DEFINED EXPECT_REGEX)
    message(FATAL_ERROR "FIXTURE_DIR, BUILD_DIR, COLD_CODE_DIR and EXPECT_REGEX are required")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE_DIR}" -B "${BUILD_DIR}"
            "-DCOLD_CODE_DIR=${COLD_CODE_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
set(output "${configure_stdout}${configure_stderr}")

if("${configure_result}" STREQUAL "0")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
    )
    string(APPEND output "${build_stdout}${build_stderr}")
    if("${build_result}" STREQUAL "0")
        message(FATAL_ERROR "fixture '${FIXTURE_DIR}' built successfully; expected a failure")
    endif()
endif()

if(NOT output MATCHES "${EXPECT_REGEX}")
    message(FATAL_ERROR "expected output to match '${EXPECT_REGEX}', got:\n${output}")
endif()

message(STATUS "expected build failure observed")
