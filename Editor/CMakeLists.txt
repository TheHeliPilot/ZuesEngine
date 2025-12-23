cmake_minimum_required(VERSION 3.29.6)
project(Editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add Engine as a subdirectory FIRST so it builds glfw/glad before we reference them
add_subdirectory(${CMAKE_SOURCE_DIR}/../Engine ${CMAKE_BINARY_DIR}/EngineBuild)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/imgui)
set(IMGUI_SRC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

set(EDITOR_SRC
        src/main.cpp
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
        src/SpriteInspector.cpp
        include/CustomInspectors/SpriteInspector.h
        src/InspectorRegistry.cpp
        include/CustomInspectors/InspectorRegistry.h
        # Custom component inspectors
        src/TransformInspector.cpp
        include/CustomInspectors/TransformInspector.h
        src/CameraInspector.cpp
        include/CustomInspectors/CameraInspector.h
        src/RigidbodyInspector.cpp
        include/CustomInspectors/RigidbodyInspector.h
        src/ColliderInspector.cpp
        include/CustomInspectors/ColliderInspector.h
        src/TextInspector.cpp
        include/CustomInspectors/TextInspector.h
        # Systems management UI
        src/SystemsUI.cpp
        include/SystemsUI.h
        # Collider gizmo for visual editing
        src/ColliderGizmo.cpp
        include/ColliderGizmo.h
        # Moved from Engine
        src/ProjectManager.cpp
        include/ProjectManager.h
        src/ViewportCameraSystem.cpp
        include/ViewportCameraSystem.h
        # Hot-reload system
        src/GameDLLLoader.cpp
        include/GameDLLLoader.h
        src/HotReloadUI.cpp
        include/HotReloadUI.h
)

add_executable(Editor
        ${IMGUI_SRC}
        ${EDITOR_SRC}
)

target_include_directories(Editor PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/../Engine/include
        ${CMAKE_SOURCE_DIR}/../Engine/include/Engine
        ${CMAKE_SOURCE_DIR}/extern/imgui
        ${CMAKE_SOURCE_DIR}/extern/imgui/backends
        # Use Engine's extern directories for glfw/glad includes
        ${CMAKE_SOURCE_DIR}/../Engine/extern/glfw/include
        ${CMAKE_SOURCE_DIR}/../Engine/extern/glad/include
        ${CMAKE_SOURCE_DIR}/../Engine/extern/KHR
)

# Link to ZuesEngine DLL - this also brings in glfw and glad from Engine
target_link_libraries(Editor PRIVATE ZuesEngine)

# Link glfw and glad from Engine's build (they're already built by Engine)
target_link_libraries(Editor PRIVATE glfw glad)

# Link platform-specific libraries
if (APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    target_link_libraries(Editor PRIVATE ${OpenGL_LIBRARY})
elseif (UNIX)
    target_link_libraries(Editor PRIVATE GL X11 pthread Xrandr Xi dl)
elseif (WIN32)
    target_link_libraries(Editor PRIVATE opengl32)
endif()

# Set output directory
set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy ZuesEngine DLL to Editor output directory on Windows
if(WIN32)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ZuesEngine>
            $<TARGET_FILE_DIR:Editor>
        COMMENT "Copying ZuesEngine.dll to Editor directory"
    )
endif()
