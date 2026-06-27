cmake_minimum_required(VERSION 3.25)

project(foundation_process_plugin_example LANGUAGES C)

add_executable(greeter_process main.c)
set_property(TARGET greeter_process PROPERTY C_STANDARD 11)
set_property(TARGET greeter_process PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET greeter_process PROPERTY C_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(greeter_process PRIVATE /W4 /WX)
else()
    target_compile_options(greeter_process PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
