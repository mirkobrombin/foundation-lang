cmake_minimum_required(VERSION 3.25)

project(foundation_lang VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FOUNDATION_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FOUNDATION_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)

add_library(foundation_runtime STATIC
    runtime/src/runtime.c
    runtime/src/task.c
    runtime/src/cancellation.c
    runtime/src/channel.c
)
target_include_directories(foundation_runtime PUBLIC runtime/include)

add_library(foundation_compiler STATIC
    stage0/src/codegen.cpp
    stage0/src/diagnostic.cpp
    stage0/src/driver.cpp
    stage0/src/formatter.cpp
    stage0/src/language_service.cpp
    stage0/src/lexer.cpp
    stage0/src/lsp.cpp
    stage0/src/lower.cpp
    stage0/src/metadata.cpp
    stage0/src/package.cpp
    stage0/src/package_cache.cpp
    stage0/src/package_catalog.cpp
    stage0/src/package_cli.cpp
    stage0/src/package_environment.cpp
    stage0/src/package_lock.cpp
    stage0/src/package_project.cpp
    stage0/src/package_registry.cpp
    stage0/src/package_resolver.cpp
    stage0/src/package_source.cpp
    stage0/src/parser.cpp
    stage0/src/process.cpp
    stage0/src/project.cpp
    stage0/src/sema.cpp
    stage0/src/sha256.cpp
    stage0/src/target.cpp
    stage0/src/type.cpp
)
target_include_directories(foundation_compiler PUBLIC stage0/include)
target_compile_definitions(foundation_compiler PRIVATE
    FOUNDATION_C_COMPILER="${CMAKE_C_COMPILER}"
    FOUNDATION_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
    FOUNDATION_RUNTIME_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
    FOUNDATION_RUNTIME_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c"
    FOUNDATION_RUNTIME_TASK_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c"
    FOUNDATION_RUNTIME_CANCELLATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c"
    FOUNDATION_RUNTIME_CHANNEL_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/channel.c"
    FOUNDATION_STANDARD_LIBRARY="${CMAKE_CURRENT_SOURCE_DIR}/std"
)

add_executable(foundationc stage0/src/main.cpp)
target_link_libraries(foundationc PRIVATE foundation_compiler)

add_executable(foundation-ls stage0/src/lsp_main.cpp)
target_link_libraries(foundation-ls PRIVATE foundation_compiler)

if(MSVC)
    target_compile_options(foundation_runtime PRIVATE /W4)
    target_compile_options(foundation_compiler PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundationc PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundation-ls PRIVATE /W4 /permissive- /EHsc)
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE /WX)
        target_compile_options(foundation_compiler PRIVATE /WX)
        target_compile_options(foundationc PRIVATE /WX)
        target_compile_options(foundation-ls PRIVATE /WX)
    endif()
