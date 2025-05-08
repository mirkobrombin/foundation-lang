if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED C_COMPILER_ID OR
   NOT DEFINED SOURCE OR NOT DEFINED GENERATED OR NOT DEFINED OUTPUT OR
   NOT DEFINED EXPECTED OR NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "allocation run assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${SOURCE}" -o "${GENERATED}"
    RESULT_VARIABLE emit_result
    OUTPUT_VARIABLE emit_output
    ERROR_VARIABLE emit_error
)
if(NOT emit_result EQUAL 0)
    message(FATAL_ERROR "C emission failed:\n${emit_output}${emit_error}")
endif()

set(executable "${OUTPUT}")
if(C_COMPILER_ID STREQUAL "MSVC")
    set(executable "${OUTPUT}.exe")
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /W4 /WX
                /DFOUNDATION_VERIFY_ALLOCATIONS "${GENERATED}" "${RUNTIME_SOURCE}"
                "/I${RUNTIME_INCLUDE}" "/Fe:${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
else()
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
                -DFOUNDATION_VERIFY_ALLOCATIONS "${GENERATED}" "${RUNTIME_SOURCE}"
                -I "${RUNTIME_INCLUDE}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
endif()
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "verified C build failed:\n${build_output}${build_error}")
endif()

set(command "${executable}")
if(DEFINED PROGRAM_ARGS)
    list(APPEND command ${PROGRAM_ARGS})
endif()

execute_process(
    COMMAND ${command}
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
set(expected_exit 0)
if(DEFINED EXPECTED_EXIT)
    set(expected_exit "${EXPECTED_EXIT}")
endif()
if(NOT run_result EQUAL expected_exit)
    message(FATAL_ERROR
        "verified program exited with ${run_result}, expected ${expected_exit}:\n${run_error}")
endif()
if(NOT run_error STREQUAL "")
    message(FATAL_ERROR "verified program wrote to stderr:\n${run_error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT run_output STREQUAL expected)
    message(FATAL_ERROR "program output mismatch:\nexpected:\n${expected}actual:\n${run_output}")
endif()

message(STATUS "program exited as expected with zero live allocations")
