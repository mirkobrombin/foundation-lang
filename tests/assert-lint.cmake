if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED RULE_SOURCE OR
   NOT DEFINED VIOLATION_SOURCE OR NOT DEFINED INVALID OR NOT DEFINED WORK OR
   NOT DEFINED TARGET)
    message(FATAL_ERROR "lint test requires COMPILER, SOURCE, RULE_SOURCE, VIOLATION_SOURCE, INVALID, WORK, and TARGET")
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

file(MAKE_DIRECTORY "${WORK}/fcs-rules")
file(COPY "${RULE_SOURCE}/" DESTINATION "${WORK}/fcs-rules")
file(WRITE "${WORK}/fcs-rules/foundation.lock"
    "format foundation.lock/v1\n"
    "root fcs.rules 1.0.0\n"
    "target ${lock_target}\n"
)
execute_process(
    COMMAND "${COMPILER}" lint "${WORK}/fcs-rules"
    RESULT_VARIABLE suppression_result
    OUTPUT_VARIABLE suppression_stdout
    ERROR_VARIABLE suppression_stderr
)
if(NOT suppression_result EQUAL 0 OR suppression_stderr MATCHES "FCS1001")
    message(FATAL_ERROR "motivated FCS suppression did not suppress its next line: ${suppression_stdout}${suppression_stderr}")
endif()

file(MAKE_DIRECTORY "${WORK}/fcs-violations")
file(COPY "${VIOLATION_SOURCE}/" DESTINATION "${WORK}/fcs-violations")
file(WRITE "${WORK}/fcs-violations/foundation.lock"
    "format foundation.lock/v1\n"
    "root fcs.violations 1.0.0\n"
    "target ${lock_target}\n"
)
execute_process(
    COMMAND "${COMPILER}" lint "${WORK}/fcs-violations"
    RESULT_VARIABLE violations_result
    OUTPUT_VARIABLE violations_stdout
    ERROR_VARIABLE violations_stderr
)
if(NOT violations_result EQUAL 1)
    message(FATAL_ERROR "strict FCS violations were accepted: ${violations_stdout}${violations_stderr}")
endif()
foreach(rule IN ITEMS FCS1001 FCS1002 FCS2001 FCS2002 FCS3001 FCS4001 FCS5001 FCS6001 FCS7001 FCS7002 FCS7003 FCS7004 FCS9001)
    if(NOT violations_stderr MATCHES "${rule}")
        message(FATAL_ERROR "strict FCS fixture omitted ${rule}: ${violations_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}/fcs-violations" --profile valid
    RESULT_VARIABLE valid_suppression_result
    OUTPUT_VARIABLE valid_suppression_stdout
    ERROR_VARIABLE valid_suppression_stderr
)
if(NOT valid_suppression_result EQUAL 1 OR
   NOT valid_suppression_stderr MATCHES "FCS9001")
    message(FATAL_ERROR "Valid profile hid malformed suppression diagnostics: ${valid_suppression_stdout}${valid_suppression_stderr}")
endif()

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
    COMMAND "${COMPILER}" lint "${WORK}" --profile standard --rule FCS2001=error
    RESULT_VARIABLE advisory_result
    OUTPUT_VARIABLE advisory_stdout
    ERROR_VARIABLE advisory_stderr
)
if(NOT advisory_result EQUAL 1 OR NOT advisory_stderr MATCHES "error\\[FCS2001\\]")
    message(FATAL_ERROR "explicit Standard advisory rule was not enabled: ${advisory_stdout}${advisory_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" lint "${WORK}" --rule FCS1001=error
    RESULT_VARIABLE rule_result
    OUTPUT_VARIABLE rule_stdout
    ERROR_VARIABLE rule_stderr
)
if(NOT rule_result EQUAL 1 OR NOT rule_stderr MATCHES "error\\[FCS1001\\]")
    message(FATAL_ERROR "command-line FCS severity override did not publish an error: ${rule_stdout}${rule_stderr}")
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

foreach(invalid_rule IN ITEMS FCS9999=off FCS9001=off)
    execute_process(
        COMMAND "${COMPILER}" lint "${WORK}" --rule "${invalid_rule}"
        RESULT_VARIABLE invalid_rule_result
        OUTPUT_VARIABLE invalid_rule_stdout
        ERROR_VARIABLE invalid_rule_stderr
    )
    if(NOT invalid_rule_result EQUAL 2)
        message(FATAL_ERROR "lint accepted invalid rule override ${invalid_rule}: ${invalid_rule_stdout}${invalid_rule_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${COMPILER}" lint "${INVALID}" --profile valid
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)
if(NOT invalid_result EQUAL 1 OR NOT invalid_stderr MATCHES "error\\[FDN")
    message(FATAL_ERROR "lint did not preserve compiler errors: ${invalid_stdout}${invalid_stderr}")
endif()
