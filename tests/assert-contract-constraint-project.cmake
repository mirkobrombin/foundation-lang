if(NOT DEFINED COMPILER OR NOT DEFINED FIXTURE OR NOT DEFINED WORK)
    message(FATAL_ERROR "contract constraint project assertion is missing an input")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${FIXTURE}/" DESTINATION "${WORK}/fixture")

set(project "${WORK}/fixture/app")
execute_process(
    COMMAND "${COMPILER}" package resolve "${project}"
    RESULT_VARIABLE resolve_result
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_result EQUAL 0)
    message(FATAL_ERROR "constraint package resolution failed:\n${resolve_output}${resolve_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${project}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "constraint package run failed:\n${run_output}${run_error}")
endif()

file(READ "${WORK}/fixture/expected.out" expected)
if(NOT run_output STREQUAL expected)
    message(FATAL_ERROR "constraint package output mismatch")
endif()
