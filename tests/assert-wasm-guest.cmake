if(NOT DEFINED CLANG OR NOT DEFINED SOURCE OR NOT DEFINED INCLUDE_DIR OR
   NOT DEFINED OUTPUT)
    message(FATAL_ERROR "WebAssembly guest test requires CLANG, SOURCE, INCLUDE_DIR, and OUTPUT")
endif()

execute_process(
    COMMAND "${CLANG}"
        --target=wasm32-unknown-unknown
        -std=c11
        -nostdlib
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -I "${INCLUDE_DIR}"
        -Wl,--no-entry
        -Wl,--export-memory
        "${SOURCE}"
        -o "${OUTPUT}"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "WebAssembly guest compilation failed:\n${compile_output}${compile_error}")
endif()

file(READ "${OUTPUT}" wasm_magic OFFSET 0 LIMIT 8 HEX)
if(NOT wasm_magic STREQUAL "0061736d01000000")
    message(FATAL_ERROR "WebAssembly guest output has invalid magic: ${wasm_magic}")
endif()
