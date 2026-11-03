if(NOT DEFINED TEST_EXE)
    message(FATAL_ERROR "TEST_EXE is required")
endif()

execute_process(
    COMMAND "${TEST_EXE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

set(output "${stdout}${stderr}")

if("${result}" STREQUAL "0")
    message(FATAL_ERROR "expected the process to abort, but it exited successfully")
endif()

if(DEFINED EXPECT_REGEX AND NOT EXPECT_REGEX STREQUAL "")
    if(NOT output MATCHES "${EXPECT_REGEX}")
        message(FATAL_ERROR "expected output to match '${EXPECT_REGEX}', got:\n${output}")
    endif()
endif()

message(STATUS "expected abort observed (result: ${result})")
