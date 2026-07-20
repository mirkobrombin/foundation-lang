if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED INVALID OR
   NOT DEFINED WORK OR NOT DEFINED TARGET)
    message(FATAL_ERROR "lint test requires COMPILER, SOURCE, INVALID, WORK, and TARGET")
endif()

string(TOLOWER "${TARGET}" lock_target)
if(lock_target STREQUAL "darwin")
    set(lock_target "macos")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${SOURCE}/" DESTINATION "${WORK}")
file(WRITE "${WORK}/foundation.lock"
    "format foundation.lock/v1\n"
    "root lint.fixture 1.0.0\n"
    "target ${lock_target}\n"
)

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}"
    RESULT_VARIABLE strict_result
    OUTPUT_VARIABLE strict_stdout
    ERROR_VARIABLE strict_stderr
)
if(NOT strict_result EQUAL 1)
    message(FATAL_ERROR "strict manifest profile did not reject findings: ${strict_stdout}${strict_stderr}")
endif()
if(NOT strict_stderr MATCHES "warning\\[FCS1001\\]" OR
   NOT strict_stderr MATCHES "warning\\[FCS2001\\]" OR
   NOT strict_stderr MATCHES "exported method MissingMethod")
    message(FATAL_ERROR "strict profile omitted width or documentation findings: ${strict_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}" --profile standard
    RESULT_VARIABLE standard_result
    OUTPUT_VARIABLE standard_stdout
    ERROR_VARIABLE standard_stderr
)
if(NOT standard_result EQUAL 0)
    message(FATAL_ERROR "standard override rejected a compliant project: ${standard_stdout}${standard_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}" --profile valid
    RESULT_VARIABLE valid_result
    OUTPUT_VARIABLE valid_stdout
    ERROR_VARIABLE valid_stderr
)
if(NOT valid_result EQUAL 0)
    message(FATAL_ERROR "valid override rejected a valid project: ${valid_stdout}${valid_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}" --profile custom
    RESULT_VARIABLE profile_result
    OUTPUT_VARIABLE profile_stdout
    ERROR_VARIABLE profile_stderr
)
if(NOT profile_result EQUAL 2)
    message(FATAL_ERROR "lint accepted an unknown profile: ${profile_stdout}${profile_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${INVALID}" --profile valid
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(NOT invalid_result EQUAL 1 OR NOT invalid_stderr MATCHES "error\\[FDN")
    message(FATAL_ERROR "lint did not preserve compiler errors: ${invalid_stdout}${invalid_stderr}")
endif()
