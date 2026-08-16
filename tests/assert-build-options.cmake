if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED OR NOT DEFINED WORK)
    message(FATAL_ERROR "build option assertion requires COMPILER, SOURCE, EXPECTED, and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
if(WIN32)
    set(executable "${WORK}/option-order.exe")
else()
    set(executable "${WORK}/option-order")
endif()

execute_process(
    COMMAND "${COMPILER}" build "${SOURCE}" --backend llvm -o "${executable}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
            "build with reordered options failed with ${build_result}:\n${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "built program failed with ${run_result}:\n${error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "built program output mismatch:\nexpected:\n${expected}actual:\n${output}")
endif()

message(STATUS "build options are order independent")
