add_library(zues_project_api INTERFACE)
add_library(zues::project_api ALIAS zues_project_api)

target_include_directories(zues_project_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
