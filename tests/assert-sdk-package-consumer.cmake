if(NOT DEFINED COMPILER OR NOT DEFINED ROOT OR NOT DEFINED SOURCE OR
   NOT DEFINED OUTPUT_DIRECTORY OR NOT DEFINED TARGET OR
   NOT DEFINED EXECUTABLE_SUFFIX)
    message(FATAL_ERROR
        "SDK package assertion requires COMPILER, ROOT, SOURCE, OUTPUT_DIRECTORY, "
        "TARGET, and EXECUTABLE_SUFFIX"
    )
endif()

function(run_checked label)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "FOUNDATION_SDK_ROOT=${ROOT}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${label} exited with ${status}:\n"
            "stdout:\n${output}"
            "stderr:\n${error}"
        )
    endif()
endfunction()

set(project "${OUTPUT_DIRECTORY}/project")
file(REMOVE_RECURSE "${OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(COPY "${SOURCE}/" DESTINATION "${project}")

run_checked(
    "resolve"
    "${COMPILER}" package resolve "${project}" --target "${TARGET}"
)
run_checked("verify" "${COMPILER}" package verify "${project}")

file(READ "${project}/foundation.lock" lock)
if(NOT lock MATCHES
   "package foundation.ui.sdl 1.15.0 sha256:[0-9a-f]+ sdk providers/sdl")
    message(FATAL_ERROR "SDK package lock entry is missing")
endif()

foreach(backend IN ITEMS llvm c)
    set(executable "${OUTPUT_DIRECTORY}/consumer-${backend}${EXECUTABLE_SUFFIX}")
    run_checked(
        "${backend} build"
        "${COMPILER}" build "${project}" -o "${executable}" --backend "${backend}"
    )
    if(NOT EXISTS "${executable}")
        message(FATAL_ERROR "${backend} build did not create ${executable}")
    endif()
endforeach()
