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
)
target_include_directories(foundation_runtime PUBLIC runtime/include)

add_library(foundation_compiler STATIC
    stage0/src/codegen.cpp
    stage0/src/diagnostic.cpp
    stage0/src/driver.cpp
    stage0/src/lexer.cpp
    stage0/src/lower.cpp
    stage0/src/parser.cpp
    stage0/src/process.cpp
    stage0/src/sema.cpp
    stage0/src/type.cpp
)
target_include_directories(foundation_compiler PUBLIC stage0/include)
target_compile_definitions(foundation_compiler PRIVATE
    FOUNDATION_C_COMPILER="${CMAKE_C_COMPILER}"
    FOUNDATION_C_COMPILER_ID="${CMAKE_C_COMPILER_ID}"
    FOUNDATION_RUNTIME_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
    FOUNDATION_RUNTIME_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.c"
)

add_executable(foundationc stage0/src/main.cpp)
target_link_libraries(foundationc PRIVATE foundation_compiler)

if(MSVC)
    target_compile_options(foundation_runtime PRIVATE /W4)
    target_compile_options(foundation_compiler PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(foundationc PRIVATE /W4 /permissive- /EHsc)
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE /WX)
        target_compile_options(foundation_compiler PRIVATE /WX)
        target_compile_options(foundationc PRIVATE /WX)
    endif()
else()
    target_compile_options(foundation_runtime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundation_compiler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(foundationc PRIVATE -Wall -Wextra -Wpedantic)
    if(FOUNDATION_WARNINGS_AS_ERRORS)
        target_compile_options(foundation_runtime PRIVATE -Werror)
        target_compile_options(foundation_compiler PRIVATE -Werror)
        target_compile_options(foundationc PRIVATE -Werror)
    endif()
endif()

if(FOUNDATION_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "Foundation fuzz targets require Clang")
    endif()

    target_compile_options(foundation_compiler PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined
    )
    target_link_options(foundationc PRIVATE -fsanitize=address,undefined)
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
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(foundation_stage0_tests stage0/tests/compiler_test.cpp)
    target_link_libraries(foundation_stage0_tests PRIVATE foundation_compiler)
    if(FOUNDATION_BUILD_FUZZERS)
        target_link_options(foundation_stage0_tests PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(foundation_stage0_tests PRIVATE /W4 /permissive- /EHsc)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_stage0_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(foundation_stage0_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(FOUNDATION_WARNINGS_AS_ERRORS)
            target_compile_options(foundation_stage0_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME stage0.unit COMMAND foundation_stage0_tests)
    add_test(
        NAME stage0.check.accept
        COMMAND foundationc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/hello.fdn
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
        NAME stage0.check.reject
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/reject/missing-semicolon.fdn
            -DCODE=FDN1014
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
        NAME stage0.run.typed-control
        COMMAND ${CMAKE_COMMAND}
            -DCOMPILER=$<TARGET_FILE:foundationc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.fdn
            -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/accept/typed-control.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert-run.cmake
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
endif()
