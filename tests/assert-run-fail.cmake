if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PATTERN)
    message(FATAL_ERROR "failure assertion requires COMPILER, SOURCE, and PATTERN")
endif()

set(command "${COMPILER}" run "${SOURCE}")
if(DEFINED BACKEND)
    list(APPEND command --backend "${BACKEND}")
endif()

execute_process(
    COMMAND ${command}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "program unexpectedly succeeded:\n${output}")
endif()

if(NOT error MATCHES "${PATTERN}")
    message(FATAL_ERROR "program did not report ${PATTERN}:\n${output}${error}")
endif()
if(DEFINED TRACE_PATTERN AND NOT error MATCHES "${TRACE_PATTERN}")
    message(FATAL_ERROR "program did not report ${TRACE_PATTERN}:\n${output}${error}")
endif()

message(STATUS "program failed with ${PATTERN}")
