if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT OR
   NOT DEFINED GENERATED OR NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "application binding assertion is missing an input")
endif()

file(REMOVE_RECURSE "${PROJECT}")
file(COPY "${SOURCE}/" DESTINATION "${PROJECT}")

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first binding host emission failed:\n${first_output}${first_error}")
endif()
file(READ "${GENERATED}" first)

foreach(fragment IN ITEMS
        "methods Settings"
        "fn Bind("
        "fn BindSources("
        "sources.CopyInto(\n                &bindingValues,\n                \"path\",\n                \"id\",\n                \"SourceId\""
        "sources.CopyInto(\n                &bindingValues,\n                \"query\",\n                \"name\",\n                \"SourceName\""
        "sources.CopyInto(\n                &bindingValues,\n                \"header\",\n                \"X-Name\",\n                \"SourceName\""
        "bindingValues.Set(\"Limit\", \"10\")"
        "fn BindJSON("
        "foundationHost1.Parse"
        "foundationHost0.CopyJsonTextList"
        "self.Body.BindJSON(source)"
        "\"display_name\""
        "foundationHost2.I8"
        "foundationHost2.I16"
        "foundationHost2.I32"
        "foundationHost2.I64"
        "foundationHost2.Isize"
        "foundationHost2.U8"
        "foundationHost2.U16"
        "foundationHost2.U32"
        "foundationHost2.U64"
        "foundationHost2.Usize"
        "foundationHost2.F32"
        "foundationHost2.F64"
        "foundationHost3.Duration.Parse"
        "foundationHost0.Append(&self.Labels")
    string(FIND "${first}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "generated binding host is missing ${fragment}")
    endif()
endforeach()

string(FIND "${first}" "self.internal" private_field_position)
if(NOT private_field_position EQUAL -1)
    message(FATAL_ERROR "generated binding host exposes a package-internal field")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second binding host emission failed:\n${second_output}${second_error}")
endif()
file(READ "${GENERATED}" second)
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated binding host emission produced different output")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${PROJECT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "generated binding host did not run:\n${run_error}")
endif()
file(READ "${EXPECTED_OUTPUT}" expected_output)
if(NOT run_output STREQUAL expected_output)
    message(FATAL_ERROR "generated binding output mismatch:\n${run_output}")
endif()

file(SHA256 "${GENERATED}" binding_host_hash)
message(STATUS "generated binding host is executable and deterministic: ${binding_host_hash}")

if(DEFINED C_COMPILER AND DEFINED C_COMPILER_ID AND DEFINED GENERATED_C AND
   DEFINED OUTPUT AND DEFINED RUNTIME_SOURCE AND DEFINED RUNTIME_PARSE_SOURCE AND
   DEFINED RUNTIME_INCLUDE)
    set(SOURCE "${PROJECT}")
    set(GENERATED "${GENERATED_C}")
    set(EXPECTED "${EXPECTED_OUTPUT}")
    include("${CMAKE_CURRENT_LIST_DIR}/assert-run-allocations.cmake")
endif()
