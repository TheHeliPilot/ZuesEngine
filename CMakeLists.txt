cmake_minimum_required(VERSION 4.0)
project(ZuesEngine)

set(CMAKE_CXX_STANDARD 23)

add_library(ZuesEngine STATIC
        src/Engine.cpp
        include/Engine/Engine.h
        src/Core.cpp
        include/Engine/Core.h
        src/Renderer.cpp
        include/Engine/Renderer.h
        src/Network.cpp
        include/Engine/Network.h
        include/Engine/Types.h)
