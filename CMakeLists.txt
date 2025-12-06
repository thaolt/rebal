cmake_minimum_required(VERSION 3.15)
project(sarena)
add_executable(debug_sarena debug_sarena.c sarena.c)

target_compile_definitions(debug_sarena PRIVATE SARENA_DEBUG)
