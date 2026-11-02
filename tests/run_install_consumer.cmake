if(NOT DEFINED BUILD_DIR OR NOT DEFINED CONSUMER_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "BUILD_DIR, CONSUMER_DIR and WORK_DIR are required")
endif()

set(prefix_dir "${WORK_DIR}/prefix")
set(consumer_build "${WORK_DIR}/build")
file(REMOVE_RECURSE "${prefix_dir}" "${consumer_build}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix_dir}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)
if(NOT "${install_result}" STREQUAL "0")
    message(FATAL_ERROR "install failed:\n${install_stdout}${install_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER_DIR}" -B "${consumer_build}"
            "-DCMAKE_PREFIX_PATH=${prefix_dir}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT "${configure_result}" STREQUAL "0")
    message(FATAL_ERROR "installed consumer configure failed:\n${configure_stdout}${configure_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT "${build_result}" STREQUAL "0")
    message(FATAL_ERROR "installed consumer build failed:\n${build_stdout}${build_stderr}")
endif()

execute_process(
    COMMAND "${consumer_build}/consumer"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)
set(output "${run_stdout}${run_stderr}")
if(NOT "${run_result}" STREQUAL "0")
    message(FATAL_ERROR "installed consumer run failed (${run_result}):\n${output}")
endif()
if(NOT output MATCHES "consumer ok")
    message(FATAL_ERROR "unexpected installed consumer output:\n${output}")
endif()

message(STATUS "installed consumer ok")
