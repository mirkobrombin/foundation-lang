if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "run assertion requires COMPILER, SOURCE, and EXPECTED")
endif()

set(command "${COMPILER}" run "${SOURCE}")
if(DEFINED BACKEND)
    list(APPEND command --backend "${BACKEND}")
endif()
if(DEFINED NATIVE_INPUTS)
    foreach(native_input IN LISTS NATIVE_INPUTS)
        list(APPEND command --native "${native_input}")
    endforeach()
endif()
if(DEFINED NATIVE_LINKS)
    foreach(native_link IN LISTS NATIVE_LINKS)
        list(APPEND command --native-link "${native_link}")
    endforeach()
endif()
if(DEFINED PROGRAM_ARGS)
    list(APPEND command -- ${PROGRAM_ARGS})
endif()

execute_process(
    COMMAND ${command}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

set(expected_exit 0)
if(DEFINED EXPECTED_EXIT)
    set(expected_exit "${EXPECTED_EXIT}")
endif()
if(NOT result EQUAL expected_exit)
    message(FATAL_ERROR "program exited with ${result}, expected ${expected_exit}:\n${error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "program output mismatch:\nexpected:\n${expected}actual:\n${output}")
endif()

message(STATUS "program output matches ${EXPECTED}")
