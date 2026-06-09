# Lync compiler plugin for Zues.
#
# Built as a shared library that the lync compiler dlopens at compile time
# (via --plugin=path/to/zues_lync_plugin.dll). The plugin reads attributes
# on top-level decls (struct + def) and emits the Zues project boilerplate
# (ZuesFieldInfo arrays + register_component calls + zues_project_entry).
#
# Note: this plugin links nothing — it just emits C text. Plugins run
# inside the lync.exe process; they don't link to the engine or the
# project_api code path.

add_library(zues_lync_plugin SHARED
    zues_lync_plugin.c)
add_library(zues::lync_plugin ALIAS zues_lync_plugin)

# The plugin includes lync_plugin.h from the LyncLang source tree. We don't
# build LyncLang here; the user must ensure the path resolves. For the
# standard "C:/LyncLang next to C:/ZuesEngine" layout, the relative path
# in the source file works.
target_include_directories(zues_lync_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../LyncLang/src")

# Output the .dll/.so right where users will reference it from --plugin=...
set_target_properties(zues_lync_plugin PROPERTIES
    OUTPUT_NAME "zues_lync_plugin"
    PREFIX ""                                         # no "lib" prefix on POSIX
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
