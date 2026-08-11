if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED PROJECT)
    message(FATAL_ERROR "derived guard assertion is missing an input")
endif()

file(REMOVE_RECURSE "${PROJECT}")
file(COPY "${SOURCE}/" DESTINATION "${PROJECT}")
file(GLOB_RECURSE source_before RELATIVE "${PROJECT}" "${PROJECT}/*.fn")

execute_process(
    COMMAND "${COMPILER}" test "${PROJECT}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "compiler-derived guard failed:\n${test_output}${test_error}")
endif()

file(GLOB_RECURSE source_after RELATIVE "${PROJECT}" "${PROJECT}/*.fn")
if(NOT source_after STREQUAL source_before)
    message(FATAL_ERROR "compiler-derived guard changed the project source inventory")
endif()

message(STATUS "guard policy is synthesized without project files")
