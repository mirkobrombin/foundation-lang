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

if(DEFINED EXPECTED_SOURCE)
    cmake_path(ABSOLUTE_PATH EXPECTED_SOURCE NORMALIZE OUTPUT_VARIABLE expected_source)
    cmake_path(GET expected_source FILENAME expected_filename)
    cmake_path(GET expected_source PARENT_PATH expected_directory)
    set(expected_file "!DIFile(filename: \"${expected_filename}\", directory: \"${expected_directory}\")")
    string(FIND "${ir}" "${expected_file}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "LLVM output does not identify ${expected_source}")
    endif()
endif()

message(STATUS "LLVM output contains source-level debug metadata")
