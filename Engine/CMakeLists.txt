cmake_minimum_required(VERSION 4.0)
project(Engine)

set(CMAKE_CXX_STANDARD 23)

add_library(Engine STATIC
        include/Engine/Core.h
        include/Engine/Engine.h
        include/Engine/Network.h
        include/Engine/Renderer.h
        include/Engine/Types.h
        src/Core.cpp
        src/Engine.cpp
        src/Network.cpp
        src/Renderer.cpp)
