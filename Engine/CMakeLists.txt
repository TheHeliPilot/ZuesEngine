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
        include/Engine/EventSystem/Events.h
        include/Engine/ECS/Component.h
        include/Engine/ECS/Entity.h
        include/Engine/ECS/System.h
        include/Engine/ECS/ECSConfig.h
        src/World.cpp
        include/Engine/ECS/World.h
        include/Engine/ECS/World.inl
        include/Engine/ECS/Components.h
        include/Engine/ECS/Systems/RenderingSystem.h
        include/Engine/Math.h
        include/Engine/SceneSetup.h
        include/Engine/SceneSetup.h
        include/Engine/ECS/Systems/CameraSystem.h
        include/Engine/ECS/Systems/Systems.h
        src/Input.cpp
        include/Engine/Input.h
        src/ProjectManager.cpp
        include/Engine/ProjectManager.h
        include/Engine/ECS/Systems/ViewportCameraSystem.h
        include/Engine/Serialization.h
)

target_include_directories(Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glad/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/KHR
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/enet/include
)

# Build ENet
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/enet)

# Link to ENet using its alias target
target_link_libraries(Engine PUBLIC enet::enet_static)

# Link other libraries
target_link_libraries(Engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/cmake-build-debug/src/libglfw3.a
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glad/cmake-build-debug/libglad.a
)

if (WIN32)
    target_link_libraries(Engine PRIVATE opengl32 ws2_32 winmm)
elseif (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Engine PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(Engine PRIVATE GL X11 pthread Xrandr Xi dl)
endif()
