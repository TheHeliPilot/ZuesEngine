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
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp        # <-- add ImGui GLFW backend
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp     # <-- add ImGui OpenGL3 backend
)

# Add your executable
add_executable(Editor
        ${IMGUI_SRC}
        ${CMAKE_SOURCE_DIR}/extern/glfw/src
        ${CMAKE_SOURCE_DIR}/extern/glad/src
        src/main.cpp
)

# Path to Engine headers
target_include_directories(Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/../Engine/include/Engine
        ${CMAKE_SOURCE_DIR}/extern/imgui
        ${CMAKE_SOURCE_DIR}/extern/imgui/backends          # <-- add backends headers
        ${CMAKE_SOURCE_DIR}/extern/glfw/include
        ${CMAKE_SOURCE_DIR}/extern/glad/include
        ${CMAKE_SOURCE_DIR}/extern/KHR

)

# Path to Engine static library
target_link_libraries(Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/../Engine/cmake-build-debug/libEngine.a
        ${CMAKE_SOURCE_DIR}/extern/glfw/cmake-build-debug/src/libglfw3.a
        ${CMAKE_SOURCE_DIR}/extern/glad/cmake-build-debug/libglad.a
)

if (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Editor PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(Editor PRIVATE GL X11 pthread Xrandr Xi dl)
elseif (WIN32)
    target_link_libraries(Editor PRIVATE opengl32)
endif()
