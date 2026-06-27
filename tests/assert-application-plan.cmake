if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED OR
   NOT DEFINED FIRST OR NOT DEFINED SECOND)
    message(FATAL_ERROR "application plan assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-app-plan "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first application plan emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-app-plan "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second application plan emission failed:\n${second_output}${second_error}")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${FIRST}" first)
file(READ "${SECOND}" second)
if(NOT first STREQUAL expected)
    message(FATAL_ERROR "application plan does not match ${EXPECTED}")
endif()
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated application plan emission produced different output")
endif()

file(SHA256 "${FIRST}" application_hash)
message(STATUS "application plan is deterministic: ${application_hash}")
