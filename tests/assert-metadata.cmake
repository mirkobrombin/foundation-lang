if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED OR
   NOT DEFINED FIRST OR NOT DEFINED SECOND)
    message(FATAL_ERROR "metadata assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-metadata "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first metadata emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-metadata "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second metadata emission failed:\n${second_output}${second_error}")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${FIRST}" first)
file(READ "${SECOND}" second)
if(NOT first STREQUAL expected)
    message(FATAL_ERROR "metadata output does not match ${EXPECTED}")
endif()
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated metadata emission produced different output")
endif()

file(SHA256 "${FIRST}" metadata_hash)
message(STATUS "metadata output is deterministic: ${metadata_hash}")
