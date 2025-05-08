if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED NATIVE)
    message(FATAL_ERROR "native project panic assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}" --native "${NATIVE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "native panic project unexpectedly succeeded:\n${output}")
endif()
if(NOT error MATCHES "foundation panic: C ABI failure")
    message(FATAL_ERROR "native project panic message is missing:\n${error}")
endif()

string(FIND "${error}" "at [native] foundation_native_fail (app/main.fdn:3:1)" native_frame)
string(FIND "${error}" "at trace.abi.main (app/main.fdn:6:5)" main_frame)
if(native_frame LESS 0 OR main_frame LESS 0 OR native_frame GREATER_EQUAL main_frame)
    message(FATAL_ERROR "generated native boundary is incomplete:\n${error}")
endif()

message(STATUS "generated C ABI wrapper preserves the native panic boundary")
