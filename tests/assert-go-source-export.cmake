if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED UNSUPPORTED_SOURCE OR
   NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED ESCAPE_SOURCE OR
   NOT DEFINED OWNER_CYCLE_SOURCE OR NOT DEFINED OPEN_GENERIC_SOURCE OR
   NOT DEFINED GENERIC_STRUCT_SOURCE OR NOT DEFINED MULTIPLE_SOURCE OR
   NOT DEFINED OWNED_CONTRACT_SOURCE OR NOT DEFINED CONTRACT_CONFLICT_SOURCE OR
   NOT DEFINED FIXTURE OR NOT DEFINED WORK OR NOT DEFINED GO_EXECUTABLE)
    message(FATAL_ERROR "go-source export test requires compiler, sources, fixture, work, and Go")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/source")
file(COPY "${SOURCE}/" DESTINATION "${WORK}/source")

execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/source"
    RESULT_VARIABLE resolve_status
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_status EQUAL 0)
    message(FATAL_ERROR "cannot resolve go-source fixture:\n${resolve_output}${resolve_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/source"
        -o "${WORK}/go-source"
        --format go-source
    RESULT_VARIABLE export_status
    OUTPUT_VARIABLE export_output
    ERROR_VARIABLE export_error
)
if(NOT export_status EQUAL 0)
    message(FATAL_ERROR "cannot export go-source package:\n${export_output}${export_error}")
endif()

foreach(path IN ITEMS go.mod sample_source.go foundation.pii.json)
    if(NOT EXISTS "${WORK}/go-source/${path}")
        message(FATAL_ERROR "go-source export did not produce ${path}")
    endif()
endforeach()
if(EXISTS "${WORK}/go-source/native")
    message(FATAL_ERROR "go-source export produced a native artifact")
endif()

file(READ "${WORK}/go-source/sample_source.go" generated_source)
if(generated_source MATCHES "import[ \t]+\"C\"" OR generated_source MATCHES "purego")
    message(FATAL_ERROR "go-source export depends on a native Go bridge")
endif()
foreach(signature IN ITEMS
        "type BoxI32 struct"
        "type BoxI322 struct"
        "func NewProfile("
        "func NewBoxI32("
        "func ProfileOrigin("
        "func (self BoxI32) Marker("
        "func (self *BoxI32) Set("
        "func (self Profile) Display("
        "func (self *Profile) AddScore("
        "func (self Wallet) Spend("
        "type Valued interface"
        "func (self *Counter) addTwice("
        "func (self OverridingTag) described(")
    string(FIND "${generated_source}" "${signature}" signature_offset)
    if(signature_offset EQUAL -1)
        message(FATAL_ERROR "go-source export omitted ${signature}")
    endif()
endforeach()
file(READ "${WORK}/go-source/foundation.pii.json" generated_interface)
string(FIND "${generated_interface}" "\"exports\":[]" source_exports)
if(source_exports EQUAL -1)
    message(FATAL_ERROR "go-source fixture unexpectedly depends on a C ABI export")
endif()

foreach(backend IN ITEMS c llvm)
    execute_process(
        COMMAND "${COMPILER}" run "${WORK}/source" --backend ${backend}
        RESULT_VARIABLE behavior_status
        OUTPUT_VARIABLE behavior_output
        ERROR_VARIABLE behavior_error
    )
    if(NOT behavior_status EQUAL 0)
        message(FATAL_ERROR
            "go-source fixture failed on the ${backend} backend:\n${behavior_output}${behavior_error}")
    endif()
    set(behavior_${backend} "${behavior_output}")
endforeach()
if(NOT behavior_c STREQUAL behavior_llvm)
    message(FATAL_ERROR
        "go-source fixture output differs between C and LLVM:\n${behavior_c}\n${behavior_llvm}")
endif()
if(behavior_c MATCHES "FAIL" OR NOT behavior_c MATCHES " ok\n")
    message(FATAL_ERROR "go-source fixture reported a failed check:\n${behavior_c}")
endif()
file(WRITE "${WORK}/go-source/behavior.out" "${behavior_c}")

