cmake_minimum_required(VERSION 3.15)
project(sarena)
add_executable(debug_rebal debug_rebal.c rebal.c)

target_compile_definitions(debug_rebal PRIVATE REBAL_DEBUG)
