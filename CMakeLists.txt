cmake_minimum_required(VERSION 3.31.0 FATAL_ERROR)
project(
    compute-test
    VERSION 0.0.1
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(compiler_settings)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(glm REQUIRED)

add_subdirectory(src)

message(STATUS "Compiler is ${CMAKE_CXX_COMPILER_ID}")