file(COPY "${FIXTURE}" DESTINATION "${WORK}/go-source")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off CGO_ENABLED=0 GOPROXY=off
        "${GO_EXECUTABLE}" test ./...
    WORKING_DIRECTORY "${WORK}/go-source"
    RESULT_VARIABLE go_status
    OUTPUT_VARIABLE go_output
    ERROR_VARIABLE go_error
)
if(NOT go_status EQUAL 0)
    message(FATAL_ERROR "generated Go source package failed:\n${go_output}${go_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off CGO_ENABLED=0 GOPROXY=off
        "${GO_EXECUTABLE}" fmt ./...
    WORKING_DIRECTORY "${WORK}/go-source"
    RESULT_VARIABLE go_fmt_status
    OUTPUT_VARIABLE go_fmt_output
    ERROR_VARIABLE go_fmt_error
)
if(NOT go_fmt_status EQUAL 0 OR NOT go_fmt_output STREQUAL "")
    message(FATAL_ERROR
        "generated Go source package is not gofmt-clean:\n${go_fmt_output}${go_fmt_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off CGO_ENABLED=0 GOPROXY=off
        "${GO_EXECUTABLE}" vet ./...
    WORKING_DIRECTORY "${WORK}/go-source"
    RESULT_VARIABLE go_vet_status
    OUTPUT_VARIABLE go_vet_output
    ERROR_VARIABLE go_vet_error
)
if(NOT go_vet_status EQUAL 0)
    message(FATAL_ERROR "generated Go source package failed go vet:\n${go_vet_output}${go_vet_error}")
endif()

file(MAKE_DIRECTORY "${WORK}/repeat")
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/source"
        -o "${WORK}/repeat"
        --format go-source
    RESULT_VARIABLE repeat_status
    OUTPUT_VARIABLE repeat_output
    ERROR_VARIABLE repeat_error
)
if(NOT repeat_status EQUAL 0)
    message(FATAL_ERROR "cannot repeat go-source export:\n${repeat_output}${repeat_error}")
endif()
foreach(path IN ITEMS go.mod sample_source.go foundation.pii.json)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${WORK}/go-source/${path}" "${WORK}/repeat/${path}"
        RESULT_VARIABLE compare_status
    )
    if(NOT compare_status EQUAL 0)
        message(FATAL_ERROR "go-source export is not deterministic: ${path}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK}/unsupported-source" "${WORK}/unsupported")
