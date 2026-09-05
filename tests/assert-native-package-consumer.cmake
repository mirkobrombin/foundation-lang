file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${CONSUMER}/" DESTINATION "${WORK}/native-consumer")
file(COPY "${DEPENDENCY}/" DESTINATION "${WORK}/native-interface")

set(project "${WORK}/native-consumer")
execute_process(
    COMMAND "${COMPILER}" package resolve "${project}"
    RESULT_VARIABLE resolve_result
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_result EQUAL 0)
    message(FATAL_ERROR
            "cannot resolve native package consumer:\n${resolve_output}${resolve_error}")
endif()

foreach(backend IN ITEMS llvm c)
    execute_process(
        COMMAND "${COMPILER}" run "${project}" --backend "${backend}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error
    )
    if(NOT run_result EQUAL 0 OR NOT run_output STREQUAL "42\n")
        message(FATAL_ERROR
                "cannot run ${backend} native package consumer:\n${run_output}${run_error}")
    endif()
endforeach()

file(REMOVE "${WORK}/native-interface/native/libfuse/increment.c")
execute_process(
    COMMAND "${COMPILER}" run "${project}" --backend llvm
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
)
if(missing_result EQUAL 0 OR NOT missing_error MATCHES "FDN4112")
    message(FATAL_ERROR
            "missing native package source was accepted:\n${missing_output}${missing_error}")
endif()
