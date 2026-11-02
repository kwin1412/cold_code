if(NOT DEFINED CONSUMER_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED COLD_CODE_DIR
   OR NOT DEFINED EXPECT_OUTPUT)
    message(FATAL_ERROR "CONSUMER_DIR, BUILD_DIR, COLD_CODE_DIR and EXPECT_OUTPUT are required")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER_DIR}" -B "${BUILD_DIR}"
            "-DCOLD_CODE_DIR=${COLD_CODE_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT "${configure_result}" STREQUAL "0")
    message(FATAL_ERROR "consumer configure failed:\n${configure_stdout}${configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT "${build_result}" STREQUAL "0")
    message(FATAL_ERROR "consumer build failed:\n${build_stdout}${build_stderr}")
endif()

execute_process(
    COMMAND "${BUILD_DIR}/consumer"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
set(output "${run_stdout}${run_stderr}")

if(NOT "${run_result}" STREQUAL "0")
    message(FATAL_ERROR "consumer run failed (${run_result}):\n${output}")
endif()

if(NOT output MATCHES "${EXPECT_OUTPUT}")
    message(FATAL_ERROR "expected output to match '${EXPECT_OUTPUT}', got:\n${output}")
endif()

message(STATUS "external consumer ok")
