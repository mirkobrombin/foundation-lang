if(NOT DEFINED COMPILER OR NOT DEFINED STAGE0 OR NOT DEFINED ROOT OR
   NOT DEFINED PROJECT OR NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR
        "self-host assertion requires COMPILER, STAGE0, ROOT, PROJECT, and OUTPUT_DIRECTORY"
    )
endif()

function(run_checked label)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${label} exited with ${status}:\n"
            "stdout:\n${output}"
            "stderr:\n${error}"
        )
    endif()
    set(command_output "${output}" PARENT_SCOPE)
    set(command_error "${error}" PARENT_SCOPE)
endfunction()

function(require_hello_output label output)
    if(NOT output STREQUAL "hello from foundation\n")
        message(FATAL_ERROR "${label} output mismatch:\n${output}")
    endif()
endfunction()

function(require_same_file label expected actual)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files "${expected}" "${actual}"
        RESULT_VARIABLE different
    )
    if(NOT different EQUAL 0)
        message(FATAL_ERROR "${label} differs from stage0:\n${expected}\n${actual}")
    endif()
endfunction()

# Emits one artifact with stage0 and with the self-hosted compiler, then
# requires byte-identical files.
function(require_emit_parity label name command source)
    set(expected "${PARITY_DIRECTORY}/stage0/${name}")
    set(actual "${PARITY_DIRECTORY}/selfhost/${name}")
    run_checked("stage0 ${label}" "${STAGE0}" ${command} "${source}" -o "${expected}" ${ARGN})
    run_checked("${label}" "${COMPILER}" ${command} "${source}" -o "${actual}" ${ARGN})
    require_same_file("${label}" "${expected}" "${actual}")
endfunction()

# Requires stage0 and the self-hosted compiler to reject one invocation with
# the same status, no standard output, and a matching standard error fragment.
function(require_rejected_parity label status fragment)
    foreach(program IN ITEMS "${STAGE0}" "${COMPILER}")
        execute_process(
            COMMAND "${program}" ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error
        )
        if(NOT result EQUAL status OR NOT output STREQUAL "" OR NOT error MATCHES "${fragment}")
            message(FATAL_ERROR
                "${label} with ${program} exited with ${result}, expected ${status}:\n"
                "stdout:\n${output}"
                "stderr:\n${error}"
            )
        endif()
    endforeach()
endfunction()

# Snapshots one package with both compilers and requires the same report and
# the same copied tree.
function(require_snapshot_parity label project)
    set(expected "${PARITY_DIRECTORY}/stage0/${label}")
    set(actual "${PARITY_DIRECTORY}/selfhost/${label}")
    run_checked("stage0 ${label}" "${STAGE0}" package snapshot "${project}" -o "${expected}")
    string(REPLACE "${expected}" "<snapshot>" expected_report "${command_output}")
    run_checked("${label}" "${COMPILER}" package snapshot "${project}" -o "${actual}")
    string(REPLACE "${actual}" "<snapshot>" actual_report "${command_output}")
    if(NOT actual_report STREQUAL expected_report OR NOT actual_report MATCHES "digest sha256:")
        message(FATAL_ERROR
            "${label} report differs from stage0:\n${expected_report}\n${actual_report}")
    endif()
    file(GLOB_RECURSE expected_files RELATIVE "${expected}" "${expected}/*")
    file(GLOB_RECURSE actual_files RELATIVE "${actual}" "${actual}/*")
    list(SORT expected_files)
    list(SORT actual_files)
    if(NOT actual_files STREQUAL expected_files)
        message(FATAL_ERROR
            "${label} copied ${actual_files}, stage0 copied ${expected_files}")
    endif()
    foreach(file IN LISTS expected_files)
        require_same_file("${label} ${file}" "${expected}/${file}" "${actual}/${file}")
    endforeach()
    require_rejected_parity("${label} existing output" 1 "FDN4114"
        package snapshot "${project}" -o "${expected}")
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(hello "${ROOT}/tests/cases/accept/hello.fn")
set(tests "${ROOT}/tests/cases/accept/test-declarations.fn")
set(llvm_ir "${OUTPUT_DIRECTORY}/hello.ll")
set(llvm_binary "${OUTPUT_DIRECTORY}/hello-llvm${EXECUTABLE_SUFFIX}")
set(c_binary "${OUTPUT_DIRECTORY}/hello-c${EXECUTABLE_SUFFIX}")

run_checked("version" "${COMPILER}" version)
if(NOT command_output STREQUAL "foundationc 1.0.0\n")
    message(FATAL_ERROR "version output mismatch:\n${command_output}")
endif()

run_checked("check" "${COMPILER}" check "${hello}")
run_checked("emit-llvm" "${COMPILER}" emit-llvm "${hello}" -o "${llvm_ir}")
if(NOT EXISTS "${llvm_ir}")
    message(FATAL_ERROR "emit-llvm did not create ${llvm_ir}")
endif()
file(SIZE "${llvm_ir}" llvm_ir_size)
if(llvm_ir_size EQUAL 0)
    message(FATAL_ERROR "emit-llvm created an empty file")
endif()

run_checked("LLVM build" "${COMPILER}" build "${hello}" -o "${llvm_binary}")
run_checked("LLVM build output" "${llvm_binary}")
require_hello_output("LLVM build" "${command_output}")

run_checked("C build" "${COMPILER}" build "${hello}" -o "${c_binary}" --backend c)
run_checked("C build output" "${c_binary}")
require_hello_output("C build" "${command_output}")

