if(NOT DEFINED COMPILER OR NOT DEFINED ROOT OR NOT DEFINED PROJECT OR
   NOT DEFINED OUTPUT_DIRECTORY)
    message(FATAL_ERROR
        "self-host assertion requires COMPILER, ROOT, PROJECT, and OUTPUT_DIRECTORY"
    )
endif()

function(run_checked label)
    execute_process(
        COMMAND ${ARGN}
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
    set(command_output "${output}" PARENT_SCOPE)
    set(command_error "${error}" PARENT_SCOPE)
endfunction()

function(require_hello_output label output)
    if(NOT output STREQUAL "hello from foundation\n")
        message(FATAL_ERROR "${label} output mismatch:\n${output}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
set(hello "${ROOT}/tests/cases/accept/hello.fn")
set(tests "${ROOT}/tests/cases/accept/test-declarations.fn")
set(llvm_ir "${OUTPUT_DIRECTORY}/hello.ll")
set(llvm_binary "${OUTPUT_DIRECTORY}/hello-llvm${EXECUTABLE_SUFFIX}")
set(c_binary "${OUTPUT_DIRECTORY}/hello-c${EXECUTABLE_SUFFIX}")

run_checked("version" "${COMPILER}" version)
if(NOT command_output STREQUAL "foundationc 0.1.0\n")
    message(FATAL_ERROR "version output mismatch:\n${command_output}")
endif()

run_checked("check" "${COMPILER}" check "${hello}")
run_checked("emit-llvm" "${COMPILER}" emit-llvm "${hello}" -o "${llvm_ir}")
if(NOT EXISTS "${llvm_ir}")
    message(FATAL_ERROR "emit-llvm did not create ${llvm_ir}")
endif()
file(SIZE "${llvm_ir}" llvm_ir_size)
if(llvm_ir_size EQUAL 0)
    message(FATAL_ERROR "emit-llvm created an empty file")
endif()

run_checked("LLVM build" "${COMPILER}" build "${hello}" -o "${llvm_binary}")
run_checked("LLVM build output" "${llvm_binary}")
require_hello_output("LLVM build" "${command_output}")

run_checked("C build" "${COMPILER}" build "${hello}" -o "${c_binary}" --backend c)
run_checked("C build output" "${c_binary}")
require_hello_output("C build" "${command_output}")

run_checked("LLVM run" "${COMPILER}" run "${hello}")
require_hello_output("LLVM run" "${command_output}")
run_checked("C run" "${COMPILER}" run "${hello}" --backend c)
require_hello_output("C run" "${command_output}")

foreach(backend IN ITEMS llvm c)
    run_checked("${backend} test" "${COMPILER}" test "${tests}" --backend "${backend}")
    if(NOT command_output MATCHES "ok addition returns the sum" OR
       NOT command_output MATCHES "ok explicit pass" OR
       NOT command_output MATCHES "2 passed; 0 failed")
        message(FATAL_ERROR "${backend} test summary is incomplete:\n${command_output}")
    endif()
endforeach()

run_checked("package inspect" "${COMPILER}" package inspect "${PROJECT}")
if(NOT command_output MATCHES "format foundation.package/v1" OR
   NOT command_output MATCHES "format foundation.lock/v1")
    message(FATAL_ERROR "package inspect output is incomplete:\n${command_output}")
endif()

message(STATUS "self-hosted compiler commands passed")
