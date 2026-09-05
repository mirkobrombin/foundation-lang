file(REMOVE_RECURSE "${WORK}")
set(project "${WORK}/project")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${SOURCE}/" DESTINATION "${project}")

execute_process(
    COMMAND "${COMPILER}" package resolve "${project}"
    RESULT_VARIABLE resolve_result
    OUTPUT_VARIABLE resolve_output
    ERROR_VARIABLE resolve_error
)
if(NOT resolve_result EQUAL 0)
    message(FATAL_ERROR "cannot resolve native library fixture:\n${resolve_output}${resolve_error}")
endif()

set(shared_dist "${WORK}/shared")
set(static_dist "${WORK}/static")
set(pic_static_llvm_dist "${WORK}/static-pic-llvm")
set(pic_static_c_dist "${WORK}/static-pic-c")
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${shared_dist}"
            --kind shared --backend llvm
    RESULT_VARIABLE shared_result
    OUTPUT_VARIABLE shared_output
    ERROR_VARIABLE shared_error
)
if(NOT shared_result EQUAL 0)
    message(FATAL_ERROR "cannot build LLVM shared library:\n${shared_output}${shared_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${static_dist}"
            --kind static --backend c
    RESULT_VARIABLE static_result
    OUTPUT_VARIABLE static_output
    ERROR_VARIABLE static_error
)
if(NOT static_result EQUAL 0)
    message(FATAL_ERROR "cannot build C static library:\n${static_output}${static_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${pic_static_llvm_dist}"
            --kind static --pic --backend llvm
    RESULT_VARIABLE pic_static_llvm_result
    OUTPUT_VARIABLE pic_static_llvm_output
    ERROR_VARIABLE pic_static_llvm_error
)
if(NOT pic_static_llvm_result EQUAL 0)
    message(FATAL_ERROR
            "cannot build position-independent LLVM static library:\n${pic_static_llvm_output}${pic_static_llvm_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${pic_static_c_dist}"
            --kind static --pic --backend c
    RESULT_VARIABLE pic_static_c_result
    OUTPUT_VARIABLE pic_static_c_output
    ERROR_VARIABLE pic_static_c_error
)
if(NOT pic_static_c_result EQUAL 0)
    message(FATAL_ERROR
            "cannot build position-independent C static library:\n${pic_static_c_output}${pic_static_c_error}")
endif()
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${WORK}/redundant-pic"
            --kind shared --pic
    RESULT_VARIABLE redundant_pic_result
    OUTPUT_QUIET
    ERROR_VARIABLE redundant_pic_error
)
if(NOT redundant_pic_result EQUAL 2 OR
   NOT redundant_pic_error MATCHES "usage:")
    message(FATAL_ERROR "shared library accepted redundant --pic:\n${redundant_pic_error}")
endif()
file(WRITE "${project}/native/not-an-object.txt" "invalid native input\n")
execute_process(
    COMMAND "${COMPILER}" build-library "${project}" -o "${WORK}/invalid"
            --kind static --native "${project}/native/not-an-object.txt"
    RESULT_VARIABLE invalid_native_result
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_native_error
)
if(NOT invalid_native_result EQUAL 2 OR
   NOT invalid_native_error MATCHES "must be C sources or objects")
    message(FATAL_ERROR "invalid native library input was accepted:\n${invalid_native_error}")
endif()

if(SYSTEM_NAME STREQUAL "Windows")
    set(shared "${shared_dist}/lib/sample_native.dll")
    set(import_library "${shared_dist}/lib/sample_native.lib")
    set(static "${static_dist}/lib/sample_native.lib")
    set(pic_static_llvm "${pic_static_llvm_dist}/lib/sample_native.lib")
    set(pic_static_c "${pic_static_c_dist}/lib/sample_native.lib")
elseif(SYSTEM_NAME STREQUAL "Darwin")
    set(shared "${shared_dist}/lib/libsample_native.2.dylib")
    set(shared_link_artifact "${shared_dist}/lib/libsample_native.dylib")
    set(static "${static_dist}/lib/libsample_native.a")
    set(pic_static_llvm "${pic_static_llvm_dist}/lib/libsample_native.a")
    set(pic_static_c "${pic_static_c_dist}/lib/libsample_native.a")
