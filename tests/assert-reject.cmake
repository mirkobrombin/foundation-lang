if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED CODE)
    message(FATAL_ERROR "reject assertion requires COMPILER, SOURCE, and CODE")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "compiler accepted rejected fixture ${SOURCE}")
endif()

if(NOT error MATCHES "error\\[${CODE}\\]")
    message(FATAL_ERROR "compiler did not report ${CODE}:\n${output}${error}")
endif()

message(STATUS "compiler rejected fixture with ${CODE}")
