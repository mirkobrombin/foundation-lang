if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED SOURCE OR
   NOT DEFINED GENERATED OR NOT DEFINED HEADER OR NOT DEFINED OUTPUT OR
   NOT DEFINED NATIVE OR NOT DEFINED NATIVE_SECOND OR NOT DEFINED EXPECTED OR
   NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "sanitized native run assertion is missing an input")
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

execute_process(
    COMMAND "${COMPILER}" emit-c-header "${SOURCE}" -o "${HEADER}"
    RESULT_VARIABLE header_result
    OUTPUT_VARIABLE header_output
    ERROR_VARIABLE header_error
)
if(NOT header_result EQUAL 0)
    message(FATAL_ERROR "C header emission failed:\n${header_output}${header_error}")
endif()

get_filename_component(header_directory "${HEADER}" DIRECTORY)
execute_process(
    COMMAND "${C_COMPILER}" -std=c11 -g -fno-omit-frame-pointer
            -DFOUNDATION_VERIFY_ALLOCATIONS
            -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror
            "${GENERATED}" "${RUNTIME_SOURCE}" "${NATIVE}" "${NATIVE_SECOND}"
            -I "${RUNTIME_INCLUDE}" -I "${header_directory}" -o "${OUTPUT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "sanitized native C build failed:\n${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1"
            "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
            "${OUTPUT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "sanitized native program failed with ${run_result}:\n${run_error}")
endif()
if(NOT run_error STREQUAL "")
    message(FATAL_ERROR "sanitizer reported diagnostics:\n${run_error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT run_output STREQUAL expected)
    message(FATAL_ERROR "sanitized native program output mismatch:\nexpected:\n${expected}actual:\n${run_output}")
endif()

message(STATUS "native boundary is clean under ASan, LSan, and UBSan")
