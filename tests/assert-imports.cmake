file(READ "${EXPECTED}" expected)
file(READ "${SOURCE}" original)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
    COMMAND "${COMPILER}" imports "${SOURCE}"
    RESULT_VARIABLE stdout_status
    OUTPUT_VARIABLE stdout_contents
    ERROR_VARIABLE stdout_errors
)
if(NOT stdout_status EQUAL 0 OR NOT stdout_errors STREQUAL "" OR
   NOT stdout_contents STREQUAL expected)
    message(FATAL_ERROR "stdout import organization failed: ${stdout_status}\n${stdout_errors}")
endif()

execute_process(
    COMMAND "${COMPILER}" imports --check "${SOURCE}"
    RESULT_VARIABLE check_status
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_errors
)
if(NOT check_status EQUAL 1 OR NOT check_errors STREQUAL "" OR check_output STREQUAL "")
    message(FATAL_ERROR "import check did not report changes")
endif()

file(COPY_FILE "${SOURCE}" "${WORK}/main.fn" ONLY_IF_DIFFERENT)
execute_process(
    COMMAND "${COMPILER}" imports --write "${WORK}"
    RESULT_VARIABLE write_status
    OUTPUT_VARIABLE write_output
    ERROR_VARIABLE write_errors
)
file(READ "${WORK}/main.fn" written)
if(NOT write_status EQUAL 0 OR NOT write_output STREQUAL "" OR
   NOT write_errors STREQUAL "" OR NOT written STREQUAL expected)
    message(FATAL_ERROR "import write failed: ${write_status}\n${write_errors}")
endif()

execute_process(
    COMMAND "${COMPILER}" imports --check "${WORK}"
    RESULT_VARIABLE clean_status
    OUTPUT_VARIABLE clean_output
    ERROR_VARIABLE clean_errors
)
if(NOT clean_status EQUAL 0 OR NOT clean_output STREQUAL "" OR NOT clean_errors STREQUAL "")
    message(FATAL_ERROR "import check rejected organized source")
endif()

file(READ "${SOURCE}" source_after)
if(NOT source_after STREQUAL original)
    message(FATAL_ERROR "import organization modified the source fixture")
endif()
