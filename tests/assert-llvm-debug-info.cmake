if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "LLVM debug assertion requires COMPILER, SOURCE, and OUTPUT")
endif()

execute_process(
    COMMAND "${COMPILER}" emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "LLVM emission failed with ${result}:\n${output}${error}")
endif()

file(READ "${OUTPUT}" ir)
foreach(marker IN ITEMS "!DICompileUnit" "!DISubprogram" "!DILocalVariable" "!dbg")
    string(FIND "${ir}" "${marker}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "LLVM output is missing ${marker}")
    endif()
endforeach()

message(STATUS "LLVM output contains source-level debug metadata")
