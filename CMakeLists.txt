cmake_minimum_required(VERSION 3.25)

project(foundation_lang VERSION 0.1.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)
find_package(LLVM 21.1 CONFIG QUIET)
if(NOT LLVM_FOUND)
    find_program(FOUNDATION_LLVM_CONFIG NAMES llvm-config llvm-config-21 REQUIRED)
    execute_process(
        COMMAND ${FOUNDATION_LLVM_CONFIG} --cmakedir
        OUTPUT_VARIABLE FOUNDATION_LLVM_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    find_package(LLVM 21.1 REQUIRED CONFIG PATHS ${FOUNDATION_LLVM_CMAKE_DIR} NO_DEFAULT_PATH)
endif()

# Official Windows LLVM archives can embed the build machine's DIA SDK path.
if(MSVC AND TARGET LLVMDebugInfoPDB)
    get_target_property(FOUNDATION_LLVM_PDB_LINKS
        LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    set(FOUNDATION_LLVM_PDB_NEEDS_DIA_REPAIR OFF)
    foreach(FOUNDATION_LLVM_PDB_LINK IN LISTS FOUNDATION_LLVM_PDB_LINKS)
        if(FOUNDATION_LLVM_PDB_LINK MATCHES "[/\\\\]diaguids\\.lib$" AND
           NOT EXISTS "${FOUNDATION_LLVM_PDB_LINK}")
            set(FOUNDATION_LLVM_PDB_NEEDS_DIA_REPAIR ON)
        endif()
    endforeach()
    if(FOUNDATION_LLVM_PDB_NEEDS_DIA_REPAIR)
        set(FOUNDATION_DIA_LIBRARY_SUFFIX "DIA SDK/lib")
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
            string(APPEND FOUNDATION_DIA_LIBRARY_SUFFIX "/arm64")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            string(APPEND FOUNDATION_DIA_LIBRARY_SUFFIX "/amd64")
        endif()
        find_library(FOUNDATION_DIAGUIDS_LIBRARY
            NAMES diaguids
            HINTS "${CMAKE_GENERATOR_INSTANCE}" "$ENV{VSINSTALLDIR}"
            PATH_SUFFIXES "${FOUNDATION_DIA_LIBRARY_SUFFIX}"
            NO_CACHE
        )
        if(NOT FOUNDATION_DIAGUIDS_LIBRARY)
            message(FATAL_ERROR "LLVM requires the Visual Studio DIA SDK diaguids library")
        endif()
        set(FOUNDATION_LLVM_PDB_REPAIRED_LINKS)
        foreach(FOUNDATION_LLVM_PDB_LINK IN LISTS FOUNDATION_LLVM_PDB_LINKS)
            if(FOUNDATION_LLVM_PDB_LINK MATCHES "[/\\\\]diaguids\\.lib$" AND
               NOT EXISTS "${FOUNDATION_LLVM_PDB_LINK}")
                list(APPEND FOUNDATION_LLVM_PDB_REPAIRED_LINKS
                    "${FOUNDATION_DIAGUIDS_LIBRARY}")
            else()
                list(APPEND FOUNDATION_LLVM_PDB_REPAIRED_LINKS
                    "${FOUNDATION_LLVM_PDB_LINK}")
            endif()
        endforeach()
        set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES
            "${FOUNDATION_LLVM_PDB_REPAIRED_LINKS}")
    endif()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:/std:c11>"
        "$<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>"
    )
endif()

