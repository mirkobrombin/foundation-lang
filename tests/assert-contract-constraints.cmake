if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED C_OUTPUT OR
   NOT DEFINED LLVM_OUTPUT)
    message(FATAL_ERROR "contract constraint assertion is missing an input")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-c "${SOURCE}" -o "${C_OUTPUT}"
    RESULT_VARIABLE c_result
    OUTPUT_VARIABLE c_stdout
    ERROR_VARIABLE c_stderr
)
if(NOT c_result EQUAL 0)
    message(FATAL_ERROR "C emission failed:\n${c_stdout}${c_stderr}")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-llvm "${SOURCE}" -o "${LLVM_OUTPUT}"
    RESULT_VARIABLE llvm_result
    OUTPUT_VARIABLE llvm_stdout
    ERROR_VARIABLE llvm_stderr
)
if(NOT llvm_result EQUAL 0)
    message(FATAL_ERROR "LLVM emission failed:\n${llvm_stdout}${llvm_stderr}")
endif()

file(READ "${C_OUTPUT}" c_source)
if(c_source MATCHES "fdn_vtable->" OR c_source MATCHES "->fdn_vtable")
    message(FATAL_ERROR "constrained generic C output contains dynamic dispatch")
endif()
if(NOT c_source MATCHES "fdn_fn_State_increment_[0-9]+" OR
   NOT c_source MATCHES "fdn_fn_Integer_current_[0-9]+")
    message(FATAL_ERROR "constrained generic C output lacks concrete method calls")
endif()

file(STRINGS "${LLVM_OUTPUT}" llvm_lines)
foreach(line IN LISTS llvm_lines)
    if(line MATCHES "call.*%[A-Za-z0-9_.]+\\(" AND NOT line MATCHES "call.*@")
        message(FATAL_ERROR "constrained generic LLVM output contains indirect call: ${line}")
    endif()
endforeach()

message(STATUS "contract constraints use static dispatch in C and LLVM")
