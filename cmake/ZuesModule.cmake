# zues_add_module(name
#     SOURCES src1.cpp src2.cpp ...
#     HEADERS h1.h h2.h ...
#     LINKS   lib1 lib2 ...
#     DEFINES FOO=1 BAR=2)
#
# Declares a shared-library module with Zues conventions:
#  - Hidden visibility, no lib prefix.
#  - Links against Engine::core (unless it IS zues_core).
#  - Defines ZUES_BUILDING_MODULE and ZUES_MODULE_NAME.
#  - Applies standard compile flags.
function(zues_add_module name)
    set(options "")
    set(one_value_args "")
    set(multi_value_args SOURCES HEADERS LINKS DEFINES)
    cmake_parse_arguments(ZM "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_library(${name} SHARED ${ZM_SOURCES} ${ZM_HEADERS})
    add_library(zues::${name} ALIAS ${name})

    set_target_properties(${name} PROPERTIES
        CXX_VISIBILITY_PRESET      hidden
        VISIBILITY_INLINES_HIDDEN  ON
        POSITION_INDEPENDENT_CODE  ON
        PREFIX                     ""
        OUTPUT_NAME                "${name}")

    # Every module links Core (unless it IS Core).
    if(NOT "${name}" STREQUAL "zues_core")
        target_link_libraries(${name} PUBLIC zues::core)
    endif()

    if(ZM_LINKS)
        target_link_libraries(${name} PRIVATE ${ZM_LINKS})
    endif()

    target_compile_definitions(${name} PRIVATE
        ZUES_BUILDING_MODULE=1
        ZUES_MODULE_NAME="${name}"
        ${ZM_DEFINES})

    zues_set_target_flags(${name})
endfunction()
