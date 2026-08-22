if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED WORK OR
   NOT DEFINED ZIG_EXECUTABLE OR NOT DEFINED CARGO_EXECUTABLE OR
   NOT DEFINED GO_EXECUTABLE OR NOT DEFINED SYSTEM_NAME)
    message(FATAL_ERROR "package export test requires compiler, source, work, Zig, Cargo, and Go")
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
    message(FATAL_ERROR "cannot resolve package export fixture:\n${resolve_output}${resolve_error}")
endif()

set(native "${WORK}/source/native/libfuse/increment.c")
foreach(format IN ITEMS zig rust go-cgo go-dynamic)
    execute_process(
        COMMAND "${COMPILER}" package export "${WORK}/source"
            -o "${WORK}/${format}"
            --format "${format}"
            --native "${native}"
        RESULT_VARIABLE export_status
        OUTPUT_VARIABLE export_output
        ERROR_VARIABLE export_error
    )
    if(NOT export_status EQUAL 0)
        message(FATAL_ERROR "cannot export ${format} package:\n${export_output}${export_error}")
    endif()
endforeach()

foreach(path IN ITEMS
        "zig/build.zig"
        "zig/build.zig.zon"
        "zig/src/root.zig"
        "zig/foundation.pii.json"
        "rust/Cargo.toml"
        "rust/build.rs"
        "rust/src/lib.rs"
        "rust/foundation.pii.json"
        "go-cgo/go.mod"
        "go-cgo/sample_native.go"
        "go-cgo/foundation.pii.json"
        "go-dynamic/go.mod"
        "go-dynamic/go.sum"
        "go-dynamic/sample_native.go"
        "go-dynamic/foundation.pii.json")
    if(NOT EXISTS "${WORK}/${path}")
        message(FATAL_ERROR "package export did not produce ${path}")
    endif()
endforeach()

file(READ "${WORK}/zig/foundation.pii.json" zig_pii)
file(READ "${WORK}/rust/foundation.pii.json" rust_pii)
if(NOT zig_pii STREQUAL rust_pii)
    message(FATAL_ERROR "Zig and Rust package exports used different PII")
endif()
foreach(format IN ITEMS go-cgo go-dynamic)
    file(READ "${WORK}/${format}/foundation.pii.json" go_pii)
    if(NOT zig_pii STREQUAL go_pii)
        message(FATAL_ERROR "${format} package export used different PII")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK}/zig-consumer")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/fixtures/package-export/zig/"
     DESTINATION "${WORK}/zig-consumer")
execute_process(
    COMMAND "${ZIG_EXECUTABLE}" build
    WORKING_DIRECTORY "${WORK}/zig-consumer"
    RESULT_VARIABLE zig_status
    OUTPUT_VARIABLE zig_output
    ERROR_VARIABLE zig_error
)
if(NOT zig_status EQUAL 0)
    message(FATAL_ERROR "generated Zig module failed:\n${zig_output}${zig_error}")
endif()

file(MAKE_DIRECTORY "${WORK}/rust/tests")
file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/fixtures/package-export/rust/native.rs"
               "${WORK}/rust/tests/native.rs")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env CARGO_NET_OFFLINE=true "${CARGO_EXECUTABLE}" test
    WORKING_DIRECTORY "${WORK}/rust"
    RESULT_VARIABLE rust_status
    OUTPUT_VARIABLE rust_output
    ERROR_VARIABLE rust_error
)
if(NOT rust_status EQUAL 0)
    message(FATAL_ERROR "generated Rust crate failed:\n${rust_output}${rust_error}")
endif()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/fixtures/package-export/go-cgo/native_test.go"
     DESTINATION "${WORK}/go-cgo")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off CGO_ENABLED=1
        "${GO_EXECUTABLE}" test ./...
    WORKING_DIRECTORY "${WORK}/go-cgo"
    RESULT_VARIABLE go_cgo_status
    OUTPUT_VARIABLE go_cgo_output
    ERROR_VARIABLE go_cgo_error
)
if(NOT go_cgo_status EQUAL 0)
    message(FATAL_ERROR "generated Go cgo package failed:\n${go_cgo_output}${go_cgo_error}")
endif()

if(SYSTEM_NAME STREQUAL "Darwin")
    set(dynamic_library "${WORK}/go-dynamic/native/lib/libsample_native.dylib")
else()
    set(dynamic_library "${WORK}/go-dynamic/native/lib/libsample_native.so")
endif()
file(COPY "${CMAKE_CURRENT_LIST_DIR}/fixtures/package-export/go-dynamic/native_test.go"
     DESTINATION "${WORK}/go-dynamic")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off CGO_ENABLED=0
        FOUNDATION_LIBRARY=${dynamic_library} "${GO_EXECUTABLE}" test ./...
    WORKING_DIRECTORY "${WORK}/go-dynamic"
    RESULT_VARIABLE go_dynamic_status
    OUTPUT_VARIABLE go_dynamic_output
    ERROR_VARIABLE go_dynamic_error
)
if(NOT go_dynamic_status EQUAL 0)
    message(FATAL_ERROR "generated Go dynamic package failed:\n${go_dynamic_output}${go_dynamic_error}")
endif()

file(MAKE_DIRECTORY "${WORK}/repeat")
execute_process(
    COMMAND "${COMPILER}" package export "${WORK}/source"
        -o "${WORK}/repeat"
        --format rust
        --native "${native}"
    RESULT_VARIABLE repeat_status
    OUTPUT_VARIABLE repeat_output
    ERROR_VARIABLE repeat_error
)
if(NOT repeat_status EQUAL 0)
    message(FATAL_ERROR "cannot repeat Rust package export:\n${repeat_output}${repeat_error}")
endif()
foreach(path IN ITEMS Cargo.toml build.rs src/lib.rs foundation.pii.json)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${WORK}/rust/${path}" "${WORK}/repeat/${path}"
        RESULT_VARIABLE compare_status
    )
    if(NOT compare_status EQUAL 0)
        message(FATAL_ERROR "Rust package export is not deterministic: ${path}")
    endif()
endforeach()
