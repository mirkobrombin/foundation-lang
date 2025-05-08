if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "project order assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ordered project emission failed:\n${output}${error}")
endif()

file(READ "${OUTPUT}" generated)
foreach(symbol IN ITEMS
        "fdn_fn_order_app_First_0"
        "fdn_fn_order_app_Middle_1"
        "fdn_fn_order_app_Last_3")
    string(FIND "${generated}" "${symbol}" position)
    if(position LESS 0)
        message(FATAL_ERROR "source ordering changed: missing ${symbol}")
    endif()
endforeach()

execute_process(
    COMMAND "${COMPILER}" run "${SOURCE}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "ordered project execution failed:\n${run_output}${run_error}")
endif()

message(STATUS "project sources use normalized relative path order")
