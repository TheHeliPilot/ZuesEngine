cmake_minimum_required(VERSION 3.20)
project(ZuesEngineRoot VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

# ============================================================================
# Output Directories - Everything goes to bin/ZuesEngine/
# ============================================================================
set(ZUES_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/ZuesEngine")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${ZUES_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${ZUES_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${ZUES_OUTPUT_DIR}/lib")

# For multi-config generators (Visual Studio, Xcode)
foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} "${ZUES_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} "${ZUES_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} "${ZUES_OUTPUT_DIR}/lib")
endforeach()

# ============================================================================
# Build Options
# ============================================================================
option(ZUES_BUILD_EDITOR "Build the ZuesEditor" ON)
option(ZUES_BUILD_LAUNCHER "Build the ZuesLauncher" ON)

# ============================================================================
# Platform Detection (Windows-only for now, modular for future)
# ============================================================================
if(WIN32)
    set(ZUES_PLATFORM "Windows")
    add_compile_definitions(ZUES_PLATFORM_WINDOWS)
elseif(APPLE)
    set(ZUES_PLATFORM "macOS")
    add_compile_definitions(ZUES_PLATFORM_MACOS)
    message(WARNING "macOS support is not yet implemented")
elseif(UNIX)
    set(ZUES_PLATFORM "Linux")
    add_compile_definitions(ZUES_PLATFORM_LINUX)
    message(WARNING "Linux support is not yet implemented")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

message(STATUS "Building ZuesEngine for ${ZUES_PLATFORM}")
message(STATUS "Output directory: ${ZUES_OUTPUT_DIR}")

# ============================================================================
# Build Engine (uses its own CMakeLists.txt)
# This builds ZuesEngine DLL + all dependencies (GLFW, GLAD, ENet, Box2D)
# ============================================================================
add_subdirectory(Engine)

# ============================================================================
# ImGui - UI Library (for Editor and Launcher)
# Built here because it's shared between Editor and Launcher
# ============================================================================
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/Editor/extern/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
# glfw and glad are built by Engine and available here
target_link_libraries(imgui PUBLIC glfw)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# ============================================================================
# Editor Executable
# ============================================================================
if(ZUES_BUILD_EDITOR)
    set(EDITOR_SRC
        Editor/src/main.cpp
        Editor/src/EditorUi.cpp
        Editor/src/LoggerUI.cpp
        Editor/src/HierarchyOperations.cpp
        Editor/src/HierarchyUI.cpp
        Editor/src/InspectorUI.cpp
        Editor/src/TextureCutterUI.cpp
        Editor/src/AssetBrowserUI.cpp
        Editor/src/SpriteInspector.cpp
        Editor/src/InspectorRegistry.cpp
        Editor/src/ProjectManager.cpp
        Editor/src/ViewportCameraSystem.cpp
        Editor/src/GameDLLLoader.cpp
        Editor/src/HotReloadUI.cpp
    )

    add_executable(ZuesEditor ${EDITOR_SRC})

    target_include_directories(ZuesEditor PRIVATE
        ${CMAKE_SOURCE_DIR}/Editor/include
        ${CMAKE_SOURCE_DIR}/Engine/include
        ${CMAKE_SOURCE_DIR}/Engine/include/Engine
        ${CMAKE_SOURCE_DIR}/Engine/extern/glfw/include
        ${CMAKE_SOURCE_DIR}/Engine/extern/glad/include
        ${CMAKE_SOURCE_DIR}/Engine/extern/KHR
    )

    # Link to ZuesEngine DLL, imgui, and OpenGL dependencies
    target_link_libraries(ZuesEditor PRIVATE ZuesEngine imgui glfw glad)

    if(WIN32)
        target_link_libraries(ZuesEditor PRIVATE opengl32)
    elseif(APPLE)
        find_library(OpenGL_LIBRARY OpenGL)
        target_link_libraries(ZuesEditor PRIVATE ${OpenGL_LIBRARY})
    elseif(UNIX)
        target_link_libraries(ZuesEditor PRIVATE GL X11 pthread Xrandr Xi dl)
    endif()
endif()

# ============================================================================
# Launcher Executable
# ============================================================================
if(ZUES_BUILD_LAUNCHER)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Launcher/CMakeLists.txt")
        add_subdirectory(Launcher)
    else()
        message(STATUS "Launcher directory not found, skipping")
    endif()
endif()

# ============================================================================
# Post-Build: Copy Required Files for Distribution
# ============================================================================

# Copy MinGW runtime DLLs if using MinGW/GCC compiler
if(MINGW)
    # Find MinGW bin directory
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    set(MINGW_DLLS
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )

    foreach(DLL ${MINGW_DLLS})
        if(EXISTS "${DLL}")
            add_custom_command(TARGET ZuesEditor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${ZUES_OUTPUT_DIR}/"
                COMMENT "Copying ${DLL} to output directory"
            )
        endif()
    endforeach()
endif()

# Copy Templates folder to output directory
add_custom_target(copy_templates ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Editor/Templates"
        "${ZUES_OUTPUT_DIR}/Templates"
    COMMENT "Copying Templates to output directory"
)

# Copy Engine include headers (needed for game project builds)
add_custom_target(copy_engine_headers ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Engine/include"
        "${ZUES_OUTPUT_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Engine/extern/glfw/include"
        "${ZUES_OUTPUT_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Engine/extern/glad/include"
        "${ZUES_OUTPUT_DIR}/include"
    COMMENT "Copying Engine and dependency headers to output directory"
)

# ============================================================================
# Convenience target to build everything
# ============================================================================
set(BUILD_ALL_DEPS ZuesEngine ZuesEditor copy_templates copy_engine_headers)
if(TARGET ZuesLauncher)
    list(APPEND BUILD_ALL_DEPS ZuesLauncher)
endif()

add_custom_target(BuildAll
    DEPENDS ${BUILD_ALL_DEPS}
    COMMENT "Building all ZuesEngine components"
)

message(STATUS "ZuesEngine root configuration complete")
message(STATUS "")
message(STATUS "To build:")
message(STATUS "  cmake -B build -G \"Visual Studio 17 2022\" -A x64")
message(STATUS "  cmake --build build --config Release")
message(STATUS "")
message(STATUS "Output will be in: ${ZUES_OUTPUT_DIR}")
