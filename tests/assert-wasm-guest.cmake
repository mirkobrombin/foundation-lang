if(NOT DEFINED CLANG OR NOT DEFINED LINKER OR NOT DEFINED SOURCE OR
   NOT DEFINED INCLUDE_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR
            "WebAssembly guest test requires CLANG, LINKER, SOURCE, INCLUDE_DIR, and OUTPUT")
endif()

set(link_options -Wl,--no-entry)
get_filename_component(linker_directory "${LINKER}" DIRECTORY)
if(NOT DEFINED EXPORT_MEMORY OR EXPORT_MEMORY)
    list(APPEND link_options -Wl,--export-memory)
else()
    list(APPEND link_options -Wl,--import-memory)
endif()

execute_process(
    COMMAND "${CLANG}"
        --target=wasm32-unknown-unknown
        "-B${linker_directory}"
        -std=c11
        -nostdlib
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        ${DEFINITIONS}
        -I "${INCLUDE_DIR}"
        ${link_options}
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
