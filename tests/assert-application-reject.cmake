if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED CODE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "application reject assertion is missing an input")
endif()

file(REMOVE "${OUTPUT}")
set(command emit-app-plan)
if(DEFINED APPLICATION_COMMAND)
    set(command "${APPLICATION_COMMAND}")
endif()
execute_process(
    COMMAND "${COMPILER}" "${command}" "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE error
)

if(result EQUAL 0)
    message(FATAL_ERROR "application command accepted rejected fixture ${SOURCE}")
endif()
if(EXISTS "${OUTPUT}")
    message(FATAL_ERROR "failed application planning left an output file at ${OUTPUT}")
endif()
if(NOT error MATCHES "error\\[${CODE}\\]")
    message(FATAL_ERROR "application command did not report ${CODE}:\n${standard_output}${error}")
endif()

message(STATUS "application command rejected fixture with ${CODE}")
