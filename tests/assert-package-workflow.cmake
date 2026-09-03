file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${FIXTURE}/" DESTINATION "${WORK}/fixture")

set(PROJECT "${WORK}/fixture/app")
set(REGISTRY "default=${WORK}/fixture/registry")
set(PACKAGE_CACHE "${WORK}/cache")

execute_process(
    COMMAND "${PROGRAM}" package requirements "${PROJECT}" --target linux
    RESULT_VARIABLE requirements_result
    OUTPUT_VARIABLE requirements_output
    ERROR_VARIABLE requirements_error
)
if(NOT requirements_result EQUAL 0 OR
   NOT requirements_output STREQUAL
       "format foundation.package.requirements/v1\nregistry default example.greeting ^1.0.0\n")
    message(FATAL_ERROR
        "package requirements report is invalid: ${requirements_error}${requirements_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package select "^1.0.0" "1.0.0" "2.0.0" "1.4.2"
    RESULT_VARIABLE select_result
    OUTPUT_VARIABLE select_output
    ERROR_VARIABLE select_error
)
if(NOT select_result EQUAL 0 OR NOT select_output STREQUAL "1.4.2\n")
    message(FATAL_ERROR
        "package version selection failed: ${select_error}${select_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package resolve "${PROJECT}" --registry "${REGISTRY}"
    RESULT_VARIABLE resolve_result
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_result EQUAL 0)
    message(FATAL_ERROR "package resolve failed: ${resolve_error}")
endif()
if(NOT resolve_output MATCHES "changed .*foundation.lock" OR
   NOT resolve_output MATCHES "resolved 2 packages")
    message(FATAL_ERROR "package resolve did not report its mutation: ${resolve_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package locked "${PROJECT}"
    RESULT_VARIABLE locked_result
    OUTPUT_VARIABLE locked_output
    ERROR_VARIABLE locked_error
)
string(CONCAT expected_locked_output
    "format foundation.package.locked/v1\n"
    "target linux\n"
    "registry default example.greeting 1.0.0 "
    "sha256:0bc682288bc50e8572e3deba8ff889750aeabcb836ab65dd5e0e1ccd121daa71\n")
if(NOT locked_result EQUAL 0 OR NOT locked_output STREQUAL expected_locked_output)
    message(FATAL_ERROR
        "package lock report is invalid: ${locked_error}${locked_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package fetch "${PROJECT}" --cache "${PACKAGE_CACHE}"
            --registry "${REGISTRY}"
    RESULT_VARIABLE fetch_result
    OUTPUT_VARIABLE fetch_output
    ERROR_VARIABLE fetch_error
)
if(NOT fetch_result EQUAL 0)
    message(FATAL_ERROR "package fetch failed: ${fetch_error}")
endif()
if(NOT fetch_output MATCHES "changed .*sha256/")
    message(FATAL_ERROR "package fetch did not report its mutation: ${fetch_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package fetch "${PROJECT}" --cache "${PACKAGE_CACHE}"
            --registry "${REGISTRY}"
    RESULT_VARIABLE repeated_fetch_result
    OUTPUT_VARIABLE repeated_fetch_output
    ERROR_VARIABLE repeated_fetch_error
)
if(NOT repeated_fetch_result EQUAL 0)
    message(FATAL_ERROR "repeated package fetch failed: ${repeated_fetch_error}")
