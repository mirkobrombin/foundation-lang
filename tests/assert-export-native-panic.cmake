if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED NATIVE)
    message(FATAL_ERROR "export panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}" --native "${NATIVE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "export panic project unexpectedly succeeded:\n${output}")
endif()
if(NOT error MATCHES "foundation panic: C ABI export failure")
    message(FATAL_ERROR "export panic message is missing:\n${error}")
endif()

string(FIND "${error}" "at trace.export.FoundationFail (app/main.fdn:6:5)" foundation_frame)
string(FIND "${error}" "at [native] foundation_fail (app/main.fdn:5:1)" export_frame)
string(FIND "${error}" "at [native] foundation_native_round_trip (app/main.fdn:3:1)" import_frame)
string(FIND "${error}" "at trace.export.main (app/main.fdn:10:5)" main_frame)
if(foundation_frame LESS 0 OR export_frame LESS 0 OR import_frame LESS 0 OR main_frame LESS 0 OR
   foundation_frame GREATER_EQUAL export_frame OR export_frame GREATER_EQUAL import_frame OR
   import_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "exported native boundary is incomplete:\n${error}")
endif()

message(STATUS "C-to-Foundation panic preserves both native boundaries")
