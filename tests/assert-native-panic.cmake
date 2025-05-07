if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "native panic assertion requires PROGRAM")
endif()

execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "native panic program unexpectedly succeeded:\n${output}")
endif()
if(NOT error MATCHES "foundation panic: native failure")
    message(FATAL_ERROR "native panic message is missing:\n${error}")
endif()
string(FIND "${error}" "at [native] native_bridge (bridge.c:12:7)" native_frame)
string(FIND "${error}" "at main.caller (ffi.fdn:3:5)" caller_frame)
if(native_frame LESS 0 OR caller_frame LESS 0 OR native_frame GREATER_EQUAL caller_frame)
    message(FATAL_ERROR "native panic boundary is incomplete:\n${error}")
endif()

message(STATUS "native panic boundary remains in the Foundation trace")
