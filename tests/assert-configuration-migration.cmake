if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT OR
   NOT DEFINED GENERATED OR NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "configuration migration assertion is missing an input")
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
    message(FATAL_ERROR "first configuration host emission failed:\n${first_output}${first_error}")
endif()
file(READ "${GENERATED}" first)

foreach(fragment IN ITEMS
        "methods Settings"
        "fn Bind("
        "fn BindJSON("
        "\"host\""
        "\"port\""
        ".U16("
        ".Parse(source)")
    string(FIND "${first}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "generated configuration host is missing ${fragment}")
    endif()
endforeach()

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second configuration host emission failed:\n${second_output}${second_error}")
endif()
file(READ "${GENERATED}" second)
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated configuration host emission produced different output")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${PROJECT}" -- "${PROJECT}/config.json" "${PROJECT}/missing.json"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "generated configuration host did not run:\n${run_error}")
endif()
file(READ "${EXPECTED_OUTPUT}" expected_output)
if(NOT run_output STREQUAL expected_output)
    message(FATAL_ERROR "generated configuration output mismatch:\n${run_output}")
endif()

if(DEFINED C_COMPILER AND DEFINED C_COMPILER_ID AND DEFINED GENERATED_C AND
   DEFINED OUTPUT AND DEFINED RUNTIME_SOURCE AND DEFINED RUNTIME_PARSE_SOURCE AND
   DEFINED RUNTIME_INCLUDE)
    set(SOURCE "${PROJECT}")
    set(GENERATED "${GENERATED_C}")
    set(EXPECTED "${EXPECTED_OUTPUT}")
    set(PROGRAM_ARGS "${PROJECT}/config.json;${PROJECT}/missing.json")
    include("${CMAKE_CURRENT_LIST_DIR}/assert-run-allocations.cmake")
endif()
