cmake_minimum_required(VERSION 3.29.6)
project(glad)

set(CMAKE_CXX_STANDARD 23)

add_library(glad STATIC
        src/glad.c
        include/glad/glad.h
        include/KHR/khrplatform.h)
