if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED FIRST OR NOT DEFINED SECOND)
    message(FATAL_ERROR "deterministic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first C emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second C emission failed:\n${second_output}${second_error}")
endif()

file(SHA256 "${FIRST}" first_hash)
file(SHA256 "${SECOND}" second_hash)
if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "repeated project builds emitted different C")
endif()

message(STATUS "project C emission is deterministic: ${first_hash}")