else()
    set(shared "${shared_dist}/lib/libsample_native.so.2")
    set(shared_link_artifact "${shared_dist}/lib/libsample_native.so")
    set(static "${static_dist}/lib/libsample_native.a")
    set(pic_static_llvm "${pic_static_llvm_dist}/lib/libsample_native.a")
    set(pic_static_c "${pic_static_c_dist}/lib/libsample_native.a")
endif()

if(SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND "${C_COMPILER}" -shared -Wl,--whole-archive "${pic_static_llvm}"
                -Wl,--no-whole-archive -lm -ldl -pthread
                -o "${WORK}/libsample_llvm_embedded.so"
        RESULT_VARIABLE pic_llvm_link_result
        OUTPUT_VARIABLE pic_llvm_link_output
        ERROR_VARIABLE pic_llvm_link_error
    )
    if(NOT pic_llvm_link_result EQUAL 0)
        message(FATAL_ERROR
                "cannot embed position-independent LLVM archive in a shared library:\n${pic_llvm_link_output}${pic_llvm_link_error}")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}" -shared -Wl,--whole-archive "${pic_static_c}"
                -Wl,--no-whole-archive -lm -ldl -pthread
                -o "${WORK}/libsample_c_embedded.so"
        RESULT_VARIABLE pic_c_link_result
        OUTPUT_VARIABLE pic_c_link_output
        ERROR_VARIABLE pic_c_link_error
    )
    if(NOT pic_c_link_result EQUAL 0)
        message(FATAL_ERROR
                "cannot embed position-independent C archive in a shared library:\n${pic_c_link_output}${pic_c_link_error}")
    endif()
endif()

set(repeat_project "${WORK}/repeat-project")
file(COPY "${SOURCE}/" DESTINATION "${repeat_project}")
execute_process(
    COMMAND "${COMPILER}" package resolve "${repeat_project}"
    RESULT_VARIABLE repeat_resolve_result
    OUTPUT_QUIET
    ERROR_VARIABLE repeat_resolve_error
)
if(NOT repeat_resolve_result EQUAL 0)
    message(FATAL_ERROR "cannot resolve repeated native library fixture:\n${repeat_resolve_error}")
endif()
set(repeat_dist "${WORK}/shared-repeat")
execute_process(
    COMMAND "${COMPILER}" build-library "${repeat_project}" -o "${repeat_dist}"
            --kind shared --backend llvm
            --native "${repeat_project}/native/libfuse/increment.c"
    RESULT_VARIABLE repeat_result
    OUTPUT_QUIET
    ERROR_VARIABLE repeat_error
)
if(NOT repeat_result EQUAL 0)
    message(FATAL_ERROR "cannot repeat LLVM shared library build:\n${repeat_error}")
endif()
if(SYSTEM_NAME STREQUAL "Windows")
    set(repeated_shared "${repeat_dist}/lib/sample_native.dll")
elseif(SYSTEM_NAME STREQUAL "Darwin")
    set(repeated_shared "${repeat_dist}/lib/libsample_native.2.dylib")
else()
    set(repeated_shared "${repeat_dist}/lib/libsample_native.so.2")
endif()
file(SHA256 "${shared}" shared_sha256)
file(SHA256 "${repeated_shared}" repeated_sha256)
if(NOT shared_sha256 STREQUAL repeated_sha256)
    message(FATAL_ERROR "shared library changes across checkout paths")
endif()

foreach(path IN ITEMS
        "${shared}"
        "${static}"
        "${pic_static_llvm}"
        "${pic_static_c}"
        "${shared_dist}/include/sample_native.h"
        "${shared_dist}/include/foundation/library.h"
        "${shared_dist}/share/foundation/sample_native.pii.json"
        "${static_dist}/include/sample_native.h"
        "${static_dist}/include/foundation/library.h"
        "${static_dist}/share/foundation/sample_native.pii.json")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "native library bundle is missing ${path}")
    endif()
