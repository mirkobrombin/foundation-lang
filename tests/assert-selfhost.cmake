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

# Runs one invocation with both compilers and requires the same status,
# standard output, and standard error. Line endings and the self-hosted
# program name in process messages are normalized before the comparison.
function(require_command_parity label)
    execute_process(
        COMMAND "${STAGE0}" ${ARGN}
        RESULT_VARIABLE expected_status
        OUTPUT_VARIABLE expected_output
        ERROR_VARIABLE expected_error
    )
    execute_process(
        COMMAND "${COMPILER}" ${ARGN}
        RESULT_VARIABLE actual_status
        OUTPUT_VARIABLE actual_output
        ERROR_VARIABLE actual_error
    )
    foreach(stream IN ITEMS expected_output expected_error actual_output actual_error)
        string(REPLACE "\r\n" "\n" ${stream} "${${stream}}")
    endforeach()
    string(REPLACE "foundationc-selfhost: " "foundationc: " actual_error "${actual_error}")
    if(NOT actual_status EQUAL expected_status OR
       NOT actual_output STREQUAL expected_output OR
       NOT actual_error STREQUAL expected_error)
        message(FATAL_ERROR
            "${label} differs from stage0:\n"
            "stage0 status ${expected_status}, self-hosted status ${actual_status}\n"
            "stage0 stdout:\n${expected_output}\nself-hosted stdout:\n${actual_output}\n"
            "stage0 stderr:\n${expected_error}\nself-hosted stderr:\n${actual_error}"
        )
    endif()
    set(command_status "${actual_status}" PARENT_SCOPE)
    set(command_output "${actual_output}" PARENT_SCOPE)
endfunction()

# Rewrites one copied source in place with each compiler and requires the
# same status and the same rewritten file.
function(require_write_parity label command source expected)
    foreach(side IN ITEMS stage0 selfhost)
        set(work "${PARITY_DIRECTORY}/${label}-${side}")
        file(REMOVE_RECURSE "${work}")
        file(MAKE_DIRECTORY "${work}")
        file(COPY_FILE "${source}" "${work}/main.fn")
    endforeach()
    run_checked("stage0 ${label}" "${STAGE0}" ${command} --write
        "${PARITY_DIRECTORY}/${label}-stage0")
    run_checked("${label}" "${COMPILER}" ${command} --write
        "${PARITY_DIRECTORY}/${label}-selfhost")
    require_same_file("${label}" "${PARITY_DIRECTORY}/${label}-stage0/main.fn"
        "${PARITY_DIRECTORY}/${label}-selfhost/main.fn")
    require_same_file("${label} fixture" "${expected}"
        "${PARITY_DIRECTORY}/${label}-selfhost/main.fn")
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

# Writes the lock a lint fixture needs for the host target.
function(write_lint_lock directory root)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" lock_target)
    if(lock_target STREQUAL "darwin")
        set(lock_target "macos")
    endif()
    file(WRITE "${directory}/foundation.lock"
        "format foundation.lock/v1\n"
        "root ${root} 1.0.0\n"
        "target ${lock_target}\n"
    )
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

