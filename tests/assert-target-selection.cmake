if(NOT DEFINED COMPILER OR NOT DEFINED PROJECT OR NOT DEFINED WORK)
    message(FATAL_ERROR "target selection assertion is missing an input")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}" --target macos
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "macOS target check failed: ${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${PROJECT}" -o "${WORK}/program.c" --target macos
    RESULT_VARIABLE c_result
    OUTPUT_VARIABLE c_output
    ERROR_VARIABLE c_error
)
if(NOT c_result EQUAL 0)
    message(FATAL_ERROR "macOS C emission failed: ${c_output}${c_error}")
endif()
file(READ "${WORK}/program.c" generated_c)
if(NOT generated_c MATCHES "macos-only" OR generated_c MATCHES "linux-only")
    message(FATAL_ERROR "C emission did not select only the macOS declaration")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c-header "${PROJECT}" -o "${WORK}/program.h"
            --target macos
    RESULT_VARIABLE header_result
    OUTPUT_VARIABLE header_output
    ERROR_VARIABLE header_error
)
if(NOT header_result EQUAL 0)
    message(FATAL_ERROR "macOS header emission failed: ${header_output}${header_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-metadata "${PROJECT}" -o "${WORK}/program.json"
            --target macos
    RESULT_VARIABLE metadata_result
    OUTPUT_VARIABLE metadata_output
    ERROR_VARIABLE metadata_error
)
if(NOT metadata_result EQUAL 0)
    message(FATAL_ERROR
        "macOS metadata emission failed: ${metadata_output}${metadata_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}" --target linux
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0 OR NOT mismatch_error MATCHES "FDN4111" OR
   NOT mismatch_error MATCHES "macos does not match build target linux")
    message(FATAL_ERROR "lock target mismatch was not rejected: ${mismatch_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}" --target unknown
    RESULT_VARIABLE unknown_result
    OUTPUT_VARIABLE unknown_output
    ERROR_VARIABLE unknown_error
)
if(NOT unknown_result EQUAL 2 OR NOT unknown_error MATCHES "usage:")
    message(FATAL_ERROR "unknown target was not rejected by the CLI: ${unknown_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${PROJECT}" --target
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 2 OR NOT missing_error MATCHES "usage:")
    message(FATAL_ERROR "missing target value was not rejected by the CLI: ${missing_error}")
endif()
