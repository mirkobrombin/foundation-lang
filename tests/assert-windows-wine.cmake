if(NOT DEFINED COMPILER OR NOT DEFINED MINGW_C_COMPILER OR NOT DEFINED WINE OR
   NOT DEFINED ROOT OR NOT DEFINED WORK)
    message(FATAL_ERROR "Windows Wine assertion is missing an input")
endif()

foreach(required_path IN ITEMS "${COMPILER}" "${MINGW_C_COMPILER}" "${WINE}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Windows Wine assertion requires ${required_path}")
    endif()
endforeach()

set(runtime_sources
    "${ROOT}/runtime/src/runtime.c"
    "${ROOT}/runtime/src/crypto.c"
    "${ROOT}/runtime/src/task.c"
    "${ROOT}/runtime/src/cancellation.c"
    "${ROOT}/runtime/src/channel.c"
    "${ROOT}/runtime/src/blocking.c"
    "${ROOT}/runtime/src/pool.c"
    "${ROOT}/runtime/src/reactor.c"
    "${ROOT}/runtime/src/resiliency.c"
    "${ROOT}/runtime/src/net.c"
)
set(case_rows
    "ownership|${ROOT}/tests/cases/accept/ownership.fn|${ROOT}/tests/cases/accept/ownership.out"
    "tasks|${ROOT}/tests/cases/accept/tasks.fn|${ROOT}/tests/cases/accept/tasks.out"
    "channels|${ROOT}/tests/cases/accept/channels.fn|${ROOT}/tests/cases/accept/channels.out"
    "channel-select|${ROOT}/tests/cases/accept/channel-select.fn|${ROOT}/tests/cases/accept/channel-select.out"
    "filesystem|${ROOT}/tests/cases/accept/filesystem-read-text.fn|${ROOT}/tests/cases/accept/filesystem-read-text.out|${ROOT}/tests/fixtures/fs/lines.jsonl"
    "httpx|${ROOT}/tests/cases/accept/httpx-network.fn|${ROOT}/tests/cases/accept/httpx-network.out"
)

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_windows_wine_case name source expected)
    set(arguments ${ARGN})
    set(generated "${WORK}/${name}.c")
    set(executable "${WORK}/${name}.exe")
    execute_process(
        COMMAND "${COMPILER}" emit-c "${source}" -o "${generated}" --target windows
        RESULT_VARIABLE emit_result
        OUTPUT_VARIABLE emit_output
        ERROR_VARIABLE emit_error
    )
    if(NOT emit_result EQUAL 0)
        message(FATAL_ERROR "${name} Windows C emission failed:\n${emit_output}${emit_error}")
    endif()
    execute_process(
        COMMAND "${MINGW_C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
                -DFOUNDATION_VERIFY_ALLOCATIONS "${generated}" ${runtime_sources}
                -I "${ROOT}/runtime/include" -pthread -lbcrypt -liphlpapi -lws2_32
                -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "${name} MinGW C11 build failed:\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env WINEDEBUG=-all WINEPREFIX=${WORK}/wineprefix
                "${WINE}" "${executable}" ${arguments}
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error
    )
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "${name} Wine execution failed:\n${run_output}${run_error}")
    endif()
    file(READ "${expected}" expected_output)
    if(NOT run_output STREQUAL expected_output)
        message(FATAL_ERROR "${name} Windows output differs:\nexpected:\n${expected_output}actual:\n${run_output}")
    endif()
endfunction()

set(target_project "${WORK}/target-selection")
file(COPY "${ROOT}/tests/projects/target-selection/" DESTINATION "${target_project}")
file(READ "${target_project}/foundation.lock" target_lock)
string(REPLACE "target macos" "target windows" target_lock "${target_lock}")
file(WRITE "${target_project}/foundation.lock" "${target_lock}")
file(APPEND "${target_project}/src/main.fn"
    "\n@target(windows)\nfn platformLabel() String {\n    \"windows-only\"\n}\n")
execute_process(
    COMMAND "${COMPILER}" check "${target_project}" --target windows
    RESULT_VARIABLE target_result
    OUTPUT_VARIABLE target_output
    ERROR_VARIABLE target_error
)
if(NOT target_result EQUAL 0)
    message(FATAL_ERROR "Windows package lock check failed:\n${target_output}${target_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" check "${target_project}" --target linux
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0 OR NOT mismatch_error MATCHES "FDN4111")
    message(FATAL_ERROR "Windows package lock mismatch was not rejected:\n${mismatch_output}${mismatch_error}")
endif()

foreach(case_row IN LISTS case_rows)
    string(REPLACE "|" ";" fields "${case_row}")
    list(GET fields 0 name)
    list(GET fields 1 source)
    list(GET fields 2 expected)
    list(LENGTH fields field_count)
    if(field_count GREATER 3)
        list(GET fields 3 argument)
        run_windows_wine_case("${name}" "${source}" "${expected}" "${argument}")
    else()
        run_windows_wine_case("${name}" "${source}" "${expected}")
    endif()
endforeach()

set(application_project "${WORK}/application-host-web-activation")
file(COPY "${ROOT}/tests/projects/application-host-web-activation/" DESTINATION "${application_project}")
file(READ "${application_project}/foundation.lock" application_lock)
string(REPLACE "target linux" "target windows" application_lock "${application_lock}")
file(WRITE "${application_project}/foundation.lock" "${application_lock}")
run_windows_wine_case(
    "application-host-web-activation"
    "${application_project}"
    "${ROOT}/tests/projects/application-host-web-activation/expected.out"
)

message(STATUS "MinGW C11 and Wine Windows matrix passed")