endif()
if(NOT repeated_fetch_output MATCHES "verified .*sha256/")
    message(FATAL_ERROR "repeated package fetch was not idempotent: ${repeated_fetch_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FOUNDATION_PACKAGE_CACHE=${PACKAGE_CACHE}"
            "${PROGRAM}" format --check "${PROJECT}"
    RESULT_VARIABLE format_result
    OUTPUT_VARIABLE format_output
    ERROR_VARIABLE format_error
)
if(NOT format_result EQUAL 0)
    message(FATAL_ERROR "locked project format check failed: ${format_error}${format_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "FOUNDATION_PACKAGE_CACHE=${WORK}/missing-format-cache"
            "${PROGRAM}" format --check "${PROJECT}"
    RESULT_VARIABLE offline_format_result
    OUTPUT_VARIABLE offline_format_output
    ERROR_VARIABLE offline_format_error
)
if(offline_format_result EQUAL 0 OR NOT offline_format_error MATCHES "FDN4070")
    message(FATAL_ERROR
        "offline formatting without locked content did not fail clearly: ${offline_format_error}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package verify "${PROJECT}" --cache "${PACKAGE_CACHE}"
    RESULT_VARIABLE verify_result
    OUTPUT_VARIABLE verify_output
    ERROR_VARIABLE verify_error
)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "package verify failed: ${verify_error}")
endif()
if(NOT verify_output MATCHES "verified .*sha256/" OR
   NOT verify_output MATCHES "verified .*local")
    message(FATAL_ERROR "package verify omitted a locked package: ${verify_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package resolve
            "${WORK}/fixture/registry/example.greeting/1.0.0" --target linux
    RESULT_VARIABLE package_check_resolve_result
    OUTPUT_VARIABLE package_check_resolve_output
    ERROR_VARIABLE package_check_resolve_error
)
if(NOT package_check_resolve_result EQUAL 0)
    message(FATAL_ERROR
        "library package resolve failed: ${package_check_resolve_error}${package_check_resolve_output}")
