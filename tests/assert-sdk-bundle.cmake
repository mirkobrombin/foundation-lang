if(NOT DEFINED BUILD_DIR OR NOT DEFINED SDK_DIR OR NOT DEFINED EXECUTABLE_SUFFIX)
    message(FATAL_ERROR "BUILD_DIR, SDK_DIR, and EXECUTABLE_SUFFIX are required")
endif()

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${SDK_DIR}")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND install_command --config "${CONFIG}")
endif()

file(REMOVE_RECURSE "${SDK_DIR}")
execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_status
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_status EQUAL 0)
    message(FATAL_ERROR "SDK install failed:\n${install_output}${install_error}")
endif()

set(compiler "${SDK_DIR}/bin/foundationc${EXECUTABLE_SUFFIX}")
set(example "${SDK_DIR}/examples/hello/src/main.fn")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=FOUNDATION_SDK_ROOT
            "${compiler}" run "${example}"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "relocated SDK run failed:\n${run_output}${run_error}")
endif()
if(NOT run_output STREQUAL "hello from foundation\n")
    message(FATAL_ERROR "unexpected relocated SDK output: ${run_output}")
endif()