endforeach()
if(NOT SYSTEM_NAME STREQUAL "Windows")
    if(NOT IS_SYMLINK "${shared_link_artifact}")
        message(FATAL_ERROR "native library bundle has no linker-name symlink")
    endif()
    file(READ_SYMLINK "${shared_link_artifact}" shared_link_target)
    get_filename_component(shared_filename "${shared}" NAME)
    if(NOT shared_link_target STREQUAL shared_filename)
        message(FATAL_ERROR "native library linker-name symlink has the wrong target")
    endif()
endif()

file(READ "${shared_dist}/include/sample_native.h" header)
file(READ "${shared_dist}/include/foundation/library.h" support_header)
foreach(expected IN ITEMS
        "FOUNDATION_LIBRARY_ABI_MAJOR UINT32_C(1)"
        "FOUNDATION_LIBRARY_ABI_MINOR UINT32_C(0)")
    string(FIND "${support_header}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "native library support header is missing ${expected}")
    endif()
endforeach()
foreach(expected IN ITEMS
        "FOUNDATION_LIBRARY_API"
        "FOUNDATION_SAMPLE_NATIVE_C_ABI_H"
        "sample_native_NativeScale"
        "sample_native_NativePoint"
        "fdn_c_function_i32_i32"
        "sample_callback"
        "sample_apply"
        "sample_increment"
        "sample_invoke"
        "sample_round_trip"
        "sample_sine"
        "foundation/library.h")
    string(FIND "${header}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "native library header is missing ${expected}")
    endif()
endforeach()
string(FIND "${header}" "sample_private_identity" private_header_entry)
if(NOT private_header_entry EQUAL -1)
    message(FATAL_ERROR "native library header exposes a private native entry")
endif()
file(READ "${shared_dist}/share/foundation/sample_native.pii.json" shared_pii)
file(READ "${static_dist}/share/foundation/sample_native.pii.json" static_pii)
if(NOT shared_pii STREQUAL static_pii)
    message(FATAL_ERROR "native library kinds produced different Package Interface IR")
endif()
foreach(expected IN ITEMS
        "\"abi_major\":1"
        "\"abi_minor\":3"
        "\"language\":1")
    string(FIND "${shared_pii}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Package Interface IR is missing ${expected}")
    endif()
endforeach()

set(consumer "${project}/consumer.c")
set(cpp_consumer "${project}/consumer.cpp")
if(C_COMPILER_ID STREQUAL "MSVC")
    set(shared_consumer "${shared_dist}/lib/shared-consumer.exe")
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /W4 /WX "${consumer}"
                "/I${shared_dist}/include" "${import_library}" bcrypt.lib ws2_32.lib
                "/Fe:${shared_consumer}"
        RESULT_VARIABLE shared_compile_result
        OUTPUT_VARIABLE shared_compile_output
        ERROR_VARIABLE shared_compile_error
    )
    set(static_consumer "${static_dist}/lib/static-consumer.exe")
    execute_process(
        COMMAND "${C_COMPILER}" /nologo /std:c11 /W4 /WX
                /DFOUNDATION_LIBRARY_STATIC=1 "${consumer}"
                "/I${static_dist}/include" "${static}" bcrypt.lib ws2_32.lib
                "/Fe:${static_consumer}"
        RESULT_VARIABLE static_compile_result
        OUTPUT_VARIABLE static_compile_output
        ERROR_VARIABLE static_compile_error
    )
    set(cpp_consumer_output "${shared_dist}/lib/cpp-consumer.exe")
    execute_process(
        COMMAND "${CXX_COMPILER}" /nologo /std:c++20 /W4 /WX "${cpp_consumer}"
                "/I${shared_dist}/include" "${import_library}" bcrypt.lib ws2_32.lib
                "/Fe:${cpp_consumer_output}"
        RESULT_VARIABLE cpp_compile_result
        OUTPUT_VARIABLE cpp_compile_output
        ERROR_VARIABLE cpp_compile_error
    )
else()
    set(shared_consumer "${shared_dist}/lib/shared-consumer")
    set(shared_link -L "${shared_dist}/lib" -lsample_native)
    if(SYSTEM_NAME STREQUAL "Darwin")
        set(shared_rpath "-Wl,-rpath,@loader_path")
        set(platform_libraries "-pthread")
    else()
        set(shared_rpath "-Wl,-rpath,$ORIGIN")
        set(platform_libraries "-pthread;-ldl;-lm")
    endif()
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra -Wpedantic -Werror
                "${consumer}" -I "${shared_dist}/include" ${shared_link}
                "${shared_rpath}" ${platform_libraries} -o "${shared_consumer}"
        RESULT_VARIABLE shared_compile_result
        OUTPUT_VARIABLE shared_compile_output
        ERROR_VARIABLE shared_compile_error
    )
    set(static_consumer "${static_dist}/lib/static-consumer")
    execute_process(
        COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra -Wpedantic -Werror
                -DFOUNDATION_LIBRARY_STATIC=1 "${consumer}"
                -I "${static_dist}/include" "${static}" ${platform_libraries}
                -o "${static_consumer}"
        RESULT_VARIABLE static_compile_result
        OUTPUT_VARIABLE static_compile_output
        ERROR_VARIABLE static_compile_error
    )
    set(cpp_consumer_output "${shared_dist}/lib/cpp-consumer")
    execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Wpedantic -Werror
                "${cpp_consumer}" -I "${shared_dist}/include" ${shared_link}
                "${shared_rpath}" ${platform_libraries} -o "${cpp_consumer_output}"
        RESULT_VARIABLE cpp_compile_result
        OUTPUT_VARIABLE cpp_compile_output
        ERROR_VARIABLE cpp_compile_error
    )
