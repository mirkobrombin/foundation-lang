if(NOT DEFINED COMPILER OR NOT DEFINED GO_EXECUTABLE OR NOT DEFINED GIT_EXECUTABLE OR
   NOT DEFINED V2_SOURCE OR NOT DEFINED FOUNDATION_RUNNER OR NOT DEFINED GO_RUNNER OR
   NOT DEFINED CONFIG OR NOT DEFINED EXPECTED OR NOT DEFINED WORK)
    message(FATAL_ERROR "configuration compatibility assertion is missing an input")
endif()

set(v2_revision "06679f06495151fbd0d491e76121ba98b939a291")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${V2_SOURCE}" rev-parse HEAD
    RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE actual_revision
    ERROR_VARIABLE revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT revision_result EQUAL 0 OR NOT actual_revision STREQUAL v2_revision)
    message(FATAL_ERROR "Foundation v2 configuration revision mismatch: ${actual_revision}${revision_error}")
endif()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${V2_SOURCE}" diff --quiet --
        core/configuration go.mod
    RESULT_VARIABLE dirty_result
)
if(NOT dirty_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 configuration baseline has local changes")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/go")
configure_file("${GO_RUNNER}/go.mod" "${WORK}/go/go.mod" COPYONLY)
configure_file("${GO_RUNNER}/main.go" "${WORK}/go/main.go" COPYONLY)
file(TO_CMAKE_PATH "${V2_SOURCE}" v2_go_path)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off "${GO_EXECUTABLE}" mod edit
        "-replace=github.com/mirkobrombin/go-foundation/v2=${v2_go_path}"
    WORKING_DIRECTORY "${WORK}/go"
    RESULT_VARIABLE edit_result
    OUTPUT_VARIABLE edit_output
    ERROR_VARIABLE edit_error
)
if(NOT edit_result EQUAL 0)
    message(FATAL_ERROR "cannot bind Foundation v2 source:\n${edit_output}${edit_error}")
endif()

set(foundation_executable "${WORK}/foundation-configuration${EXECUTABLE_SUFFIX}")
execute_process(
    COMMAND "${COMPILER}" build "${FOUNDATION_RUNNER}" -o "${foundation_executable}"
    RESULT_VARIABLE foundation_build_result
    OUTPUT_VARIABLE foundation_build_output
    ERROR_VARIABLE foundation_build_error
)
if(NOT foundation_build_result EQUAL 0)
    message(FATAL_ERROR "cannot build Foundation configuration fixture:\n${foundation_build_output}${foundation_build_error}")
endif()
set(go_executable "${WORK}/go-configuration${EXECUTABLE_SUFFIX}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env GOWORK=off GOFLAGS=-mod=mod
        "${GO_EXECUTABLE}" build -o "${go_executable}" .
    WORKING_DIRECTORY "${WORK}/go"
    RESULT_VARIABLE go_build_result
    OUTPUT_VARIABLE go_build_output
    ERROR_VARIABLE go_build_error
)
if(NOT go_build_result EQUAL 0)
    message(FATAL_ERROR "cannot build Foundation v2 configuration fixture:\n${go_build_output}${go_build_error}")
endif()
execute_process(
    COMMAND "${foundation_executable}" "${CONFIG}" "${WORK}/missing.json"
    RESULT_VARIABLE foundation_result
    OUTPUT_VARIABLE foundation_output
    ERROR_VARIABLE foundation_error
)
if(NOT foundation_result EQUAL 0)
    message(FATAL_ERROR "Foundation configuration fixture failed:\n${foundation_output}${foundation_error}")
endif()
execute_process(
    COMMAND "${go_executable}" "${CONFIG}"
    RESULT_VARIABLE go_result
    OUTPUT_VARIABLE go_output
    ERROR_VARIABLE go_error
)
if(NOT go_result EQUAL 0)
    message(FATAL_ERROR "Foundation v2 configuration fixture failed:\n${go_output}${go_error}")
endif()
file(READ "${EXPECTED}" expected_output)
if(NOT foundation_output STREQUAL expected_output OR NOT go_output STREQUAL expected_output OR
   NOT foundation_output STREQUAL go_output)
    message(FATAL_ERROR "Foundation configuration output differs from Foundation v2")
endif()
message(STATUS "Foundation and Foundation v2 configuration behavior matches ${v2_revision}")
