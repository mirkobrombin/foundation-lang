foreach(required IN ITEMS COMPILER PROJECT WORK MODE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "freestanding library assertion requires ${required}")
    endif()
endforeach()
if(NOT DEFINED BACKEND)
    set(BACKEND llvm)
endif()
if(NOT DEFINED EXECUTABLE_SUFFIX)
    set(EXECUTABLE_SUFFIX "")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${PROJECT}/" DESTINATION "${WORK}/project")
set(project "${WORK}/project")
set(cc_arguments)
if(DEFINED CC AND NOT CC STREQUAL "")
    set(cc_arguments --cc "${CC}")
endif()

function(build_library project_path output triple)
    execute_process(
        COMMAND "${COMPILER}" build-library "${project_path}" -o "${output}" --kind static
                --target freestanding --triple "${triple}" --backend "${BACKEND}"
                ${cc_arguments} ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "freestanding build-library failed:\n${output_text}${error_text}")
    endif()
endfunction()

function(archive_path output directory)
    file(GLOB archives "${directory}/lib/*.a")
    list(LENGTH archives count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "expected one archive in ${directory}/lib, found: ${archives}")
    endif()
    set(${output} "${archives}" PARENT_SCOPE)
endfunction()

# Runs the compiler and requires a failure. An expectation of "usage" requires status 2 and a
# foundationc message; otherwise the named diagnostic code must be reported.
function(expect_rejected label expectation)
    execute_process(
        COMMAND "${COMPILER}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(expectation STREQUAL "usage")
        if(NOT result EQUAL 2 OR NOT error_text MATCHES "(usage:|foundationc:)")
            message(FATAL_ERROR
                "${label} was not a usage error (${result}):\n${output_text}${error_text}")
        endif()
    elseif(result EQUAL 0 OR NOT error_text MATCHES "error\\[${expectation}\\]")
        message(FATAL_ERROR
            "${label} did not report ${expectation} (${result}):\n${output_text}${error_text}")
    endif()
endfunction()

function(compile_harness output harness include_directory)
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra -DFOUNDATION_LIBRARY_STATIC
                -I "${include_directory}" "${harness}" ${ARGN} -o "${output}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "harness ${harness} did not build:\n${output_text}${error_text}")
    endif()
endfunction()

function(run_harness label executable expected)
    execute_process(
        COMMAND "${executable}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0 OR NOT output_text MATCHES "${expected}")
        message(FATAL_ERROR "${label} failed (${result}):\n${output_text}${error_text}")
    endif()
    message(STATUS "${label}: ${output_text}")
endfunction()

function(read_symbols archive defined_variable undefined_variable)
    execute_process(
        COMMAND "${NM}" "${archive}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE symbols
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "llvm-nm failed:\n${error_text}")
    endif()
    string(REPLACE "\n" ";" lines "${symbols}")
    set(defined)
    set(undefined)
    foreach(line IN LISTS lines)
        if(line MATCHES "^ +[Uw] ([^ ]+)$")
            list(APPEND undefined "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^[0-9a-fA-F]+ [A-Za-z] ([^ ]+)$")
            list(APPEND defined "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES defined)
    list(REMOVE_DUPLICATES undefined)
    set(${defined_variable} "${defined}" PARENT_SCOPE)
    set(${undefined_variable} "${undefined}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "symbols")
    build_library("${project}" "${WORK}/dist" "${TRIPLE}")
    archive_path(archive "${WORK}/dist")
    read_symbols("${archive}" defined undefined)
    set(unexpected)
    foreach(symbol IN LISTS undefined)
        string(REGEX REPLACE "^_" "" name "${symbol}")
        list(FIND defined "${symbol}" found)
        if(found GREATER -1)
            continue()
        endif()
        if(name MATCHES "^fdn_hook_(alloc|context|free|panic|write)$" OR
           name MATCHES "^mem(cpy|move|set|cmp)$" OR
           name MATCHES "^harness_(switch_context|reenter)$" OR
           name MATCHES "^_(aeabi_|atomic_)" OR name MATCHES "^_u?(div|mod|mul)[sdt]i3$" OR
           name STREQUAL "GLOBAL_OFFSET_TABLE_")
            continue()
        endif()
        list(APPEND unexpected "${symbol}")
    endforeach()
    if(unexpected)
        message(FATAL_ERROR "freestanding archive references forbidden symbols: ${unexpected}")
    endif()
    foreach(required IN ITEMS freestanding_greeting freestanding_print freestanding_outer
            fdn_context_init fdn_alloc fdn_dealloc fdn_string_drop fdn_panic fdn_panic_cstr
            fdn_frame_enter fdn_frame_leave fdn_bounds_check fdn_println
            foundation_runtime_string_copy)
        list(FIND defined "${required}" plain)
        list(FIND defined "_${required}" prefixed)
        if(plain EQUAL -1 AND prefixed EQUAL -1)
            message(FATAL_ERROR "freestanding archive does not define ${required}")
        endif()
    endforeach()
    set(hosted_symbols main malloc free calloc realloc printf fprintf fwrite fputs fputc puts
        stderr stdout _Exit exit abort write _stack_chk_fail _stack_chk_guard)
    list(JOIN hosted_symbols "|" hosted_pattern)
    foreach(symbol IN LISTS defined undefined)
        string(REGEX REPLACE "^_" "" name "${symbol}")
        if(name MATCHES "^(${hosted_pattern})$" OR name MATCHES "^pthread_")
            message(FATAL_ERROR "freestanding archive contains hosted symbol ${symbol}")
        endif()
    endforeach()
    message(STATUS "freestanding archive symbols stay within the permitted set")
elseif(MODE STREQUAL "run")
    # Optimized LLVM library code records only native boundary frames, so a panic names the
    # exported C symbol at its declaration. The C backend also records the Foundation frame.
    if(BACKEND STREQUAL "c")
        set(index_location
            "test\\.freestanding_library\\.FreestandingIndex \\([^)]*library\\.fn:21\\)")
        set(nested_location "FreestandingNestedPanic")
    else()
        set(index_location "\\[native\\] freestanding_index \\([^)]*library\\.fn:19\\)")
        set(nested_location "freestanding_nested_panic")
    endif()
    foreach(required IN ITEMS TRIPLE C_COMPILER HARNESS_DIRECTORY RUN)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "run mode requires ${required}")
        endif()
    endforeach()
    build_library("${project}" "${WORK}/dist" "${TRIPLE}" --pic)
    archive_path(archive "${WORK}/dist")
    set(include_directory "${WORK}/dist/include")
    set(executable "${WORK}/harness${EXECUTABLE_SUFFIX}")
    if(RUN STREQUAL "hooks")
        compile_harness("${executable}" "${HARNESS_DIRECTORY}/freestanding_hooks.c"
                        "${include_directory}" "${archive}")
        run_harness("hooks" "${executable}" "hooks ok: [1-9][0-9]* allocations" hooks)
        if(NOT DEFINED SILENT_PROJECT)
            message(FATAL_ERROR "hooks run requires SILENT_PROJECT")
        endif()
        file(COPY "${SILENT_PROJECT}/" DESTINATION "${WORK}/silent")
        build_library("${WORK}/silent" "${WORK}/silent-dist" "${TRIPLE}" --pic)
        archive_path(silent_archive "${WORK}/silent-dist")
        file(READ "${WORK}/silent-dist/share/foundation/freestanding_silent.pii.json" silent_pii)
        if(silent_pii MATCHES "fdn_hook_write")
            message(FATAL_ERROR "a library that never prints requires the write hook")
        endif()
        set(silent "${WORK}/silent-harness${EXECUTABLE_SUFFIX}")
        compile_harness("${silent}" "${HARNESS_DIRECTORY}/freestanding_silent.c"
                        "${WORK}/silent-dist/include" "${silent_archive}")
        run_harness("silent" "${silent}" "silent ok")
    elseif(RUN STREQUAL "contexts")
        compile_harness("${executable}" "${HARNESS_DIRECTORY}/freestanding_contexts.c"
                        "${include_directory}" "${archive}" -pthread)
        run_harness("contexts" "${executable}" "contexts ok: panic location ${nested_location}")
    elseif(RUN STREQUAL "panic-index")
        compile_harness("${executable}" "${HARNESS_DIRECTORY}/freestanding_hooks.c"
                        "${include_directory}" "${archive}")
        run_harness("index panic" "${executable}" "panic index out of bounds at ${index_location}"
                    index)
    elseif(RUN STREQUAL "panic-allocation")
        compile_harness("${executable}" "${HARNESS_DIRECTORY}/freestanding_hooks.c"
                        "${include_directory}" "${archive}")
        run_harness("allocation panic" "${executable}" "panic allocation failed at " allocation)
    elseif(RUN STREQUAL "panic-return")
        compile_harness("${executable}" "${HARNESS_DIRECTORY}/freestanding_panic_return.c"
                        "${include_directory}" "${archive}")
        execute_process(
            COMMAND "${executable}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output_text
            ERROR_VARIABLE error_text
        )
        if(result EQUAL 0 OR output_text MATCHES "execution resumed" OR
           NOT output_text MATCHES "panic hook returned for nested panic")
            message(FATAL_ERROR
                "a returning panic hook did not stop the program (${result}):\n"
                "${output_text}${error_text}")
        endif()
        message(STATUS "a returning panic hook stopped the program: ${result}")
    else()
        message(FATAL_ERROR "unknown run ${RUN}")
    endif()
elseif(MODE STREQUAL "cpu")
    file(READ "${project}/foundation.lock" lock_before)
    build_library("${project}" "${WORK}/arm" "thumbv7em-none-eabi" --cpu cortex-m4)
    archive_path(arm_archive "${WORK}/arm")
    execute_process(
        COMMAND "${READELF}" --arch-specific "${arm_archive}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE attributes
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "llvm-readelf failed:\n${error_text}")
    endif()
    string(REGEX MATCHALL "File: [^\n]*" members "${attributes}")
    string(REGEX MATCHALL "TagName: CPU_name[ \n]+Value: cortex-m4" cpus "${attributes}")
    list(LENGTH members member_count)
    list(LENGTH cpus cpu_count)
    if(member_count LESS 3 OR NOT member_count EQUAL cpu_count)
        message(FATAL_ERROR
            "not every Arm member records CPU cortex-m4 (${cpu_count} of ${member_count}):\n"
            "${attributes}")
    endif()
    file(READ "${WORK}/arm/share/foundation/freestanding_library.pii.json" arm_pii)
    if(NOT arm_pii MATCHES "\"triple\":\"thumbv7em-unknown-none-eabi\"")
        message(FATAL_ERROR "PII does not record the normalized Arm triple:\n${arm_pii}")
    endif()
    if(NOT arm_pii MATCHES "\"cpu\":\"cortex-m4\",\"features\":\\[\\]")
        message(FATAL_ERROR "PII does not record the Arm CPU:\n${arm_pii}")
    endif()

    build_library("${project}" "${WORK}/riscv" "riscv32-unknown-none-elf" --features "+zba,-c")
    archive_path(riscv_archive "${WORK}/riscv")
    execute_process(
        COMMAND "${READELF}" --arch-specific "${riscv_archive}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE attributes
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "llvm-readelf failed:\n${error_text}")
    endif()
    string(REGEX MATCHALL "File: [^\n]*" members "${attributes}")
    string(REGEX MATCHALL "TagName: arch[ \n]+Value: [^\n]*" arches "${attributes}")
    list(LENGTH members member_count)
    list(LENGTH arches arch_count)
    if(member_count LESS 3 OR NOT member_count EQUAL arch_count)
        message(FATAL_ERROR
            "not every RISC-V member records its ISA (${arch_count} of ${member_count}):\n"
            "${attributes}")
    endif()
    foreach(arch IN LISTS arches)
        if(NOT arch MATCHES "_zba" OR arch MATCHES "_c[0-9]")
            message(FATAL_ERROR "RISC-V member ISA ignores the features: ${arch}")
        endif()
    endforeach()
    file(READ "${WORK}/riscv/share/foundation/freestanding_library.pii.json" riscv_pii)
    if(NOT riscv_pii MATCHES "\"cpu\":\"generic\",\"features\":\\[\"-c\",\"\\+zba\"\\]")
        message(FATAL_ERROR "PII does not record the sorted RISC-V features:\n${riscv_pii}")
    endif()
    file(READ "${project}/foundation.lock" lock_after)
    if(NOT lock_before STREQUAL lock_after)
        message(FATAL_ERROR "freestanding build-library changed the lock")
    endif()
    message(STATUS "CPU and features reached every member and PII")
elseif(MODE STREQUAL "cpu-reject")
    set(common "${project}" -o "${WORK}/rejected" --kind static --target freestanding
               --triple thumbv7em-none-eabi ${cc_arguments})
    expect_rejected("unknown CPU" FDN8006 build-library ${common} --cpu no-such-cpu)
    expect_rejected("unknown feature" FDN8007 build-library ${common} --features +no-such-feature)
    expect_rejected("unsigned feature" FDN8007 build-library ${common} --features neon)
    # A function drops empty arguments, so the empty list is passed directly.
    execute_process(
        COMMAND "${COMPILER}" build-library ${common} --features ""
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(result EQUAL 0 OR NOT error_text MATCHES "error\\[FDN8007\\]")
        message(FATAL_ERROR "empty feature list did not report FDN8007:\n${error_text}")
    endif()
    expect_rejected("duplicate feature" FDN8007 build-library ${common} --features "+neon,-neon")
    expect_rejected("CPU without the freestanding target" usage build-library "${project}"
                    -o "${WORK}/rejected" --kind static --cpu cortex-m4)
    expect_rejected("PII CPU without a triple" usage emit-pii "${project}"
                    -o "${WORK}/rejected.json" --cpu cortex-m4)
    expect_rejected("PII unknown CPU" FDN8006 emit-pii "${project}" -o "${WORK}/rejected.json"
                    --triple thumbv7em-none-eabi --cpu no-such-cpu)
    message(STATUS "invalid CPUs and feature lists were rejected")
elseif(MODE STREQUAL "cc")
    set(gcc_stub "${WORK}/gcc-stub")
    set(clang_stub "${WORK}/clang-stub")
    file(WRITE "${gcc_stub}" "#!/bin/sh\necho 'gcc (stub) 1.0.0'\nexit 0\n")
    file(WRITE "${clang_stub}" "#!/bin/sh\n"
         "if [ \"$1\" = --version ]; then echo 'clang version 99.0.0'; exit 0; fi\nexit 1\n")
    file(CHMOD "${gcc_stub}" "${clang_stub}"
         PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    set(common "${project}" -o "${WORK}/rejected" --kind static --target freestanding
               --triple "${TRIPLE}")
    expect_rejected("non-Clang compiler" FDN8008 build-library ${common} --cc "${gcc_stub}")
    expect_rejected("Clang without the target" FDN8008 build-library ${common}
                    --cc "${clang_stub}")
    build_library("${project}" "${WORK}/dist" "${TRIPLE}")
    message(STATUS "C compiler identification accepted Clang and rejected the stubs")
elseif(MODE STREQUAL "wasm")
    foreach(required IN ITEMS CLANG LINKER HARNESS_DIRECTORY)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "wasm mode requires ${required}")
        endif()
    endforeach()
    build_library("${project}" "${WORK}/dist" "wasm32-unknown-unknown")
    archive_path(archive "${WORK}/dist")
    get_filename_component(linker_directory "${LINKER}" DIRECTORY)
    set(module "${WORK}/module.wasm")
    execute_process(
        COMMAND "${CLANG}" --target=wasm32-unknown-unknown "-B${linker_directory}" -std=c11
                -ffreestanding -nostdlib -O2 -Wall -Wextra -Wpedantic -Werror
                -I "${WORK}/dist/include" -Wl,--no-entry -Wl,--export-memory
                "${HARNESS_DIRECTORY}/freestanding_wasm.c" "${archive}" -o "${module}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "WebAssembly module did not link:\n${output_text}${error_text}")
    endif()
    file(READ "${module}" magic OFFSET 0 LIMIT 8 HEX)
    if(NOT magic STREQUAL "0061736d01000000")
        message(FATAL_ERROR "WebAssembly module has invalid magic: ${magic}")
    endif()
    message(STATUS "freestanding wasm32 archive linked into a module without WASI")
elseif(MODE STREQUAL "deterministic")
    build_library("${project}" "${WORK}/first" "${TRIPLE}")
    build_library("${project}" "${WORK}/second" "${TRIPLE}")
    file(GLOB_RECURSE first_files RELATIVE "${WORK}/first" "${WORK}/first/*")
    file(GLOB_RECURSE second_files RELATIVE "${WORK}/second" "${WORK}/second/*")
    list(SORT first_files)
    list(SORT second_files)
    if(NOT first_files STREQUAL second_files)
        message(FATAL_ERROR "bundle layouts differ:\n${first_files}\n${second_files}")
    endif()
    foreach(relative IN LISTS first_files)
        file(SHA256 "${WORK}/first/${relative}" first_hash)
        file(SHA256 "${WORK}/second/${relative}" second_hash)
        if(NOT first_hash STREQUAL second_hash)
            message(FATAL_ERROR "${relative} differs between identical builds")
        endif()
    endforeach()
    message(STATUS "two freestanding builds produced identical bundles")
elseif(MODE STREQUAL "cli-reject")
    set(output "${WORK}/rejected")
    expect_rejected("shared library" usage build-library "${project}" -o "${output}"
                    --kind shared --target freestanding --triple "${TRIPLE}")
    expect_rejected("missing triple" usage build-library "${project}" -o "${output}"
                    --kind static --target freestanding)
    expect_rejected("triple without target" usage build-library "${project}" -o "${output}"
                    --kind static --triple "${TRIPLE}")
    expect_rejected("repeated option" usage build-library "${project}" -o "${output}"
                    --kind static --target freestanding --triple "${TRIPLE}"
                    --triple "${TRIPLE}")
    expect_rejected("freestanding lock without target" usage build-library "${project}"
                    -o "${output}" --kind static)
    expect_rejected("PII without triple" usage emit-pii "${project}" -o "${output}.json")
    file(COPY "${PROJECT}/" DESTINATION "${WORK}/hosted")
    execute_process(
        COMMAND "${COMPILER}" package resolve "${WORK}/hosted"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_VARIABLE error_text
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "host resolution failed:\n${error_text}")
    endif()
    expect_rejected("host lock" usage build-library "${WORK}/hosted" -o "${output}"
                    --kind static --target freestanding --triple "${TRIPLE}")
    expect_rejected("PII triple for a host lock" usage emit-pii "${WORK}/hosted"
                    -o "${output}.json" --triple "${TRIPLE}")
    execute_process(
        COMMAND "${COMPILER}" build-library "${project}" -o "${output}" --kind static
                --target freestanding --triple avr-unknown-unknown ${cc_arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output_text
        ERROR_VARIABLE error_text
    )
    if(error_text MATCHES "error\\[FDN8002\\]")
        message(STATUS "LLVM does not register a 16-bit target; FDN8005 was not exercised")
    elseif(result EQUAL 0 OR NOT error_text MATCHES "error\\[FDN8005\\]")
        message(FATAL_ERROR "a 16-bit triple did not report FDN8005:\n${output_text}${error_text}")
    endif()
    message(STATUS "invalid freestanding command lines were rejected")
else()
    message(FATAL_ERROR "unknown freestanding library mode ${MODE}")
endif()
