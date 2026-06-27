if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT OR
   NOT DEFINED GENERATED OR NOT DEFINED EXPECTED OR NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "application host assertion is missing an input")
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
    message(FATAL_ERROR "first application host emission failed:\n${first_output}${first_error}")
endif()
file(READ "${GENERATED}" first)

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second application host emission failed:\n${second_output}${second_error}")
endif()

file(READ "${EXPECTED}" expected)
file(READ "${GENERATED}" second)
if(NOT first STREQUAL expected)
    message(FATAL_ERROR "application host does not match ${EXPECTED}")
endif()
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated application host emission produced different output")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${PROJECT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "generated application host did not run:\n${run_error}")
endif()
file(READ "${EXPECTED_OUTPUT}" expected_output)
if(NOT run_output STREQUAL expected_output)
    message(FATAL_ERROR "generated application output mismatch")
endif()

set(protected "${PROJECT}/src/manual.fdn")
file(WRITE "${protected}" "package example.services\n\nfn Manual() void {}\n")
execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${protected}"
    RESULT_VARIABLE protected_result
    OUTPUT_VARIABLE protected_output
    ERROR_VARIABLE protected_error
)
if(protected_result EQUAL 0)
    message(FATAL_ERROR "application host replaced a hand-written source")
endif()
file(READ "${protected}" protected_contents)
if(NOT protected_contents STREQUAL "package example.services\n\nfn Manual() void {}\n")
    message(FATAL_ERROR "application host changed a hand-written source")
endif()

file(SHA256 "${GENERATED}" application_host_hash)
message(STATUS "application host is executable and deterministic: ${application_host_hash}")
