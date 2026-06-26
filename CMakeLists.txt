cmake_minimum_required(VERSION 3.15)
project(rebal)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Library target
add_library(rebal STATIC rebal.c)
target_include_directories(rebal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Debug executable — compiles rebal.c directly with REBAL_DEBUG to get dump functions
add_executable(debug_rebal debug_rebal.c rebal.c)
target_compile_definitions(debug_rebal PRIVATE REBAL_DEBUG)

# Test executable
add_executable(test_rebal test_rebal.c)
target_link_libraries(test_rebal rebal)

# Enable testing
enable_testing()
add_test(NAME rebal_tests COMMAND test_rebal)
