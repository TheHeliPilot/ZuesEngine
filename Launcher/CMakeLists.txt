cmake_minimum_required(VERSION 3.20)

# Only set project if building standalone (not as subdirectory)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Launcher LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    message(FATAL_ERROR "Launcher must be built from the root CMakeLists.txt. Run cmake from the ZuesEngine root directory.")
endif()

# ============================================================================
# Launcher Executable
# ============================================================================
add_executable(ZuesLauncher WIN32
    src/main.cpp
    src/Prerequisites.cpp
    src/ProjectManager.cpp
    src/LauncherUI.cpp
)

# ============================================================================
# Include Directories
# ============================================================================
target_include_directories(ZuesLauncher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# Link Dependencies
# ============================================================================
target_link_libraries(ZuesLauncher PRIVATE
    imgui
    glfw
    glad
)

# ============================================================================
# Platform-specific Libraries
# ============================================================================
if(WIN32)
    target_link_libraries(ZuesLauncher PRIVATE opengl32 shell32 ole32)
elseif(APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(ZuesLauncher PRIVATE ${OpenGL_LIBRARY})
elseif(UNIX)
    target_link_libraries(ZuesLauncher PRIVATE GL X11 pthread Xrandr Xi dl)
endif()

# ============================================================================
# Compile Definitions
# ============================================================================
target_compile_definitions(ZuesLauncher PRIVATE
    $<$<CONFIG:Debug>:ZUES_DEBUG>
    $<$<CONFIG:Release>:ZUES_RELEASE>
)

# ============================================================================
# Set Output Name
# ============================================================================
set_target_properties(ZuesLauncher PROPERTIES
    OUTPUT_NAME "ZuesLauncher"
)