run_checked("LLVM run" "${COMPILER}" run "${hello}")
require_hello_output("LLVM run" "${command_output}")
run_checked("C run" "${COMPILER}" run "${hello}" --backend c)
require_hello_output("C run" "${command_output}")

foreach(backend IN ITEMS llvm c)
    run_checked("${backend} test" "${COMPILER}" test "${tests}" --backend "${backend}")
    if(NOT command_output MATCHES "ok addition returns the sum" OR
       NOT command_output MATCHES "ok explicit pass" OR
       NOT command_output MATCHES "2 passed; 0 failed")
        message(FATAL_ERROR "${backend} test summary is incomplete:\n${command_output}")
    endif()
endforeach()

run_checked("package inspect" "${COMPILER}" package inspect "${PROJECT}")
if(NOT command_output MATCHES "format foundation.package/v1" OR
   NOT command_output MATCHES "format foundation.lock/v1")
    message(FATAL_ERROR "package inspect output is incomplete:\n${command_output}")
endif()

set(initialized "${OUTPUT_DIRECTORY}/package-init")
file(REMOVE_RECURSE "${initialized}")
run_checked("package init" "${COMPILER}" package init "${initialized}" selfhost.initialized)
file(READ "${initialized}/foundation.package" initialized_manifest)
if(NOT initialized_manifest MATCHES "language 1")
    message(FATAL_ERROR "self-hosted package init omitted Language 1")
endif()
run_checked("package resolve" "${COMPILER}" package resolve "${initialized}")

set(PARITY_DIRECTORY "${OUTPUT_DIRECTORY}/parity")
file(REMOVE_RECURSE "${PARITY_DIRECTORY}")
set(c_abi "${ROOT}/examples/c-abi")
set(application_plan "${ROOT}/tests/cases/accept/application-plan.fn")
set(web_application "${ROOT}/tests/projects/application-host-web-middleware")
set(state_machine "${ROOT}/examples/state-machine")
set(openapi "${ROOT}/tests/projects/openapi-document")

require_emit_parity("emit-c-header" "c-abi.h" emit-c-header "${c_abi}")
require_rejected_parity("emit-c-header target" 2 "emit-c-header <source-or-project>"
    emit-c-header "${c_abi}" -o "${PARITY_DIRECTORY}/invalid.h" --target plan9)
require_rejected_parity("emit-c-header output" 2 "emit-c-header <source-or-project>"
    emit-c-header "${c_abi}" -o)

require_emit_parity("emit-app-plan" "application-plan.json" emit-app-plan "${application_plan}")
require_same_file("emit-app-plan fixture"
    "${ROOT}/tests/cases/accept/application-plan.json"
    "${PARITY_DIRECTORY}/selfhost/application-plan.json")
require_emit_parity("emit-app-plan web" "web-plan.json" emit-app-plan "${web_application}")
require_rejected_parity("emit-app-plan arguments" 2 "emit-app-plan <source-or-project>"
    emit-app-plan "${application_plan}" -o "${PARITY_DIRECTORY}/invalid.json" --target linux)

require_emit_parity("emit-fsm Mermaid" "machine.mmd" emit-fsm "${state_machine}"
    --format mermaid --machine Order)
require_emit_parity("emit-fsm default machine" "default.mmd" emit-fsm "${state_machine}"
    --format mermaid)
require_emit_parity("emit-fsm Graphviz" "machine.dot" emit-fsm "${state_machine}"
    --machine Order --format graphviz)
require_same_file("emit-fsm fixture" "${state_machine}/expected.dot"
    "${PARITY_DIRECTORY}/selfhost/machine.dot")
require_rejected_parity("emit-fsm format" 2 "emit-fsm <source-or-project>"
    emit-fsm "${state_machine}" -o "${PARITY_DIRECTORY}/invalid.mmd" --format invalid)
require_rejected_parity("emit-fsm missing format" 2 "emit-fsm <source-or-project>"
    emit-fsm "${state_machine}" -o "${PARITY_DIRECTORY}/invalid.mmd" --machine Order)
require_rejected_parity("emit-fsm machine" 1 "FDN2422"
    emit-fsm "${state_machine}" -o "${PARITY_DIRECTORY}/invalid.mmd"
    --format mermaid --machine Missing)

require_emit_parity("emit-openapi" "openapi.json" emit-openapi "${openapi}")
require_emit_parity("emit-openapi override" "openapi-override.json" emit-openapi "${openapi}"
    --title "Users API" --version "9.8.7")
require_rejected_parity("emit-openapi extension" 2
    "OpenAPI output must use the \\.json extension"
    emit-openapi "${openapi}" -o "${PARITY_DIRECTORY}/openapi.txt")
require_rejected_parity("emit-openapi duplicate" 2 "emit-openapi <source-or-project>"
    emit-openapi "${openapi}" -o "${PARITY_DIRECTORY}/invalid.json"
    --title First --title Second)

require_snapshot_parity("package-snapshot" "${ROOT}/tests/package-workflow/app")
require_snapshot_parity("native-snapshot" "${ROOT}/tests/package-workflow/native")
require_rejected_parity("package snapshot output" 2 "package snapshot <project> -o <directory>"
    package snapshot "${ROOT}/tests/package-workflow/app" -o)
require_rejected_parity("package snapshot option" 2 "package snapshot <project> -o <directory>"
    package snapshot "${ROOT}/tests/package-workflow/app"
    --output "${PARITY_DIRECTORY}/invalid-snapshot")

message(STATUS "self-hosted compiler commands passed")
