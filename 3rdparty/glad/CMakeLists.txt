cmake_minimum_required(VERSION 3.31)
project(glad)

add_library(glad STATIC)
target_sources(
    glad
    PUBLIC
    include/glad/glad.h
    include/KHR/khrplatform.h
    PRIVATE
    src/glad.c)
target_include_directories(glad PUBLIC include/)
