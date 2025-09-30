# Project-specific CMakeLists.txt for MyGameProject
cmake_minimum_required(VERSION 3.10)
project(MyGameProject VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Specify the project's main source file
set(PROJECT_SOURCES MyGameProject.cpp
        test.cpp
        test.h
)

# Create the executable
add_executable(${PROJECT_NAME} WIN32 ${PROJECT_SOURCES})

# Engine headers are provided via the ENGINE_INCLUDE_DIR CMake argument.
if(NOT ENGINE_INCLUDE_DIR)
    message(FATAL_ERROR "ENGINE_INCLUDE_DIR is not defined. The game project cannot find Engine headers.")
endif()
if(NOT GLFW_INCLUDE_DIR OR NOT GLAD_INCLUDE_DIR)
    message(FATAL_ERROR "External library include paths (GLFW/GLAD) are missing. Check ProjectManager::BuildProject for configuration errors.")
endif()
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${ENGINE_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${GLAD_INCLUDE_DIR}
)

# Absolute library paths provided via the ENGINE_LIBS_PATHS CMake argument.
if(NOT ENGINE_LIBS_PATHS)
    message(FATAL_ERROR "ENGINE_LIBS_PATHS is not defined. The game project cannot find Engine library files.")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE
    "${ENGINE_LIBS_PATHS}"
    opengl32
    Ws2_32
    Gdi32
    winmm
)

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Builds")
