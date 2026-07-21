if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "project panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "panic project unexpectedly succeeded:\n${output}")
endif()

string(FIND "${error}" "at trace.lib.Crash (lib/lib.fn:4:5)" crash_frame)
string(FIND "${error}" "at trace.app.main (app/main.fn:6:9)" main_frame)
if(crash_frame LESS 0 OR main_frame LESS 0 OR crash_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "package panic frames are incomplete or out of order:\n${error}")
endif()

message(STATUS "package panic trace preserves originating files")
