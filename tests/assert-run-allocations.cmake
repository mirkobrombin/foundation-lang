if(NOT DEFINED COMPILER OR NOT DEFINED C_COMPILER OR NOT DEFINED C_COMPILER_ID OR
   NOT DEFINED SOURCE OR NOT DEFINED GENERATED OR NOT DEFINED OUTPUT OR
   NOT DEFINED EXPECTED OR NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR "allocation run assertion is missing an input")
endif()

set(runtime_sources "${RUNTIME_SOURCE}")
if(DEFINED RUNTIME_CRYPTO_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_CRYPTO_SOURCE}")
endif()
if(DEFINED RUNTIME_PARSE_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_PARSE_SOURCE}")
endif()
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
if(DEFINED RUNTIME_POOL_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_POOL_SOURCE}")
endif()
if(DEFINED RUNTIME_REACTOR_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_REACTOR_SOURCE}")
endif()
if(DEFINED RUNTIME_RESILIENCY_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_RESILIENCY_SOURCE}")
endif()
if(DEFINED RUNTIME_NET_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_NET_SOURCE}")
endif()
if(DEFINED RUNTIME_PLUGIN_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_PLUGIN_SOURCE}")
endif()
if(DEFINED RUNTIME_PLUGIN_SANDBOX_SOURCE)
    list(APPEND runtime_sources "${RUNTIME_PLUGIN_SANDBOX_SOURCE}")
endif()
if(DEFINED NATIVE)
    list(APPEND runtime_sources "${NATIVE}")
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
    set(platform_libraries)
    list(APPEND platform_libraries bcrypt.lib)
    if(DEFINED RUNTIME_NET_SOURCE)
        list(APPEND platform_libraries ws2_32.lib)
    endif()
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /W4 /WX
                /DFOUNDATION_VERIFY_ALLOCATIONS "${GENERATED}" ${runtime_sources}
                "/I${RUNTIME_INCLUDE}" ${platform_libraries} "/Fe:${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
else()
    set(thread_arguments)
    if(DEFINED RUNTIME_BLOCKING_SOURCE OR DEFINED RUNTIME_POOL_SOURCE OR
       DEFINED RUNTIME_REACTOR_SOURCE OR DEFINED RUNTIME_RESILIENCY_SOURCE OR
       DEFINED RUNTIME_NET_SOURCE OR
       DEFINED RUNTIME_PLUGIN_SANDBOX_SOURCE)
        list(APPEND thread_arguments -pthread)
    endif()
    set(platform_libraries)
    if(WIN32)
        list(APPEND platform_libraries -lbcrypt)
    endif()
    if(WIN32 AND DEFINED RUNTIME_NET_SOURCE)
        list(APPEND platform_libraries -lws2_32)
    endif()
    if(DEFINED RUNTIME_PLUGIN_SOURCE AND NOT WIN32 AND NOT APPLE)
        list(APPEND platform_libraries -ldl)
    endif()
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
                -DFOUNDATION_VERIFY_ALLOCATIONS "${GENERATED}" ${runtime_sources}
                -I "${RUNTIME_INCLUDE}" ${thread_arguments} ${platform_libraries}
                -o "${executable}"
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