# The self-hosted compiler reports every freestanding rejection with the stage0 code at the stage0
# location.
foreach(rejection IN ITEMS
        "task-declaration;FDN2186" "spawn;FDN2186" "channel-construction;FDN2187"
        "channel-send;FDN2187" "select;FDN2187" "blocking-import;FDN2188"
        "callback-import;FDN2188" "retry;FDN2189" "import-fs;FDN3011"
        "import-concurrent;FDN3011" "import-collections;FDN3011" "main;FDN3012"
        "usize-literal;FDN2005")
    list(GET rejection 0 name)
    list(GET rejection 1 code)
    set(fixture "${ROOT}/tests/cases/freestanding-reject/${name}.fn")
    foreach(compiler IN ITEMS COMPILER STAGE0)
        execute_process(
            COMMAND "${${compiler}}" check "${fixture}" --target freestanding
            RESULT_VARIABLE status
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error
        )
        # Stage0 ends the location with a colon and the self-hosted compiler does not.
        string(REGEX MATCHALL ":[0-9]+:[0-9]+:? error\\[${code}\\]" locations "${error}")
        list(TRANSFORM locations REPLACE ":? error\\[.*$" "")
        list(SORT locations)
        if(status EQUAL 0 OR NOT locations)
            message(FATAL_ERROR
                "${compiler} did not reject ${name} with ${code}:\n${output}${error}")
        endif()
        set(${compiler}_locations "${locations}")
    endforeach()
    if(NOT COMPILER_locations STREQUAL STAGE0_locations)
        message(FATAL_ERROR
            "${name} reports ${code} at ${COMPILER_locations} instead of ${STAGE0_locations}")
    endif()
endforeach()