endif()
execute_process(
    COMMAND "${PROGRAM}" package check
            "${WORK}/fixture/registry/example.greeting/1.0.0" --target linux
    RESULT_VARIABLE package_check_result
    OUTPUT_VARIABLE package_check_output
    ERROR_VARIABLE package_check_error
)
if(NOT package_check_result EQUAL 0)
    message(FATAL_ERROR
        "library package check failed: ${package_check_error}${package_check_output}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FOUNDATION_PACKAGE_CACHE=${PACKAGE_CACHE}"
            "${PROGRAM}" check "${PROJECT}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "locked project check failed: ${check_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "FOUNDATION_PACKAGE_CACHE=${PACKAGE_CACHE}"
            "${PROGRAM}" run "${PROJECT}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0 OR NOT run_output STREQUAL "hello from cache\n42\n")
    message(FATAL_ERROR "locked project run failed: ${run_error}${run_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package verify "${PROJECT}" --cache "${WORK}/missing-cache"
    RESULT_VARIABLE offline_result
    OUTPUT_VARIABLE offline_output
    ERROR_VARIABLE offline_error
)
if(offline_result EQUAL 0 OR NOT offline_error MATCHES "FDN4070")
    message(FATAL_ERROR "missing offline artifact did not fail clearly: ${offline_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "FOUNDATION_PACKAGE_CACHE=${WORK}/missing-build-cache"
            "${PROGRAM}" check "${PROJECT}"
    RESULT_VARIABLE offline_check_result
    OUTPUT_VARIABLE offline_check_output
    ERROR_VARIABLE offline_check_error
)
if(offline_check_result EQUAL 0 OR NOT offline_check_error MATCHES "FDN4070")
    message(FATAL_ERROR
        "offline compilation without locked content did not fail clearly: ${offline_check_error}")
endif()

file(MAKE_DIRECTORY "${PACKAGE_CACHE}/sha256/.tmp-interrupted")
file(WRITE "${PACKAGE_CACHE}/sha256/.tmp-interrupted/partial" "partial")
execute_process(
    COMMAND "${PROGRAM}" package prune "${PROJECT}" --cache "${PACKAGE_CACHE}"
    RESULT_VARIABLE prune_result
    OUTPUT_VARIABLE prune_output
    ERROR_VARIABLE prune_error
)
if(NOT prune_result EQUAL 0)
    message(FATAL_ERROR "package prune failed: ${prune_error}")
endif()
if(NOT prune_output MATCHES "changed .*[.]tmp-interrupted")
    message(FATAL_ERROR "package prune omitted interrupted staging: ${prune_output}")
endif()

execute_process(
    COMMAND "${PROGRAM}" package inspect "${PROJECT}"
    RESULT_VARIABLE inspect_result
    OUTPUT_VARIABLE inspect_output
    ERROR_VARIABLE inspect_error
)
if(NOT inspect_result EQUAL 0 OR
   NOT inspect_output MATCHES "format foundation.package/v1" OR
   NOT inspect_output MATCHES "format foundation.lock/v1")
    message(FATAL_ERROR "package inspect failed: ${inspect_error}${inspect_output}")
endif()

set(SNAPSHOT "${WORK}/snapshot")
execute_process(
    COMMAND "${PROGRAM}" package snapshot "${PROJECT}" -o "${SNAPSHOT}"
    RESULT_VARIABLE snapshot_result
    OUTPUT_VARIABLE snapshot_output
    ERROR_VARIABLE snapshot_error
)
if(NOT snapshot_result EQUAL 0 OR
   NOT snapshot_output MATCHES "name example.app" OR
   NOT snapshot_output MATCHES "version 1.0.0" OR
   NOT snapshot_output MATCHES "digest sha256:" OR
   NOT snapshot_output MATCHES "files 2")
    message(FATAL_ERROR "package snapshot failed: ${snapshot_error}${snapshot_output}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${PROJECT}/src/main.fn" "${SNAPSHOT}/src/main.fn"
    RESULT_VARIABLE snapshot_source_result
)
if(NOT snapshot_source_result EQUAL 0 OR NOT EXISTS "${SNAPSHOT}/foundation.package" OR
   EXISTS "${SNAPSHOT}/foundation.lock")
    message(FATAL_ERROR "package snapshot did not preserve the canonical source set")
endif()
execute_process(
    COMMAND "${PROGRAM}" package snapshot "${PROJECT}" -o "${SNAPSHOT}"
    RESULT_VARIABLE repeated_snapshot_result
    ERROR_VARIABLE repeated_snapshot_error
)
if(repeated_snapshot_result EQUAL 0 OR NOT repeated_snapshot_error MATCHES "FDN4114")
    message(FATAL_ERROR "package snapshot replaced an existing output")
endif()

execute_process(
    COMMAND "${PROGRAM}" package init "${WORK}/initialized" example.initialized
    RESULT_VARIABLE init_result
    OUTPUT_VARIABLE init_output
    ERROR_VARIABLE init_error
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "package init failed: ${init_error}")
endif()
if(NOT init_output MATCHES "changed .*foundation.package")
    message(FATAL_ERROR "package init omitted its manifest mutation: ${init_output}")
endif()
execute_process(
    COMMAND "${PROGRAM}" package locked "${WORK}/initialized"
    RESULT_VARIABLE absent_lock_result
    OUTPUT_VARIABLE absent_lock_output
    ERROR_VARIABLE absent_lock_error
)
if(NOT absent_lock_result EQUAL 0 OR NOT absent_lock_output STREQUAL
   "format foundation.package.locked/v1\nabsent\n")
    message(FATAL_ERROR
        "absent package lock report is invalid: ${absent_lock_error}${absent_lock_output}")
endif()
file(READ "${WORK}/initialized/foundation.package" initialized_manifest)
if(NOT initialized_manifest MATCHES "language 1")
    message(FATAL_ERROR "package init omitted the Foundation language level")
endif()
if(NOT initialized_manifest MATCHES "fcs standard")
    message(FATAL_ERROR "package init omitted the explicit Standard profile")
endif()
file(GLOB init_staging "${WORK}/initialized/.foundation.package.tmp-*")
if(init_staging)
    message(FATAL_ERROR "package init left staging content: ${init_staging}")
endif()

set(EXISTING_PROJECT "${WORK}/existing")
file(MAKE_DIRECTORY "${EXISTING_PROJECT}")
file(WRITE "${EXISTING_PROJECT}/foundation.package"
    "format foundation.package/v1\n"
    "name example.existing\n"
    "version 0.1.0\n"
    "sdk ^0.1.0\n"
    "source src\n")
execute_process(
    COMMAND "${PROGRAM}" package init "${EXISTING_PROJECT}" example.replacement
    RESULT_VARIABLE repeated_init_result
    OUTPUT_VARIABLE repeated_init_output
    ERROR_VARIABLE repeated_init_error
)
if(repeated_init_result EQUAL 0 OR NOT repeated_init_error MATCHES "FDN4103")
    message(FATAL_ERROR "package init replaced an existing manifest: ${repeated_init_error}")
endif()
if(EXISTS "${EXISTING_PROJECT}/src")
    message(FATAL_ERROR "package init mutated a project after finding an existing manifest")
endif()
