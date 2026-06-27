if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT OR
   NOT DEFINED GENERATED OR NOT DEFINED EXPECTED OR NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "package application host assertion is missing an input")
endif()

file(REMOVE_RECURSE "${PROJECT}")
file(COPY "${SOURCE}/" DESTINATION "${PROJECT}")

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}/app" -o "${GENERATED}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "package application host emission failed:\n${first_output}${first_error}")
endif()
file(READ "${GENERATED}" first)

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}/app" -o "${GENERATED}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "package application host regeneration failed:\n${second_output}${second_error}")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${GENERATED}" second)
if(NOT first STREQUAL expected OR NOT second STREQUAL first)
    message(FATAL_ERROR "package application host is not deterministic")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${PROJECT}/app"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "package application host did not run:\n${run_error}")
endif()
file(READ "${EXPECTED_OUTPUT}" expected_output)
if(NOT run_output STREQUAL expected_output)
    message(FATAL_ERROR "package application output mismatch")
endif()

file(SHA256 "${GENERATED}" application_host_hash)
message(STATUS "package application host is executable: ${application_host_hash}")
