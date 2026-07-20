if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED OR NOT DEFINED WORK)
    message(FATAL_ERROR "documentation test requires COMPILER, SOURCE, EXPECTED, and WORK")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(FIRST "${WORK}/reference.md")
set(SECOND "${WORK}/reference-second.md")

execute_process(
    COMMAND "${COMPILER}" documentation "${SOURCE}" -o "${FIRST}"
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_stdout
    ERROR_VARIABLE first_stderr
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "documentation failed: ${first_stdout}${first_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" documentation "${SOURCE}" -o "${SECOND}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_stdout
    ERROR_VARIABLE second_stderr
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second documentation run failed: ${second_stdout}${second_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${FIRST}" "${SECOND}"
    RESULT_VARIABLE deterministic_result
)
if(NOT deterministic_result EQUAL 0)
    message(FATAL_ERROR "documentation output is not deterministic")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${FIRST}" "${EXPECTED}"
    RESULT_VARIABLE expected_result
)
if(NOT expected_result EQUAL 0)
    message(FATAL_ERROR "documentation output differs from expected reference")
endif()

execute_process(
    COMMAND "${COMPILER}" documentation "${SOURCE}" -o "${WORK}/reference.txt"
    RESULT_VARIABLE extension_result
    OUTPUT_VARIABLE extension_stdout
    ERROR_VARIABLE extension_stderr
)
if(NOT extension_result EQUAL 2)
    message(FATAL_ERROR
        "documentation accepted a non-Markdown output: ${extension_stdout}${extension_stderr}"
    )
endif()
