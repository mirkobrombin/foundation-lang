if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "replace panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(run_result EQUAL 0)
    message(FATAL_ERROR "replace panic program unexpectedly succeeded:\n${run_output}")
endif()
if(NOT run_error MATCHES "foundation panic: replacement failed")
    message(FATAL_ERROR "replacement was not evaluated first:\n${run_error}")
endif()
if(run_error MATCHES "place evaluated")
    message(FATAL_ERROR "replacement panic evaluated the destination place:\n${run_error}")
endif()
if(NOT run_error MATCHES "at example.replace_order.replacement" OR
   NOT run_error MATCHES "at example.replace_order.main")
    message(FATAL_ERROR "replace panic frames are incomplete:\n${run_error}")
endif()

message(STATUS "replacement panic leaves the destination place unevaluated")
