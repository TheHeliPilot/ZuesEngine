cmake_minimum_required(VERSION 4.0)
project(Editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
set(IMGUI_SRC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        src/EditorUi.cpp
        include/EditorUi.h
        src/LoggerUI.cpp
        include/LoggerUI.h
        src/HierarchyOperations.cpp
        include/HierarchyOperations.h
        include/EditorDefines.h
        src/HierarchyUI.cpp
        include/HierarchyUI.h
        include/InspectorUI.h
        src/InspectorUI.cpp
        src/TextureCutterUI.cpp
        include/TextureCutterUI.h
        src/AssetBrowserUI.cpp
        include/AssetBrowserUI.h
)

add_executable(Editor
        ${IMGUI_SRC}
        ${CMAKE_SOURCE_DIR}/extern/glfw/src
        ${CMAKE_SOURCE_DIR}/extern/glad/src
        src/main.cpp
)

target_include_directories(Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/../Engine/include/Engine
        ${CMAKE_SOURCE_DIR}/extern/imgui
        ${CMAKE_SOURCE_DIR}/extern/imgui/backends
        ${CMAKE_SOURCE_DIR}/extern/glfw/include
        ${CMAKE_SOURCE_DIR}/extern/glad/include
        ${CMAKE_SOURCE_DIR}/extern/KHR
)

# Add Engine as a subdirectory so CMake builds Engine first
add_subdirectory(${CMAKE_SOURCE_DIR}/../Engine ${CMAKE_BINARY_DIR}/EngineBuild)

# Link to Engine
target_link_libraries(Editor PRIVATE Engine)

# Link platform-specific libraries
if (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Editor PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(Editor PRIVATE GL X11 pthread Xrandr Xi dl)
elseif (WIN32)
    target_link_libraries(Editor PRIVATE opengl32)
endif()
