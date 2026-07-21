if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED WORK)
    message(FATAL_ERROR "retired source extension assertion is missing an input")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
string(CONCAT retired_extension ".fd" "n")
set(retired_source "${WORK}/main${retired_extension}")
configure_file("${SOURCE}" "${retired_source}" COPYONLY)

execute_process(
    COMMAND "${COMPILER}" check "${retired_source}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
file(REMOVE "${retired_source}")
if(result EQUAL 0)
    message(FATAL_ERROR
        "retired source extension was accepted:\n${output}${error}")
endif()
if(NOT error MATCHES "source file must use the \\.fn extension")
    message(FATAL_ERROR "retired source extension diagnostic changed:\n${error}")
endif()
