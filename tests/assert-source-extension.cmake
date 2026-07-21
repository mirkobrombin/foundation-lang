if(NOT DEFINED ROOT)
    message(FATAL_ERROR "source extension assertion is missing ROOT")
endif()

string(CONCAT retired_extension ".fd" "n")
string(CONCAT retired_reference "\\.fd" "n([^A-Za-z0-9_]|$)")
string(CONCAT retired_pattern "${ROOT}/*" "${retired_extension}")
file(GLOB_RECURSE retired_sources LIST_DIRECTORIES false "${retired_pattern}")
list(FILTER retired_sources EXCLUDE REGEX "/build/")
list(FILTER retired_sources EXCLUDE REGEX "/\\.git/")
if(retired_sources)
    list(JOIN retired_sources "\n" retired_list)
    message(FATAL_ERROR "retired Foundation sources found:\n${retired_list}")
endif()

file(GLOB_RECURSE repository_text LIST_DIRECTORIES false
    "${ROOT}/CMakeLists.txt"
    "${ROOT}/*.c"
    "${ROOT}/*.cmake"
    "${ROOT}/*.cpp"
    "${ROOT}/*.fn"
    "${ROOT}/*.h"
    "${ROOT}/*.hpp"
    "${ROOT}/*.js"
    "${ROOT}/*.json"
    "${ROOT}/*.lock"
    "${ROOT}/*.md"
    "${ROOT}/*.package"
    "${ROOT}/*.txt"
    "${ROOT}/*.yaml"
    "${ROOT}/*.yml"
)
list(FILTER repository_text EXCLUDE REGEX "/build/")
list(FILTER repository_text EXCLUDE REGEX "/\\.git/")
foreach(path IN LISTS repository_text)
    file(READ "${path}" contents)
    string(REGEX MATCH "${retired_reference}" stale_reference "${contents}")
    if(stale_reference)
        message(FATAL_ERROR "retired Foundation extension referenced by ${path}")
    endif()
endforeach()
