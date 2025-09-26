cmake_minimum_required(VERSION 4.0)
project(Editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add your executable
add_executable(Editor
        main.cpp
)

# Path to Engine headers
target_include_directories(Editor PRIVATE
        ../Engine/include/Engine/
)

# Path to Engine static library
target_link_libraries(Editor PRIVATE
        ../Engine/cmake-build-debug/libEngine.a
)
