cmake_minimum_required(VERSION 3.29.6)
project(ZuesEngine VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build as SHARED library (DLL on Windows, .so on Linux, .dylib on macOS)
add_library(ZuesEngine SHARED
        include/Engine/ZuesAPI.h
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
        include/Engine/ECS/Systems/TextRenderingSystem.h
        include/Engine/Math.h
        include/Engine/SceneSetup.h
        include/Engine/ECS/Systems/CameraSystem.h
        include/Engine/ECS/Systems/Systems.h
        src/Input.cpp
        include/Engine/Input.h
        include/Engine/ECS/WorldSerializationHelpers.h
        include/json/json.hpp
        include/json/json_fwd.hpp
        src/HitTest.cpp
        include/Engine/HitTest.h
        include/Engine/ECS/Systems/HierarchySystem.h
        src/CameraSystem.cpp
        src/HierarchySystem.cpp
        src/RenderingSystem.cpp
        src/TextRenderingSystem.cpp
        include/Engine/ECS/HierarchyOutliner.h
        src/HierarchyOutliner.cpp
        src/TestObjectMoverSystem.cpp
        include/Engine/ECS/Systems/TestObjectMoverSystem.h
        src/TextureManager.cpp
        include/Engine/TextureManager.h
        src/PhysicsSystem.cpp
        include/Engine/ECS/Systems/PhysicsSystem.h
        include/Engine/GameDLLInterface.h
)

# Define ZUES_ENGINE_EXPORTS when building the DLL
target_compile_definitions(ZuesEngine PRIVATE ZUES_ENGINE_EXPORTS)

# Public include directories - consumers of the library get these
target_include_directories(ZuesEngine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Engine
)

# Private include directories - only for building the library
target_include_directories(ZuesEngine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/glad/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/KHR
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/enet/include
)

# Build ENet
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/enet)

# Build Box2D
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/box2d)

# Build GLFW as part of the engine build
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/glfw)

# Build GLAD
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/glad)

# Link dependencies - PUBLIC so consumers also link these
target_link_libraries(ZuesEngine PUBLIC enet::enet_static)
target_link_libraries(ZuesEngine PUBLIC box2d)
# GLFW and GLAD must be PUBLIC so Editor shares the same instance
# This ensures the OpenGL context created by Editor is visible to Engine
target_link_libraries(ZuesEngine PUBLIC glfw)
target_link_libraries(ZuesEngine PUBLIC glad)

# Platform-specific libraries
if (WIN32)
    target_link_libraries(ZuesEngine PRIVATE opengl32 ws2_32 winmm)
elseif (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(ZuesEngine PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(ZuesEngine PRIVATE GL X11 pthread Xrandr Xi dl)
endif()

# Set output directories for the DLL
set_target_properties(ZuesEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

# On Windows, copy the DLL to a shared bin directory for easy access
if(WIN32)
    add_custom_command(TARGET ZuesEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ZuesEngine>
            "${CMAKE_SOURCE_DIR}/../bin/"
        COMMENT "Copying ZuesEngine.dll to shared bin directory"
    )
endif()
