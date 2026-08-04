if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED EXPECTED_MERMAID OR
   NOT DEFINED EXPECTED_GRAPHVIZ OR NOT DEFINED WORK)
    message(FATAL_ERROR "FSM assertion is missing an input")
endif()

file(MAKE_DIRECTORY "${WORK}")
set(first_mermaid "${WORK}/first.mmd")
set(second_mermaid "${WORK}/second.mmd")
set(graphviz "${WORK}/machine.dot")

execute_process(
    COMMAND "${COMPILER}" emit-fsm "${SOURCE}" -o "${first_mermaid}"
            --format mermaid --machine Order
    RESULT_VARIABLE first_result
    OUTPUT_VARIABLE first_output
    ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "first FSM emission failed:\n${first_output}${first_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-fsm "${SOURCE}" -o "${second_mermaid}"
            --format mermaid
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_output
    ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "second FSM emission failed:\n${second_output}${second_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-fsm "${SOURCE}" -o "${graphviz}"
            --format graphviz --machine Order
    RESULT_VARIABLE graphviz_result
    OUTPUT_VARIABLE graphviz_output
    ERROR_VARIABLE graphviz_error
)
if(NOT graphviz_result EQUAL 0)
    message(FATAL_ERROR "Graphviz FSM emission failed:\n${graphviz_output}${graphviz_error}")
endif()

file(READ "${EXPECTED_MERMAID}" expected_mermaid)
file(READ "${EXPECTED_GRAPHVIZ}" expected_graphviz)
file(READ "${first_mermaid}" first)
file(READ "${second_mermaid}" second)
file(READ "${graphviz}" actual_graphviz)
if(NOT first STREQUAL expected_mermaid)
    message(FATAL_ERROR "Mermaid FSM output does not match ${EXPECTED_MERMAID}")
endif()
if(NOT second STREQUAL first)
    message(FATAL_ERROR "repeated FSM emission produced different output")
endif()
if(NOT actual_graphviz STREQUAL expected_graphviz)
    message(FATAL_ERROR "Graphviz FSM output does not match ${EXPECTED_GRAPHVIZ}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-fsm "${SOURCE}" -o "${WORK}/invalid.out"
            --format invalid
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_output
    ERROR_VARIABLE invalid_error
)
if(NOT invalid_result EQUAL 2 OR NOT invalid_error MATCHES "foundationc emit-fsm")
    message(FATAL_ERROR "FSM emission accepted an invalid format: ${invalid_output}${invalid_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-fsm "${SOURCE}" -o "${WORK}/missing.out"
            --format mermaid --machine Missing
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 1 OR NOT missing_error MATCHES "FDN2422")
    message(FATAL_ERROR "FSM emission accepted an unknown machine: ${missing_output}${missing_error}")
endif()

file(SHA256 "${first_mermaid}" mermaid_hash)
file(SHA256 "${graphviz}" graphviz_hash)
message(STATUS "FSM diagrams are deterministic: ${mermaid_hash} ${graphviz_hash}")