file(COPY "${UNSUPPORTED_SOURCE}/" DESTINATION "${WORK}/unsupported-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/unsupported-source"
    RESULT_VARIABLE unsupported_resolve_status
    OUTPUT_VARIABLE unsupported_resolve_output
    ERROR_VARIABLE unsupported_resolve_error
)
if(NOT unsupported_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve unsupported go-source fixture:\n${unsupported_resolve_output}${unsupported_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/unsupported-source"
        -o "${WORK}/unsupported"
        --format go-source
    RESULT_VARIABLE unsupported_status
    OUTPUT_VARIABLE unsupported_output
    ERROR_VARIABLE unsupported_error
)
if(unsupported_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted an unsupported native package")
endif()
set(unsupported_diagnostics "${unsupported_output}${unsupported_error}")
if(NOT unsupported_diagnostics MATCHES "FDN4120" OR
   NOT unsupported_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "go-source rejection omitted its diagnostic or alternatives:\n${unsupported_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/runtime-source" "${WORK}/runtime-output")
file(COPY "${RUNTIME_SOURCE}/" DESTINATION "${WORK}/runtime-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/runtime-source"
    RESULT_VARIABLE runtime_resolve_status
    OUTPUT_VARIABLE runtime_resolve_output
    ERROR_VARIABLE runtime_resolve_error
)
if(NOT runtime_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve runtime go-source fixture:\n${runtime_resolve_output}${runtime_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/runtime-source"
        -o "${WORK}/runtime-output"
        --format go-source
    RESULT_VARIABLE runtime_status
    OUTPUT_VARIABLE runtime_output
    ERROR_VARIABLE runtime_error
)
if(runtime_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted an unsupported task")
endif()
set(runtime_diagnostics "${runtime_output}${runtime_error}")
if(NOT runtime_diagnostics MATCHES "FDN4120" OR
   NOT runtime_diagnostics MATCHES "unsupported function" OR
   NOT runtime_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "runtime rejection omitted its contract or alternatives:\n${runtime_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/escape-source" "${WORK}/escape-output")
file(COPY "${ESCAPE_SOURCE}/" DESTINATION "${WORK}/escape-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/escape-source"
    RESULT_VARIABLE escape_resolve_status
    OUTPUT_VARIABLE escape_resolve_output
    ERROR_VARIABLE escape_resolve_error
)
if(NOT escape_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve conditional escape fixture:\n${escape_resolve_output}${escape_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/escape-source"
        -o "${WORK}/escape-output"
        --format go-source
    RESULT_VARIABLE escape_status
    OUTPUT_VARIABLE escape_output
    ERROR_VARIABLE escape_error
)
if(escape_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted a match arm that escapes from a call argument")
endif()
set(escape_diagnostics "${escape_output}${escape_error}")
if(NOT escape_diagnostics MATCHES "FDN4120" OR
   NOT escape_diagnostics MATCHES "only when the expression initializes or assigns a local" OR
   NOT escape_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "conditional escape rejection omitted its contract or alternatives:\n${escape_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/owner-cycle-source" "${WORK}/owner-cycle-output")
file(COPY "${OWNER_CYCLE_SOURCE}/" DESTINATION "${WORK}/owner-cycle-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/owner-cycle-source"
    RESULT_VARIABLE owner_cycle_resolve_status
    OUTPUT_VARIABLE owner_cycle_resolve_output
    ERROR_VARIABLE owner_cycle_resolve_error
)
if(NOT owner_cycle_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve owner cycle fixture:\n${owner_cycle_resolve_output}${owner_cycle_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/owner-cycle-source"
        -o "${WORK}/owner-cycle-output"
        --format go-source
    RESULT_VARIABLE owner_cycle_status
    OUTPUT_VARIABLE owner_cycle_output
    ERROR_VARIABLE owner_cycle_error
)
if(owner_cycle_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted an owner cycle outside an enum payload")
endif()
set(owner_cycle_diagnostics "${owner_cycle_output}${owner_cycle_error}")
if(NOT owner_cycle_diagnostics MATCHES "FDN4120" OR
   NOT owner_cycle_diagnostics MATCHES "unless an owned enum payload closes the cycle" OR
   NOT owner_cycle_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "owner cycle rejection omitted its contract or alternatives:\n${owner_cycle_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/open-generic-source" "${WORK}/open-generic-output")
file(COPY "${OPEN_GENERIC_SOURCE}/" DESTINATION "${WORK}/open-generic-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/open-generic-source"
    RESULT_VARIABLE open_generic_resolve_status
    OUTPUT_VARIABLE open_generic_resolve_output
    ERROR_VARIABLE open_generic_resolve_error
)
if(NOT open_generic_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve open generic fixture:\n${open_generic_resolve_output}${open_generic_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/open-generic-source"
        -o "${WORK}/open-generic-output"
        --format go-source
    RESULT_VARIABLE open_generic_status
    OUTPUT_VARIABLE open_generic_output
    ERROR_VARIABLE open_generic_error
)
if(open_generic_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted an open generic API")
endif()
set(open_generic_diagnostics "${open_generic_output}${open_generic_error}")
if(NOT open_generic_diagnostics MATCHES "FDN4120" OR
   NOT open_generic_diagnostics MATCHES "cannot expose open generic function" OR
   NOT open_generic_diagnostics MATCHES "non-generic exported wrapper" OR
   NOT open_generic_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "open generic rejection omitted its contract or alternatives:\n${open_generic_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/generic-struct-source" "${WORK}/generic-struct-output")
file(COPY "${GENERIC_STRUCT_SOURCE}/" DESTINATION "${WORK}/generic-struct-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/generic-struct-source"
    RESULT_VARIABLE generic_struct_resolve_status
    OUTPUT_VARIABLE generic_struct_resolve_output
    ERROR_VARIABLE generic_struct_resolve_error
)
if(NOT generic_struct_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve generic struct fixture:\n${generic_struct_resolve_output}${generic_struct_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/generic-struct-source"
        -o "${WORK}/generic-struct-output"
        --format go-source
    RESULT_VARIABLE generic_struct_status
    OUTPUT_VARIABLE generic_struct_output
    ERROR_VARIABLE generic_struct_error
)
if(generic_struct_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted a generic struct with custom drop")
endif()
set(generic_struct_diagnostics "${generic_struct_output}${generic_struct_error}")
if(NOT generic_struct_diagnostics MATCHES "FDN4120" OR
   NOT generic_struct_diagnostics MATCHES "cannot translate struct.*guarded" OR
   NOT generic_struct_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "generic custom-drop rejection omitted its contract or alternatives:\n${generic_struct_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/multiple-source" "${WORK}/multiple-output")
file(COPY "${MULTIPLE_SOURCE}/" DESTINATION "${WORK}/multiple-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/multiple-source"
    RESULT_VARIABLE multiple_resolve_status
    OUTPUT_VARIABLE multiple_resolve_output
    ERROR_VARIABLE multiple_resolve_error
)
if(NOT multiple_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve multiple rejection fixture:\n${multiple_resolve_output}${multiple_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/multiple-source"
        -o "${WORK}/multiple-output"
        --format go-source
    RESULT_VARIABLE multiple_status
    OUTPUT_VARIABLE multiple_output
    ERROR_VARIABLE multiple_error
)
if(multiple_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted several unsupported constructs")
endif()
set(multiple_diagnostics "${multiple_output}${multiple_error}")
string(REGEX MATCHALL "error\\[FDN4120\\]" multiple_codes "${multiple_diagnostics}")
list(LENGTH multiple_codes multiple_count)
if(NOT multiple_count EQUAL 5 OR
   NOT multiple_diagnostics MATCHES "unless an owned enum payload closes the cycle" OR
   NOT multiple_diagnostics MATCHES "only when the expression initializes or assigns a local" OR
   NOT multiple_diagnostics MATCHES "panic only as a statement, return value, or branch value" OR
   NOT multiple_diagnostics MATCHES "function without a same-package body")
    message(FATAL_ERROR
        "go-source did not report every rejection in one run:\n${multiple_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/owned-contract-source" "${WORK}/owned-contract-output")
file(COPY "${OWNED_CONTRACT_SOURCE}/" DESTINATION "${WORK}/owned-contract-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/owned-contract-source"
    RESULT_VARIABLE owned_contract_resolve_status
    OUTPUT_VARIABLE owned_contract_resolve_output
    ERROR_VARIABLE owned_contract_resolve_error
)
if(NOT owned_contract_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve owned contract fixture:\n${owned_contract_resolve_output}${owned_contract_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/owned-contract-source"
        -o "${WORK}/owned-contract-output"
        --format go-source
    RESULT_VARIABLE owned_contract_status
    OUTPUT_VARIABLE owned_contract_output
    ERROR_VARIABLE owned_contract_error
)
if(owned_contract_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted an owned contract with an editing method")
endif()
set(owned_contract_diagnostics "${owned_contract_output}${owned_contract_error}")
if(NOT owned_contract_diagnostics MATCHES "FDN4120" OR
   NOT owned_contract_diagnostics MATCHES "would share the value its editing methods change" OR
   NOT owned_contract_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "owned contract rejection omitted its contract or alternatives:\n${owned_contract_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/contract-conflict-source" "${WORK}/contract-conflict-output")
file(COPY "${CONTRACT_CONFLICT_SOURCE}/" DESTINATION "${WORK}/contract-conflict-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/contract-conflict-source"
    RESULT_VARIABLE contract_conflict_resolve_status
    OUTPUT_VARIABLE contract_conflict_resolve_output
    ERROR_VARIABLE contract_conflict_resolve_error
)
if(NOT contract_conflict_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve contract conflict fixture:\n${contract_conflict_resolve_output}${contract_conflict_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/contract-conflict-source"
        -o "${WORK}/contract-conflict-output"
        --format go-source
    RESULT_VARIABLE contract_conflict_status
    OUTPUT_VARIABLE contract_conflict_output
    ERROR_VARIABLE contract_conflict_error
)
if(contract_conflict_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted two different default methods with one name")
endif()
set(contract_conflict_diagnostics "${contract_conflict_output}${contract_conflict_error}")
if(NOT contract_conflict_diagnostics MATCHES "FDN4120" OR
   NOT contract_conflict_diagnostics MATCHES "two different rank methods" OR
   NOT contract_conflict_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "contract conflict rejection omitted its contract or alternatives:\n${contract_conflict_diagnostics}")
endif()
