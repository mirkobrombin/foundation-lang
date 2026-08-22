if(NOT DEFINED COMPILER OR NOT DEFINED GO_EXECUTABLE OR NOT DEFINED GIT_EXECUTABLE OR
   NOT DEFINED V2_SOURCE OR NOT DEFINED FOUNDATION_RUNNER OR NOT DEFINED GO_RUNNER OR
   NOT DEFINED WORK)
    message(FATAL_ERROR "OpenAPI compatibility assertion is missing an input")
endif()

set(v2_revision "06679f06495151fbd0d491e76121ba98b939a291")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${V2_SOURCE}" rev-parse HEAD
    RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE actual_revision
    ERROR_VARIABLE revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT revision_result EQUAL 0)
    message(FATAL_ERROR "cannot read Foundation v2 revision:\n${revision_error}")
endif()
if(NOT actual_revision STREQUAL v2_revision)
    message(FATAL_ERROR
        "Foundation v2 revision is ${actual_revision}, expected ${v2_revision}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${V2_SOURCE}" diff --quiet -- core/openapi go.mod
    RESULT_VARIABLE dirty_result
)
if(NOT dirty_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 OpenAPI baseline has local changes")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/go")
configure_file("${GO_RUNNER}/go.mod" "${WORK}/go/go.mod" COPYONLY)
configure_file("${GO_RUNNER}/main.go" "${WORK}/go/main.go" COPYONLY)

file(TO_CMAKE_PATH "${V2_SOURCE}" v2_go_path)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off
        "${GO_EXECUTABLE}" mod edit
        "-replace=github.com/mirkobrombin/go-foundation/v2=${v2_go_path}"
    WORKING_DIRECTORY "${WORK}/go"
    RESULT_VARIABLE edit_result
    OUTPUT_VARIABLE edit_output
    ERROR_VARIABLE edit_error
)
if(NOT edit_result EQUAL 0)
    message(FATAL_ERROR "cannot bind Foundation v2 source:\n${edit_output}${edit_error}")
endif()

set(foundation_document "${WORK}/foundation.json")
execute_process(
    COMMAND "${COMPILER}" emit-openapi "${FOUNDATION_RUNNER}" -o "${foundation_document}"
    RESULT_VARIABLE foundation_result
    OUTPUT_VARIABLE foundation_output
    ERROR_VARIABLE foundation_error
)
if(NOT foundation_result EQUAL 0)
    message(FATAL_ERROR
        "Foundation OpenAPI fixture failed:\n${foundation_output}${foundation_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off GOFLAGS=-mod=mod
        "${GO_EXECUTABLE}" run .
    WORKING_DIRECTORY "${WORK}/go"
    RESULT_VARIABLE go_result
    OUTPUT_VARIABLE go_document
    ERROR_VARIABLE go_error
)
if(NOT go_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 OpenAPI fixture failed:\n${go_error}")
endif()

file(READ "${foundation_document}" foundation_json)

foreach(field IN ITEMS openapi)
    string(JSON foundation_value GET "${foundation_json}" "${field}")
    string(JSON go_value GET "${go_document}" "${field}")
    if(NOT foundation_value STREQUAL go_value)
        message(FATAL_ERROR "OpenAPI field ${field} differs")
    endif()
endforeach()
foreach(field IN ITEMS title version)
    string(JSON foundation_value GET "${foundation_json}" info "${field}")
    string(JSON go_value GET "${go_document}" info "${field}")
    if(NOT foundation_value STREQUAL go_value)
        message(FATAL_ERROR "OpenAPI info field ${field} differs")
    endif()
endforeach()
foreach(field IN ITEMS summary description)
    string(JSON foundation_value GET "${foundation_json}" paths "/users/{id}" get "${field}")
    string(JSON go_value GET "${go_document}" paths "/users/{id}" get "${field}")
    if(NOT foundation_value STREQUAL go_value)
        message(FATAL_ERROR "OpenAPI operation field ${field} differs")
    endif()
endforeach()
foreach(index IN ITEMS 0 1 2)
    foreach(field IN ITEMS name in)
        string(JSON foundation_value GET "${foundation_json}" paths "/users/{id}" get
            parameters ${index} "${field}")
        string(JSON go_value GET "${go_document}" paths "/users/{id}" get
            parameters ${index} "${field}")
        if(NOT foundation_value STREQUAL go_value)
            message(FATAL_ERROR "OpenAPI parameter ${index} field ${field} differs")
        endif()
    endforeach()
    string(JSON foundation_type GET "${foundation_json}" paths "/users/{id}" get
        parameters ${index} schema type)
    string(JSON go_type GET "${go_document}" paths "/users/{id}" get
        parameters ${index} schema type)
    if(NOT foundation_type STREQUAL go_type)
        message(FATAL_ERROR "OpenAPI parameter ${index} schema type differs")
    endif()
endforeach()
foreach(status IN ITEMS 200 404)
    string(JSON foundation_value GET "${foundation_json}" paths "/users/{id}" get
        responses "${status}" description)
    string(JSON go_value GET "${go_document}" paths "/users/{id}" get
        responses "${status}" description)
    if(NOT foundation_value STREQUAL go_value)
        message(FATAL_ERROR "OpenAPI response ${status} differs")
    endif()
endforeach()

string(JSON foundation_path_required GET "${foundation_json}" paths "/users/{id}" get
    parameters 0 required)
string(JSON go_path_required GET "${go_document}" paths "/users/{id}" get
    parameters 0 required)
if(NOT foundation_path_required OR NOT go_path_required)
    message(FATAL_ERROR "OpenAPI path parameters must remain required")
endif()
string(JSON foundation_query_required GET "${foundation_json}" paths "/users/{id}" get
    parameters 1 required)
string(JSON go_query_required GET "${go_document}" paths "/users/{id}" get
    parameters 1 required)
if(NOT foundation_query_required OR go_query_required)
    message(FATAL_ERROR
        "Foundation must document its required query binding instead of copying v2 optionality")
endif()

message(STATUS "Foundation OpenAPI matches the v2 contract at ${v2_revision}")
