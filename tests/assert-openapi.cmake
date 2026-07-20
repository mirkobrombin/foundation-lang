if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED OR
   NOT DEFINED FIRST OR NOT DEFINED SECOND OR NOT DEFINED OVERRIDE)
    message(FATAL_ERROR "OpenAPI assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-openapi "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first OpenAPI emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-openapi "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second OpenAPI emission failed:\n${second_output}${second_error}")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${FIRST}" first)
file(READ "${SECOND}" second)
if(NOT first STREQUAL expected)
    message(FATAL_ERROR "OpenAPI document does not match ${EXPECTED}")
endif()
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated OpenAPI emission produced different output")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${SOURCE}" --profile standard
    RESULT_VARIABLE lint_result
    OUTPUT_VARIABLE lint_output
    ERROR_VARIABLE lint_error
)
if(NOT lint_result EQUAL 0 OR lint_error MATCHES "\\.foundation\\.generated\\.fdn")
    message(FATAL_ERROR "OpenAPI project exposed generated source to lint:\n${lint_output}${lint_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-openapi "${SOURCE}" -o "${OVERRIDE}"
            --title "Users API" --version "9.8.7"
    RESULT_VARIABLE override_result
    OUTPUT_VARIABLE override_output
    ERROR_VARIABLE override_error
)
if(NOT override_result EQUAL 0)
    message(FATAL_ERROR "OpenAPI override emission failed:\n${override_output}${override_error}")
endif()
file(READ "${OVERRIDE}" overridden)
if(NOT overridden MATCHES "\\\"title\\\": \\\"Users API\\\"")
    message(FATAL_ERROR "OpenAPI title override was not emitted")
endif()
if(NOT overridden MATCHES "\\\"version\\\": \\\"9.8.7\\\"")
    message(FATAL_ERROR "OpenAPI version override was not emitted")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-openapi "${SOURCE}" -o "${FIRST}.txt"
    RESULT_VARIABLE extension_result
    OUTPUT_VARIABLE extension_output
    ERROR_VARIABLE extension_error
)
if(NOT extension_result EQUAL 2 OR
   NOT extension_error MATCHES "OpenAPI output must use the \\.json extension")
    message(FATAL_ERROR "OpenAPI emission accepted a non-JSON output: ${extension_output}${extension_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-openapi "${SOURCE}" -o "${OVERRIDE}"
            --title "First" --title "Second"
    RESULT_VARIABLE duplicate_result
    OUTPUT_VARIABLE duplicate_output
    ERROR_VARIABLE duplicate_error
)
if(NOT duplicate_result EQUAL 2 OR NOT duplicate_error MATCHES "foundationc emit-openapi")
    message(FATAL_ERROR "OpenAPI emission accepted a duplicate option: ${duplicate_output}${duplicate_error}")
endif()

file(SHA256 "${FIRST}" openapi_hash)
message(STATUS "OpenAPI document is deterministic: ${openapi_hash}")
