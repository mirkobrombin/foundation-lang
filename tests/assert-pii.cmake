file(REMOVE_RECURSE "${WORK}")
set(first_project "${WORK}/first/project")
set(second_project "${WORK}/second/nested/project")
file(MAKE_DIRECTORY "${first_project}" "${second_project}")
file(COPY "${SOURCE}/" DESTINATION "${first_project}")
file(COPY "${SOURCE}/" DESTINATION "${second_project}")

execute_process(
    COMMAND "${COMPILER}" package resolve "${first_project}"
    RESULT_VARIABLE resolve_result
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_result EQUAL 0)
    message(FATAL_ERROR "cannot resolve PII fixture:\n${resolve_output}${resolve_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" package resolve "${second_project}"
    RESULT_VARIABLE second_resolve_result
    OUTPUT_VARIABLE second_resolve_output
    ERROR_VARIABLE second_resolve_error
)
if(NOT second_resolve_result EQUAL 0)
    message(FATAL_ERROR "cannot resolve second PII fixture:\n${second_resolve_output}${second_resolve_error}")
endif()

set(first "${WORK}/first.json")
set(second "${WORK}/second.json")
execute_process(
    COMMAND "${COMPILER}" emit-pii "${first_project}" -o "${first}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "cannot emit PII fixture:\n${first_output}${first_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" emit-pii "${second_project}" -o "${second}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "cannot repeat PII emission:\n${second_output}${second_error}")
endif()

file(READ "${first}" interface)
file(READ "${second}" repeated)
if(NOT interface STREQUAL repeated)
    message(FATAL_ERROR "PII emission changes across checkout paths")
endif()
string(FIND "${interface}" "${WORK}" leaked_path)
if(NOT leaked_path EQUAL -1)
    message(FATAL_ERROR "PII output contains an absolute checkout path")
endif()
foreach(expected IN ITEMS
        "\"package\":\"sample.native\""
        "\"library\":\"sample_native\""
        "\"c_symbol\":\"sample_increment\""
        "\"c_symbol\":\"sample_callback\""
        "\"c_symbol\":\"sample_native_increment\""
        "\"c_symbol\":\"sample_native_double_start\""
        "\"protocol\":\"foundation_reactor_v1\""
        "\"lifetime\":\"once\""
        "\"context_handle\":\"foundation.reactor.operation\""
        "\"cancel_symbol\":\"sample_native_double_cancel\""
        "\"ecosystem\":\"c\""
        "\"identifier\":\"libfuse\""
        "\"canonical_sha256\":\"sha256:")
    string(FIND "${interface}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "PII output is missing ${expected}")
    endif()
endforeach()

set(reserved_manifest "${second_project}/foundation.package")
file(READ "${reserved_manifest}" reserved_source)
string(REPLACE "path native/libfuse" "registry default" reserved_source "${reserved_source}")
file(WRITE "${reserved_manifest}" "${reserved_source}")
execute_process(
    COMMAND "${COMPILER}" emit-pii "${second_project}" -o "${WORK}/reserved.json"
    RESULT_VARIABLE reserved_emit_result
    OUTPUT_VARIABLE reserved_emit_output
    ERROR_VARIABLE reserved_emit_error
)
if(reserved_emit_result EQUAL 0 OR
   NOT "${reserved_emit_output}${reserved_emit_error}" MATCHES "FDN4057")
    message(FATAL_ERROR "emit-pii accepted a reserved foreign resolver")
endif()
execute_process(
    COMMAND "${COMPILER}" build "${second_project}" -o "${WORK}/reserved-app"
    RESULT_VARIABLE reserved_build_result
    OUTPUT_VARIABLE reserved_build_output
    ERROR_VARIABLE reserved_build_error
)
if(reserved_build_result EQUAL 0 OR
   NOT "${reserved_build_output}${reserved_build_error}" MATCHES "FDN4057")
    message(FATAL_ERROR "build accepted a reserved foreign resolver")
endif()
