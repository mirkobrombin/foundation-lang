# Go ships its race detector only for these targets; elsewhere the fixture runs without it.
execute_process(
    COMMAND "${GO_EXECUTABLE}" env GOOS GOARCH
    RESULT_VARIABLE go_platform_result
    OUTPUT_VARIABLE go_platform
    ERROR_VARIABLE go_platform_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT go_platform_result EQUAL 0)
    message(FATAL_ERROR "cannot read the Go target:\n${go_platform_error}")
endif()
string(REGEX REPLACE "[\r\n]+" "/" go_platform "${go_platform}")
set(go_race_flag)
if(go_platform MATCHES
   "^(linux/(amd64|arm64|ppc64le|s390x)|darwin/(amd64|arm64)|freebsd/amd64|netbsd/amd64|windows/amd64)$")
    set(go_race_flag -race)
endif()
