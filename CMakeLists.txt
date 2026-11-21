cmake_minimum_required(VERSION 3.25)

project(sessionflow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SESSIONFLOW_BUILD_TESTS "Build tests" ON)

add_library(sessionflow_warnings INTERFACE)
if(MSVC)
    target_compile_options(sessionflow_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(sessionflow_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)

if(SESSIONFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
