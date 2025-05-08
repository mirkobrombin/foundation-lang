if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "contract panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "contract panic project unexpectedly succeeded:\n${output}")
endif()

string(FIND "${error}" "at trace.failure.Crashing.Fail (lib/lib.fdn:9:9)" method_frame)
string(FIND "${error}" "at trace.app.run (app/main.fdn:6:11)" contract_frame)
string(FIND "${error}" "at trace.app.main (app/main.fdn:11:5)" main_frame)
if(method_frame LESS 0 OR contract_frame LESS 0 OR main_frame LESS 0 OR
   method_frame GREATER_EQUAL contract_frame OR contract_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "contract panic frames are incomplete or out of order:\n${error}")
endif()

message(STATUS "contract panic trace preserves method and call sites")
