if(NOT DEFINED COMPILER OR NOT DEFINED GO_EXECUTABLE OR NOT DEFINED GIT_EXECUTABLE OR
   NOT DEFINED V2_SOURCE OR NOT DEFINED FOUNDATION_RUNNER OR NOT DEFINED GO_RUNNER OR
   NOT DEFINED WORK)
    message(FATAL_ERROR "scheduler compatibility assertion is missing an input")
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
    COMMAND "${GIT_EXECUTABLE}" -C "${V2_SOURCE}" diff --quiet -- core/scheduler go.mod
    RESULT_VARIABLE dirty_result
)
if(NOT dirty_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 scheduler baseline has local changes")
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

execute_process(
    COMMAND "${COMPILER}" run "${FOUNDATION_RUNNER}" -- "${WORK}/foundation-store"
    RESULT_VARIABLE foundation_result
    OUTPUT_VARIABLE foundation_output
    ERROR_VARIABLE foundation_error
)
if(NOT foundation_result EQUAL 0)
    message(FATAL_ERROR
        "Foundation scheduler fixture failed:\n${foundation_output}${foundation_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off GOFLAGS=-mod=mod
        "FOUNDATION_SCHEDULER_STORE=${WORK}/go-store"
        "${GO_EXECUTABLE}" run -race .
    WORKING_DIRECTORY "${WORK}/go"
    RESULT_VARIABLE go_result
    OUTPUT_VARIABLE go_output
    ERROR_VARIABLE go_error
)
if(NOT go_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 scheduler fixture failed:\n${go_error}")
endif()

if(NOT foundation_output STREQUAL go_output)
    message(FATAL_ERROR
        "Foundation scheduler output differs from v2:\n"
        "Foundation:\n${foundation_output}v2:\n${go_output}")
endif()

message(STATUS "Foundation scheduler matches the v2 contract at ${v2_revision}")