endif()
if(NOT shared_compile_result EQUAL 0)
    message(FATAL_ERROR "cannot compile shared-library consumer:\n${shared_compile_output}${shared_compile_error}")
endif()
if(NOT static_compile_result EQUAL 0)
    message(FATAL_ERROR "cannot compile static-library consumer:\n${static_compile_output}${static_compile_error}")
endif()
if(NOT cpp_compile_result EQUAL 0)
    message(FATAL_ERROR "cannot compile C++ shared-library consumer:\n${cpp_compile_output}${cpp_compile_error}")
endif()
execute_process(COMMAND "${shared_consumer}" RESULT_VARIABLE shared_run_result)
execute_process(COMMAND "${static_consumer}" RESULT_VARIABLE static_run_result)
execute_process(COMMAND "${cpp_consumer_output}" RESULT_VARIABLE cpp_run_result)
if(NOT shared_run_result EQUAL 0 OR NOT static_run_result EQUAL 0 OR
   NOT cpp_run_result EQUAL 0)
    message(FATAL_ERROR "native library consumer failed: shared=${shared_run_result}, static=${static_run_result}, cpp=${cpp_run_result}")
endif()

if(SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND "${READELF}" -d "${shared}"
        RESULT_VARIABLE readelf_result
        OUTPUT_VARIABLE dynamic
    )
    if(NOT readelf_result EQUAL 0 OR
       NOT dynamic MATCHES "SONAME.*libsample_native.so.2" OR
       NOT dynamic MATCHES "NEEDED.*libm\\.so")
        message(FATAL_ERROR "shared library dynamic contract is invalid:\n${dynamic}")
    endif()
    execute_process(
        COMMAND "${NM}" -D --defined-only "${shared}"
        RESULT_VARIABLE nm_result
        OUTPUT_VARIABLE symbols
    )
    if(NOT nm_result EQUAL 0 OR
       NOT symbols MATCHES "sample_increment" OR
       NOT symbols MATCHES "sample_apply" OR
       NOT symbols MATCHES "sample_callback" OR
       NOT symbols MATCHES "sample_invoke" OR
       NOT symbols MATCHES "sample_round_trip" OR
       NOT symbols MATCHES "sample_sine" OR
       NOT symbols MATCHES "sample_shift" OR
       NOT symbols MATCHES "fdn_string_drop" OR
       symbols MATCHES "fdn_fn_" OR
       symbols MATCHES "sample_private_identity" OR
       symbols MATCHES "sample_native_double_(start|cancel)" OR
       symbols MATCHES "[ \\t]main(@|$)")
        message(FATAL_ERROR "shared library export surface is invalid:\n${symbols}")
    endif()
endif()
