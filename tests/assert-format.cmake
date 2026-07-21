file(READ "${EXPECTED}" expected)
file(READ "${SOURCE}" original)
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

execute_process(
    COMMAND "${COMPILER}" format "${SOURCE}"
    RESULT_VARIABLE stdout_status
    OUTPUT_VARIABLE stdout_contents
    ERROR_VARIABLE stdout_errors
)
if(NOT stdout_status EQUAL 0 OR NOT stdout_errors STREQUAL "" OR
   NOT stdout_contents STREQUAL expected)
    message(FATAL_ERROR "stdout formatter failed: ${stdout_status}\n${stdout_errors}")
endif()

execute_process(
    COMMAND "${COMPILER}" format --check "${SOURCE}"
    RESULT_VARIABLE check_status
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_errors
)
if(NOT check_status EQUAL 1 OR NOT check_errors STREQUAL "" OR check_output STREQUAL "")
    message(FATAL_ERROR "formatter check did not report unformatted source")
endif()

file(COPY_FILE "${SOURCE}" "${WORK}/main.fn" ONLY_IF_DIFFERENT)
execute_process(
    COMMAND "${COMPILER}" format --write "${WORK}"
    RESULT_VARIABLE write_status
    OUTPUT_VARIABLE write_output
    ERROR_VARIABLE write_errors
)
file(READ "${WORK}/main.fn" written)
if(NOT write_status EQUAL 0 OR NOT write_output STREQUAL "" OR
   NOT write_errors STREQUAL "" OR NOT written STREQUAL expected)
    message(FATAL_ERROR "formatter write failed: ${write_status}\n${write_errors}")
endif()

execute_process(
    COMMAND "${COMPILER}" format --check "${WORK}"
    RESULT_VARIABLE clean_status
    OUTPUT_VARIABLE clean_output
    ERROR_VARIABLE clean_errors
)
if(NOT clean_status EQUAL 0 OR NOT clean_output STREQUAL "" OR NOT clean_errors STREQUAL "")
    message(FATAL_ERROR "formatter check rejected formatted project")
endif()

file(COPY_FILE "${INVALID}" "${WORK}/invalid.fn" ONLY_IF_DIFFERENT)
file(READ "${WORK}/invalid.fn" invalid_original)
execute_process(
    COMMAND "${COMPILER}" format --write "${WORK}/invalid.fn"
    RESULT_VARIABLE invalid_status
    OUTPUT_VARIABLE invalid_output
    ERROR_VARIABLE invalid_errors
)
file(READ "${WORK}/invalid.fn" invalid_written)
if(NOT invalid_status EQUAL 1 OR invalid_errors STREQUAL "" OR
   NOT invalid_original STREQUAL invalid_written)
    message(FATAL_ERROR "formatter changed invalid source")
endif()

file(COPY_FILE "${SOURCE}" "${WORK}/main.fn" ONLY_IF_DIFFERENT)
execute_process(
    COMMAND "${COMPILER}" format --write "${WORK}"
    RESULT_VARIABLE project_invalid_status
    OUTPUT_VARIABLE project_invalid_output
    ERROR_VARIABLE project_invalid_errors
)
file(READ "${WORK}/main.fn" project_valid_after)
if(NOT project_invalid_status EQUAL 1 OR project_invalid_errors STREQUAL "" OR
   NOT project_valid_after STREQUAL original)
    message(FATAL_ERROR "formatter partially changed an invalid project")
endif()

file(REMOVE "${WORK}/invalid.fn")
file(COPY_FILE "${SOURCE}" "${WORK}/main.fn" ONLY_IF_DIFFERENT)
file(CREATE_LINK "${SOURCE}" "${WORK}/z-link.fn" SYMBOLIC RESULT link_result)
if(link_result STREQUAL "0")
    execute_process(
        COMMAND "${COMPILER}" format --write "${WORK}"
        RESULT_VARIABLE link_status
        OUTPUT_VARIABLE link_output
        ERROR_VARIABLE link_errors
    )
    file(READ "${WORK}/main.fn" link_project_after)
    if(NOT link_status EQUAL 1 OR link_errors STREQUAL "" OR
       NOT link_project_after STREQUAL original)
        message(FATAL_ERROR "formatter partially changed a project containing a symbolic link")
    endif()
    file(REMOVE "${WORK}/z-link.fn")
endif()

file(REMOVE "${WORK}/main.fn")
file(COPY_FILE "${SOURCE}" "${WORK}/z.fn" ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE}" "${WORK}/a.fn" ONLY_IF_DIFFERENT)
execute_process(
    COMMAND "${COMPILER}" format --check "${WORK}"
    RESULT_VARIABLE ordered_status
    OUTPUT_VARIABLE ordered_output
    ERROR_VARIABLE ordered_errors
)
set(expected_order "${WORK}/a.fn\n${WORK}/z.fn\n")
if(NOT ordered_status EQUAL 1 OR NOT ordered_errors STREQUAL "" OR
   NOT ordered_output STREQUAL expected_order)
    message(FATAL_ERROR "formatter check output is not deterministic")
endif()

file(READ "${SOURCE}" source_after)
if(NOT source_after STREQUAL original)
    message(FATAL_ERROR "formatter modified the source fixture")
endif()
