if(NOT DEFINED BUILD_DIRECTORY OR NOT DEFINED OUTPUT_DIRECTORY OR
   NOT DEFINED ARCHIVE_NAME OR NOT DEFINED EXECUTABLE_SUFFIX)
    message(FATAL_ERROR
        "BUILD_DIRECTORY, OUTPUT_DIRECTORY, ARCHIVE_NAME, and EXECUTABLE_SUFFIX are required")
endif()
if(NOT ARCHIVE_NAME MATCHES "^foundation-[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "ARCHIVE_NAME is not a safe release name")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(staging "${OUTPUT_DIRECTORY}/${ARCHIVE_NAME}")
file(REMOVE_RECURSE "${staging}")

set(install_command
    "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}" --prefix "${staging}")
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    list(APPEND install_command --config "${CONFIG}")
endif()
execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_status
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_status EQUAL 0)
    message(FATAL_ERROR "SDK install failed:\n${install_output}${install_error}")
endif()

foreach(path IN ITEMS
        "bin/foundationc${EXECUTABLE_SUFFIX}"
        "bin/foundationc-selfhost${EXECUTABLE_SUFFIX}"
        "bin/foundation-ls${EXECUTABLE_SUFFIX}"
        "runtime/include/foundation/library.h"
        "std"
        "foundation"
        "docs"
        "README.md"
        "CONTRIBUTING.md"
        "SECURITY.md"
        "LICENSE-MIT"
        "LICENSE-APACHE")
    if(NOT EXISTS "${staging}/${path}")
        message(FATAL_ERROR "installed SDK is missing ${path}")
    endif()
endforeach()

if(WIN32)
    set(asset "${OUTPUT_DIRECTORY}/${ARCHIVE_NAME}.zip")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${asset}" --format=zip "${ARCHIVE_NAME}"
        WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
        RESULT_VARIABLE archive_status
        ERROR_VARIABLE archive_error
    )
else()
    set(asset "${OUTPUT_DIRECTORY}/${ARCHIVE_NAME}.tar.gz")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar czf "${asset}" "${ARCHIVE_NAME}"
        WORKING_DIRECTORY "${OUTPUT_DIRECTORY}"
        RESULT_VARIABLE archive_status
        ERROR_VARIABLE archive_error
    )
endif()
if(NOT archive_status EQUAL 0)
    message(FATAL_ERROR "SDK archive failed:\n${archive_error}")
endif()

file(SHA256 "${asset}" digest)
get_filename_component(asset_name "${asset}" NAME)
file(WRITE "${asset}.sha256" "${digest}  ${asset_name}\n")
file(REMOVE_RECURSE "${staging}")
