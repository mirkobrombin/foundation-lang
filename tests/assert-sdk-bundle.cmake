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

function(run_relocated_package_command label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=FOUNDATION_SDK_ROOT
                "${compiler}" ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "relocated SDK ${label} failed:\n${output}${error}"
        )
    endif()
endfunction()

set(consumer "${SDK_DIR}-consumer")
file(REMOVE_RECURSE "${consumer}")
file(MAKE_DIRECTORY "${consumer}/src")
file(WRITE "${consumer}/foundation.package"
    "format foundation.package/v1\n"
    "name sdk.ui.consumer\n"
    "version 1.0.0\n"
    "language 1\n"
    "fcs strict\n"
    "source src\n"
    "dependency foundation.ui.sdl 1.5.0 sdk providers/sdl\n"
)
file(WRITE "${consumer}/src/main.fn"
    "package sdk.ui.consumer\n\n"
    "import foundation.ui.sdl\n\n"
    "fn main() i32 {\n"
    "    const opened = sdl.Open(\"SDK UI consumer\", 320, 240) else { return 1 }\n"
    "    var window = opened\n"
    "    const size = window.Size() else { return 1 }\n"
    "    if size.Width != 320 || size.Height != 240 { return 1 }\n"
    "    const dialog = window.PollOpenFileDialog() else { return 1 }\n"
    "    match dialog { Idle: {} _: { return 1 } }\n"
    "    0\n"
    "}\n"
)
run_relocated_package_command("package resolve" package resolve "${consumer}")
run_relocated_package_command("package verify" package verify "${consumer}")
run_relocated_package_command("package check" package check "${consumer}")
file(REMOVE_RECURSE "${consumer}")
