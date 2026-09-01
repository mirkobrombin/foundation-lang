if(NOT DEFINED COMPILER OR NOT DEFINED ROOT)
    message(FATAL_ERROR "host lock preparation requires COMPILER and ROOT")
endif()

file(GLOB_RECURSE lock_files
    LIST_DIRECTORIES false
    "${ROOT}/examples/foundation.lock"
    "${ROOT}/examples/*/foundation.lock"
    "${ROOT}/examples/*/*/foundation.lock"
    "${ROOT}/tests/compatibility/*/foundation/foundation.lock"
    "${ROOT}/tests/projects/*/foundation.lock"
    "${ROOT}/tests/projects/*/*/foundation.lock"
)
cmake_path(NORMAL_PATH ROOT OUTPUT_VARIABLE normalized_root)

foreach(lock_file IN LISTS lock_files)
    get_filename_component(project "${lock_file}" DIRECTORY)
    cmake_path(NORMAL_PATH project OUTPUT_VARIABLE normalized_project)
    if(normalized_project STREQUAL
       "${normalized_root}/tests/projects/target-selection")
        continue()
    endif()
    if(NOT EXISTS "${project}/foundation.package")
        continue()
    endif()
    execute_process(
        COMMAND "${COMPILER}" package resolve "${project}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "cannot prepare ${project} for the host target:\n${error}")
    endif()
endforeach()

message(STATUS "package locks match the CI host target")
