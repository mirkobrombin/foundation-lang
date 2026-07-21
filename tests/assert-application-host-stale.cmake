if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT OR
   NOT DEFINED GENERATED OR NOT DEFINED EXPECTED_OUTPUT)
    message(FATAL_ERROR "stale application host assertion is missing an input")
endif()

file(REMOVE_RECURSE "${PROJECT}")
file(COPY "${SOURCE}/" DESTINATION "${PROJECT}")
file(REMOVE "${GENERATED}")

set(application "${PROJECT}/src/main.fn")
file(READ "${application}" source)
string(REPLACE
    "@web.Query(\"limit\") limit i32,"
    "@web.Query(\"limit\") limit i64,"
    changed
    "${source}"
)
if(changed STREQUAL source)
    message(FATAL_ERROR "stale application host fixture did not change the route signature")
endif()
file(WRITE "${application}" "${changed}")

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}"
    RESULT_VARIABLE derived_result
    OUTPUT_VARIABLE derived_output
    ERROR_VARIABLE derived_error
)
if(NOT derived_result EQUAL 0)
    message(FATAL_ERROR "changed route was not derived automatically:\n${derived_output}${derived_error}")
endif()
if(EXISTS "${GENERATED}")
    message(FATAL_ERROR "automatic host derivation wrote a project source")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE emit_result
    OUTPUT_VARIABLE emit_output
    ERROR_VARIABLE emit_error
)
if(NOT emit_result EQUAL 0)
    message(FATAL_ERROR "stale application host regeneration failed:\n${emit_output}${emit_error}")
endif()

file(READ "${GENERATED}" first)
string(FIND "${first}" ".I64(" parser)
if(parser EQUAL -1)
    message(FATAL_ERROR "regenerated application host did not use the changed route type")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-app-host "${PROJECT}" -o "${GENERATED}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "repeated stale application host emission failed:\n${second_output}${second_error}")
endif()
file(READ "${GENERATED}" second)
if(NOT second STREQUAL first)
    message(FATAL_ERROR "stale application host regeneration is not deterministic")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "regenerated application host did not pass check:\n${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${PROJECT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "regenerated application host did not run:\n${run_error}")
endif()
file(READ "${EXPECTED_OUTPUT}" expected_output)
if(NOT run_output STREQUAL expected_output)
    message(FATAL_ERROR "regenerated application output mismatch")
endif()

file(SHA256 "${GENERATED}" application_host_hash)
message(STATUS "changed route derived without project files: ${application_host_hash}")
