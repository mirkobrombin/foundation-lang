if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED NATIVE OR
   NOT DEFINED EXPECTED)
    message(FATAL_ERROR "native run assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}" --native "${NATIVE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "native program failed with ${result}:\n${error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "native program output mismatch:\nexpected:\n${expected}actual:\n${output}")
endif()

message(STATUS "native program output matches ${EXPECTED}")
