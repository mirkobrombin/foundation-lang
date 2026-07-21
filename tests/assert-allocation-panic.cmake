if(NOT DEFINED PROGRAM)
    message(FATAL_ERROR "allocation panic assertion requires PROGRAM")
endif()

execute_process(
    COMMAND "${PROGRAM}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "impossible allocation unexpectedly succeeded:\n${output}")
endif()
if(NOT error MATCHES "foundation panic: allocation failed")
    message(FATAL_ERROR "allocation panic message is missing:\n${error}")
endif()
if(NOT error MATCHES "at main.allocate \\(allocation.fn:9:5\\)")
    message(FATAL_ERROR "allocation panic lost its Foundation location:\n${error}")
endif()

message(STATUS "allocation failure preserves the Foundation trace")
