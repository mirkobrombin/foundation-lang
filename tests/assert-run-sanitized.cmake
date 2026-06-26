if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED SOURCE OR
   NOT DEFINED GENERATED OR NOT DEFINED OUTPUT OR NOT DEFINED EXPECTED OR
   NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "sanitized run assertion is missing an input")
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

set(runtime_sources "${RUNTIME_SOURCE}")
if(DEFINED RUNTIME_TASK_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_TASK_SOURCE}")
endif()
if(DEFINED RUNTIME_CANCELLATION_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_CANCELLATION_SOURCE}")
endif()
if(DEFINED RUNTIME_CHANNEL_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_CHANNEL_SOURCE}")
endif()
if(DEFINED RUNTIME_BLOCKING_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_BLOCKING_SOURCE}")
endif()
if(DEFINED RUNTIME_REACTOR_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_REACTOR_SOURCE}")
endif()
if(DEFINED RUNTIME_NET_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_NET_SOURCE}")
endif()
if(DEFINED NATIVE)
    list(APPEND runtime_sources "${NATIVE}")
endif()

set(thread_arguments)
if(DEFINED RUNTIME_BLOCKING_SOURCE OR DEFINED RUNTIME_REACTOR_SOURCE OR
   DEFINED RUNTIME_NET_SOURCE)
    list(APPEND thread_arguments -pthread)
endif()

execute_process(
    COMMAND "${C_COMPILER}" -std=c11 -g -fno-omit-frame-pointer
            -DFOUNDATION_VERIFY_ALLOCATIONS
            -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror
            "${GENERATED}" ${runtime_sources} -I "${RUNTIME_INCLUDE}"
            ${thread_arguments} -o "${OUTPUT}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "sanitized C build failed:\n${build_output}${build_error}")
endif()

set(command "${OUTPUT}")
if(DEFINED PROGRAM_ARGS)
    list(APPEND command ${PROGRAM_ARGS})
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1"
            "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
            ${command}
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "sanitized program failed with ${run_result}:\n${run_error}")
endif()
if(NOT run_error STREQUAL "")
    message(FATAL_ERROR "sanitizer reported diagnostics:\n${run_error}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT run_output STREQUAL expected)
    message(FATAL_ERROR "program output mismatch:\nexpected:\n${expected}actual:\n${run_output}")
endif()

message(STATUS "program is clean under ASan, LSan, and UBSan")
