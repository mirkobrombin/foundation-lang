set(backend_args)
if(DEFINED BACKEND)
    list(APPEND backend_args --backend "${BACKEND}")
endif()

execute_process(
    COMMAND "${COMPILER}" test "${SUCCESS}" ${backend_args}
    RESULT_VARIABLE success_status
    OUTPUT_VARIABLE success_output
    ERROR_VARIABLE success_error
)
if(NOT success_status EQUAL 0)
    message(FATAL_ERROR "successful test project exited with ${success_status}:\n${success_output}\n${success_error}")
endif()
if(NOT success_output MATCHES "ok addition returns the sum" OR
   NOT success_output MATCHES "ok explicit pass" OR
   NOT success_output MATCHES "2 passed; 0 failed")
    message(FATAL_ERROR "successful test summary is incomplete:\n${success_output}")
endif()

execute_process(
    COMMAND "${COMPILER}" test "${ISOLATION}" ${backend_args}
    RESULT_VARIABLE isolation_status
    OUTPUT_VARIABLE isolation_output
    ERROR_VARIABLE isolation_error
)
if(isolation_status EQUAL 0)
    message(FATAL_ERROR "failing test project unexpectedly succeeded")
endif()
if(NOT isolation_output MATCHES "FAILED failure remains isolated" OR
   NOT isolation_output MATCHES "ok later test still runs" OR
   NOT isolation_output MATCHES "1 passed; 1 failed")
    message(FATAL_ERROR "test isolation summary is incomplete:\n${isolation_output}\n${isolation_error}")
endif()