option(FOUNDATION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FOUNDATION_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(FOUNDATION_BUILD_OPENSSL_PROVIDER "Build the optional OpenSSL 3.5.6 provider" OFF)
option(FOUNDATION_OPENSSL_TSAN "Build the OpenSSL provider native test with ThreadSanitizer" OFF)
option(FOUNDATION_BUILD_WAMR_PROVIDER "Build the optional WAMR 2.4.5 provider" OFF)
option(FOUNDATION_WAMR_SANITIZERS "Instrument the WAMR provider path with ASan and UBSan" OFF)
option(FOUNDATION_WAMR_TSAN "Instrument the WAMR provider path with ThreadSanitizer" OFF)
option(FOUNDATION_ENABLE_WINDOWS_WINE_MATRIX
    "Register the local MinGW and Wine platform matrix" OFF)
set(FOUNDATION_WAMR_SOURCE "" CACHE PATH "Path to the pinned WAMR 2.4.5 source")
set(FOUNDATION_WAMR_LIBRARY "" CACHE FILEPATH "Path to the pinned WAMR 2.4.5 iwasm library")
set(FOUNDATION_V2_SOURCE "" CACHE PATH
    "Path to the pinned go-foundation v2 source used by compatibility tests")

add_library(foundation_runtime STATIC
    runtime/src/runtime.c
    runtime/src/fs_host.c
    runtime/src/process.c
    runtime/src/pty_posix.c
    runtime/src/pty_windows.c
    runtime/src/terminal.c
    runtime/src/crypto.c
    runtime/src/parse.c
    runtime/src/task.c
    runtime/src/cancellation.c
    runtime/src/channel.c
    runtime/src/blocking.c
    runtime/src/pool.c
    runtime/src/reactor.c
    runtime/src/watch.c
    runtime/src/resiliency.c
    runtime/src/net.c
    runtime/src/plugin.c
    runtime/src/plugin_sandbox.c
)
target_include_directories(foundation_runtime PUBLIC runtime/include)
target_link_libraries(foundation_runtime PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(foundation_runtime PUBLIC util)
endif()
if(WIN32)
    target_link_libraries(foundation_runtime PUBLIC bcrypt iphlpapi ws2_32)
endif()

if(FOUNDATION_WAMR_SANITIZERS AND FOUNDATION_WAMR_TSAN)
    message(FATAL_ERROR "FOUNDATION_WAMR_SANITIZERS and FOUNDATION_WAMR_TSAN are separate runs")
endif()
if((FOUNDATION_WAMR_SANITIZERS OR FOUNDATION_WAMR_TSAN) AND
   NOT FOUNDATION_BUILD_WAMR_PROVIDER)
    message(FATAL_ERROR "WAMR sanitizer runs require FOUNDATION_BUILD_WAMR_PROVIDER=ON")
endif()
if(FOUNDATION_WAMR_SANITIZERS OR FOUNDATION_WAMR_TSAN)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "WAMR sanitizer runs require Clang or GCC")
    endif()
    if(FOUNDATION_WAMR_SANITIZERS)
        target_compile_options(foundation_runtime PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_runtime PUBLIC -fsanitize=address,undefined)
    else()
        target_compile_options(foundation_runtime PRIVATE -fsanitize=thread)
        target_link_options(foundation_runtime PUBLIC -fsanitize=thread)
    endif()
endif()

if(FOUNDATION_BUILD_WAMR_PROVIDER)
    if(NOT EXISTS "${FOUNDATION_WAMR_SOURCE}/core/version.h" OR
       NOT EXISTS "${FOUNDATION_WAMR_SOURCE}/core/iwasm/include/wasm_export.h")
        message(FATAL_ERROR
            "FOUNDATION_WAMR_SOURCE must point to the pinned WAMR 2.4.5 source"
        )
    endif()
    if(NOT EXISTS "${FOUNDATION_WAMR_LIBRARY}")
        message(FATAL_ERROR
            "FOUNDATION_WAMR_LIBRARY must point to the pinned WAMR 2.4.5 iwasm library"
        )
    endif()
    file(READ "${FOUNDATION_WAMR_SOURCE}/core/version.h" FOUNDATION_WAMR_VERSION_HEADER)
    if(NOT FOUNDATION_WAMR_VERSION_HEADER MATCHES "#define WAMR_VERSION_MAJOR 2" OR
       NOT FOUNDATION_WAMR_VERSION_HEADER MATCHES "#define WAMR_VERSION_MINOR 4" OR
       NOT FOUNDATION_WAMR_VERSION_HEADER MATCHES "#define WAMR_VERSION_PATCH 5")
        message(FATAL_ERROR "FOUNDATION_WAMR_SOURCE must be WAMR 2.4.5")
    endif()
    add_library(foundation_wamr_provider SHARED
        runtime/providers/wamr/wamr_provider.c
    )
    target_include_directories(foundation_wamr_provider PRIVATE
        runtime/include
        "${FOUNDATION_WAMR_SOURCE}/core"
        "${FOUNDATION_WAMR_SOURCE}/core/iwasm/include"
    )
    target_compile_definitions(foundation_wamr_provider PRIVATE FOUNDATION_WAMR_PROVIDER_BUILD=1)
    target_link_libraries(foundation_wamr_provider PRIVATE
        "${FOUNDATION_WAMR_LIBRARY}"
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    if(NOT WIN32)
        target_link_libraries(foundation_wamr_provider PRIVATE m)
    else()
        target_link_libraries(foundation_wamr_provider PRIVATE ntdll)
    endif()
    if(MSVC)
        target_compile_definitions(foundation_wamr_provider PRIVATE WASM_RUNTIME_API_EXTERN=)
        target_compile_options(foundation_wamr_provider PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_wamr_provider PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_wamr_provider PRIVATE
            -Wall -Wextra -Wpedantic
        )
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_wamr_provider PRIVATE -Werror)
        endif()
    endif()
    if(FOUNDATION_WAMR_SANITIZERS)
        target_compile_options(foundation_wamr_provider PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_wamr_provider PRIVATE -fsanitize=address,undefined)
    elseif(FOUNDATION_WAMR_TSAN)
        target_compile_options(foundation_wamr_provider PRIVATE -fsanitize=thread)
        target_link_options(foundation_wamr_provider PRIVATE -fsanitize=thread)
    endif()
endif()

if(FOUNDATION_BUILD_OPENSSL_PROVIDER)
    find_package(OpenSSL 3.5.6 EXACT REQUIRED COMPONENTS SSL Crypto)
    add_library(foundation_openssl_provider STATIC
        providers/openssl/foundation_openssl.c
    )
    target_include_directories(foundation_openssl_provider
        PUBLIC providers/openssl
        PRIVATE runtime/include
    )
    target_link_libraries(foundation_openssl_provider
        PUBLIC foundation_runtime OpenSSL::SSL OpenSSL::Crypto
    )
    if(MSVC)
        target_compile_options(foundation_openssl_provider PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_openssl_provider PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_openssl_provider PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_openssl_provider PRIVATE -Werror)
        endif()
    endif()
    if(FOUNDATION_BUILD_FUZZERS)
        target_compile_options(foundation_openssl_provider PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_openssl_provider PRIVATE -fsanitize=address,undefined)
    elseif(FOUNDATION_OPENSSL_TSAN)
        target_compile_options(foundation_openssl_provider PRIVATE -fsanitize=thread)
        target_link_options(foundation_openssl_provider PRIVATE -fsanitize=thread)
    endif()
endif()

add_library(foundation_compiler STATIC
    compiler/src/application.cpp
    compiler/src/backend.cpp
    compiler/src/code_standard.cpp
    compiler/src/codegen.cpp
    compiler/src/diagnostic.cpp
    compiler/src/documentation.cpp
    compiler/src/driver.cpp
    compiler/src/formatter.cpp
    compiler/src/fsm.cpp
    compiler/src/imports.cpp
    compiler/src/language_service.cpp
    compiler/src/lexer.cpp
    compiler/src/lint.cpp
    compiler/src/lsp.cpp
    compiler/src/lower.cpp
    compiler/src/llvm_codegen.cpp
    compiler/src/metadata.cpp
    compiler/src/numeric.cpp
    compiler/src/package.cpp
    compiler/src/package_cache.cpp
    compiler/src/package_catalog.cpp
    compiler/src/package_cli.cpp
    compiler/src/package_environment.cpp
    compiler/src/package_export.cpp
    compiler/src/package_interface.cpp
    compiler/src/package_lock.cpp
    compiler/src/package_project.cpp
    compiler/src/package_registry.cpp
    compiler/src/package_resolver.cpp
    compiler/src/package_source.cpp
    compiler/src/parser.cpp
    compiler/src/process.cpp
    compiler/src/project.cpp
    compiler/src/sema.cpp
    compiler/src/sha256.cpp
    compiler/src/sdk.cpp
    compiler/src/target.cpp
    compiler/src/type.cpp
)
target_include_directories(foundation_compiler PUBLIC compiler/include)
target_include_directories(foundation_compiler SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(FOUNDATION_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(foundation_compiler PRIVATE ${FOUNDATION_LLVM_DEFINITIONS})
if(LLVM_LINK_LLVM_DYLIB AND TARGET LLVM)
    target_link_libraries(foundation_compiler PRIVATE LLVM)
else()
    llvm_map_components_to_libnames(FOUNDATION_LLVM_LIBRARIES
        Core
        Passes
        Support
        Target
        TargetParser
        ${LLVM_TARGETS_TO_BUILD}
    )
    target_link_libraries(foundation_compiler PRIVATE ${FOUNDATION_LLVM_LIBRARIES})
endif()
get_filename_component(FOUNDATION_C_COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)
get_filename_component(FOUNDATION_ARCHIVER_NAME "${CMAKE_AR}" NAME)
target_compile_definitions(foundation_compiler PRIVATE
    FOUNDATION_C_COMPILER="${FOUNDATION_C_COMPILER_NAME}"
    FOUNDATION_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
    FOUNDATION_ARCHIVER="${FOUNDATION_ARCHIVER_NAME}"
    FOUNDATION_RUNTIME_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
    FOUNDATION_RUNTIME_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c"
    FOUNDATION_RUNTIME_FS_HOST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/fs_host.c"
    FOUNDATION_RUNTIME_PROCESS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/process.c"
    FOUNDATION_RUNTIME_PTY_POSIX_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pty_posix.c"
    FOUNDATION_RUNTIME_PTY_WINDOWS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pty_windows.c"
    FOUNDATION_RUNTIME_TERMINAL_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/terminal.c"
    FOUNDATION_RUNTIME_CRYPTO_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c"
    FOUNDATION_RUNTIME_PARSE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c"
    FOUNDATION_RUNTIME_TASK_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c"
    FOUNDATION_RUNTIME_CANCELLATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c"
    FOUNDATION_RUNTIME_CHANNEL_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c"
    FOUNDATION_RUNTIME_BLOCKING_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c"
    FOUNDATION_RUNTIME_POOL_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c"
    FOUNDATION_RUNTIME_REACTOR_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c"
    FOUNDATION_RUNTIME_WATCH_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/watch.c"
    FOUNDATION_RUNTIME_RESILIENCY_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c"
    FOUNDATION_RUNTIME_NET_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c"
    FOUNDATION_RUNTIME_PLUGIN_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin.c"
    FOUNDATION_RUNTIME_PLUGIN_SANDBOX_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin_sandbox.c"
    FOUNDATION_STANDARD_LIBRARY="${CMAKE_CURRENT_SOURCE_DIR}/std"
    FOUNDATION_FRAMEWORK_LIBRARY="${CMAKE_CURRENT_SOURCE_DIR}/foundation"
)
if(FOUNDATION_WAMR_SANITIZERS)
    target_compile_definitions(foundation_compiler PRIVATE
        FOUNDATION_GENERATED_ADDRESS_UNDEFINED_SANITIZER=1
    )
elseif(FOUNDATION_WAMR_TSAN OR FOUNDATION_OPENSSL_TSAN)
    target_compile_definitions(foundation_compiler PRIVATE
        FOUNDATION_GENERATED_THREAD_SANITIZER=1
    )
endif()

add_executable(foundationc compiler/src/main.cpp)
target_link_libraries(foundationc PRIVATE foundation_compiler)

add_executable(foundation-ls compiler/src/lsp_main.cpp)
target_link_libraries(foundation-ls PRIVATE foundation_compiler)

if(WIN32)
    set(FOUNDATION_SELFHOST_TARGET windows)
elseif(APPLE)
    set(FOUNDATION_SELFHOST_TARGET macos)
else()
    set(FOUNDATION_SELFHOST_TARGET linux)
endif()
find_program(FOUNDATION_SELFHOST_CLANG
    NAMES clang-${LLVM_VERSION_MAJOR} clang
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
)
if(NOT FOUNDATION_SELFHOST_CLANG)
    set(FOUNDATION_SELFHOST_CLANG clang)
endif()
if(MSVC)
    set(FOUNDATION_SELFHOST_C_COMPILER "${FOUNDATION_SELFHOST_CLANG}")
else()
    set(FOUNDATION_SELFHOST_C_COMPILER "${CMAKE_C_COMPILER}")
endif()
set(FOUNDATION_SELFHOST_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/selfhost")
set(FOUNDATION_SELFHOST_PROJECT "${FOUNDATION_SELFHOST_DIRECTORY}/project")
set(FOUNDATION_SELFHOST_STAGE1 "${FOUNDATION_SELFHOST_DIRECTORY}/stage1.c")
set(FOUNDATION_SELFHOST_STAGE2 "${FOUNDATION_SELFHOST_DIRECTORY}/stage2.c")
set(FOUNDATION_SELFHOST_STAGE3 "${FOUNDATION_SELFHOST_DIRECTORY}/stage3.c")
set(FOUNDATION_SELFHOST_STAMP "${FOUNDATION_SELFHOST_DIRECTORY}/bootstrap.stamp")
file(GLOB_RECURSE FOUNDATION_SELFHOST_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost/*.fn"
    "${CMAKE_CURRENT_SOURCE_DIR}/foundation/*.fn"
    "${CMAKE_CURRENT_SOURCE_DIR}/std/*.fn"
)
list(APPEND FOUNDATION_SELFHOST_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost/foundation.package"
)
add_custom_command(
    OUTPUT "${FOUNDATION_SELFHOST_STAGE1}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${FOUNDATION_SELFHOST_PROJECT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FOUNDATION_SELFHOST_PROJECT}"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost/foundation.package"
        "${FOUNDATION_SELFHOST_PROJECT}/foundation.package"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost/src"
        "${FOUNDATION_SELFHOST_PROJECT}/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost/tests"
        "${FOUNDATION_SELFHOST_PROJECT}/tests"
    COMMAND ${CMAKE_COMMAND} -E env
        "FOUNDATION_SDK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        $<TARGET_FILE:foundationc> package resolve
        "${FOUNDATION_SELFHOST_PROJECT}"
        --target "${FOUNDATION_SELFHOST_TARGET}"
    COMMAND ${CMAKE_COMMAND} -E env
        "FOUNDATION_SDK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        $<TARGET_FILE:foundationc> emit-c
        "${FOUNDATION_SELFHOST_PROJECT}"
        -o "${FOUNDATION_SELFHOST_STAGE1}"
    DEPENDS foundationc ${FOUNDATION_SELFHOST_INPUTS}
    VERBATIM
)
add_executable(foundationc_selfhost_stage1 EXCLUDE_FROM_ALL "${FOUNDATION_SELFHOST_STAGE1}")
set_target_properties(foundationc_selfhost_stage1 PROPERTIES
    OUTPUT_NAME foundationc-selfhost-stage1
)
target_include_directories(foundationc_selfhost_stage1 PRIVATE runtime/include)
target_link_libraries(foundationc_selfhost_stage1 PRIVATE foundation_runtime)
add_custom_command(
    OUTPUT "${FOUNDATION_SELFHOST_STAGE2}"
    COMMAND ${CMAKE_COMMAND} -E env
        "FOUNDATION_SDK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        $<TARGET_FILE:foundationc_selfhost_stage1> emit-c
        "${FOUNDATION_SELFHOST_PROJECT}"
        -o "${FOUNDATION_SELFHOST_STAGE2}"
    DEPENDS foundationc_selfhost_stage1 "${FOUNDATION_SELFHOST_STAGE1}"
    VERBATIM
)
add_executable(foundationc_selfhost EXCLUDE_FROM_ALL "${FOUNDATION_SELFHOST_STAGE2}")
set_target_properties(foundationc_selfhost PROPERTIES OUTPUT_NAME foundationc-selfhost)
target_include_directories(foundationc_selfhost PRIVATE runtime/include)
target_link_libraries(foundationc_selfhost PRIVATE foundation_runtime)
add_custom_command(
    OUTPUT "${FOUNDATION_SELFHOST_STAMP}"
    BYPRODUCTS "${FOUNDATION_SELFHOST_STAGE3}"
    COMMAND ${CMAKE_COMMAND} -E env
        "FOUNDATION_SDK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        $<TARGET_FILE:foundationc_selfhost> emit-c
        "${FOUNDATION_SELFHOST_PROJECT}"
        -o "${FOUNDATION_SELFHOST_STAGE3}"
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${FOUNDATION_SELFHOST_STAGE2}"
        "${FOUNDATION_SELFHOST_STAGE3}"
    COMMAND ${CMAKE_COMMAND} -E touch "${FOUNDATION_SELFHOST_STAMP}"
    DEPENDS foundationc_selfhost "${FOUNDATION_SELFHOST_STAGE2}"
    VERBATIM
)
add_custom_target(foundation_selfhost_bootstrap
    DEPENDS "${FOUNDATION_SELFHOST_STAMP}"
)
add_custom_target(foundation_selfhost_verify
    COMMAND ${CMAKE_COMMAND} -E env
        "FOUNDATION_SDK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "FOUNDATION_CLANG=${FOUNDATION_SELFHOST_CLANG}"
        "FOUNDATION_C_COMPILER=${FOUNDATION_SELFHOST_C_COMPILER}"
        ${CMAKE_COMMAND}
        -DCOMPILER=$<TARGET_FILE:foundationc_selfhost>
        -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DPROJECT=${FOUNDATION_SELFHOST_PROJECT}
        -DOUTPUT_DIRECTORY=${FOUNDATION_SELFHOST_DIRECTORY}/verify
        -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-selfhost.cmake
    VERBATIM
)
add_dependencies(foundation_selfhost_verify foundation_selfhost_bootstrap)
if(MSVC)
    target_link_options(foundationc PRIVATE /STACK:8388608)
    target_link_options(foundation-ls PRIVATE /STACK:8388608)
    target_link_options(foundationc_selfhost_stage1 PRIVATE /STACK:8388608)
    target_link_options(foundationc_selfhost PRIVATE /STACK:8388608)
endif()

include(GNUInstallDirs)
install(TARGETS foundationc foundation-ls RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS foundationc_selfhost
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
if(TARGET foundation_openssl_provider)
    install(TARGETS foundation_openssl_provider ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES providers/openssl/foundation_openssl.h DESTINATION include/foundation)
endif()
if(TARGET foundation_wamr_provider)
    install(TARGETS foundation_wamr_provider
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(DIRECTORY runtime/include/ DESTINATION runtime/include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY runtime/src/ DESTINATION runtime/src
        FILES_MATCHING PATTERN "*.c" PATTERN "*.h")
install(DIRECTORY std/ DESTINATION std FILES_MATCHING PATTERN "*.fn")
install(DIRECTORY foundation/ DESTINATION foundation FILES_MATCHING PATTERN "*.fn")
install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING PATTERN "*.md")
install(DIRECTORY examples/ DESTINATION examples
        FILES_MATCHING
        PATTERN "*.fn"
        PATTERN "*.c"
        PATTERN "*.h"
        PATTERN "*.md"
        PATTERN "*.out"
        PATTERN "CMakeLists.txt"
        PATTERN "foundation.package"
        PATTERN "foundation.lock")
install(FILES LICENSE-MIT LICENSE-APACHE README.md CONTRIBUTING.md SECURITY.md DESTINATION .)

if(MSVC)
    target_compile_options(foundation_runtime PRIVATE /W4)
    target_compile_options(foundation_compiler PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundationc PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundation-ls PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundationc_selfhost_stage1 PRIVATE
        /W4 $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
    target_compile_options(foundationc_selfhost PRIVATE
        /W4 $<$<NOT:$<CONFIG:Debug>>:/O2>
    )
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE /WX)
        target_compile_options(foundation_compiler PRIVATE /WX)
        target_compile_options(foundationc PRIVATE /WX)
        target_compile_options(foundation-ls PRIVATE /WX)
        target_compile_options(foundationc_selfhost_stage1 PRIVATE /WX)
        target_compile_options(foundationc_selfhost PRIVATE /WX)
    endif()
else()
    target_compile_options(foundation_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundation_compiler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundationc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundation-ls PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundationc_selfhost_stage1 PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(foundationc_selfhost PRIVATE -O2 -Wall -Wextra -Wpedantic)
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE -Werror)
        target_compile_options(foundation_compiler PRIVATE -Werror)
        target_compile_options(foundationc PRIVATE -Werror)
        target_compile_options(foundation-ls PRIVATE -Werror)
        target_compile_options(foundationc_selfhost_stage1 PRIVATE -Werror)
        target_compile_options(foundationc_selfhost PRIVATE -Werror)
    endif()
endif()

if(FOUNDATION_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Foundation fuzz targets require Clang")
    endif()

    target_compile_options(foundation_compiler PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
    )
    target_compile_options(foundation_runtime PRIVATE -fsanitize=address,undefined)
    target_link_options(foundation_runtime PUBLIC -fsanitize=address,undefined)
    target_compile_definitions(foundation_compiler PRIVATE
        FOUNDATION_GENERATED_ADDRESS_UNDEFINED_SANITIZER=1
    )
    target_link_options(foundationc PRIVATE -fsanitize=address,undefined)
    target_link_options(foundation-ls PRIVATE -fsanitize=address,undefined)
    target_compile_options(foundationc_selfhost_stage1 PRIVATE -fsanitize=address,undefined)
    target_link_options(foundationc_selfhost_stage1 PRIVATE -fsanitize=address,undefined)
    target_compile_options(foundationc_selfhost PRIVATE -fsanitize=address,undefined)
    target_link_options(foundationc_selfhost PRIVATE -fsanitize=address,undefined)
    add_executable(foundation_parser_fuzz compiler/fuzz/parser_fuzz.cpp)
    target_link_libraries(foundation_parser_fuzz PRIVATE foundation_compiler)
    target_compile_options(foundation_parser_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
    target_link_options(foundation_parser_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
    )

    set(FOUNDATION_FUZZ_CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus")
    file(MAKE_DIRECTORY "${FOUNDATION_FUZZ_CORPUS_DIR}")
    configure_file(
        tests/fuzz/corpus/language-baseline.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/language-baseline.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/typed-attributes.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/typed-attributes.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/serializer.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/serializer.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/caching.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/caching.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/pooling.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/pooling.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/tracing.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/tracing.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/telemetry.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/telemetry.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/align.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/align.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/errors.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/errors.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/pipeline-middleware.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/pipeline-middleware.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/cancellation.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/cancellation.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/task-suspension.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/task-suspension.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/task-match-storage.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/task-match-storage.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/supervisor.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/supervisor.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/callback.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/callback.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/closures.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/closures.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/transferable-functions.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/transferable-functions.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/result-else-without-binding.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/result-else-without-binding.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/network.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/network.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/generic-lookahead.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/generic-lookahead.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/contract-constraints.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/contract-constraints.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/compound-assignment.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/compound-assignment.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/raw-pointers.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/raw-pointers.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/services.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/services.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/application-plan.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/application-plan.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/match-guards.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/match-guards.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/guard-policy.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/guard-policy.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/hooks.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/hooks.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/httpx-vhost.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/httpx-vhost.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/httpx-client.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/httpx-client.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/httpx-network.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/httpx-network.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/httpx-transport-policy.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/httpx-transport-policy.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/httpx-upload.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/httpx-upload.fn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/select-timeout.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/select-timeout.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/state-machine.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/state-machine.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/state-machine-timeout.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/state-machine-timeout.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/generic-state-machine.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/generic-state-machine.fn"
        COPYONLY
    )
    configure_file(
        tests/cases/accept/shift-operators.fn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/shift-operators.fn"
        COPYONLY
    )
endif()

include(CTest)
if(BUILD_TESTING)
    set(FOUNDATION_SHORT_COMPILE_RUN_TIMEOUT 15)
    set(FOUNDATION_COMPILE_RUN_TIMEOUT 30)
    set(FOUNDATION_SELFHOST_TEST_TIMEOUT 1500)
    if(FOUNDATION_BUILD_FUZZERS OR FOUNDATION_WAMR_SANITIZERS OR
       FOUNDATION_WAMR_TSAN OR FOUNDATION_OPENSSL_TSAN)
        set(FOUNDATION_SHORT_COMPILE_RUN_TIMEOUT 180)
        set(FOUNDATION_COMPILE_RUN_TIMEOUT 180)
        set(FOUNDATION_SELFHOST_TEST_TIMEOUT 7200)
    endif()
    set(FOUNDATION_WAMR_COMPILE_RUN_TIMEOUT ${FOUNDATION_COMPILE_RUN_TIMEOUT})
    if(FOUNDATION_WAMR_COMPILE_RUN_TIMEOUT LESS 60)
        set(FOUNDATION_WAMR_COMPILE_RUN_TIMEOUT 60)
    endif()
    if(TARGET foundation_openssl_provider)
        find_package(Threads REQUIRED)
        target_compile_definitions(foundation_openssl_provider PRIVATE FOUNDATION_OPENSSL_TESTING=1)
        add_executable(foundation_openssl_provider_test
            providers/openssl/foundation_openssl_test.c
        )
        target_link_libraries(foundation_openssl_provider_test PRIVATE foundation_openssl_provider Threads::Threads)
        if(FOUNDATION_BUILD_FUZZERS)
            target_compile_options(foundation_openssl_provider_test PRIVATE -fsanitize=address,undefined)
            target_link_options(foundation_openssl_provider_test PRIVATE -fsanitize=address,undefined)
        elseif(FOUNDATION_OPENSSL_TSAN)
            target_compile_options(foundation_openssl_provider_test PRIVATE -fsanitize=thread)
            target_link_options(foundation_openssl_provider_test PRIVATE -fsanitize=thread)
        endif()
        add_test(
            NAME providers.openssl.native
            COMMAND foundation_openssl_provider_test
        )
        set_tests_properties(providers.openssl.native PROPERTIES TIMEOUT 30)
        add_test(
            NAME providers.openssl.foundation
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/providers-openssl.fn
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/providers-openssl.out
                "-DNATIVE_INPUTS=$<TARGET_FILE:foundation_openssl_provider>;$<TARGET_FILE:OpenSSL::SSL>;$<TARGET_FILE:OpenSSL::Crypto>"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
        )
        set_tests_properties(providers.openssl.foundation PROPERTIES
            TIMEOUT ${FOUNDATION_COMPILE_RUN_TIMEOUT}
        )
    endif()
    if(TARGET foundation_wamr_provider)
        target_compile_definitions(foundation_wamr_provider PRIVATE FOUNDATION_WAMR_TESTING=1)
        add_executable(foundation_wamr_provider_test
            runtime/providers/wamr/wamr_provider_test.c
        )
        target_include_directories(foundation_wamr_provider_test PRIVATE runtime/include)
        target_link_libraries(foundation_wamr_provider_test PRIVATE
            foundation_wamr_provider
            Threads::Threads
        )
        if(MSVC)
            target_compile_options(foundation_wamr_provider_test PRIVATE /W4)
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(foundation_wamr_provider_test PRIVATE /WX)
            endif()
        else()
            target_compile_options(foundation_wamr_provider_test PRIVATE
                -Wall -Wextra -Wpedantic
            )
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(foundation_wamr_provider_test PRIVATE -Werror)
            endif()
        endif()
        if(FOUNDATION_WAMR_SANITIZERS)
            target_compile_options(foundation_wamr_provider_test PRIVATE
                -fsanitize=address,undefined
            )
            target_link_options(foundation_wamr_provider_test PRIVATE
                -fsanitize=address,undefined
            )
        elseif(FOUNDATION_WAMR_TSAN)
            target_compile_options(foundation_wamr_provider_test PRIVATE -fsanitize=thread)
            target_link_options(foundation_wamr_provider_test PRIVATE -fsanitize=thread)
        endif()
    endif()
    add_test(
        NAME repository.no-zz-sources
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-no-zz-sources.cmake
    )
    add_test(
        NAME repository.source-extension
        COMMAND ${CMAKE_COMMAND}
            -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-source-extension.cmake
    )
    if(FOUNDATION_ENABLE_WINDOWS_WINE_MATRIX)
        find_program(FOUNDATION_MINGW_C_COMPILER NAMES x86_64-w64-mingw32-gcc REQUIRED)
        find_program(FOUNDATION_WINE NAMES wine wine64 REQUIRED)
        add_test(
            NAME platform.windows-wine
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DMINGW_C_COMPILER=${FOUNDATION_MINGW_C_COMPILER}
                -DWINE=${FOUNDATION_WINE}
                -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/platform-windows-wine
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-windows-wine.cmake
        )
    endif()
    add_test(
        NAME compiler.check.retired-source-extension
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/retired-source-extension
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-retired-source-extension.cmake
    )
    if(FOUNDATION_V2_SOURCE)
        if(NOT EXISTS "${FOUNDATION_V2_SOURCE}/go.mod")
            message(FATAL_ERROR "FOUNDATION_V2_SOURCE does not contain go.mod")
        endif()
        find_package(Git REQUIRED)
        find_program(FOUNDATION_GO_EXECUTABLE NAMES go REQUIRED)
        add_test(
            NAME compatibility.validation.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/validation/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/validation/go
                -DCASES=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/validation/cases.tsv
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/validation/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-validation-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-validation-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.guard.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/guard/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/guard/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/guard/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-guard-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-guard-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.logger.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/logger/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/logger/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/logger/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-logger-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-logger-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.tracing.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/tracing/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/tracing/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/tracing/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-tracing-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-tracing-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.telemetry.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/telemetry/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/telemetry/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/telemetry/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-telemetry-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-telemetry-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.hooks.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/hooks/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/hooks/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/hooks/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-hooks-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-hooks-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.serializer.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/serializer/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/serializer/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/serializer/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-serializer-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-serializer-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.configuration.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/configuration-migration
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration/go
                -DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/configuration-migration/config.json
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-configuration-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-configuration-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.configuration.directory.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration-directory/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration-directory/go
                -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration-directory/input
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/configuration-directory/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-configuration-directory-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-configuration-directory-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.openapi.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/openapi/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/openapi/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-openapi-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-openapi-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.auth.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/auth/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/auth/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-auth-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-auth-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.resiliency.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/resiliency/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/resiliency/go
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resiliency_native.c
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-resiliency-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-resiliency-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.httpx.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/httpx/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/httpx/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/httpx/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-httpx-v2
                -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-httpx-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.ring.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/ring/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/ring/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-ring-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-ring-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.pipeline.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/pipeline/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/pipeline/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-pipeline-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-pipeline-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.caching.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/caching/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/caching/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-caching-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-caching-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.pooling.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/pooling/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/pooling/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-pooling-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-pooling-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.align.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/align/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/align/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-align-v2
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-align-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.cpio.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/cpio/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/cpio/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-cpio-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-cpio-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.errutil.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/errutil/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/errutil/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-errutil-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-errutil-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.adapters.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/adapters/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/adapters/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-adapters-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-adapters-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.fsm.machine.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/fsm-machine/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/fsm-machine/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-fsm-machine-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-fsm-machine-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.safemap.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/safemap/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/safemap/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-safemap-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-safemap-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.lock.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/lock/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/lock/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-lock-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-lock-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.worker.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/worker/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/worker/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-worker-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-worker-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.scheduler.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/scheduler/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/scheduler/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-scheduler-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-scheduler-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.secrets.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/secrets/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/secrets/go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-secrets-v2
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-secrets-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.app.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/app/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/app/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/app/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-app-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-app-v2-compatibility.cmake
        )
        add_test(
            NAME compatibility.saga.v2
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXECUTABLE}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                -DV2_SOURCE=${FOUNDATION_V2_SOURCE}
                -DFOUNDATION_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/saga/foundation
                -DGO_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/saga/go
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/saga/expected.out
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/compatibility-saga-v2
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-saga-v2-compatibility.cmake
        )
        set_tests_properties(
            compatibility.configuration.v2
            PROPERTIES ENVIRONMENT "FOUNDATION_CONFIGURATION_HOST=environment.example"
        )
        set_tests_properties(
            compatibility.secrets.v2
            PROPERTIES ENVIRONMENT "FOUNDATION_SECRET_TEST=environment-value"
        )
    endif()
    add_test(
        NAME tooling.sdk.relocated
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DSDK_DIR=${CMAKE_BINARY_DIR}/sdk-test
            -DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-sdk-bundle.cmake
    )
    add_executable(foundation_runtime_panic_test runtime/tests/panic_trace_test.c)
    target_link_libraries(foundation_runtime_panic_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_allocation_test runtime/tests/allocation_panic_test.c)
    target_link_libraries(foundation_runtime_allocation_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_counter_test runtime/tests/allocation_counter_test.c)
    target_link_libraries(foundation_runtime_counter_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_string_test runtime/tests/string_test.c)
    target_link_libraries(foundation_runtime_string_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_guard_test runtime/tests/guard_test.c)
    target_link_libraries(foundation_runtime_guard_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_u64_test runtime/tests/u64_test.c)
    target_link_libraries(foundation_runtime_u64_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_parse_test runtime/tests/parse_test.c)
    target_link_libraries(foundation_runtime_parse_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_crypto_test runtime/tests/crypto_test.c)
    target_link_libraries(foundation_runtime_crypto_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_env_test runtime/tests/env_test.c)
    target_link_libraries(foundation_runtime_env_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_fs_test runtime/tests/fs_test.c)
    target_link_libraries(foundation_runtime_fs_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_fs_host_test runtime/tests/fs_host_test.c)
    target_link_libraries(foundation_runtime_fs_host_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_fs_package_test runtime/tests/fs_package_test.c)
    target_link_libraries(foundation_runtime_fs_package_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_process_test runtime/tests/process_test.c)
    target_link_libraries(foundation_runtime_process_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_io_test runtime/tests/io_test.c)
    target_link_libraries(foundation_runtime_io_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_terminal_test runtime/tests/terminal_test.c)
    target_link_libraries(foundation_runtime_terminal_test PRIVATE foundation_runtime)
    set(foundation_host_service_test_targets
        foundation_runtime_fs_host_test
        foundation_runtime_fs_package_test
        foundation_runtime_process_test
        foundation_runtime_io_test
        foundation_runtime_terminal_test
    )
    if(MSVC)
        foreach(host_service_test IN LISTS foundation_host_service_test_targets)
            target_compile_options(${host_service_test} PRIVATE /W4)
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(${host_service_test} PRIVATE /WX)
            endif()
        endforeach()
    else()
        foreach(host_service_test IN LISTS foundation_host_service_test_targets)
            target_compile_options(${host_service_test} PRIVATE
                -Wall -Wextra -Wpedantic
            )
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(${host_service_test} PRIVATE -Werror)
            endif()
            if(FOUNDATION_BUILD_FUZZERS)
                target_compile_options(${host_service_test} PRIVATE
                    -fsanitize=address,undefined
                )
                target_link_options(${host_service_test} PRIVATE
                    -fsanitize=address,undefined
                )
            endif()
        endforeach()
    endif()
    add_executable(foundation_runtime_time_test runtime/tests/time_test.c)
    target_link_libraries(foundation_runtime_time_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_uuid_test runtime/tests/uuid_test.c)
    target_link_libraries(foundation_runtime_uuid_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_task_test runtime/tests/task_test.c)
    target_link_libraries(foundation_runtime_task_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_cancellation_test runtime/tests/cancellation_test.c)
    target_link_libraries(foundation_runtime_cancellation_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_channel_test runtime/tests/channel_test.c)
    target_link_libraries(foundation_runtime_channel_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_blocking_test runtime/tests/blocking_test.c)
    target_link_libraries(foundation_runtime_blocking_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_pool_test runtime/tests/pool_test.c)
    target_link_libraries(foundation_runtime_pool_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_reactor_test runtime/tests/reactor_test.c)
    target_link_libraries(foundation_runtime_reactor_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_resiliency_test runtime/tests/resiliency_test.c)
    target_link_libraries(foundation_runtime_resiliency_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_net_test runtime/tests/net_test.c)
    target_link_libraries(foundation_runtime_net_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_plugin_test runtime/tests/plugin_test.c)
    target_link_libraries(foundation_runtime_plugin_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_plugin_sandbox_test
        runtime/tests/plugin_sandbox_test.c
    )
    target_link_libraries(foundation_runtime_plugin_sandbox_test PRIVATE foundation_runtime)
    add_executable(foundation_wasm_plugin_abi_test
        runtime/tests/wasm_plugin_abi_test.c
    )
    target_include_directories(foundation_wasm_plugin_abi_test PRIVATE runtime/include)
    add_executable(foundation_plugin_sandbox_helper
        tests/fixtures/plugin_sandbox_helper.c
    )
    if(MSVC)
        target_compile_options(foundation_runtime_plugin_sandbox_test PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_plugin_sandbox_test PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_runtime_plugin_sandbox_test PRIVATE
            -Wall -Wextra -Wpedantic
        )
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_plugin_sandbox_test PRIVATE -Werror)
        endif()
    endif()
    if(MSVC)
        target_compile_options(foundation_plugin_sandbox_helper PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_plugin_sandbox_helper PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_plugin_sandbox_helper PRIVATE
            -Wall -Wextra -Wpedantic
        )
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_plugin_sandbox_helper PRIVATE -Werror)
        endif()
    endif()
    add_library(foundation_plugin_fixture SHARED tests/fixtures/plugin_fixture.c)
    target_include_directories(foundation_plugin_fixture PRIVATE runtime/include)
    add_library(foundation_plugin_abi_mismatch SHARED tests/fixtures/plugin_fixture.c)
    target_include_directories(foundation_plugin_abi_mismatch PRIVATE runtime/include)
    target_compile_definitions(foundation_plugin_abi_mismatch PRIVATE
        FDN_PLUGIN_FIXTURE_ABI_MISMATCH=1
    )
    add_library(foundation_plugin_start_fail SHARED tests/fixtures/plugin_fixture.c)
    target_include_directories(foundation_plugin_start_fail PRIVATE runtime/include)
    target_compile_definitions(foundation_plugin_start_fail PRIVATE
        FDN_PLUGIN_FIXTURE_START_FAIL=1
    )
    foreach(plugin_failure IN ITEMS query target contract descriptor create)
        add_library(foundation_plugin_${plugin_failure}_failure SHARED
            tests/fixtures/plugin_fixture.c
        )
        target_include_directories(foundation_plugin_${plugin_failure}_failure
            PRIVATE runtime/include
        )
    endforeach()
    target_compile_definitions(foundation_plugin_query_failure PRIVATE
        FDN_PLUGIN_FIXTURE_QUERY_FAIL=1
    )
    add_library(foundation_plugin_sdk_compat SHARED tests/fixtures/plugin_fixture.c)
    target_include_directories(foundation_plugin_sdk_compat PRIVATE runtime/include)
    target_compile_definitions(foundation_plugin_sdk_compat PRIVATE
        FDN_PLUGIN_FIXTURE_OTHER_SDK=1
    )
    target_compile_definitions(foundation_plugin_target_failure PRIVATE
        FDN_PLUGIN_FIXTURE_TARGET_MISMATCH=1
    )
    target_compile_definitions(foundation_plugin_contract_failure PRIVATE
        FDN_PLUGIN_FIXTURE_CONTRACT_MISMATCH=1
    )
    target_compile_definitions(foundation_plugin_descriptor_failure PRIVATE
        FDN_PLUGIN_FIXTURE_INVALID_DESCRIPTOR=1
    )
    target_compile_definitions(foundation_plugin_create_failure PRIVATE
        FDN_PLUGIN_FIXTURE_CREATE_FAIL=1
    )
    add_library(foundation_plugin_missing_query SHARED tests/fixtures/plugin_missing_query.c)
    add_library(foundation_wamr_provider_fake SHARED
        tests/fixtures/wamr_provider_fake.c
    )
    target_compile_definitions(foundation_runtime PRIVATE
        FOUNDATION_WAMR_BRIDGE_TESTING=1
    )
    target_include_directories(foundation_wamr_provider_fake PRIVATE runtime/include)
    target_compile_definitions(foundation_wamr_provider_fake PRIVATE FOUNDATION_WAMR_PROVIDER_BUILD=1)
    target_link_libraries(foundation_wamr_provider_fake PRIVATE Threads::Threads)
    add_executable(foundation_wamr_bridge_test
        runtime/tests/wamr_bridge_test.c
    )
    target_link_libraries(foundation_wamr_bridge_test PRIVATE
        foundation_runtime
        foundation_wamr_provider_fake
        Threads::Threads
    )
    add_test(
        NAME runtime.wamr-bridge
        COMMAND foundation_wamr_bridge_test $<TARGET_FILE:foundation_wamr_provider_fake>
    )
    set_tests_properties(runtime.wamr-bridge PROPERTIES TIMEOUT 30)
    add_library(foundation_example_native_plugin SHARED
        examples/native-plugin/native/plugin.c
    )
    target_include_directories(foundation_example_native_plugin PRIVATE runtime/include)
    set(foundation_native_plugin_fixture_targets
        foundation_plugin_fixture
        foundation_plugin_abi_mismatch
        foundation_plugin_start_fail
        foundation_plugin_query_failure
        foundation_plugin_sdk_compat
        foundation_plugin_target_failure
        foundation_plugin_contract_failure
        foundation_plugin_descriptor_failure
        foundation_plugin_create_failure
        foundation_plugin_missing_query
        foundation_wamr_provider_fake
        foundation_example_native_plugin
    )
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        foreach(plugin_target IN LISTS foundation_native_plugin_fixture_targets)
            target_compile_options(${plugin_target} PRIVATE -fno-sanitize=all)
            target_link_options(${plugin_target} PRIVATE -fno-sanitize=all)
        endforeach()
    endif()
    if(FOUNDATION_WAMR_SANITIZERS)
        target_compile_options(foundation_wamr_provider_fake PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_wamr_provider_fake PRIVATE -fsanitize=address,undefined)
        target_compile_options(foundation_wamr_bridge_test PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_wamr_bridge_test PRIVATE -fsanitize=address,undefined)
    elseif(FOUNDATION_WAMR_TSAN)
        target_compile_options(foundation_wamr_provider_fake PRIVATE -fsanitize=thread)
        target_link_options(foundation_wamr_provider_fake PRIVATE -fsanitize=thread)
        target_compile_options(foundation_wamr_bridge_test PRIVATE -fsanitize=thread)
        target_link_options(foundation_wamr_bridge_test PRIVATE -fsanitize=thread)
    endif()
    add_executable(foundation_example_process_plugin
        examples/process-plugin/worker/main.c
    )
    set(foundation_plugin_test_targets
        foundation_runtime_plugin_test
        foundation_wasm_plugin_abi_test
        foundation_plugin_fixture
        foundation_plugin_abi_mismatch
        foundation_plugin_start_fail
        foundation_plugin_query_failure
        foundation_plugin_sdk_compat
        foundation_plugin_target_failure
        foundation_plugin_contract_failure
        foundation_plugin_descriptor_failure
        foundation_plugin_create_failure
        foundation_plugin_missing_query
        foundation_wamr_provider_fake
        foundation_wamr_bridge_test
        foundation_example_native_plugin
        foundation_example_process_plugin
    )
    if(FOUNDATION_BUILD_FUZZERS)
        target_compile_options(foundation_runtime_plugin_test PRIVATE
            -fsanitize=address,undefined
        )
        target_link_options(foundation_runtime_plugin_test PRIVATE
            -fsanitize=address,undefined
        )
        target_compile_options(foundation_runtime_plugin_sandbox_test PRIVATE
            -fsanitize=address,undefined
        )
        target_link_options(foundation_runtime_plugin_sandbox_test PRIVATE
            -fsanitize=address,undefined
        )
    endif()
    if(MSVC)
        foreach(plugin_target IN LISTS foundation_plugin_test_targets)
            target_compile_options(${plugin_target} PRIVATE /W4)
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(${plugin_target} PRIVATE /WX)
            endif()
        endforeach()
    else()
        foreach(plugin_target IN LISTS foundation_plugin_test_targets)
            target_compile_options(${plugin_target} PRIVATE -Wall -Wextra -Wpedantic)
            if(FOUNDATION_WARNINGS_AS_ERRORS)
                target_compile_options(${plugin_target} PRIVATE -Werror)
            endif()
        endforeach()
    endif()
    if(FOUNDATION_BUILD_FUZZERS)
        foreach(runtime_test IN ITEMS
                foundation_runtime_panic_test
                foundation_runtime_allocation_test
                foundation_runtime_counter_test
                foundation_runtime_string_test
                foundation_runtime_guard_test
                foundation_runtime_u64_test
                foundation_runtime_parse_test
                foundation_runtime_crypto_test
                foundation_runtime_env_test
                foundation_runtime_fs_test
                foundation_runtime_time_test
                foundation_runtime_uuid_test
                foundation_runtime_task_test
                foundation_runtime_cancellation_test
                foundation_runtime_channel_test
                foundation_runtime_blocking_test
                foundation_runtime_pool_test
                foundation_runtime_reactor_test
                foundation_runtime_resiliency_test
                foundation_runtime_net_test)
            target_compile_options(${runtime_test} PRIVATE -fsanitize=address,undefined)
            target_link_options(${runtime_test} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
    if(MSVC)
        target_compile_options(foundation_runtime_panic_test PRIVATE /W4)
        target_compile_options(foundation_runtime_allocation_test PRIVATE /W4)
        target_compile_options(foundation_runtime_counter_test PRIVATE /W4)
        target_compile_options(foundation_runtime_string_test PRIVATE /W4)
        target_compile_options(foundation_runtime_guard_test PRIVATE /W4)
        target_compile_options(foundation_runtime_u64_test PRIVATE /W4)
        target_compile_options(foundation_runtime_parse_test PRIVATE /W4)
        target_compile_options(foundation_runtime_env_test PRIVATE /W4)
        target_compile_options(foundation_runtime_fs_test PRIVATE /W4)
        target_compile_options(foundation_runtime_time_test PRIVATE /W4)
        target_compile_options(foundation_runtime_uuid_test PRIVATE /W4)
        target_compile_options(foundation_runtime_task_test PRIVATE /W4)
        target_compile_options(foundation_runtime_cancellation_test PRIVATE /W4)
        target_compile_options(foundation_runtime_channel_test PRIVATE /W4)
        target_compile_options(foundation_runtime_blocking_test PRIVATE /W4)
        target_compile_options(foundation_runtime_pool_test PRIVATE /W4)
        target_compile_options(foundation_runtime_reactor_test PRIVATE /W4)
        target_compile_options(foundation_runtime_resiliency_test PRIVATE /W4)
        target_compile_options(foundation_runtime_net_test PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_panic_test PRIVATE /WX)
            target_compile_options(foundation_runtime_allocation_test PRIVATE /WX)
            target_compile_options(foundation_runtime_counter_test PRIVATE /WX)
            target_compile_options(foundation_runtime_string_test PRIVATE /WX)
            target_compile_options(foundation_runtime_guard_test PRIVATE /WX)
            target_compile_options(foundation_runtime_u64_test PRIVATE /WX)
            target_compile_options(foundation_runtime_parse_test PRIVATE /WX)
            target_compile_options(foundation_runtime_env_test PRIVATE /WX)
            target_compile_options(foundation_runtime_fs_test PRIVATE /WX)
            target_compile_options(foundation_runtime_time_test PRIVATE /WX)
            target_compile_options(foundation_runtime_uuid_test PRIVATE /WX)
            target_compile_options(foundation_runtime_task_test PRIVATE /WX)
            target_compile_options(foundation_runtime_cancellation_test PRIVATE /WX)
            target_compile_options(foundation_runtime_channel_test PRIVATE /WX)
            target_compile_options(foundation_runtime_blocking_test PRIVATE /WX)
            target_compile_options(foundation_runtime_pool_test PRIVATE /WX)
            target_compile_options(foundation_runtime_reactor_test PRIVATE /WX)
            target_compile_options(foundation_runtime_resiliency_test PRIVATE /WX)
            target_compile_options(foundation_runtime_net_test PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_runtime_panic_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_allocation_test PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(foundation_runtime_counter_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_string_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_guard_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_u64_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_parse_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_env_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_fs_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_time_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_uuid_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_task_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_cancellation_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_channel_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_blocking_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_pool_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_reactor_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_resiliency_test PRIVATE
                               -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_net_test PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_panic_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_allocation_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_counter_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_string_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_guard_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_u64_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_parse_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_env_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_fs_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_time_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_uuid_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_task_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_cancellation_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_channel_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_blocking_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_pool_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_reactor_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_resiliency_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_net_test PRIVATE -Werror)
        endif()
    endif()

    add_executable(foundation_compiler_tests compiler/tests/compiler_test.cpp)
    target_link_libraries(foundation_compiler_tests PRIVATE foundation_compiler)
    target_compile_definitions(foundation_compiler_tests PRIVATE
        FOUNDATION_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_executable(foundation_lsp_tests compiler/tests/lsp_test.cpp)
    target_link_libraries(foundation_lsp_tests PRIVATE foundation_compiler)
    add_executable(foundation_formatter_tests compiler/tests/formatter_test.cpp)
    target_link_libraries(foundation_formatter_tests PRIVATE foundation_compiler)
    target_compile_definitions(foundation_formatter_tests PRIVATE
        FOUNDATION_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(FOUNDATION_BUILD_FUZZERS)
        target_link_options(foundation_compiler_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_lsp_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_formatter_tests PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(foundation_compiler_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_options(foundation_lsp_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_options(foundation_formatter_tests PRIVATE /W4 /permissive- /EHsc)
        target_link_options(foundation_compiler_tests PRIVATE /STACK:8388608)
        target_link_options(foundation_lsp_tests PRIVATE /STACK:8388608)
        target_link_options(foundation_formatter_tests PRIVATE /STACK:8388608)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_compiler_tests PRIVATE /WX)
            target_compile_options(foundation_lsp_tests PRIVATE /WX)
            target_compile_options(foundation_formatter_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_compiler_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_lsp_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_formatter_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_compiler_tests PRIVATE -Werror)
            target_compile_options(foundation_lsp_tests PRIVATE -Werror)
            target_compile_options(foundation_formatter_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME compiler.unit COMMAND foundation_compiler_tests)
    add_test(NAME tooling.lsp.unit COMMAND foundation_lsp_tests)
    add_test(NAME tooling.formatter.unit COMMAND foundation_formatter_tests)
    if(FOUNDATION_BUILD_FUZZERS)
        set_tests_properties(tooling.lsp.unit PROPERTIES TIMEOUT 3600)
    endif()
    add_test(
        NAME tooling.test.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSUCCESS=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/test-declarations.fn
            -DISOLATION=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/test-isolation.fn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-tests.cmake
    )
    add_test(
        NAME tooling.test.cli.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSUCCESS=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/test-declarations.fn
            -DISOLATION=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/test-isolation.fn
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-tests.cmake
    )
    add_test(
        NAME compiler.check.test-empty-name
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/test-empty-name.fn
            -DCODE=FDN2211
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.test-duplicate-name
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/test-duplicate-name.fn
            -DCODE=FDN2211
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME tooling.formatter.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/messy.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/expected.fn
            -DINVALID=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/invalid.fn
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/formatter-project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-format.cmake
    )
    add_test(
        NAME tooling.imports.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/imports/messy.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/imports/expected.fn
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/imports-project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-imports.cmake
    )
    add_test(
        NAME tooling.documentation.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/documentation-api
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/documentation-api/expected.md
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/documentation-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-documentation.cmake
    )
    add_test(
        NAME tooling.lint.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/lint-profile
            -DRULE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/fcs-rules
            -DVIOLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/fcs-violations
            -DINVALID=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unknown-associated-function.fn
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/lint-project
            -DTARGET=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-lint.cmake
    )
    add_executable(foundation_package_tests
        compiler/tests/package_test.cpp
        compiler/tests/package_cache_test.cpp
        compiler/tests/package_catalog_test.cpp
        compiler/tests/package_lock_test.cpp
        compiler/tests/package_project_test.cpp
        compiler/tests/package_registry_test.cpp
        compiler/tests/package_resolver_test.cpp
        compiler/tests/package_source_test.cpp
        compiler/tests/sha256_test.cpp
    )
    target_link_libraries(foundation_package_tests PRIVATE foundation_compiler)
    if(FOUNDATION_BUILD_FUZZERS)
        target_link_options(foundation_package_tests PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(foundation_package_tests PRIVATE /W4 /permissive- /EHsc)
        target_link_options(foundation_package_tests PRIVATE /STACK:8388608)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_package_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_package_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_package_tests PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME packages.unit COMMAND foundation_package_tests)
    add_test(
        NAME packages.workflow
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:foundationc>
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/package-workflow
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/package-workflow-test
            -DHOST_TARGET=${FOUNDATION_SELFHOST_TARGET}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-package-workflow.cmake
    )
    add_test(
        NAME compiler.target-selection.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/target-selection
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/target-selection-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-target-selection.cmake
    )
    add_test(NAME runtime.string COMMAND foundation_runtime_string_test)
    add_test(NAME runtime.guard COMMAND foundation_runtime_guard_test)
    add_test(NAME runtime.u64 COMMAND foundation_runtime_u64_test)
    add_test(NAME runtime.parse COMMAND foundation_runtime_parse_test)
    add_test(NAME runtime.crypto COMMAND foundation_runtime_crypto_test)
    add_test(NAME runtime.env COMMAND foundation_runtime_env_test)
    add_test(NAME runtime.time COMMAND foundation_runtime_time_test)
    add_test(NAME runtime.uuid COMMAND foundation_runtime_uuid_test)
    add_test(NAME runtime.task COMMAND foundation_runtime_task_test)
    add_test(NAME runtime.cancellation COMMAND foundation_runtime_cancellation_test)
    add_test(NAME runtime.channel COMMAND foundation_runtime_channel_test)
    add_test(NAME runtime.blocking COMMAND foundation_runtime_blocking_test)
    add_test(NAME runtime.pool COMMAND foundation_runtime_pool_test)
    add_test(NAME runtime.reactor COMMAND foundation_runtime_reactor_test)
    add_test(NAME runtime.resiliency COMMAND foundation_runtime_resiliency_test)
    add_test(NAME runtime.net COMMAND foundation_runtime_net_test)
    add_test(
        NAME runtime.plugin
        COMMAND foundation_runtime_plugin_test
            $<TARGET_FILE:foundation_plugin_fixture>
            $<TARGET_FILE:foundation_plugin_missing_query>
            $<TARGET_FILE:foundation_plugin_abi_mismatch>
            $<TARGET_FILE:foundation_plugin_start_fail>
            $<TARGET_FILE:foundation_plugin_query_failure>
            $<TARGET_FILE:foundation_plugin_sdk_compat>
            $<TARGET_FILE:foundation_plugin_target_failure>
            $<TARGET_FILE:foundation_plugin_contract_failure>
            $<TARGET_FILE:foundation_plugin_descriptor_failure>
            $<TARGET_FILE:foundation_plugin_create_failure>
    )
    add_test(NAME runtime.plugin-sandbox COMMAND foundation_runtime_plugin_sandbox_test)
    add_test(NAME runtime.wasm-plugin-abi COMMAND foundation_wasm_plugin_abi_test)
    add_test(
        NAME compiler.run.plugin-wamr-provider-fake
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wamr-provider.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wamr-provider.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_wamr_provider_fake>;fixture.wasm"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    set_tests_properties(
        compiler.run.plugin-wamr-provider-fake
        PROPERTIES TIMEOUT ${FOUNDATION_COMPILE_RUN_TIMEOUT}
    )
    find_program(FOUNDATION_WASM_CLANG
        NAMES clang clang-21
        HINTS ${LLVM_TOOLS_BINARY_DIR}
    )
    find_program(FOUNDATION_WASM_LINKER
        NAMES wasm-ld wasm-ld-21
        HINTS ${LLVM_TOOLS_BINARY_DIR}
    )
    if(FOUNDATION_WASM_CLANG AND FOUNDATION_WASM_LINKER)
        add_test(
            NAME abi.wasm-plugin-guest
            COMMAND ${CMAKE_COMMAND}
                -DCLANG=${FOUNDATION_WASM_CLANG}
                -DLINKER=${FOUNDATION_WASM_LINKER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm_plugin_fixture.c
                -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-fixture.wasm
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-wasm-guest.cmake
        )
        add_test(
            NAME abi.wasm-plugin-guest-bad-signature
            COMMAND ${CMAKE_COMMAND}
                -DCLANG=${FOUNDATION_WASM_CLANG}
                -DLINKER=${FOUNDATION_WASM_LINKER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm_plugin_fixture.c
                -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -DDEFINITIONS=-DFDN_WASM_FIXTURE_BAD_SIGNATURE=1
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-bad-signature.wasm
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-wasm-guest.cmake
        )
        add_test(
            NAME abi.wasm-plugin-guest-invalid-metadata
            COMMAND ${CMAKE_COMMAND}
                -DCLANG=${FOUNDATION_WASM_CLANG}
                -DLINKER=${FOUNDATION_WASM_LINKER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm_plugin_fixture.c
                -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -DDEFINITIONS=-DFDN_WASM_FIXTURE_INVALID_METADATA=1
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-invalid-metadata.wasm
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-wasm-guest.cmake
        )
        add_test(
            NAME abi.wasm-plugin-guest-missing-memory-export
            COMMAND ${CMAKE_COMMAND}
                -DCLANG=${FOUNDATION_WASM_CLANG}
                -DLINKER=${FOUNDATION_WASM_LINKER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm_plugin_fixture.c
                -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -DEXPORT_MEMORY=OFF
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-missing-memory.wasm
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-wasm-guest.cmake
        )
        set_tests_properties(
            abi.wasm-plugin-guest
            abi.wasm-plugin-guest-bad-signature
            abi.wasm-plugin-guest-invalid-metadata
            abi.wasm-plugin-guest-missing-memory-export
            PROPERTIES FIXTURES_SETUP foundation_wamr_guests
        )
        if(TARGET foundation_wamr_provider)
            add_test(
                NAME providers.wamr.native
                COMMAND foundation_wamr_provider_test
                    ${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-fixture.wasm
            )
            add_test(
                NAME compiler.run.plugin-wamr-provider-real
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wamr-provider.fn
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wamr-provider.out
                    "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_wamr_provider>;${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-fixture.wasm;${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-bad-signature.wasm;${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-invalid-metadata.wasm;${CMAKE_CURRENT_BINARY_DIR}/wasm-plugin-missing-memory.wasm"
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
            )
            set_tests_properties(
                providers.wamr.native
                PROPERTIES
                    FIXTURES_REQUIRED foundation_wamr_guests
                    TIMEOUT 30
            )
            set_tests_properties(
                compiler.run.plugin-wamr-provider-real
                PROPERTIES
                    FIXTURES_REQUIRED foundation_wamr_guests
                    TIMEOUT ${FOUNDATION_WAMR_COMPILE_RUN_TIMEOUT}
            )
        endif()
    endif()
    add_test(
        NAME compiler.check.plugins
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugins.fn
    )
    add_test(
        NAME compiler.check.plugin-wasm-policy
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-policy.fn
    )
    add_test(
        NAME compiler.run.plugin-wasm-policy
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-policy.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-policy.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-wasm-policy-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-policy.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-wasm-policy.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-wasm-policy-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-policy.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.plugin-wasm-discovery
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-discovery.fn
    )
    add_test(
        NAME compiler.run.plugin-wasm-discovery
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-discovery.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-discovery.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm-discovery"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-wasm-discovery-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-discovery.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-wasm-discovery.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-wasm-discovery-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-wasm-discovery.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wasm-discovery"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.plugins
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugins.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugins.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_fixture>;$<TARGET_FILE:foundation_plugin_start_fail>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugins-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugins.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugins.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugins-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugins.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_PLUGIN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_fixture>;$<TARGET_FILE:foundation_plugin_start_fail>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.native-plugin-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/native-plugin
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/native-plugin/expected.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_example_native_plugin>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.process-plugin-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/process-plugin
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/process-plugin/expected.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_example_process_plugin>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-duplicate
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-duplicate.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-duplicate.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_fixture>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-duplicate-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-duplicate.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-duplicate.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-duplicate-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-duplicate.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_PLUGIN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_fixture>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.plugin-factories
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-factories.fn
    )
    add_test(
        NAME compiler.run.plugin-factories
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-factories.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-factories.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-factories-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-factories.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-factories.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-factories-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-factories.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.plugin-sandbox
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox.fn
    )
    add_test(
        NAME compiler.run.plugin-sandbox
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_sandbox_helper>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-sandbox-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-sandbox.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-sandbox-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_PLUGIN_SANDBOX_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin_sandbox.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_sandbox_helper>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.plugin-sandbox-reject
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox-reject.fn
    )
    add_test(
        NAME compiler.run.plugin-sandbox-reject
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox-reject.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox-reject.out
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_sandbox_helper>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.plugin-sandbox-reject-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox-reject.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/plugin-sandbox-reject.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/plugin-sandbox-reject-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/plugin-sandbox-reject.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_PLUGIN_SANDBOX_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/plugin_sandbox.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_plugin_sandbox_helper>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    set_tests_properties(runtime.env PROPERTIES
        ENVIRONMENT "FOUNDATION_ENV_TEST=foundation env value"
    )
    add_test(
        NAME runtime.fs
        COMMAND foundation_runtime_fs_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs
            ${CMAKE_CURRENT_BINARY_DIR}/fs-growing.jsonl
    )
    add_test(
        NAME runtime.fs-host
        COMMAND foundation_runtime_fs_host_test ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME runtime.fs-package
        COMMAND foundation_runtime_fs_package_test ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(
        NAME runtime.process
        COMMAND foundation_runtime_process_test ${CMAKE_CURRENT_BINARY_DIR}
    )
    add_test(NAME runtime.io COMMAND foundation_runtime_io_test)
    add_test(NAME runtime.terminal COMMAND foundation_runtime_terminal_test)
    add_test(
        NAME compiler.run.host-services.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/host-services.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/host-services.out
            -DBACKEND=llvm
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_runtime_process_test>;${CMAKE_CURRENT_BINARY_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.host-services.c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/host-services.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/host-services.out
            -DBACKEND=c
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_runtime_process_test>;${CMAKE_CURRENT_BINARY_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.process-pty.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/process-pty.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/process-pty.out
            -DBACKEND=llvm
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_runtime_process_test>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.process-pty.c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/process-pty.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/process-pty.out
            -DBACKEND=c
            "-DPROGRAM_ARGS=$<TARGET_FILE:foundation_runtime_process_test>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.terminal.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/terminal.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/terminal.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.terminal.c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/terminal.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/terminal.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.package-primitives.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.out
            -DEXPECTED_ERROR=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.err
            -DBACKEND=llvm
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_BINARY_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.package-primitives.c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.out
            -DEXPECTED_ERROR=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/package-primitives.err
            -DBACKEND=c
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_BINARY_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    set_tests_properties(
        runtime.fs-host
        runtime.fs-package
        runtime.process
        runtime.io
        runtime.terminal
        compiler.run.host-services.llvm
        compiler.run.host-services.c
        compiler.run.package-primitives.llvm
        compiler.run.package-primitives.c
        PROPERTIES TIMEOUT ${FOUNDATION_COMPILE_RUN_TIMEOUT}
    )
    add_test(
        NAME runtime.panic-native-boundary
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:foundation_runtime_panic_test>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-native-panic.cmake
    )
    add_test(
        NAME runtime.allocation-panic
        COMMAND ${CMAKE_COMMAND}
            -DPROGRAM=$<TARGET_FILE:foundation_runtime_allocation_test>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-allocation-panic.cmake
    )
    set_tests_properties(runtime.allocation-panic PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=allocator_may_return_null=1"
    )
    add_test(NAME runtime.allocation-counters COMMAND foundation_runtime_counter_test)
    add_test(
        NAME compiler.selfhost.llvm
        COMMAND $<TARGET_FILE:foundationc> test
            ${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost
            --backend llvm
    )
    add_test(
        NAME compiler.selfhost.c
        COMMAND $<TARGET_FILE:foundationc> test
            ${CMAKE_CURRENT_SOURCE_DIR}/compiler/selfhost
            --backend c
    )
    set_tests_properties(
        compiler.selfhost.llvm
        compiler.selfhost.c
        PROPERTIES TIMEOUT ${FOUNDATION_SELFHOST_TEST_TIMEOUT}
    )
    foreach(backend IN ITEMS llvm c)
        add_test(
            NAME compiler.test.temporary-field-cleanup.${backend}
            COMMAND $<TARGET_FILE:foundationc> test
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/temporary-field-cleanup.fn
                --backend ${backend}
        )
    endforeach()
    add_test(
        NAME compiler.check.accept
        COMMAND foundationc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
    )
    add_test(
        NAME compiler.check.comments
        COMMAND foundationc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/comments.fn
    )
    add_test(
        NAME compiler.check.ownership-markers
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership-markers.fn
    )
    add_test(
        NAME compiler.check.constructors
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/constructors.fn
    )
    foreach(constructor_case IN ITEMS
            "constructor-top-level;FDN1242"
            "constructor-enum;FDN1243"
            "constructor-contract;FDN1244"
            "constructor-explicit-return;FDN1241")
        list(GET constructor_case 0 constructor_name)
        list(GET constructor_case 1 constructor_code)
        add_test(
            NAME compiler.check.${constructor_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${constructor_name}.fn
                -DCODE=${constructor_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.ownership-markers
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership-markers.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership-markers.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.accept
        COMMAND foundationc emit-c
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello.c
    )
    add_test(
        NAME compiler.emit.pii
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/native-interface
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/native-interface-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-pii.cmake
    )
    add_test(
        NAME compiler.build.library-artifacts
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/native-interface
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/native-library-artifact-test
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}
            -DNM=${CMAKE_NM}
            -DREADELF=${CMAKE_READELF}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-library-artifacts.cmake
    )
    find_program(FOUNDATION_ZIG_EXECUTABLE NAMES zig
                 HINTS "$ENV{HOME}/.local/zig-0.16.0")
    find_program(FOUNDATION_CARGO_EXECUTABLE NAMES cargo)
    find_program(FOUNDATION_GO_EXPORT_EXECUTABLE NAMES go go.exe)
    if(UNIX AND FOUNDATION_ZIG_EXECUTABLE AND FOUNDATION_CARGO_EXECUTABLE AND
       FOUNDATION_GO_EXPORT_EXECUTABLE)
        add_test(
            NAME compiler.package.export-native
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/native-interface
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/package-export-test
                -DZIG_EXECUTABLE=${FOUNDATION_ZIG_EXECUTABLE}
                -DCARGO_EXECUTABLE=${FOUNDATION_CARGO_EXECUTABLE}
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXPORT_EXECUTABLE}
                -DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-package-export.cmake
        )
    endif()
    if(FOUNDATION_GO_EXPORT_EXECUTABLE)
        add_test(
            NAME compiler.package.export-go-source
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-interface
                -DUNSUPPORTED_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/native-interface
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-task
                -DOWN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-own-receiver
                -DESCAPE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-conditional-escape
                -DOWNER_DESTRUCTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-owner-destructure
                -DOPEN_GENERIC_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-open-generic
                -DGENERIC_STRUCT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/go-source-unsupported-generic-struct
                -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/package-export/go-source/source_test.go
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/go-source-export-test
                -DGO_EXECUTABLE=${FOUNDATION_GO_EXPORT_EXECUTABLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-go-source-export.cmake
        )
    endif()
    add_test(
        NAME compiler.run.accept
        COMMAND foundationc run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
    )
    set_tests_properties(compiler.run.accept PROPERTIES
        PASS_REGULAR_EXPRESSION "hello from foundation"
    )
    add_test(
        NAME compiler.emit.llvm.accept
        COMMAND foundationc emit-llvm
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello.ll
    )
    add_test(
        NAME compiler.emit.llvm.debug-info
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/llvm-aggregates.fn
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/llvm-debug-info.ll
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-llvm-debug-info.cmake
    )
    add_test(
        NAME compiler.emit.llvm.project-debug-info
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/services
            -DEXPECTED_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/services/src/main.fn
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/llvm-project-debug-info.ll
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-llvm-debug-info.cmake
    )
    add_test(
        NAME compiler.build.llvm.option-order
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.out
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/build-option-order
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-build-options.cmake
    )
    add_test(
        NAME compiler.run.llvm.accept
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.aggregates
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/llvm-aggregates.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/llvm-aggregates.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.runtime-boundaries
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/llvm-runtime-boundaries.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/llvm_runtime_boundaries.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/llvm-runtime-boundaries.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.llvm.task-suspension
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.channel-operations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-operations.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-operations.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.channel-select
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-select.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-select.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.channel-close-cancel
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-close-cancel.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channel-close-cancel.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.cancellation
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.blocking-native
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.llvm.callback-native
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/callback_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.llvm.c-abi
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/native.c
            -DNATIVE_SECOND=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/support.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/expected.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native.cmake
    )
    add_test(
        NAME compiler.run.llvm.closures
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.errors
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.match-exits
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-exits.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-exits.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.for-sequences
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.adapters
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.machine-scalars
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/machine-scalars.fn
            --backend llvm
    )
    add_test(
        NAME compiler.run.llvm.lock
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.main-arguments
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.out
            "-DPROGRAM_ARGS=alpha;two words"
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.contract-inheritance
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.for-iterators
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.range
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/range.fn
            --backend llvm
    )
    add_test(
        NAME compiler.run.llvm.state-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.state-machine-timeout
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.generic-state-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.workflows
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.llvm.fsm-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.check.state-machine
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.fn
    )
    add_test(
        NAME compiler.run.state-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.state-machine-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/state-machine.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/state-machine-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.state-machine-timeout
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.fn
    )
    add_test(
        NAME compiler.run.state-machine-timeout
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.state-machine-timeout-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/state-machine-timeout.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/state-machine-timeout-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.generic-state-machine
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.fn
    )
    add_test(
        NAME compiler.run.c.generic-state-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.generic-state-machine-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/generic-state-machine.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generic-state-machine-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-state-machine.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.state-machine-timeout-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/state-machine-timeout.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/state-machine-timeout-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/state-machine-timeout-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME tooling.fsm.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine
            -DEXPECTED_MERMAID=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine/expected.mmd
            -DEXPECTED_GRAPHVIZ=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine/expected.dot
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/fsm-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-fsm.cmake
    )
    add_test(
        NAME compiler.run.example-state-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.example-state-machine-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/example-state-machine.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-state-machine-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/state-machine/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.fsm-machine
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.fsm-machine-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fsm-machine.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fsm-machine-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.fsm-machine-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-machine.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/fsm-machine-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/fsm-machine-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.fsm-concurrent
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-concurrent.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-concurrent.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.fsm-concurrent-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-concurrent.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fsm-concurrent.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fsm-concurrent-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-concurrent.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.fsm-concurrent-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fsm-concurrent.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/fsm-concurrent-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/fsm-concurrent-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.check.fsm-guard-error-type
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/fsm-guard-error-type.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.state-machine-unknown-state
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/state-machine-unknown-state.fn
            -DCODE=FDN1210
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.state-machine-borrowed-payload
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/state-machine-borrowed-payload.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.workflows
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.fn
    )
    add_test(
        NAME compiler.run.workflows
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.workflows-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/workflows.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/workflows-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.workflows-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/workflows.metadata.first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/workflows.metadata.second.json
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/workflows.metadata.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-metadata.cmake
    )
    add_test(
        NAME compiler.check.pipeline-middleware
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pipeline-middleware.fn
    )
    add_test(
        NAME compiler.run.pipeline-middleware
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pipeline-middleware.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pipeline-middleware.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.pipeline-middleware-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pipeline-middleware.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/pipeline-middleware.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pipeline-middleware-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pipeline-middleware.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.example-runtime-pipeline
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/runtime-pipeline
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/runtime-pipeline/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.example-runtime-pipeline-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/runtime-pipeline
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/example-runtime-pipeline.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-runtime-pipeline
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/runtime-pipeline/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.package-keyword-import-alias
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/package-keyword-import-alias.fn
            -DCODE=FDN1093
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.pipeline-builder-use-after-then
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/pipeline-builder-use-after-then.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    foreach(workflow_reject IN ITEMS
            workflow-unknown-step:FDN3009
            workflow-edit-step:FDN2224
            workflow-chain-mismatch:FDN2011
            saga-compensation-result:FDN2011
            saga-without-compensation:FDN1237
            workflow-malformed-header:FDN1215
            workflow-retry-range:FDN1230)
        string(REPLACE ":" ";" workflow_reject_parts ${workflow_reject})
        list(GET workflow_reject_parts 0 workflow_reject_name)
        list(GET workflow_reject_parts 1 workflow_reject_code)
        add_test(
            NAME compiler.check.${workflow_reject_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${workflow_reject_name}.fn
                -DCODE=${workflow_reject_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.tasks
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.tasks-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/tasks.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tasks
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.cancellation
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.cancellation-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/cancellation.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cancellation
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.supervisor
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.supervisor-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/supervisor.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/supervisor
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.supervisor-result-task
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/supervisor-result-task.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.task-group
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-group.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-group.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.task-group-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-group.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/task-group.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/task-group
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-group.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.worker-pool
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.worker-pool-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/worker-pool.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/worker-pool
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.worker-pool-channel
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-channel.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-channel.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.guard-transfer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/guard-transfer.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/guard-transfer.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.guard-transfer-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/guard-transfer.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/guard-transfer.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/guard-transfer
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/guard-transfer.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.worker-pool-function
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-function.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-function.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.worker-pool-channel-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-channel.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/worker-pool-channel.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/worker-pool-channel
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/worker-pool-channel.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    foreach(worker_pool_case IN ITEMS
            worker-pool-stored-task
            worker-pool-custom-drop
            worker-pool-function-capture
            worker-pool-function-borrow
            worker-pool-function-unqualified
            worker-pool-function-generic
            worker-pool-function-unconstrained)
        add_test(
            NAME compiler.check.${worker_pool_case}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${worker_pool_case}.fn
                -DCODE=FDN2185
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.worker-pool-result-task
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/worker-pool-result-task.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.worker-pool-after-shutdown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/worker-pool-after-shutdown.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.scheduler-after-shutdown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/scheduler-after-shutdown.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.scheduler
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.scheduler-cron
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler-cron.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler-cron.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.scheduler-store-drain-failure
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler-store-drain-failure.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler-store-drain-failure.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.channels
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.channels-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/channels.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/channels
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    foreach(channel_case IN ITEMS
            channel-operations
            channel-close-cancel
            channel-select
            channel-sender-clone)
        add_test(
            NAME compiler.run.${channel_case}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${channel_case}.fn
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${channel_case}.out
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
        )
        add_test(
            NAME compiler.run.${channel_case}-allocations
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${channel_case}.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${channel_case}.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${channel_case}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${channel_case}.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
        )
    endforeach()
    foreach(channel_clone_reject IN ITEMS
            "channel-receiver-clone;FDN2100"
            "channel-sender-clone-argument;FDN2010")
        list(GET channel_clone_reject 0 channel_clone_name)
        list(GET channel_clone_reject 1 channel_clone_code)
        add_test(
            NAME compiler.check.${channel_clone_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${channel_clone_name}.fn
                -DCODE=${channel_clone_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.blocking-native
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.resiliency-rate-limit
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.resiliency-policies
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resiliency_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.raw-pointers
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/raw-pointers.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/raw_pointers.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/raw-pointers.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    foreach(native_systems_backend IN ITEMS llvm c)
        add_test(
            NAME compiler.run.native-systems-${native_systems_backend}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/native-systems.fn
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native_systems.c
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/native-systems.out
                -DBACKEND=${native_systems_backend}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.service-actions
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/service-actions.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/service-actions.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.services-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/services/src/main.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/services/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.config-binding-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/config-binding
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/config-binding/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-app-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-app-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app/expected.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.auth-web-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/auth-web
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/auth-web/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.auth-web-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/auth-web
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/auth-web/expected.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.resiliency-web-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-web
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-web/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.resiliency-web-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-web
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-web/expected.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )

    add_test(
        NAME compiler.run.resiliency-policies-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-policies
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-policies/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.resiliency-policies-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-policies
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/resiliency-policies/expected.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.hosting
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hosting.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hosting.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.hosting-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hosting.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hosting.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.health
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/health.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/health.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.health-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/health.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/health.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.saga-runtime
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/saga-runtime.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/saga-runtime.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.saga-runtime-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/saga-runtime.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/saga-runtime.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.errors
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.errors-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.out
            -DASSERT_ALLOCATIONS=1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.errors-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/errors-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/errors-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.emit.service-actions-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/service-actions.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/service-actions.metadata.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/service-actions-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/service-actions-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-metadata.cmake
    )
    add_test(
        NAME compiler.emit.application-plan
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/application-plan.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/application-plan.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/application-plan-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/application-plan-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-plan.cmake
    )
    add_test(
        NAME compiler.emit.openapi
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/openapi-document
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/openapi-document/expected.openapi.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/openapi-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/openapi-second.json
            -DOVERRIDE=${CMAKE_CURRENT_BINARY_DIR}/openapi-override.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-openapi.cmake
    )
    add_test(
        NAME compiler.emit.application-host
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/services
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-project
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-project-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/application-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/services/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.web-app-host
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/web-app-host
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-app-host-inspection.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/examples/web-app/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-packages
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-packages
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-packages
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-packages-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-packages/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-packages/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host-packages.cmake
    )
    add_test(
        NAME compiler.emit.application-host-fallible-startup
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-fallible
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-fallible
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-fallible-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-fallible/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-fallible/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-transient
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-transient
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-transient-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-scope
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-scope
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-scope
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-scope-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-scope/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-scope/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-di
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-di
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-di
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-di-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-di/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-di/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web-regex
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-regex
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-regex
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-regex-inspection.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-regex/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web-activation
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation-inspection.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web-middleware
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware-inspection.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web-middleware-plan
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware/expected-plan.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware-plan-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware-plan-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-plan.cmake
    )
    add_test(
        NAME compiler.emit.application-host-bind
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-bind
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-bind
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-bind-inspection.fn
            -DGENERATED_C=${CMAKE_CURRENT_BINARY_DIR}/application-host-bind.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/application-host-bind-allocations
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-bind/expected.out
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-bind.cmake
    )
    add_test(
        NAME compiler.emit.configuration-migration
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/configuration-migration
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/configuration-migration
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/configuration-migration-inspection.fn
            -DGENERATED_C=${CMAKE_CURRENT_BINARY_DIR}/configuration-migration.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/configuration-migration-allocations
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/configuration-migration/expected.out
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-configuration-migration.cmake
    )
    set_tests_properties(
        compiler.emit.configuration-migration
        PROPERTIES ENVIRONMENT "FOUNDATION_CONFIGURATION_HOST=environment.example"
    )
    add_test(
        NAME compiler.derive.package-validation
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/derived-validation
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/derived-validation
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-derived-validation.cmake
    )
    add_test(
        NAME compiler.derive.package-guard
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/derived-guard
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/derived-guard
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-derived-guard.cmake
    )
    add_test(
        NAME compiler.emit.guard-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/compatibility/guard/foundation
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/guard-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/guard-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.emit.application-host-web-stale
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-stale
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-stale-inspection.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host-stale.cmake
    )
    add_test(
        NAME compiler.emit.application-host-transient-fallible
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient-fallible
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-transient-fallible
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-transient-fallible-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient-fallible/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-transient-fallible/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-dispatcher
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-dispatcher
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-dispatcher
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-dispatcher-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-dispatcher/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-dispatcher/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-task-dispatcher
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-task-dispatcher
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-task-dispatcher
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-task-dispatcher-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-task-dispatcher/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-task-dispatcher/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    add_test(
        NAME compiler.emit.application-host-events
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-events
            -DPROJECT=${CMAKE_CURRENT_BINARY_DIR}/application-host-events
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-events-inspection.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-events/expected-host.fn
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-events/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-host.cmake
    )
    foreach(application_host_case IN ITEMS
            "application-host-fallible;FDN2332"
            "application-host-singleton-scoped;FDN2338"
            "application-host-scope-fallible;FDN2339"
            "application-host-activation-fallible;FDN2340"
            "application-host-action-borrowed-parameter;FDN2341"
            "application-host-action-borrowed-result;FDN2342"
            "application-host-introspection-collision;FDN2334"
            "application-host-empty-policy;FDN2343"
            "application-host-duplicate-policy;FDN2344"
            "application-host-web-method-route;FDN2350"
            "application-host-web-generic-route;FDN2351"
            "application-host-web-empty-path;FDN2353"
            "application-host-web-duplicate-route;FDN2354"
            "application-host-web-multiple-binding;FDN2355"
            "application-host-web-edit-binding;FDN2356"
            "application-host-web-missing-provider;FDN2357"
            "application-host-web-ambiguous-provider;FDN2358"
            "application-host-web-unsupported-binding;FDN2359"
            "application-host-web-empty-binding;FDN2360"
            "application-host-web-return;FDN2362"
            "application-host-web-invalid-path;FDN2364"
            "application-host-web-invalid-regex;FDN2364"
            "application-host-web-duplicate-binding;FDN2365"
            "application-host-web-body-form;FDN2366"
            "application-host-web-path-binding;FDN2367"
            "application-host-web-body-type;FDN2368"
            "application-host-web-ambiguous-route;FDN2369"
            "application-host-bind-generic;FDN2370"
            "application-host-bind-orphan-field;FDN2371"
            "application-host-bind-conflicting-field;FDN2372"
            "application-host-bind-duplicate-key;FDN2373"
            "application-host-bind-unsupported-field;FDN2374"
            "application-host-bind-method-collision;FDN2375"
            "application-host-bind-empty-source;FDN2376"
            "application-host-bind-duplicate-source;FDN2377"
            "application-host-bind-json-conflict;FDN2378"
            "application-host-bind-json-body-type;FDN2379"
            "application-host-bind-json-empty-name;FDN2380"
            "application-host-bind-json-duplicate-name;FDN2381"
            "application-host-web-body-uninitialized;FDN2382"
            "application-host-validation-generic;FDN2384"
            "application-host-validation-orphan-field;FDN2385"
            "application-host-validation-method-collision;FDN2386"
            "application-host-validation-field-type;FDN2387"
            "application-host-validation-range;FDN2388"
            "application-host-validation-rule-signature;FDN2392"
            "application-host-validation-nested-cycle;FDN2390"
            "application-host-validation-pattern;FDN2391"
            "application-host-guard-orphan-field;FDN2430"
            "application-host-guard-generic;FDN2431"
            "application-host-guard-method-collision;FDN2432"
            "application-host-guard-get-roles-collision;FDN2432"
            "application-host-guard-empty-rule;FDN2433"
            "application-host-guard-dynamic-empty-rule;FDN2433"
            "application-host-guard-duplicate-rule;FDN2434"
            "application-host-guard-dynamic-duplicate-rule;FDN2434"
            "application-host-guard-field-type;FDN2435"
            "application-host-web-activation-input;FDN2393"
            "application-host-web-activation-errors;FDN2394"
            "application-host-web-task-singleton-transfer;FDN2395"
            "application-host-web-task-owned-duplicate;FDN2396"
            "application-host-web-middleware-signature;FDN2397"
            "application-host-web-middleware-prefix;FDN2398"
            "application-host-web-middleware-order;FDN2399"
            "application-host-web-middleware-route;FDN2400"
            "application-host-web-middleware-group;FDN2400"
            "application-host-openapi-non-route;FDN2401"
            "application-host-openapi-response;FDN2402"
            "application-host-openapi-parameter;FDN2403"
            "application-host-serializer-orphan-field;FDN2410"
            "application-host-serializer-generic;FDN2411"
            "application-host-serializer-empty-name;FDN2412"
            "application-host-serializer-duplicate-name;FDN2413"
            "application-host-serializer-ignored-name;FDN2414"
            "application-host-serializer-method-signature;FDN2415"
            "application-host-serializer-method-pair;FDN2416"
            "application-host-serializer-unsupported-field;FDN2417"
            "application-host-serializer-method-collision;FDN2418"
            "application-host-transfer;FDN2333")
        list(GET application_host_case 0 application_host_name)
        list(GET application_host_case 1 application_host_code)
        add_test(
            NAME compiler.emit.${application_host_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DAPPLICATION_COMMAND=emit-app-host
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${application_host_name}.fn
                -DCODE=${application_host_code}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${application_host_name}.fn
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-reject.cmake
        )
    endforeach()
    foreach(application_case IN ITEMS
            "application-missing-provider;FDN2307"
            "application-ambiguous-provider;FDN2308"
            "application-cycle;FDN2311"
            "application-lifetime-capture;FDN2310"
            "application-missing-constructor;FDN2301"
            "application-multiple-constructors;FDN2302"
            "application-orphan-from;FDN2319"
            "application-empty-provider-name;FDN2345"
            "application-empty-provider-selector;FDN2346"
            "application-duplicate-action;FDN2315"
            "application-duplicate-key;FDN2317"
            "application-consuming-singleton;FDN2320")
        list(GET application_case 0 application_name)
        list(GET application_case 1 application_code)
        add_test(
            NAME compiler.emit.${application_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${application_name}.fn
                -DCODE=${application_code}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${application_name}.json
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-application-reject.cmake
        )
    endforeach()
    foreach(action_case IN ITEMS action-owner action-receiver)
        add_test(
            NAME compiler.check.${action_case}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${action_case}.fn
                -DCODE=FDN2215
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.blocking-native-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/blocking-native.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/blocking-native
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.resiliency-rate-limit-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/resiliency-rate-limit.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/resiliency-rate-limit
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.resiliency-policies-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/resiliency-policies.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/resiliency-policies
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resiliency_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.callback-native
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/callback_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.callback-native-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/callback-native.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/callback-native
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/callback_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.fs-watch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/watch_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.fs-watch-c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/watch_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.fs-watch-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fs-watch.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fs-watch-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/watch_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_WATCH_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/watch.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    foreach(blocking_case IN ITEMS
            "blocking-outside-task;FDN2180"
            "blocking-nested;FDN2168"
            "blocking-body;FDN2178"
            "blocking-parentheses;FDN2179"
            "blocking-duplicate;FDN2179"
            "blocking-function-value;FDN2178"
            "blocking-wrong-target;FDN2178"
            "blocking-reserved-name;FDN2150")
        list(GET blocking_case 0 blocking_name)
        list(GET blocking_case 1 blocking_code)
        add_test(
            NAME compiler.check.${blocking_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${blocking_name}.fn
                -DCODE=${blocking_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(callback_case IN ITEMS
            "callback-outside-task;FDN2183"
            "callback-nested;FDN2168"
            "callback-body;FDN2178"
            "callback-return;FDN2182"
            "callback-parentheses;FDN2181"
            "callback-duplicate;FDN2181"
            "callback-blocking;FDN2181"
            "callback-function-value;FDN2178"
            "callback-wrong-target;FDN2178"
            "callback-reserved-name;FDN2150"
            "callback-cancel-invalid;FDN2181"
            "callback-cancel-conflict;FDN2181")
        list(GET callback_case 0 callback_name)
        list(GET callback_case 1 callback_code)
        add_test(
            NAME compiler.check.${callback_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${callback_name}.fn
                -DCODE=${callback_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(select_reject IN ITEMS
            select-outside-task
            select-missing-error
            select-receive-unbound
            select-timeout-type
            select-invalid-operation)
        if(select_reject STREQUAL "select-outside-task")
            set(select_code FDN2174)
        elseif(select_reject STREQUAL "select-missing-error")
            set(select_code FDN1156)
        elseif(select_reject STREQUAL "select-receive-unbound")
            set(select_code FDN2177)
        elseif(select_reject STREQUAL "select-timeout-type")
            set(select_code FDN2011)
        else()
            set(select_code FDN2175)
        endif()
        add_test(
            NAME compiler.check.${select_reject}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${select_reject}.fn
                -DCODE=${select_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.task-suspension
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.task-suspension-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/task-suspension.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/task-suspension
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    foreach(task_case IN ITEMS
            "task-direct-call;FDN2163"
            "task-wait-without-transfer;FDN2166"
            "task-double-wait;FDN2065"
            "spawn-function;FDN2163"
            "qualified-task-direct-call;FDN2163"
            "qualified-spawn-function;FDN2163"
            "task-borrow-parameter;FDN2165"
            "cancellation-token-reuse;FDN2065"
            "task-wait-expression;FDN2168"
            "task-wait-ignored-result;FDN2168"
            "channel-borrow-payload;FDN2165"
            "channel-missing-endpoint;FDN2133"
            "channel-wrong-capacity;FDN2011"
            "channel-never-payload;FDN2047"
            "channel-operation-outside-task;FDN2168"
            "channel-operation-nested;FDN2168")
        list(GET task_case 0 task_name)
        list(GET task_case 1 task_code)
        add_test(
            NAME compiler.check.${task_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${task_name}.fn
                -DCODE=${task_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.reject
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/legacy-semicolon.fn
            -DCODE=FDN0001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.legacy-let
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/legacy-let.fn
            -DCODE=FDN1184
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    foreach(legacy_mode_case IN ITEMS
            "legacy-receiver-mode;FDN1185"
            "legacy-parameter-mode;FDN1186"
            "legacy-edit-expression;FDN1187"
            "legacy-capture-mode;FDN1188"
            "legacy-capture-clause;FDN1189")
        list(GET legacy_mode_case 0 legacy_mode_name)
        list(GET legacy_mode_case 1 legacy_mode_code)
        add_test(
            NAME compiler.check.${legacy_mode_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${legacy_mode_name}.fn
                -DCODE=${legacy_mode_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.unterminated-block-comment
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unterminated-block-comment.fn
            -DCODE=FDN0006
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/does-not-exist.fn
            -DCODE=FDN3001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.type-mismatch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/type-mismatch.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.typed-attributes
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.typed-attributes-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.metadata.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-metadata.cmake
    )
    foreach(attribute_case IN ITEMS
            "attribute-unknown;FDN2152"
            "attribute-wrong-target;FDN2153"
            "attribute-duplicate;FDN2154"
            "attribute-positional-after-named;FDN2155"
            "attribute-unknown-argument;FDN2156"
            "attribute-extra-argument;FDN2156"
            "attribute-duplicate-argument;FDN2157"
            "attribute-missing-argument;FDN2158"
            "attribute-nonconstant;FDN2160"
            "attribute-unsafe-type;FDN2161"
            "attribute-metadata-cycle;FDN2162"
            "attribute-expanding-cycle;FDN2162"
            "attribute-reserved-target;FDN2150"
            "attribute-duplicate-schema;FDN2151"
            "attribute-target-member;FDN1146"
            "attribute-type-mismatch;FDN2011")
        list(GET attribute_case 0 attribute_name)
        list(GET attribute_case 1 attribute_code)
        add_test(
            NAME compiler.check.${attribute_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${attribute_name}.fn
                -DCODE=${attribute_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.typed-control
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.main-arguments
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.out
            "-DPROGRAM_ARGS=alpha;two words"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.main-arguments-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/main-arguments.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/main-arguments
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=alpha;two words"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.platform-current
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.platform-current-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/platform-current.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/platform-current
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.target-unknown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/target-unknown.fn
            -DCODE=FDN1142
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.target-duplicate
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/target-duplicate.fn
            -DCODE=FDN1144
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.environment
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    set_tests_properties(compiler.run.environment PROPERTIES
        ENVIRONMENT "FOUNDATION_ENV_TEST=foundation env value"
    )
    add_test(
        NAME compiler.run.environment-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/environment.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/environment
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    set_tests_properties(compiler.run.environment-allocations PROPERTIES
        ENVIRONMENT "FOUNDATION_ENV_TEST=foundation env value"
    )
    add_test(
        NAME compiler.run.text-path
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.bytes-crypto
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/bytes-crypto.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/bytes-crypto.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.bytes-crypto-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/bytes-crypto.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/bytes-crypto.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bytes-crypto
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/bytes-crypto.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    set(cpio_security_program_args "")
    if(NOT WIN32)
        set(cpio_security_root "${CMAKE_CURRENT_BINARY_DIR}/cpio-security")
        set(cpio_symlink_source "${cpio_security_root}/source")
        set(cpio_symlink_destination "${cpio_security_root}/destination")
        set(cpio_symlink_outside "${cpio_security_root}/outside")
        file(REMOVE_RECURSE "${cpio_security_root}")
        file(MAKE_DIRECTORY
            "${cpio_symlink_source}"
            "${cpio_symlink_destination}"
            "${cpio_symlink_outside}"
        )
        file(CREATE_LINK "${cpio_symlink_outside}"
            "${cpio_symlink_source}/link" SYMBOLIC
            RESULT cpio_source_link_result
        )
        file(CREATE_LINK "${cpio_symlink_outside}"
            "${cpio_symlink_destination}/link" SYMBOLIC
            RESULT cpio_destination_link_result
        )
        if(NOT cpio_source_link_result EQUAL 0 OR
           NOT cpio_destination_link_result EQUAL 0)
            message(FATAL_ERROR "cannot create CPIO security fixtures")
        endif()
        set(cpio_security_program_args
            ";${cpio_symlink_source};${cpio_symlink_destination}"
        )
    endif()
    add_test(
        NAME compiler.run.cpio
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cpio.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cpio.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs;${CMAKE_CURRENT_BINARY_DIR}/cpio-output${cpio_security_program_args}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.cpio-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cpio.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/cpio.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cpio
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cpio.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs;${CMAKE_CURRENT_BINARY_DIR}/cpio-output-allocations${cpio_security_program_args}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.fs-tree
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-tree.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-tree.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs;${CMAKE_CURRENT_BINARY_DIR}/fs-tree-output"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.fs-tree-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-tree.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fs-tree.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fs-tree
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-tree.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs;${CMAKE_CURRENT_BINARY_DIR}/fs-tree-output-allocations"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/secrets-store")
    add_test(
        NAME compiler.run.secrets
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets.out
            -DPROGRAM_ARGS=${CMAKE_CURRENT_BINARY_DIR}/secrets-store
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    set_tests_properties(compiler.run.secrets PROPERTIES
        ENVIRONMENT "FOUNDATION_SECRET_TEST=environment-value")
    add_test(
        NAME compiler.run.secrets-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/secrets.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/secrets
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets.out
            -DPROGRAM_ARGS=${CMAKE_CURRENT_BINARY_DIR}/secrets-store
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    set_tests_properties(compiler.run.secrets-allocations PROPERTIES
        ENVIRONMENT "FOUNDATION_SECRET_TEST=environment-value")
    add_test(
        NAME compiler.run.secrets-concurrent
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.secrets-concurrent-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.auth-hs256
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-hs256.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-hs256.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.auth-hs256-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-hs256.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/auth-hs256.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/auth-hs256
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-hs256.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.auth-web
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-web.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-web.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.auth-web-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-web.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/auth-web.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/auth-web
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/auth-web.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.text-path-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/text-path.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/text-path
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.text-builder-format
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.text-builder-format-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/text-builder-format.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/text-builder-format
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.pattern
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pattern.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pattern.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.pattern-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pattern.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/pattern.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pattern
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pattern.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.json-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.json-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/json-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/json-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.json-stringify
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.json-stringify-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/json-stringify.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/json-stringify
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.serializer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.serializer-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/serializer.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/serializer
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.example-serializer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.example-serializer-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/example-serializer.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-serializer
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.parse-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.uuid-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/uuid-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/uuid-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.uuid-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/uuid-values.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/uuid-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/uuid-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/uuid-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.parse-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/parse-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parse-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.field-borrows
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.field-borrows-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/field-borrows.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/field-borrows
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.conflicting-field-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conflicting-field-borrow.fn
            -DCODE=FDN2073
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.filesystem-lines
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.filesystem-lines-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-lines.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-lines
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.filesystem-read-text
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.filesystem-read-text-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-read-text.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-read-text
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.network-tcp
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/net_server.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.network-tcp-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/network-tcp.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/network-tcp
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/net_server.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.network-datagram.llvm
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-datagram.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-datagram.out
            -DBACKEND=llvm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.network-datagram.c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-datagram.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-datagram.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-server
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-server-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-server.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-server
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.web-cors
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-cors-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-cors.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-cors
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.web-routing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-routing-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-routing.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-routing
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.web-middleware
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-compression
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-compression.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-compression.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-middleware-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-middleware.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-middleware
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.application-host-web-activation-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.web-protocol-errors
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.web-protocol-errors-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-protocol-errors.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-protocol-errors
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.filesystem-directory
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.filesystem-directory-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-directory.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-directory
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.time-filesystem
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.time-filesystem-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/time-filesystem.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/time-filesystem
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.main-arguments
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/main-arguments.fn
            -DCODE=FDN2007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.len-invalid
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/len-invalid.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.language-tour
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/language-tour/src/main.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/language-tour/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.packages
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.typed-attributes-project
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.typed-attributes-project-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes/expected.metadata.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-project-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-project-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-metadata.cmake
    )
    add_test(
        NAME compiler.check.typed-attributes-private
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes-private
            -DCODE=FDN3008
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.emit.packages-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/packages-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/packages-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.emit.package-source-order
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/order
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/package-order.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-project-order.cmake
    )
    add_test(
        NAME compiler.run.package-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/panic
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-project-panic.cmake
    )
    add_test(
        NAME compiler.run.contract-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/contract-panic
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-contract-panic.cmake
    )
    add_test(
        NAME compiler.run.contract-default-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/contract-default-panic
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-contract-default-panic.cmake
    )
    foreach(project_case IN ITEMS
            "empty;FDN3002"
            "missing-package;FDN3003"
            "malformed-package;FDN1090"
            "malformed-import;FDN1091"
            "missing-import;FDN3005"
            "duplicate-import;FDN3004"
            "duplicate-alias;FDN3004"
            "cycle;FDN3006"
            "private;FDN3008"
            "private-function-value;FDN3008"
            "private-members;FDN3008"
            "private-method;FDN3008"
            "private-associated-function;FDN3008"
            "private-contract-method;FDN3008"
            "unknown-declaration;FDN3009"
            "alias-shadow;FDN3004"
            "alias-match-shadow;FDN3004"
            "multiple-main;FDN3010"
            "duplicate-symbol;FDN2001"
            "duplicate-type;FDN2020")
        list(GET project_case 0 project_name)
        list(GET project_case 1 project_code)
        add_test(
            NAME compiler.check.project-${project_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/${project_name}
                -DCODE=${project_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.struct-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/struct-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/struct-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.field-defaults
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-defaults.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-defaults.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.field-defaults-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-defaults.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/field-defaults.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/field-defaults
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-defaults.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.options-migration
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.options-migration-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/options-migration.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/options-migration
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.field-default-type
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/field-default-type.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.attribute-field-default
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/attribute-field-default.fn
            -DCODE=FDN2160
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.struct-missing-field
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/struct-missing-field.fn
            -DCODE=FDN2027
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.enum-match
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/enum-match.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/enum-match.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.match-read-parameter
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-read-parameter.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-read-parameter.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.match-read-parameter-c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-read-parameter.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-read-parameter.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.match-blocks
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-blocks.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-blocks.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.match-blocks-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-blocks.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/match-blocks.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/match-blocks
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-blocks.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.match-block-tail
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-block-tail.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.named-enum-payloads
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/named-enum-payloads.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/named-enum-payloads.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.named-enum-payloads-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/named-enum-payloads.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/named-enum-payloads.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/named-enum-payloads
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/named-enum-payloads.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.match-guards
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-guards.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-guards.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.match-guards-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-guards.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/match-guards.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/match-guards
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-guards.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    foreach(match_guard_reject IN ITEMS
            "match-guard-type;FDN2011"
            "match-guard-nonexhaustive;FDN2040"
            "match-guard-move;FDN2228"
            "match-wildcard-unreachable;FDN2039"
            "match-wildcard-payload;FDN2041")
        list(GET match_guard_reject 0 match_guard_name)
        list(GET match_guard_reject 1 match_guard_code)
        add_test(
            NAME compiler.check.${match_guard_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${match_guard_name}.fn
                -DCODE=${match_guard_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.named-enum-payload-unknown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/named-enum-payload-unknown.fn
            -DCODE=FDN2206
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.unnamed-enum-payload-label
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unnamed-enum-payload-label.fn
            -DCODE=FDN2209
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.match-literal-nonexhaustive
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-literal-nonexhaustive.fn
            -DCODE=FDN2040
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.match-literal-duplicate
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-literal-duplicate.fn
            -DCODE=FDN2039
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.match-literal-after-binding
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-literal-after-binding.fn
            -DCODE=FDN2039
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.match-literal-unsupported
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-literal-unsupported.fn
            -DCODE=FDN2210
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.conditionals
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/conditionals.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/conditionals.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.empty-tests
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/empty-tests.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/empty-tests.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.check.empty-test-number
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/empty-test-number.fn
            -DCODE=FDN2220
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.empty-test-edit-method
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/empty-test-edit-method.fn
            -DCODE=FDN2220
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.conditional-branch-mismatch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conditional-branch-mismatch.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.conditional-missing-value
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conditional-missing-value.fn
            -DCODE=FDN1178
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.conditional-maybe-transferred
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conditional-maybe-transferred.fn
            -DCODE=FDN2066
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.short-guard-multiline
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/short-guard-multiline.fn
            -DCODE=FDN1173
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.loop-jumps
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/loop-jumps.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/loop-jumps.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.check.break-outside-loop
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/break-outside-loop.fn
            -DCODE=FDN2200
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.continue-outside-loop
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/continue-outside-loop.fn
            -DCODE=FDN2200
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.break-value
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/break-value.fn
            -DCODE=FDN1179
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.for-sequences
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.for-sequences-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/for-sequences.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/for-sequences
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-sequences.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.for-iterators
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.for-iterators-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/for-iterators.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/for-iterators
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/for-iterators.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.for-iterator-next
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-iterator-next.fn
            -DCODE=FDN2204
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.for-iterator-edit
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-iterator-edit.fn
            -DCODE=FDN2205
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.for-non-sequence
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-non-sequence.fn
            -DCODE=FDN2201
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.for-edit-immutable
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-edit-immutable.fn
            -DCODE=FDN2202
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.for-task-rvalue
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-task-rvalue.fn
            -DCODE=FDN2203
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.for-transfer-sequence
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/for-transfer-sequence.fn
            -DCODE=FDN2067
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.match-exhaustive
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-non-exhaustive.fn
            -DCODE=FDN2040
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.generic-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.generic-copy-callback-option
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-copy-callback-option.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-copy-callback-option.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.generic-lookahead
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-lookahead.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-lookahead.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.contract-constraints
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.contract-constraints-c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.contract-constraints-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-constraints.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-constraints-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.contract-constraints-static
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-constraints.fn
            -DC_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-constraints-static.c
            -DLLVM_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-constraints-static.ll
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-contract-constraints.cmake
    )
    add_test(
        NAME compiler.run.contract-constraint-package
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/contract-constraint-package
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/contract-constraint-package
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-contract-constraint-project.cmake
    )
    add_test(
        NAME compiler.check.contract-constraint-missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/contract-constraint-missing.fn
            -DCODE=FDN2251
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.contract-constraint-non-contract
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/contract-constraint-non-contract.fn
            -DCODE=FDN2250
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.contract-constraint-existential
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/contract-constraint-existential.fn
            -DCODE=FDN2251
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.contract-constraint-own-bare
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/contract-constraint-own-bare.fn
            -DCODE=FDN2103
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.contract-constraint-own-use-after-transfer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/contract-constraint-own-use-after-transfer.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.compound-assignment
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.compound-assignment-c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.compound-assignment-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/compound-assignment.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/compound-assignment-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/compound-assignment.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.compound-immutable
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/compound-immutable.fn
            -DCODE=FDN2077
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.compound-string-subtract
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/compound-string-subtract.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.compound-conflicting-access
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/compound-conflicting-access.fn
            -DCODE=FDN2073
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    foreach(compound_failure IN ITEMS compound-overflow:overflow compound-divide-zero:division.by.zero)
        string(REPLACE ":" ";" compound_failure_parts ${compound_failure})
        list(GET compound_failure_parts 0 compound_failure_name)
        list(GET compound_failure_parts 1 compound_failure_pattern)
        add_test(
            NAME compiler.run.${compound_failure_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${compound_failure_name}.fn
                -DPATTERN=${compound_failure_pattern}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
        )
        add_test(
            NAME compiler.run.${compound_failure_name}-c
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${compound_failure_name}.fn
                -DPATTERN=${compound_failure_pattern}
                -DBACKEND=c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.shift-operators
        COMMAND foundationc check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.fn
    )
    add_test(
        NAME compiler.run.shift-operators
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.shift-operators-c
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.out
            -DBACKEND=c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.shift-operators-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/shift-operators.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/shift-operators-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/shift-operators.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.shift-noninteger
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/shift-noninteger.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    foreach(shift_failure IN ITEMS
            shift-negative-count:shift.count.out.of.range
            shift-wide-count:shift.count.out.of.range
            shift-overflow:i32.overflow)
        string(REPLACE ":" ";" shift_failure_parts ${shift_failure})
        list(GET shift_failure_parts 0 shift_failure_name)
        list(GET shift_failure_parts 1 shift_failure_pattern)
        add_test(
            NAME compiler.run.${shift_failure_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${shift_failure_name}.fn
                -DPATTERN=${shift_failure_pattern}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
        )
        add_test(
            NAME compiler.run.${shift_failure_name}-c
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${shift_failure_name}.fn
                -DPATTERN=${shift_failure_pattern}
                -DBACKEND=c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.primitive-values
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/primitive-values.fn
    )
    add_test(
        NAME compiler.run.u64-values
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/u64-values.fn
    )
    add_test(
        NAME compiler.run.usize-values
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/usize-values.fn
    )
    add_test(
        NAME compiler.run.machine-scalars
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/machine-scalars.fn
    )
    add_test(
        NAME compiler.run.named-arguments
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/named-arguments.fn
    )
    add_test(
        NAME compiler.run.range
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/range.fn
    )
    add_test(
        NAME compiler.check.u64-negative
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-negative.fn
            -DCODE=FDN2005
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.usize-negative
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/usize-negative.fn
            -DCODE=FDN2005
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.scalar-literal-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/scalar-literal-overflow.fn
            -DCODE=FDN2005
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.float-remainder
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/float-remainder.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.named-argument-unknown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/named-argument-unknown.fn
            -DCODE=FDN2206
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.named-argument-duplicate
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/named-argument-duplicate.fn
            -DCODE=FDN2208
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.named-argument-positional
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/named-argument-positional.fn
            -DCODE=FDN2207
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.ownership
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    foreach(edit_scalar_backend IN ITEMS llvm c)
        set(edit_scalar_backend_argument)
        if(edit_scalar_backend STREQUAL "c")
            set(edit_scalar_backend_argument -DBACKEND=c)
        endif()
        add_test(
            NAME compiler.run.edit-scalar-parameter-${edit_scalar_backend}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/edit-scalar-parameter.fn
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/edit-scalar-parameter.out
                ${edit_scalar_backend_argument}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.owned-places
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-places.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-places.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    foreach(owned_temporary_backend IN ITEMS llvm c)
        set(owned_temporary_backend_argument)
        if(owned_temporary_backend STREQUAL "c")
            set(owned_temporary_backend_argument -DBACKEND=c)
        endif()
        add_test(
            NAME compiler.run.owned-temporary-borrows-${owned_temporary_backend}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-temporary-borrows.fn
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-temporary-borrows.out
                ${owned_temporary_backend_argument}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
        )
    endforeach()
    add_test(
        NAME compiler.run.owned-temporary-borrows-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-temporary-borrows.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/owned-temporary-borrows.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/owned-temporary-borrows
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-temporary-borrows.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.collections
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.queue-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/queue-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/queue-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.queue-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/queue-values.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/queue-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/queue-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/queue-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.queue-transfer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/queue-transfer.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.match-exits
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-exits.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/match-exits.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.partial-return-panic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/partial-return-panic.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/partial-return-panic.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.adapters
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.adapters-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/adapters.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/adapters
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.adapters-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/adapters-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/adapters-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.adapters-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/adapters
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/adapters/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.adapters-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/adapters
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/adapters-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/adapters-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/adapters/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.adapters-must-get
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/adapters-must-get.fn
            -DPATTERN=adapters:.not.found:.missing
            -DTRACE_PATTERN=foundation.adapters.requireNamed
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.adapters-default
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/adapters-default.fn
            -DPATTERN=adapters:.no.default.set
            -DTRACE_PATTERN=foundation.adapters.requireDefault
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.align
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.align-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/align.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/align
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.align-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/align-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/align-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.align-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/alignment
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/alignment/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.align-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/alignment
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/align-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/align-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/alignment/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.align-invalid
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/align-invalid.fn
            -DPATTERN=align:.alignment.must.be.a.power.of.two
            -DTRACE_PATTERN=std.align.UpU64
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.align-zero
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/align-zero.fn
            -DPATTERN=align:.alignment.must.be.a.power.of.two
            -DTRACE_PATTERN=std.align.DownU64
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.align-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/align-overflow.fn
            -DPATTERN=u8.overflow
            -DTRACE_PATTERN=std.align.UpU8
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.ring-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.fn
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ring_native.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    add_test(
        NAME compiler.run.ring-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/ring-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ring-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.out
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ring_native.c
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.check.ring-push-result
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/ring-push-result.fn
            -DCODE=FDN2051
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.emit.ring-values-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/ring-values-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/ring-values-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.caching
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.caching-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/caching.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/caching
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.caching-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/caching-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/caching-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.pooling
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.pooling-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/pooling.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pooling
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.pooling-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/pooling-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/pooling-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.caching-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/caching
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/caching/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.caching-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/caching
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/caching-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/caching-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/caching/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.safemap
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/safemap.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/safemap.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.safemap-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/safemap.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/safemap.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/safemap
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/safemap.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.safemap-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/safemap.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/safemap-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/safemap-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.safemap-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/safemap
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/safemap/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.safemap-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/safemap
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/safemap-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/safemap-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/safemap/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.lock
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.lock-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/lock.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/lock
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.lock-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/lock.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/lock-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/lock-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.keyed-lock-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/keyed-lock
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/keyed-lock/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.keyed-lock-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/keyed-lock
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/keyed-lock-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/keyed-lock-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/keyed-lock/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.ring-buffer-example
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ring-buffer
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/ring-buffer/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.ring-buffer-example-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ring-buffer
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/ring-buffer-example.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ring-buffer-example
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/ring-buffer/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.events
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.hooks
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.hooks-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/hooks.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hooks
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.hooks-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/hooks-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/hooks-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.check.hooks-borrowed-callback
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/hooks-borrowed-callback.fn
            -DCODE=FDN2185
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.hooks-use-after-parallel
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/hooks-use-after-parallel.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.logger
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.logger-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/logger.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/logger
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.logger-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/logger-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/logger-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.tracing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.tracing-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/tracing.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tracing
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.tracing-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/tracing-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/tracing-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.telemetry-traceparent
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.telemetry-traceparent-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.telemetry-traceparent-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.telemetry-prometheus
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.telemetry-prometheus-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.telemetry-prometheus-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.telemetry-provider
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-provider.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-provider.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.telemetry-web
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-web.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-web.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.metrics
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.metrics-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/metrics.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/metrics
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.metrics-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/metrics-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/metrics-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.httpx-vhost
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.httpx-vhost-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.httpx-vhost-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.httpx-client
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.httpx-client-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-client.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-client
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.httpx-client-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/httpx-client-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/httpx-client-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.httpx-network
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.httpx-network-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-network.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-network
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
            -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
            -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
            -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
            -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.httpx-network-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/httpx-network-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/httpx-network-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    foreach(httpx_policy_case IN ITEMS httpx-transport-policy httpx-upload httpx-tls-validation)
        add_test(
            NAME compiler.run.${httpx_policy_case}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.fn
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.out
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
        )
        add_test(
            NAME compiler.run.${httpx_policy_case}-allocations
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
        )
        add_test(
            NAME compiler.emit.${httpx_policy_case}-deterministic
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.fn
                -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-first.c
                -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-second.c
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.httpx-use-after-do
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/httpx-use-after-do.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.httpx-use-after-registration
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/httpx-use-after-registration.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.events-concurrent
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.events-concurrent-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/events-concurrent.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/events-concurrent
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
            -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
            -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.events-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/events.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/events
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.collections-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/collections-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/collections-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.collections-stress
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/collections-stress
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/collections-stress.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/collections-stress
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/collections-stress/expected.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.replace-panic-order
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/panic/replace-order.fn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-replace-panic.cmake
    )
    add_test(
        NAME compiler.run.sequences
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.methods-contracts
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.contract-inheritance
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.contract-inheritance-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.contract-inheritance-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.function-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/function-values.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/function-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.closures
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.closure-inference
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.closure-inference-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closure-inference-allocations.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closure-inference-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.run.result-else-without-binding
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/result-else-without-binding.fn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/result-else-without-binding.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.run.result-else-without-binding-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/result-else-without-binding.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/result-else-without-binding-allocations.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/result-else-without-binding-allocations
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/result-else-without-binding.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME compiler.emit.result-else-without-binding-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/result-else-without-binding.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/result-else-without-binding-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/result-else-without-binding-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.emit.closures-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/closures-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/closures-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.closure-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/panic/closure-trace.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closure-panic.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closure-panic
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-closure-panic.cmake
    )
    add_test(
        NAME compiler.run.contract-project
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/app
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.contract-project-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/app
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/contracts-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/contracts-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.distributed-methods
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME compiler.emit.distributed-methods-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/distributed-methods-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/distributed-methods-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME compiler.run.c-abi
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/native.c
            -DNATIVE_SECOND=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/support.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native.cmake
    )
    add_test(
        NAME compiler.run.c-abi-native-object
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/native.c
            -DSUPPORT=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/support.c
            -DOBJECT=${CMAKE_CURRENT_BINARY_DIR}/c-abi-support${CMAKE_C_OUTPUT_EXTENSION}
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-object.cmake
    )
    add_test(
        NAME compiler.run.desktop-notes
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/desktop-notes
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/desktop-notes/native/desktop_host.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/desktop-notes/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-single.cmake
    )
    set_tests_properties(compiler.run.desktop-notes PROPERTIES
        ENVIRONMENT "FOUNDATION_DESKTOP_HEADLESS=1"
        TIMEOUT ${FOUNDATION_COMPILE_RUN_TIMEOUT}
    )
    add_test(
        NAME compiler.emit.c-abi-header-cpp
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
            -DCPP_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/header_check.cpp
            -DHEADER=${CMAKE_CURRENT_BINARY_DIR}/foundation_abi.h
            -DOBJECT=${CMAKE_CURRENT_BINARY_DIR}/c-abi-header${CMAKE_CXX_OUTPUT_EXTENSION}
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-c-abi-cpp-header.cmake
    )
    add_test(
        NAME compiler.emit.c-abi-header-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/c-abi-first.h
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/c-abi-second.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-header-deterministic.cmake
    )
    add_test(
        NAME compiler.run.c-abi-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-panic
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-panic/native/native.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-native-project-panic.cmake
    )
    add_test(
        NAME compiler.run.c-abi-export-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-export-panic
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-export-panic/native/native.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-export-native-panic.cmake
    )
    foreach(method_case IN ITEMS
            "missing-contract-method;FDN2096"
            "contract-receiver-mismatch;FDN2097"
            "contract-signature-mismatch;FDN2011"
            "duplicate-contract-method;FDN2091"
            "duplicate-struct-method;FDN2095"
            "duplicate-associated-function;FDN2001"
            "unknown-associated-function;FDN2190"
            "implements-non-contract;FDN2092"
            "edit-method-immutable;FDN2101"
            "own-method-non-owner;FDN2102"
            "own-contract-borrow;FDN2103"
            "contract-by-value;FDN2099"
            "non-conforming-contract-borrow;FDN2011"
            "duplicate-contract-implementation;FDN2093"
            "empty-contract;FDN2090"
            "use-after-own-method;FDN2065"
            "extends-non-contract;FDN2140"
            "duplicate-contract-parent;FDN2141"
            "contract-inheritance-cycle;FDN2142"
            "contract-inherited-conflict;FDN2143"
            "contract-default-ambiguity;FDN2144"
            "delegation-unknown-field;FDN2146"
            "delegation-non-conforming;FDN2147"
            "contract-own-default;FDN2148"
            "delegation-own-method;FDN2149"
            "own-contract-use-after-move;FDN2065")
        list(GET method_case 0 method_name)
        list(GET method_case 1 method_code)
        add_test(
            NAME compiler.check.${method_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${method_name}.fn
                -DCODE=${method_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(closure_case IN ITEMS
            "closure-implicit-capture;FDN2120"
            "closure-duplicate-capture;FDN2121"
            "closure-capture-parameter;FDN2122"
            "closure-own-existing-borrow;FDN2123"
            "closure-edit-immutable;FDN2124"
            "closure-conflicting-capture;FDN2125"
            "closure-capture-consume;FDN2126"
            "closure-borrow-return;FDN2127"
            "closure-borrow-struct;FDN2127"
            "closure-borrow-enum;FDN2127"
            "closure-borrow-array;FDN2127"
            "closure-not-callable;FDN2128"
            "closure-signature-mismatch;FDN2011"
            "closure-inference-context;FDN2184"
            "closure-inference-parameter-context;FDN2184"
            "closure-inference-return-context;FDN2184"
            "closure-inference-arity;FDN2184"
            "closure-inference-mode;FDN2011"
            "closure-use-after-own-capture;FDN2065"
            "closure-edit-alias;FDN2073"
            "closure-view-move;FDN2067")
        list(GET closure_case 0 closure_name)
        list(GET closure_case 1 closure_code)
        add_test(
            NAME compiler.check.${closure_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${closure_name}.fn
                -DCODE=${closure_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(function_value_case IN ITEMS
            "function-value-equality;FDN2012"
            "function-value-unresolved-generic;FDN2045")
        list(GET function_value_case 0 function_value_name)
        list(GET function_value_case 1 function_value_code)
        add_test(
            NAME compiler.check.${function_value_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${function_value_name}.fn
                -DCODE=${function_value_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(c_abi_case IN ITEMS
            "c-abi-generic;FDN2112"
            "c-abi-return;FDN2113"
            "c-abi-owner;FDN2114"
            "c-abi-function;FDN2114"
            "c-abi-task;FDN2114"
            "c-abi-task-internal;FDN2114"
            "c-abi-edit-slice;FDN2114"
            "c-abi-duplicate-symbol;FDN2111"
            "c-abi-reserved-export;FDN2115"
            "c-abi-reserved-import;FDN2115"
            "c-abi-keyword-symbol;FDN2110"
            "c-abi-cpp-keyword-symbol;FDN2110"
            "c-abi-struct-parameter;FDN2114"
            "c-abi-method;FDN2117"
            "c-abi-main;FDN2116"
            "c-abi-unknown-abi;FDN1113")
        list(GET c_abi_case 0 c_abi_name)
        list(GET c_abi_case 1 c_abi_code)
        add_test(
            NAME compiler.check.${c_abi_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${c_abi_name}.fn
                -DCODE=${c_abi_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(raw_pointer_case IN ITEMS
            "unsafe-missing-proof;FDN2212"
            "raw-call-safe;FDN2213"
            "raw-null-safe;FDN2213"
            "raw-dereference-safe;FDN2213"
            "raw-const-write;FDN2077"
            "raw-void-dereference;FDN2214"
            "raw-unsupported-pointee;FDN2214")
        list(GET raw_pointer_case 0 raw_pointer_name)
        list(GET raw_pointer_case 1 raw_pointer_code)
        add_test(
            NAME compiler.check.${raw_pointer_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${raw_pointer_name}.fn
                -DCODE=${raw_pointer_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    if(FOUNDATION_BUILD_FUZZERS)
        foreach(crypto_case IN ITEMS bytes-crypto auth-hs256 auth-web)
            add_test(
                NAME compiler.run.${crypto_case}-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${crypto_case}.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${crypto_case}-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${crypto_case}-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${crypto_case}.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endforeach()
        add_test(
            NAME compiler.run.options-migration-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/options-migration-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/options-migration-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/options-migration.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.parse-values-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/parse-values-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parse-values-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.json-stringify-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/json-stringify-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/json-stringify-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-stringify.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.serializer-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/serializer-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/serializer-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/serializer.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.example-serializer-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/example-serializer-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/example-serializer-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/serializer/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.ownership-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/ownership-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ownership-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.sequences-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/sequences-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/sequences-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.methods-contracts-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/methods-contracts-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/methods-contracts-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.contract-inheritance-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.closures-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closures-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closures-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.closure-inference-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closure-inference-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closure-inference-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closure-inference.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.collections-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/collections-stress
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/collections-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/collections-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/collections-stress/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.ring-values-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/ring-values-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ring-values-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ring-values.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ring_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.align-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/align-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/align-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/align.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.errors-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/errors-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/errors-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/errors.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.caching-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/caching-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/caching-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.pooling-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/pooling-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pooling-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.adapters-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/adapters-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/adapters-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(
                NAME compiler.run.caching-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/caching-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/caching-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/caching.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                    -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
            add_test(
                NAME compiler.run.pooling-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/pooling-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/pooling-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/pooling.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
            add_test(
                NAME compiler.run.adapters-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/adapters-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/adapters-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/adapters.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endif()
        foreach(cross_executor_case IN ITEMS worker-pool-channel safemap lock fsm-concurrent)
            add_test(
                NAME compiler.run.${cross_executor_case}-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${cross_executor_case}.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${cross_executor_case}-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${cross_executor_case}-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${cross_executor_case}.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                add_test(
                    NAME compiler.run.${cross_executor_case}-thread-sanitized
                    COMMAND ${CMAKE_COMMAND}
                        -DCOMPILER=$<TARGET_FILE:foundationc>
                        -DC_COMPILER=${CMAKE_C_COMPILER}
                        -DTHREAD_SANITIZER=ON
                        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${cross_executor_case}.fn
                        -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${cross_executor_case}-thread-sanitized.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${cross_executor_case}-thread-sanitized
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${cross_executor_case}.out
                        -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                        -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                        -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                        -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                        -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
                )
            endif()
        endforeach()
        add_test(
            NAME compiler.run.scheduler-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/scheduler-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scheduler-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(
                NAME compiler.run.scheduler-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/scheduler-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/scheduler-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/scheduler.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_PARSE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/parse.c
                    -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                    -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                    -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                    -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endif()
        add_test(
            NAME compiler.run.secrets-concurrent-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(
                NAME compiler.run.secrets-concurrent-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/secrets-concurrent-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/secrets-concurrent.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endif()
        add_test(
            NAME compiler.run.logger-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/logger-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/logger-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(
                NAME compiler.run.logger-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/logger-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/logger-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/logger.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endif()
        add_test(
            NAME compiler.run.tracing-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/tracing-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tracing-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tracing.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.telemetry-traceparent-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/telemetry-traceparent-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-traceparent.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.telemetry-prometheus-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/telemetry-prometheus-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/telemetry-prometheus.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.metrics-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/metrics-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/metrics-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/metrics.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.events-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/events-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/events-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.hooks-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/hooks-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hooks-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.httpx-vhost-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-vhost-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-vhost.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.httpx-client-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-client-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-client-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-client.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.httpx-network-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/httpx-network-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/httpx-network-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/httpx-network.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        foreach(httpx_policy_case IN ITEMS httpx-transport-policy httpx-upload httpx-tls-validation)
            add_test(
                NAME compiler.run.${httpx_policy_case}-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                    -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                    -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endforeach()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            foreach(httpx_policy_case IN ITEMS httpx-transport-policy httpx-upload httpx-tls-validation)
                add_test(
                    NAME compiler.run.${httpx_policy_case}-thread-sanitized
                    COMMAND ${CMAKE_COMMAND}
                        -DCOMPILER=$<TARGET_FILE:foundationc>
                        -DC_COMPILER=${CMAKE_C_COMPILER}
                        -DTHREAD_SANITIZER=ON
                        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.fn
                        -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-thread-sanitized.c
                        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${httpx_policy_case}-thread-sanitized
                        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/${httpx_policy_case}.out
                        -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                        -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                        -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                        -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                        -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                        -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                        -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                        -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                        -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                        -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                        -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
                )
            endforeach()
            add_test(
                NAME compiler.run.fs-watch-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fs-watch-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fs-watch-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.out
                    -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/watch_native.c
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                    -DRUNTIME_WATCH_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/watch.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
            add_test(
                NAME compiler.run.hooks-thread-sanitized
                COMMAND ${CMAKE_COMMAND}
                    -DCOMPILER=$<TARGET_FILE:foundationc>
                    -DC_COMPILER=${CMAKE_C_COMPILER}
                    -DTHREAD_SANITIZER=ON
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.fn
                    -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/hooks-thread-sanitized.c
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hooks-thread-sanitized
                    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hooks.out
                    -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                    -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                    -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                    -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                    -DRUNTIME_POOL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/pool.c
                    -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
            )
        endif()
        add_test(
            NAME compiler.run.events-concurrent-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/events-concurrent-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/events-concurrent-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/events-concurrent.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.contract-project-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/app
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-project-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-project-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.c-abi-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/c-abi-sanitized.c
                -DHEADER=${CMAKE_CURRENT_BINARY_DIR}/foundation_abi.h
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/c-abi-sanitized
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/native.c
                -DNATIVE_SECOND=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/support.c
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native-sanitized.cmake
        )
        add_test(
            NAME compiler.run.task-suspension-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/task-suspension-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/task-suspension-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.supervisor-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/supervisor-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/supervisor-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/supervisor.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.blocking-native-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/blocking-native-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/blocking-native-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/blocking-native.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.resiliency-rate-limit-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/resiliency-rate-limit-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/resiliency-rate-limit-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-rate-limit.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/blocking_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.resiliency-policies-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/resiliency-policies-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/resiliency-policies-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/resiliency-policies.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/resiliency_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_RESILIENCY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/resiliency.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.callback-native-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/callback-native-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/callback-native-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/callback-native.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/callback_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.fs-watch-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/fs-watch-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fs-watch-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/fs-watch.out
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/watch_native.c
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_WATCH_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/watch.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.filesystem-read-text-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-read-text-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-read-text-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-read-text.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.network-tcp-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/network-tcp-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/network-tcp-sanitized
                -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/net_server.c
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/network-tcp.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.web-server-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-server-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-server-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-server.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_CHANNEL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.web-cors-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-cors-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-cors-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-cors.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.web-routing-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-routing-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-routing-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-routing.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.web-middleware-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-middleware-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-middleware-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-middleware.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.application-host-web-activation-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-activation-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-activation/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.application-host-web-middleware-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/application-host-web-middleware-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/application-host-web-middleware/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME compiler.run.web-protocol-errors-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.fn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/web-protocol-errors-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/web-protocol-errors-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/web-protocol-errors.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_CRYPTO_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/crypto.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_BLOCKING_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/blocking.c
                -DRUNTIME_REACTOR_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/reactor.c
                -DRUNTIME_NET_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/net.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
    endif()
    add_test(
        NAME compiler.check.use-after-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/use-after-move.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.new-nonconstruct
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/new-nonconstruct.fn
            -DCODE=FDN2191
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.edit-marker-missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/edit-marker-missing.fn
            -DCODE=FDN2192
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.transfer-marker-missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/transfer-marker-missing.fn
            -DCODE=FDN2193
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.read-marker-invalid
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/read-marker-invalid.fn
            -DCODE=FDN2194
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.use-after-transfer
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/use-after-transfer.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.function-edit-marker-missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/function-edit-marker-missing.fn
            -DCODE=FDN2192
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.function-transfer-marker-missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/function-transfer-marker-missing.fn
            -DCODE=FDN2193
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.double-consumption
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/double-consumption.fn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.move-while-borrowed
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/move-while-borrowed.fn
            -DCODE=FDN2067
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.maybe-moved
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/maybe-moved.fn
            -DCODE=FDN2066
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.conflicting-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conflicting-borrow.fn
            -DCODE=FDN2073
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.field-assignment-without-edit
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/field-assignment-without-edit.fn
            -DCODE=FDN2077
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.owned-field-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/owned-field-move.fn
            -DCODE=FDN2078
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.custom-drop-temporary-field
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/custom-drop-temporary-field.fn
            -DCODE=FDN2078
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.edit-immutable
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/edit-immutable.fn
            -DCODE=FDN2072
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.stored-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/stored-borrow.fn
            -DCODE=FDN2074
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.borrow-return
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/borrow-return.fn
            -DCODE=FDN2063
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.borrow-field
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/borrow-field.fn
            -DCODE=FDN2062
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.loop-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/loop-move.fn
            -DCODE=FDN2079
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.short-circuit-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/short-circuit-move.fn
            -DCODE=FDN2066
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.index-out-of-bounds
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/index-out-of-bounds.fn
            -DPATTERN=index.out.of.bounds
            -DTRACE_PATTERN=index-out-of-bounds.fn:3:11
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.check.empty-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/empty-array.fn
            -DCODE=FDN2081
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.array-length-mismatch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/array-length-mismatch.fn
            -DCODE=FDN2082
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.mixed-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/mixed-array.fn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.immutable-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/immutable-index.fn
            -DCODE=FDN2077
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.owned-array-element
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/owned-array-element.fn
            -DCODE=FDN2083
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.nested-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/nested-slice.fn
            -DCODE=FDN2080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.negative-array-length
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negative-array-length.fn
            -DCODE=FDN1080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.large-array-length
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/large-array-length.fn
            -DCODE=FDN2080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.stored-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/stored-slice.fn
            -DCODE=FDN2074
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.returned-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/returned-slice.fn
            -DCODE=FDN2063
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.string-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/string-index.fn
            -DCODE=FDN2084
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.negative-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negative-index.fn
            -DCODE=FDN2005
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.array-length-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/array-length-overflow.fn
            -DCODE=FDN1080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.recursive-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/recursive-array.fn
            -DCODE=FDN2023
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.invalid-utf8
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/invalid-utf8.fn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-invalid-utf8.cmake
    )
    add_test(
        NAME compiler.check.unhandled-result
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unhandled-result.fn
            -DCODE=FDN2052
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.check.discarded-result-expression
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/discarded-result-expression.fn
            -DCODE=FDN2051
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/panic-trace.fn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/panic-trace.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/panic-trace
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-panic.cmake
    )
    add_test(
        NAME compiler.check.generic-inference
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/generic-inference.fn
            -DCODE=FDN2045
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME compiler.run.divide-zero
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/divide-zero.fn
            -DPATTERN=division.by.zero
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/overflow.fn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.u64-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-overflow.fn
            -DPATTERN=u64.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.u64-underflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-underflow.fn
            -DPATTERN=u64.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.remainder-zero
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/remainder-zero.fn
            -DPATTERN=remainder.by.zero
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.subtract-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/subtract-overflow.fn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.multiply-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/multiply-overflow.fn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.negate-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negate-overflow.fn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.i8-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/i8-overflow.fn
            -DPATTERN=i8.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME compiler.run.i64-divide-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/i64-divide-overflow.fn
            -DPATTERN=i64.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )

    foreach(owned_place_case IN ITEMS
            "replace-non-place;FDN2134"
            "replace-immutable;FDN2135"
            "replace-type-mismatch;FDN2011"
            "replace-consumes-destination;FDN2136"
            "replace-borrowed;FDN2075"
            "replace-borrowed-closure;FDN2127"
            "replace-borrowed-closure-target;FDN2127"
            "destructure-non-struct;FDN2130"
            "destructure-wrong-type;FDN2131"
            "destructure-unknown-field;FDN2025"
            "destructure-duplicate-field;FDN2132"
            "destructure-missing-field;FDN2133"
            "destructure-malformed;FDN1133"
            "destructure-var;FDN1130"
            "destructure-use-after-move;FDN2065"
            "drop-signature;FDN2137"
            "drop-explicit-call;FDN2138"
            "drop-contract;FDN2138"
            "destructure-custom-drop;FDN2139")
        list(GET owned_place_case 0 owned_place_name)
        list(GET owned_place_case 1 owned_place_code)
        add_test(
            NAME compiler.check.${owned_place_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${owned_place_name}.fn
                -DCODE=${owned_place_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME compiler.check.project-private-destructure
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/private-destructure
            -DCODE=FDN3008
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    find_program(FOUNDATION_NODE_EXECUTABLE NAMES node node.exe)
    if(FOUNDATION_NODE_EXECUTABLE)
        add_test(
            NAME tools.vscode
            COMMAND ${FOUNDATION_NODE_EXECUTABLE} --test test/extension.test.js
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools/vscode
        )
    endif()
endif()
