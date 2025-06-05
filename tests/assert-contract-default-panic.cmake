if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "default contract panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "default contract panic project unexpectedly succeeded:\n${output}")
endif()

string(FIND "${error}" "at trace.defaultapp.PrincipalImpl.Cause (app/main.fdn:9:9)" concrete_frame)
string(FIND "${error}" "at trace.defaults.Failure.Fail (lib/lib.fdn:7:14)" default_frame)
string(FIND "${error}" "at trace.defaultapp.run (app/main.fdn:18:11)" contract_frame)
string(FIND "${error}" "at trace.defaultapp.main (app/main.fdn:23:5)" main_frame)
if(concrete_frame LESS 0 OR default_frame LESS 0 OR contract_frame LESS 0 OR
   main_frame LESS 0 OR concrete_frame GREATER_EQUAL default_frame OR
   default_frame GREATER_EQUAL contract_frame OR contract_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "default contract panic frames are incomplete or out of order:\n${error}")
endif()

message(STATUS "default contract panic trace preserves dynamic and default frames")
