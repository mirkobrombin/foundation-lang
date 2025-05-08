if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED C_COMPILER_ID OR
   NOT DEFINED SOURCE OR NOT DEFINED NATIVE OR NOT DEFINED SUPPORT OR
   NOT DEFINED OBJECT OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "native object assertion is missing an input")
endif()

if(C_COMPILER_ID STREQUAL "MSVC")
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /W4 /WX /c "${SUPPORT}" "/Fo:${OBJECT}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
else()
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra -Wpedantic -Werror
            -c "${SUPPORT}" -o "${OBJECT}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
endif()
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "native object compilation failed:\n${compile_output}${compile_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}" --native "${NATIVE}" --native "${OBJECT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "native object program failed with ${result}:\n${error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT output STREQUAL expected)
    message(FATAL_ERROR "native object program output mismatch:\nexpected:\n${expected}actual:\n${output}")
endif()

message(STATUS "native object input executes with the Foundation program")
