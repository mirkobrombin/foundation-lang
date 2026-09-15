if(NOT DEFINED COMPILER OR NOT DEFINED PROJECT OR NOT DEFINED WORK)
    message(FATAL_ERROR "freestanding selection assertion is missing an input")
endif()

# The fixture declares each selected function and method twice. A check succeeds only when
# exactly one declaration of each pair is active for the resolved target.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
set(project "${WORK}/project")

function(run_compiler label)
    execute_process(
        COMMAND "${COMPILER}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${label} failed:\n${output}${error}")
    endif()
endfunction()

run_compiler("freestanding resolution" package resolve "${project}" --target freestanding)
file(READ "${project}/foundation.lock" freestanding_lock)
if(NOT freestanding_lock MATCHES "\ntarget freestanding\n")
    message(FATAL_ERROR "freestanding resolution did not lock the target:\n${freestanding_lock}")
endif()
run_compiler("freestanding check" check "${project}" --target freestanding)

execute_process(
    COMMAND "${COMPILER}" check "${project}"
    RESULT_VARIABLE mismatch_result
    OUTPUT_VARIABLE mismatch_output
    ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0 OR NOT mismatch_error MATCHES "FDN4111")
    message(FATAL_ERROR "a freestanding lock was accepted for the host target:\n"
        "${mismatch_output}${mismatch_error}")
endif()

execute_process(
    COMMAND "${COMPILER}" check "${project}" --target hosted
    RESULT_VARIABLE hosted_result
    OUTPUT_VARIABLE hosted_output
    ERROR_VARIABLE hosted_error
)
if(NOT hosted_result EQUAL 2 OR NOT hosted_error MATCHES "usage:")
    message(FATAL_ERROR "--target hosted was not a usage error:\n${hosted_output}${hosted_error}")
endif()

run_compiler("host resolution" package resolve "${project}")
run_compiler("host check" check "${project}")
run_compiler("host C emission" emit-c "${project}" -o "${WORK}/host.c")
file(READ "${WORK}/host.c" host_c)
if(NOT host_c MATCHES "hosted-only" OR NOT host_c MATCHES "hosted-methods" OR
   host_c MATCHES "freestanding-only" OR host_c MATCHES "freestanding-methods")
    message(FATAL_ERROR "host C emission did not select only the hosted declarations")
endif()

message(STATUS "freestanding and hosted selectors activated the expected declarations")
