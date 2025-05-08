if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED CXX_COMPILER OR
   NOT DEFINED CXX_COMPILER_ID OR NOT DEFINED CPP_SOURCE OR NOT DEFINED HEADER OR
   NOT DEFINED OBJECT OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "C++ header assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c-header "${SOURCE}" -o "${HEADER}"
    RESULT_VARIABLE emit_result
    OUTPUT_VARIABLE emit_output
    ERROR_VARIABLE emit_error
)
if(NOT emit_result EQUAL 0)
    message(FATAL_ERROR "C ABI header emission failed:\n${emit_output}${emit_error}")
endif()

get_filename_component(header_directory "${HEADER}" DIRECTORY)
if(CXX_COMPILER_ID STREQUAL "MSVC")
    execute_process(
        COMMAND "${CXX_COMPILER}" /nologo /std:c++20 /W4 /WX /c "${CPP_SOURCE}"
            "/I${header_directory}" "/I${RUNTIME_INCLUDE}" "/Fo:${OBJECT}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
else()
    execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Wpedantic -Werror
            -c "${CPP_SOURCE}" -I "${header_directory}" -I "${RUNTIME_INCLUDE}"
            -o "${OBJECT}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error
    )
endif()
if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "generated C ABI header failed in C++:\n${compile_output}${compile_error}")
endif()

message(STATUS "generated C ABI header compiles as C++20")
