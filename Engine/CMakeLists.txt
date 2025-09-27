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
        src/Renderer.cpp
        src/Log.cpp
        include/Engine/Log.h
        src/EventSystem.cpp
        include/Engine/EventSystem/EventSystem.h
        include/Engine/EngineDefines.h
        include/Engine/EventSystem/Events.h)

target_include_directories(Engine PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/glfw/include
        ${CMAKE_SOURCE_DIR}/extern/glad/include
        ${CMAKE_SOURCE_DIR}/extern/KHR
)

target_link_libraries(Engine PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/glfw/cmake-build-debug/src/libglfw3.a
        ${CMAKE_SOURCE_DIR}/extern/glad/cmake-build-debug/libglad.a
)

if (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Engine PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(Engine PRIVATE GL X11 pthread Xrandr Xi dl)
elseif (WIN32)
    target_link_libraries(Engine PRIVATE opengl32)
endif()
