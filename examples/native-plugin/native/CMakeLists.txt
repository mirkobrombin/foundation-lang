cmake_minimum_required(VERSION 3.20)
project(foundation_native_plugin_example C)

add_library(greeter_native SHARED plugin.c)
target_include_directories(greeter_native PRIVATE ../../../runtime/include)
set_target_properties(greeter_native PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