else()
    target_compile_options(foundation_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundation_compiler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundationc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundation-ls PRIVATE -Wall -Wextra -Wpedantic)
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE -Werror)
        target_compile_options(foundation_compiler PRIVATE -Werror)
        target_compile_options(foundationc PRIVATE -Werror)
        target_compile_options(foundation-ls PRIVATE -Werror)
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
    target_link_options(foundationc PRIVATE -fsanitize=address,undefined)
    target_link_options(foundation-ls PRIVATE -fsanitize=address,undefined)
    add_executable(foundation_parser_fuzz stage0/fuzz/parser_fuzz.cpp)
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
        tests/fuzz/corpus/bootstrap.fdn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/bootstrap.fdn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/typed-attributes.fdn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/typed-attributes.fdn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/cancellation.fdn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/cancellation.fdn"
        COPYONLY
    )
    configure_file(
        tests/fuzz/corpus/task-suspension.fdn
        "${FOUNDATION_FUZZ_CORPUS_DIR}/task-suspension.fdn"
        COPYONLY
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(foundation_runtime_panic_test runtime/tests/panic_trace_test.c)
    target_link_libraries(foundation_runtime_panic_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_allocation_test runtime/tests/allocation_panic_test.c)
    target_link_libraries(foundation_runtime_allocation_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_counter_test runtime/tests/allocation_counter_test.c)
    target_link_libraries(foundation_runtime_counter_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_string_test runtime/tests/string_test.c)
    target_link_libraries(foundation_runtime_string_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_u64_test runtime/tests/u64_test.c)
    target_link_libraries(foundation_runtime_u64_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_env_test runtime/tests/env_test.c)
    target_link_libraries(foundation_runtime_env_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_fs_test runtime/tests/fs_test.c)
    target_link_libraries(foundation_runtime_fs_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_time_test runtime/tests/time_test.c)
    target_link_libraries(foundation_runtime_time_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_task_test runtime/tests/task_test.c)
    target_link_libraries(foundation_runtime_task_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_cancellation_test runtime/tests/cancellation_test.c)
    target_link_libraries(foundation_runtime_cancellation_test PRIVATE foundation_runtime)
    add_executable(foundation_runtime_channel_test runtime/tests/channel_test.c)
    target_link_libraries(foundation_runtime_channel_test PRIVATE foundation_runtime)
    if(FOUNDATION_BUILD_FUZZERS)
        foreach(runtime_test IN ITEMS
                foundation_runtime_panic_test
                foundation_runtime_allocation_test
                foundation_runtime_counter_test
                foundation_runtime_string_test
                foundation_runtime_u64_test
                foundation_runtime_env_test
                foundation_runtime_fs_test
                foundation_runtime_time_test
                foundation_runtime_task_test
                foundation_runtime_cancellation_test
                foundation_runtime_channel_test)
            target_compile_options(${runtime_test} PRIVATE -fsanitize=address,undefined)
            target_link_options(${runtime_test} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
    if(MSVC)
        target_compile_options(foundation_runtime_panic_test PRIVATE /W4)
        target_compile_options(foundation_runtime_allocation_test PRIVATE /W4)
        target_compile_options(foundation_runtime_counter_test PRIVATE /W4)
        target_compile_options(foundation_runtime_string_test PRIVATE /W4)
        target_compile_options(foundation_runtime_u64_test PRIVATE /W4)
        target_compile_options(foundation_runtime_env_test PRIVATE /W4)
        target_compile_options(foundation_runtime_fs_test PRIVATE /W4)
        target_compile_options(foundation_runtime_time_test PRIVATE /W4)
        target_compile_options(foundation_runtime_task_test PRIVATE /W4)
        target_compile_options(foundation_runtime_cancellation_test PRIVATE /W4)
        target_compile_options(foundation_runtime_channel_test PRIVATE /W4)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_panic_test PRIVATE /WX)
            target_compile_options(foundation_runtime_allocation_test PRIVATE /WX)
            target_compile_options(foundation_runtime_counter_test PRIVATE /WX)
            target_compile_options(foundation_runtime_string_test PRIVATE /WX)
            target_compile_options(foundation_runtime_u64_test PRIVATE /WX)
            target_compile_options(foundation_runtime_env_test PRIVATE /WX)
            target_compile_options(foundation_runtime_fs_test PRIVATE /WX)
            target_compile_options(foundation_runtime_time_test PRIVATE /WX)
            target_compile_options(foundation_runtime_task_test PRIVATE /WX)
            target_compile_options(foundation_runtime_cancellation_test PRIVATE /WX)
            target_compile_options(foundation_runtime_channel_test PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_runtime_panic_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_allocation_test PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(foundation_runtime_counter_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_string_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_u64_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_env_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_fs_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_time_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_task_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_cancellation_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_runtime_channel_test PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_runtime_panic_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_allocation_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_counter_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_string_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_u64_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_env_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_fs_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_time_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_task_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_cancellation_test PRIVATE -Werror)
            target_compile_options(foundation_runtime_channel_test PRIVATE -Werror)
        endif()
    endif()

    add_executable(foundation_stage0_tests stage0/tests/compiler_test.cpp)
    target_link_libraries(foundation_stage0_tests PRIVATE foundation_compiler)
    target_compile_definitions(foundation_stage0_tests PRIVATE
        FOUNDATION_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_executable(foundation_lsp_tests stage0/tests/lsp_test.cpp)
    target_link_libraries(foundation_lsp_tests PRIVATE foundation_compiler)
    add_executable(foundation_formatter_tests stage0/tests/formatter_test.cpp)
    target_link_libraries(foundation_formatter_tests PRIVATE foundation_compiler)
    target_compile_definitions(foundation_formatter_tests PRIVATE
        FOUNDATION_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(FOUNDATION_BUILD_FUZZERS)
        target_link_options(foundation_stage0_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_lsp_tests PRIVATE -fsanitize=address,undefined)
        target_link_options(foundation_formatter_tests PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(foundation_stage0_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_options(foundation_lsp_tests PRIVATE /W4 /permissive- /EHsc)
        target_compile_options(foundation_formatter_tests PRIVATE /W4 /permissive- /EHsc)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_stage0_tests PRIVATE /WX)
            target_compile_options(foundation_lsp_tests PRIVATE /WX)
            target_compile_options(foundation_formatter_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_stage0_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_lsp_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(foundation_formatter_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_stage0_tests PRIVATE -Werror)
            target_compile_options(foundation_lsp_tests PRIVATE -Werror)
            target_compile_options(foundation_formatter_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME stage0.unit COMMAND foundation_stage0_tests)
    add_test(NAME tooling.lsp.unit COMMAND foundation_lsp_tests)
    add_test(NAME tooling.formatter.unit COMMAND foundation_formatter_tests)
    add_test(
        NAME tooling.formatter.cli
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/messy.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/expected.fdn
            -DINVALID=${CMAKE_CURRENT_SOURCE_DIR}/tests/formatter/invalid.fdn
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/formatter-project
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-format.cmake
    )
    add_executable(foundation_package_tests
        stage0/tests/package_test.cpp
        stage0/tests/package_cache_test.cpp
        stage0/tests/package_catalog_test.cpp
        stage0/tests/package_lock_test.cpp
        stage0/tests/package_project_test.cpp
        stage0/tests/package_registry_test.cpp
        stage0/tests/package_resolver_test.cpp
        stage0/tests/package_source_test.cpp
        stage0/tests/sha256_test.cpp
    )
    target_link_libraries(foundation_package_tests PRIVATE foundation_compiler)
    if(FOUNDATION_BUILD_FUZZERS)
        target_link_options(foundation_package_tests PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(foundation_package_tests PRIVATE /W4 /permissive- /EHsc)
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
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-package-workflow.cmake
    )
    add_test(NAME runtime.string COMMAND foundation_runtime_string_test)
    add_test(NAME runtime.u64 COMMAND foundation_runtime_u64_test)
    add_test(NAME runtime.env COMMAND foundation_runtime_env_test)
    add_test(NAME runtime.time COMMAND foundation_runtime_time_test)
    add_test(NAME runtime.task COMMAND foundation_runtime_task_test)
    add_test(NAME runtime.cancellation COMMAND foundation_runtime_cancellation_test)
    add_test(NAME runtime.channel COMMAND foundation_runtime_channel_test)
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
        NAME stage0.check.accept
        COMMAND foundationc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fdn
    )
    add_test(
        NAME stage0.check.comments
        COMMAND foundationc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/comments.fdn
    )
    add_test(
        NAME stage0.emit.accept
        COMMAND foundationc emit-c
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fdn
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello.c
    )
    add_test(
        NAME stage0.run.accept
        COMMAND foundationc run ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fdn
    )
    set_tests_properties(stage0.run.accept PROPERTIES
        PASS_REGULAR_EXPRESSION "hello from foundation"
    )
    add_test(
        NAME stage0.run.tasks
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.tasks-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/tasks.fdn
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
        NAME stage0.run.cancellation
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.cancellation-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/cancellation.fdn
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
        NAME stage0.run.channels
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.channels-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/channels.fdn
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
    add_test(
        NAME stage0.run.task-suspension
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.task-suspension-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fdn
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
            "task-borrow-parameter;FDN2165"
            "cancellation-token-reuse;FDN2065"
            "task-wait-expression;FDN2168"
            "task-wait-ignored-result;FDN2168"
            "channel-borrow-payload;FDN2165"
            "channel-missing-endpoint;FDN2133"
            "channel-wrong-capacity;FDN2011")
        list(GET task_case 0 task_name)
        list(GET task_case 1 task_code)
        add_test(
            NAME stage0.check.${task_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${task_name}.fdn
                -DCODE=${task_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME stage0.check.reject
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/legacy-semicolon.fdn
            -DCODE=FDN0001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.unterminated-block-comment
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unterminated-block-comment.fdn
            -DCODE=FDN0006
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.missing
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/does-not-exist.fdn
            -DCODE=FDN3001
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.type-mismatch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/type-mismatch.fdn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.typed-attributes
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.typed-attributes-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-attributes.fdn
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
            NAME stage0.check.${attribute_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${attribute_name}.fdn
                -DCODE=${attribute_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME stage0.run.typed-control
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.main-arguments
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.out
            "-DPROGRAM_ARGS=alpha;two words"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.main-arguments-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/main-arguments.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/main-arguments
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/main-arguments.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=alpha;two words"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.platform-current
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.platform-current-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/platform-current.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/platform-current
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/platform-current.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.check.target-unknown
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/target-unknown.fdn
            -DCODE=FDN1142
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.target-duplicate
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/target-duplicate.fdn
            -DCODE=FDN1144
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.environment
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    set_tests_properties(stage0.run.environment PROPERTIES
        ENVIRONMENT "FOUNDATION_ENV_TEST=foundation env value"
    )
    add_test(
        NAME stage0.run.environment-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/environment.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/environment
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/environment.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    set_tests_properties(stage0.run.environment-allocations PROPERTIES
        ENVIRONMENT "FOUNDATION_ENV_TEST=foundation env value"
    )
    add_test(
        NAME stage0.run.text-path
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.text-path-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/text-path.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/text-path
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-path.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.text-builder-format
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.text-builder-format-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/text-builder-format.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/text-builder-format
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/text-builder-format.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.json-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.json-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/json-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/json-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/json-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.parse-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.parse-values-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/parse-values.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/parse-values
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/parse-values.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.field-borrows
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.field-borrows-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/field-borrows.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/field-borrows
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/field-borrows.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.check.conflicting-field-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conflicting-field-borrow.fdn
            -DCODE=FDN2073
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.filesystem-lines
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.filesystem-lines-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-lines.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-lines
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-lines.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.filesystem-directory
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.filesystem-directory-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/filesystem-directory.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/filesystem-directory
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/filesystem-directory.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.run.time-filesystem
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.out
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.time-filesystem-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/time-filesystem.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/time-filesystem
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/time-filesystem.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            "-DPROGRAM_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/lines.jsonl"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.check.main-arguments
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/main-arguments.fdn
            -DCODE=FDN2007
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.len-invalid
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/len-invalid.fdn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.language-tour
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/language-tour/main.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/language-tour/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.packages
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.typed-attributes-project
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.typed-attributes-project-metadata
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes/expected.metadata.json
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-project-first.json
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/typed-attributes-project-second.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-metadata.cmake
    )
    add_test(
        NAME stage0.check.typed-attributes-private
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/typed-attributes-private
            -DCODE=FDN3008
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.emit.packages-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/packages
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/packages-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/packages-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.emit.package-source-order
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/order
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/package-order.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-project-order.cmake
    )
    add_test(
        NAME stage0.run.package-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/panic
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-project-panic.cmake
    )
    add_test(
        NAME stage0.run.contract-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/contract-panic
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-contract-panic.cmake
    )
    add_test(
        NAME stage0.run.contract-default-panic-trace
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
            NAME stage0.check.project-${project_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/${project_name}
                -DCODE=${project_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME stage0.run.struct-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/struct-values.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/struct-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.check.struct-missing-field
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/struct-missing-field.fdn
            -DCODE=FDN2027
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.enum-match
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/enum-match.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/enum-match.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.check.match-exhaustive
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/match-non-exhaustive.fdn
            -DCODE=FDN2040
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.generic-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-values.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.generic-lookahead
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-lookahead.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/generic-lookahead.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.primitive-values
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/primitive-values.fdn
    )
    add_test(
        NAME stage0.run.u64-values
        COMMAND foundationc run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/u64-values.fdn
    )
    add_test(
        NAME stage0.check.u64-negative
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-negative.fdn
            -DCODE=FDN2005
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.ownership
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.owned-places
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-places.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/owned-places.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.collections
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.collections-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/collections
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/collections-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/collections-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.run.collections-stress
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
        NAME stage0.run.replace-panic-order
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/panic/replace-order.fdn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-replace-panic.cmake
    )
    add_test(
        NAME stage0.run.sequences
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.methods-contracts
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.contract-inheritance
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.contract-inheritance-allocations
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-allocations.cmake
    )
    add_test(
        NAME stage0.emit.contract-inheritance-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fdn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.run.function-values
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/function-values.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/function-values.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.run.closures
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.closures-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fdn
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/closures-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/closures-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.run.closure-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/panic/closure-trace.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closure-panic.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closure-panic
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-closure-panic.cmake
    )
    add_test(
        NAME stage0.run.contract-project
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.contract-project-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/contracts-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/contracts-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.run.distributed-methods
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
    )
    add_test(
        NAME stage0.emit.distributed-methods-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/distributed-methods
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/distributed-methods-first.c
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/distributed-methods-second.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-deterministic.cmake
    )
    add_test(
        NAME stage0.run.c-abi
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/native.c
            -DNATIVE_SECOND=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/native/support.c
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi/expected.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-native.cmake
    )
    add_test(
        NAME stage0.run.c-abi-native-object
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
        NAME stage0.emit.c-abi-header-cpp
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
        NAME stage0.emit.c-abi-header-deterministic
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/c-abi
            -DFIRST=${CMAKE_CURRENT_BINARY_DIR}/c-abi-first.h
            -DSECOND=${CMAKE_CURRENT_BINARY_DIR}/c-abi-second.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-header-deterministic.cmake
    )
    add_test(
        NAME stage0.run.c-abi-panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-panic
            -DNATIVE=${CMAKE_CURRENT_SOURCE_DIR}/tests/projects/c-abi-panic/native/native.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-native-project-panic.cmake
    )
    add_test(
        NAME stage0.run.c-abi-export-panic-trace
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
            NAME stage0.check.${method_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${method_name}.fdn
                -DCODE=${method_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(closure_case IN ITEMS
            "closure-implicit-capture;FDN2120"
            "closure-duplicate-capture;FDN2121"
            "closure-capture-parameter;FDN2122"
            "closure-copy-owner;FDN2123"
            "closure-edit-immutable;FDN2124"
            "closure-conflicting-capture;FDN2125"
            "closure-capture-consume;FDN2126"
            "closure-borrow-return;FDN2127"
            "closure-borrow-struct;FDN2127"
            "closure-borrow-enum;FDN2127"
            "closure-borrow-array;FDN2127"
            "closure-not-callable;FDN2128"
            "closure-signature-mismatch;FDN2011"
            "closure-use-after-own-capture;FDN2065"
            "closure-edit-alias;FDN2073"
            "closure-view-move;FDN2067")
        list(GET closure_case 0 closure_name)
        list(GET closure_case 1 closure_code)
        add_test(
            NAME stage0.check.${closure_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${closure_name}.fdn
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
            NAME stage0.check.${function_value_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${function_value_name}.fdn
                -DCODE=${function_value_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    foreach(c_abi_case IN ITEMS
            "c-abi-generic;FDN2112"
            "c-abi-return;FDN2113"
            "c-abi-owner;FDN2114"
            "c-abi-function;FDN2114"
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
            NAME stage0.check.${c_abi_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${c_abi_name}.fdn
                -DCODE=${c_abi_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    if(FOUNDATION_BUILD_FUZZERS)
        add_test(
            NAME stage0.run.ownership-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/ownership-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ownership-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/ownership.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.sequences-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/sequences-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/sequences-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/sequences.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.methods-contracts-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/methods-contracts-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/methods-contracts-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/methods-contracts.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.contract-inheritance-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-inheritance-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/contract-inheritance.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.closures-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/closures-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/closures-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/closures.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.collections-sanitized
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
            NAME stage0.run.contract-project-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/contract-project-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/contract-project-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/contracts/expected.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
        add_test(
            NAME stage0.run.c-abi-sanitized
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
            NAME stage0.run.task-suspension-sanitized
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DC_COMPILER=${CMAKE_C_COMPILER}
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.fdn
                -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/task-suspension-sanitized.c
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/task-suspension-sanitized
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/task-suspension.out
                -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
                -DRUNTIME_TASK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/task.c
                -DRUNTIME_CANCELLATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/cancellation.c
                -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-sanitized.cmake
        )
    endif()
    add_test(
        NAME stage0.check.use-after-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/use-after-move.fdn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.double-consumption
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/double-consumption.fdn
            -DCODE=FDN2065
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.move-while-borrowed
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/move-while-borrowed.fdn
            -DCODE=FDN2067
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.maybe-moved
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/maybe-moved.fdn
            -DCODE=FDN2066
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.conflicting-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/conflicting-borrow.fdn
            -DCODE=FDN2073
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.field-assignment-without-edit
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/field-assignment-without-edit.fdn
            -DCODE=FDN2077
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.owned-field-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/owned-field-move.fdn
            -DCODE=FDN2078
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.edit-immutable
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/edit-immutable.fdn
            -DCODE=FDN2072
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.stored-borrow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/stored-borrow.fdn
            -DCODE=FDN2074
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.borrow-return
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/borrow-return.fdn
            -DCODE=FDN2063
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.borrow-field
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/borrow-field.fdn
            -DCODE=FDN2062
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.loop-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/loop-move.fdn
            -DCODE=FDN2079
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.short-circuit-move
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/short-circuit-move.fdn
            -DCODE=FDN2066
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.index-out-of-bounds
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/index-out-of-bounds.fdn
            -DPATTERN=index.out.of.bounds
            -DTRACE_PATTERN=index-out-of-bounds.fdn:3:11
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.check.empty-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/empty-array.fdn
            -DCODE=FDN2081
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.array-length-mismatch
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/array-length-mismatch.fdn
            -DCODE=FDN2082
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.mixed-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/mixed-array.fdn
            -DCODE=FDN2011
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.immutable-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/immutable-index.fdn
            -DCODE=FDN2077
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.owned-array-element
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/owned-array-element.fdn
            -DCODE=FDN2083
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.nested-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/nested-slice.fdn
            -DCODE=FDN2080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.negative-array-length
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negative-array-length.fdn
            -DCODE=FDN1080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.large-array-length
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/large-array-length.fdn
            -DCODE=FDN2080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.stored-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/stored-slice.fdn
            -DCODE=FDN2074
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.returned-slice
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/returned-slice.fdn
            -DCODE=FDN2063
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.string-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/string-index.fdn
            -DCODE=FDN2084
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.negative-index
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negative-index.fdn
            -DPATTERN=index.out.of.bounds
            -DTRACE_PATTERN=negative-index.fdn:3:11
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.check.array-length-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/array-length-overflow.fdn
            -DCODE=FDN1080
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.recursive-array
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/recursive-array.fdn
            -DCODE=FDN2023
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.invalid-utf8
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/invalid-utf8.fdn
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-invalid-utf8.cmake
    )
    add_test(
        NAME stage0.check.unhandled-result
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/unhandled-result.fdn
            -DCODE=FDN2052
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.check.discarded-result-expression
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/discarded-result-expression.fdn
            -DCODE=FDN2051
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.panic-trace
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/panic-trace.fdn
            -DGENERATED=${CMAKE_CURRENT_BINARY_DIR}/panic-trace.c
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/panic-trace
            -DRUNTIME_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c
            -DRUNTIME_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/runtime/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-panic.cmake
    )
    add_test(
        NAME stage0.check.generic-inference
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/generic-inference.fdn
            -DCODE=FDN2045
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
    )
    add_test(
        NAME stage0.run.divide-zero
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/divide-zero.fdn
            -DPATTERN=division.by.zero
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/overflow.fdn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.u64-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-overflow.fdn
            -DPATTERN=u64.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.u64-underflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/u64-underflow.fdn
            -DPATTERN=u64.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.remainder-zero
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/remainder-zero.fdn
            -DPATTERN=remainder.by.zero
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.subtract-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/subtract-overflow.fdn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.multiply-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/multiply-overflow.fdn
            -DPATTERN=i32.overflow
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run-fail.cmake
    )
    add_test(
        NAME stage0.run.negate-overflow
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/negate-overflow.fdn
            -DPATTERN=i32.overflow
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
            NAME stage0.check.${owned_place_name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMPILER=$<TARGET_FILE:foundationc>
                -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/${owned_place_name}.fdn
                -DCODE=${owned_place_code}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-reject.cmake
        )
    endforeach()
    add_test(
        NAME stage0.check.project-private-destructure
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
