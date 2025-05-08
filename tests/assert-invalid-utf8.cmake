if(NOT DEFINED COMPILER OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "invalid UTF-8 assertion requires COMPILER and OUTPUT")
endif()

string(ASCII 195 invalid_lead)
file(WRITE "${OUTPUT}" "fn main() i32 { print(\"${invalid_lead}\") 0 }\n")
execute_process(
    COMMAND "${COMPILER}" check "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0)
    message(FATAL_ERROR "invalid UTF-8 source unexpectedly passed")
endif()
if(NOT error MATCHES "FDN0005")
    message(FATAL_ERROR "invalid UTF-8 source did not report FDN0005:\n${output}${error}")
endif()

message(STATUS "invalid UTF-8 source reports FDN0005")
