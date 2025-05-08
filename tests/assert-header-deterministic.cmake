if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED FIRST OR NOT DEFINED SECOND)
    message(FATAL_ERROR "header determinism assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c-header "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first header emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c-header "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second header emission failed:\n${second_output}${second_error}")
endif()

file(READ "${FIRST}" first)
file(READ "${SECOND}" second)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "repeated project builds emitted different C ABI headers")
endif()
if(NOT first MATCHES "foundation_double\\(int32_t fdn_arg_0\\)")
    message(FATAL_ERROR "C ABI header is missing the Foundation export:\n${first}")
endif()
if(first MATCHES "foundation_native_sum")
    message(FATAL_ERROR "C ABI header exposes an imported C symbol:\n${first}")
endif()

message(STATUS "C ABI header is deterministic and export-only")
