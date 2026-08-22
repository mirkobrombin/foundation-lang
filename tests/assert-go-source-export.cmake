if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED UNSUPPORTED_SOURCE OR
   NOT DEFINED RUNTIME_SOURCE OR NOT DEFINED OWN_SOURCE OR NOT DEFINED ESCAPE_SOURCE OR
   NOT DEFINED OWNER_DESTRUCTURE_SOURCE OR NOT DEFINED OPEN_GENERIC_SOURCE OR
   NOT DEFINED GENERIC_STRUCT_SOURCE OR
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
        "func (self *Profile) AddScore(")
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

file(MAKE_DIRECTORY "${WORK}/own-source" "${WORK}/own-output")
file(COPY "${OWN_SOURCE}/" DESTINATION "${WORK}/own-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/own-source"
    RESULT_VARIABLE own_resolve_status
    OUTPUT_VARIABLE own_resolve_output
    ERROR_VARIABLE own_resolve_error
)
if(NOT own_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve consuming receiver fixture:\n${own_resolve_output}${own_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/own-source"
        -o "${WORK}/own-output"
        --format go-source
    RESULT_VARIABLE own_status
    OUTPUT_VARIABLE own_output
    ERROR_VARIABLE own_error
)
if(own_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted a consuming method receiver")
endif()
set(own_diagnostics "${own_output}${own_error}")
if(NOT own_diagnostics MATCHES "FDN4120" OR
   NOT own_diagnostics MATCHES "consuming method receiver" OR
   NOT own_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "receiver rejection omitted its contract or alternatives:\n${own_diagnostics}")
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
    message(FATAL_ERROR "go-source accepted a conditional branch that escapes its outer scope")
endif()
set(escape_diagnostics "${escape_output}${escape_error}")
if(NOT escape_diagnostics MATCHES "FDN4120" OR
   NOT escape_diagnostics MATCHES "conditional branches cannot return or escape an outer loop" OR
   NOT escape_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "conditional escape rejection omitted its contract or alternatives:\n${escape_diagnostics}")
endif()

file(MAKE_DIRECTORY "${WORK}/owner-destructure-source" "${WORK}/owner-destructure-output")
file(COPY "${OWNER_DESTRUCTURE_SOURCE}/" DESTINATION "${WORK}/owner-destructure-source")
execute_process(
    COMMAND "${COMPILER}" package resolve "${WORK}/owner-destructure-source"
    RESULT_VARIABLE owner_destructure_resolve_status
    OUTPUT_VARIABLE owner_destructure_resolve_output
    ERROR_VARIABLE owner_destructure_resolve_error
)
if(NOT owner_destructure_resolve_status EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve owner destructuring fixture:\n${owner_destructure_resolve_output}${owner_destructure_resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/owner-destructure-source"
        -o "${WORK}/owner-destructure-output"
        --format go-source
    RESULT_VARIABLE owner_destructure_status
    OUTPUT_VARIABLE owner_destructure_output
    ERROR_VARIABLE owner_destructure_error
)
if(owner_destructure_status EQUAL 0)
    message(FATAL_ERROR "go-source accepted owner destructuring")
endif()
set(owner_destructure_diagnostics
    "${owner_destructure_output}${owner_destructure_error}")
if(NOT owner_destructure_diagnostics MATCHES "FDN4120" OR
   NOT owner_destructure_diagnostics MATCHES "cannot preserve owner destructuring" OR
   NOT owner_destructure_diagnostics MATCHES "go-cgo or go-dynamic")
    message(FATAL_ERROR
        "owner destructuring rejection omitted its contract or alternatives:\n${owner_destructure_diagnostics}")
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
