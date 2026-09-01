if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED C_COMPILER_ID OR
   NOT DEFINED SOURCE OR NOT DEFINED GENERATED OR NOT DEFINED OUTPUT OR
   NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "closure panic assertion is missing an input")
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

if(C_COMPILER_ID STREQUAL "MSVC")
    set(executable "${OUTPUT}.exe")
    set(object_directory "${OUTPUT}.objects")
    file(REMOVE_RECURSE "${object_directory}")
    file(MAKE_DIRECTORY "${object_directory}")
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /O2 /W4 /WX "${GENERATED}"
                "${RUNTIME_SOURCE}" "/I${RUNTIME_INCLUDE}" bcrypt.lib
                "/Fe:${executable}" "/Fo:${object_directory}/" /link /STACK:8388608
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
else()
    set(executable "${OUTPUT}")
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
                "${GENERATED}" "${RUNTIME_SOURCE}" -I "${RUNTIME_INCLUDE}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
endif()
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "optimized C build failed:\n${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(run_result EQUAL 0)
    message(FATAL_ERROR "closure panic program unexpectedly succeeded:\n${run_output}")
endif()

string(FIND "${run_error}" "foundation panic: closure failure" message_frame)
string(FIND "${run_error}" "at main.$closure_1 (${SOURCE}:7:9)" closure_frame)
string(FIND "${run_error}" "at main.invoke (${SOURCE}:2:5)" invoke_frame)
string(FIND "${run_error}" "at main.main (${SOURCE}:9:5)" main_frame)
if(message_frame LESS 0 OR closure_frame LESS 0 OR invoke_frame LESS 0 OR main_frame LESS 0 OR
   closure_frame GREATER_EQUAL invoke_frame OR invoke_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "closure panic frames are incomplete or out of order:\n${run_error}")
endif()

message(STATUS "optimized closure panic trace preserves Foundation source frames")