# Both compilers reject the hosted-only UUID constructors with their existing unknown
# associated function diagnostics, which already differ for hosted programs.
set(fixture "${ROOT}/tests/cases/freestanding-reject/uuid-new-v4.fn")
foreach(compiler IN ITEMS COMPILER STAGE0)
    execute_process(
        COMMAND "${${compiler}}" check "${fixture}" --target freestanding
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(status EQUAL 0 OR NOT error MATCHES ":2:[0-9]+:? error\\[FDN[0-9]+\\]")
        message(FATAL_ERROR
            "${compiler} did not reject UUID.NewV4 for freestanding:\n${output}${error}")
    endif()
    if(compiler STREQUAL "STAGE0" AND NOT error MATCHES ":2:10: error\\[FDN2190\\]")
        message(FATAL_ERROR "STAGE0 did not reject UUID.NewV4 with FDN2190:\n${error}")
    endif()
endforeach()

run_checked("freestanding selection" "${CMAKE_COMMAND}"
    "-DCOMPILER=${COMPILER}"
    "-DPROJECT=${ROOT}/tests/projects/freestanding-selection"
    "-DWORK=${OUTPUT_DIRECTORY}/freestanding-selection"
    -P "${ROOT}/tests/assert-freestanding-selection.cmake")

set(freestanding_c "${OUTPUT_DIRECTORY}/freestanding.c")
run_checked("freestanding C emission" "${COMPILER}" emit-c
    "${ROOT}/tests/projects/freestanding-library" -o "${freestanding_c}" --target freestanding)
file(READ "${freestanding_c}" freestanding_source)
if(freestanding_source MATCHES "foundation/runtime\\.h" OR freestanding_source MATCHES "int main")
    message(FATAL_ERROR "freestanding C emission contains hosted code")
endif()
if(DEFINED ENV{FOUNDATION_CLANG} AND NOT "$ENV{FOUNDATION_CLANG}" STREQUAL "")
    run_checked("freestanding C compilation" "$ENV{FOUNDATION_CLANG}"
        --target=wasm32-unknown-unknown -std=c11 -ffreestanding -nostdlibinc -O2 -Wall -Wextra
        -Wpedantic -Werror -DFOUNDATION_FREESTANDING=1 -I "${ROOT}/runtime/include"
        -c "${freestanding_c}" -o "${OUTPUT_DIRECTORY}/freestanding.o")
endif()

set(native_first "${PARITY_DIRECTORY}/native-first/project")
set(native_second "${PARITY_DIRECTORY}/native-second/nested/project")
file(MAKE_DIRECTORY "${native_first}" "${native_second}")
file(COPY "${ROOT}/tests/projects/native-interface/" DESTINATION "${native_first}")
file(COPY "${ROOT}/tests/projects/native-interface/" DESTINATION "${native_second}")
run_checked("native package resolve" "${COMPILER}" package resolve "${native_first}")
run_checked("second native package resolve" "${COMPILER}" package resolve "${native_second}")
require_emit_parity("emit-pii" "native.pii.json" emit-pii "${native_first}")
run_checked("emit-pii second checkout" "${COMPILER}" emit-pii "${native_second}"
    -o "${PARITY_DIRECTORY}/selfhost/native-second.pii.json")
require_same_file("emit-pii checkout path" "${PARITY_DIRECTORY}/selfhost/native.pii.json"
    "${PARITY_DIRECTORY}/selfhost/native-second.pii.json")
require_rejected_parity("emit-pii output" 2 "emit-pii <project> -o <output.json>"
    emit-pii "${native_first}" -o)
require_rejected_parity("emit-pii native library" 2 "emit-pii requires native_library c"
    emit-pii "${ROOT}/tests/package-workflow/app" -o "${PARITY_DIRECTORY}/invalid.json")
file(READ "${native_second}/foundation.package" reserved_manifest)
string(REPLACE "native_source native/libfuse/increment.c\n" "" reserved_manifest
    "${reserved_manifest}")
string(REPLACE "path native/libfuse" "registry default" reserved_manifest
    "${reserved_manifest}")
file(WRITE "${native_second}/foundation.package" "${reserved_manifest}")
require_rejected_parity("emit-pii reserved resolver" 1 "FDN4057"
    emit-pii "${native_second}" -o "${PARITY_DIRECTORY}/reserved.json")
require_rejected_parity("emit-pii freestanding lock" 2
    "emit-pii requires --triple for a freestanding lock"
    emit-pii "${ROOT}/tests/projects/freestanding-library"
    -o "${PARITY_DIRECTORY}/freestanding.json")
require_rejected_parity("emit-pii hosted triple" 2
    "--triple, --cpu, and --features require a freestanding lock"
    emit-pii "${native_first}" -o "${PARITY_DIRECTORY}/hosted.json"
    --triple x86_64-unknown-none-elf)
require_rejected_parity("emit-pii cpu without triple" 2 "emit-pii <project> -o <output.json>"
    emit-pii "${native_first}" -o "${PARITY_DIRECTORY}/hosted.json" --cpu generic)

require_emit_parity("emit-metadata" "workflows.metadata.json" emit-metadata
    "${ROOT}/tests/cases/accept/workflows.fn")
require_same_file("emit-metadata fixture" "${ROOT}/tests/cases/accept/workflows.metadata.json"
    "${PARITY_DIRECTORY}/selfhost/workflows.metadata.json")
require_emit_parity("emit-metadata project" "typed-attributes.metadata.json" emit-metadata
    "${ROOT}/tests/projects/typed-attributes")
require_same_file("emit-metadata project fixture"
    "${ROOT}/tests/projects/typed-attributes/expected.metadata.json"
    "${PARITY_DIRECTORY}/selfhost/typed-attributes.metadata.json")
require_rejected_parity("emit-metadata target" 2 "emit-metadata <source-or-project>"
    emit-metadata "${ROOT}/tests/cases/accept/workflows.fn"
    -o "${PARITY_DIRECTORY}/invalid.json" --target plan9)
require_emit_parity("emit-metadata freestanding" "freestanding.metadata.json" emit-metadata
    "${ROOT}/tests/projects/freestanding-library" --target freestanding)

set(imports_source "${ROOT}/tests/imports/messy.fn")
set(imports_expected "${ROOT}/tests/imports/expected.fn")
require_command_parity("imports stdout" imports "${imports_source}")
if(NOT command_output STREQUAL "")
    file(READ "${imports_expected}" imports_organized)
    if(NOT command_output STREQUAL imports_organized)
        message(FATAL_ERROR "imports stdout differs from ${imports_expected}")
    endif()
endif()
require_command_parity("imports check" imports --check "${imports_source}")
if(NOT command_status EQUAL 1)
    message(FATAL_ERROR "imports check did not report the unorganized source")
endif()
require_command_parity("imports clean check" imports --check "${imports_expected}")
require_write_parity("imports-write" imports "${imports_source}" "${imports_expected}")
require_rejected_parity("imports mode" 2 "imports --check <source-or-project>"
    imports --bogus "${imports_source}")

set(format_source "${ROOT}/tests/formatter/messy.fn")
set(format_expected "${ROOT}/tests/formatter/expected.fn")
require_command_parity("format stdout" format "${format_source}")
file(READ "${format_expected}" format_layout)
if(NOT command_output STREQUAL format_layout)
    message(FATAL_ERROR "format stdout differs from ${format_expected}")
endif()
require_command_parity("format check" format --check "${format_source}")
require_command_parity("format clean check" format --check "${format_expected}")
require_command_parity("format invalid" format "${ROOT}/tests/formatter/invalid.fn")
require_write_parity("format-write" format "${format_source}" "${format_expected}")
require_rejected_parity("format mode" 2 "format --check <source-or-project>"
    format --bogus "${format_source}")

set(documentation_source "${ROOT}/tests/projects/documentation-api")
require_emit_parity("documentation" "reference.md" documentation "${documentation_source}"
    --target linux)
require_same_file("documentation fixture" "${documentation_source}/expected.md"
    "${PARITY_DIRECTORY}/selfhost/reference.md")
require_rejected_parity("documentation extension" 2
    "documentation output must use the \\.md extension"
    documentation "${documentation_source}" -o "${PARITY_DIRECTORY}/reference.txt")
require_rejected_parity("documentation target" 2 "documentation <source-or-project>"
    documentation "${documentation_source}" -o "${PARITY_DIRECTORY}/reference.md"
    --target plan9)
require_emit_parity("documentation freestanding" "freestanding.md" documentation
    "${ROOT}/tests/projects/freestanding-library" --target freestanding)

set(lint_work "${PARITY_DIRECTORY}/lint")
file(MAKE_DIRECTORY "${lint_work}/fcs-rules" "${lint_work}/fcs-violations")
file(COPY "${ROOT}/tests/projects/lint-profile/" DESTINATION "${lint_work}")
write_lint_lock("${lint_work}" lint.fixture)
file(COPY "${ROOT}/tests/projects/fcs-rules/" DESTINATION "${lint_work}/fcs-rules")
write_lint_lock("${lint_work}/fcs-rules" fcs.rules)
file(COPY "${ROOT}/tests/projects/fcs-violations/" DESTINATION "${lint_work}/fcs-violations")
write_lint_lock("${lint_work}/fcs-violations" fcs.violations)
require_command_parity("lint suppression" lint "${lint_work}/fcs-rules")
require_command_parity("lint violations" lint "${lint_work}/fcs-violations")
if(NOT command_status EQUAL 1)
    message(FATAL_ERROR "lint accepted the strict violation fixture")
endif()
require_command_parity("lint violations valid" lint "${lint_work}/fcs-violations"
    --profile valid)
require_command_parity("lint strict manifest" lint "${lint_work}")
require_command_parity("lint standard" lint "${lint_work}" --profile standard)
require_command_parity("lint advisory rule" lint "${lint_work}" --profile standard
    --rule FCS2001=error)
require_command_parity("lint rule severity" lint "${lint_work}" --rule FCS1001=error)
require_command_parity("lint valid" lint "${lint_work}" --profile valid)
require_rejected_parity("lint profile" 2 "lint <source-or-project>"
    lint "${lint_work}" --profile custom)
require_rejected_parity("lint unknown rule" 2 "lint <source-or-project>"
    lint "${lint_work}" --rule FCS9999=off)
require_rejected_parity("lint fixed rule" 2 "lint <source-or-project>"
    lint "${lint_work}" --rule FCS9001=off)
require_rejected_parity("lint compiler errors" 1 "error\\[FDN"
    lint "${ROOT}/tests/cases/reject/unknown-associated-function.fn" --profile valid)

message(STATUS "self-hosted compiler commands passed")
