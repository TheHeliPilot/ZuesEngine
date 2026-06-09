cmake_minimum_required(VERSION 3.28)
project(ZuesEngine VERSION 0.1.0 LANGUAGES CXX C)

# ---- Global policy ----------------------------------------------------------
# Single dynamic CRT everywhere — see docs/04-dll-safety.md for why.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable C++20 module scanning — we don't use modules, and it forces
# clang-scan-deps which isn't on every clang distribution.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# All DLLs / EXEs / import libs land in one folder so the editor can LoadLibrary them
# from its own directory at runtime.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Options ----------------------------------------------------------------
option(ZUES_BUILD_EDITOR       "Build the editor app"                                 ON)
option(ZUES_BUILD_SAMPLE_GAME  "Build the MyGame sample project"                      ON)
option(ZUES_BUILD_TESTS        "Build the engine regression tests"                    ON)
option(ZUES_USE_REFLECTION     "Enable C++26 P2996 reflection paths (clang-p2996)"    OFF)

# ---- Shared helpers ---------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(ZuesCompilerFlags)
include(ZuesModule)
include(ZuesDependencies)

# ---- Subprojects ------------------------------------------------------------
add_subdirectory(ProjectAPI)
add_subdirectory(Core)
add_subdirectory(Modules)
# HostShared sits between Core and the runtime/editor exes -- it owns the
# ZuesHostApi vtable + project DLL loader + default sprite render system,
# things that BOTH the editor and the standalone runtime need to expose
# to the project DLL identically.
add_subdirectory(HostShared)
add_subdirectory(Runtime)

if(ZUES_BUILD_EDITOR)
    add_subdirectory(Editor)
    add_subdirectory(Launcher)
endif()

if(ZUES_BUILD_SAMPLE_GAME)
    add_subdirectory(MyGame)
endif()

if(ZUES_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# Lync compiler plugin — only built if the LyncLang sibling tree is present.
# Plugin is pure C, links nothing engine-side; it just emits C text inside
# the lync compiler at project-DLL build time.
if(EXISTS "${CMAKE_SOURCE_DIR}/../LyncLang/src/lync_plugin.h")
    add_subdirectory(LyncPlugin)
endif()

# ---- Deploy / package -------------------------------------------------------
# `cmake --build <build> --target zues_deploy` gathers everything a friend
# needs to run the engine into <build>/dist/. Layout:
#
#   dist/
#     launcher.exe        editor.exe       *.dll
#     assets/             (icons, fonts)
#     lync/               lync.exe          (if LYNC_EXE_PATH is set)
#                         zues_lync_plugin.dll
#                         zues_api.lync     (prelude)
#                         include/          (project_api.h + zues headers)
#     templates/
#       starter/          a fresh .zuesproject + empty src/ to copy on New Project
#
# Set LYNC_EXE_PATH at configure time to bundle lync.exe:
#   cmake -DLYNC_EXE_PATH=C:/LyncLang/build_win/lync.exe ...
set(LYNC_EXE_PATH "" CACHE FILEPATH "Path to lync.exe (bundled into dist/lync/)")

set(ZUES_DIST_DIR ${CMAKE_BINARY_DIR}/dist)

# Whitelist of binaries we actually ship. Anything else in bin/ (PDBs,
# import libs, .broken-* backups, editor state files like
# recent_worlds.json) is intentionally NOT bundled -- a friend bundle
# should be the minimum to run the editor + build a project, not the
# full debug working tree.
set(ZUES_DEPLOY_BINS
    editor.exe
    launcher.exe
    zues_runtime.exe
    zues_core.dll
    zues_renderer_gl.dll
    zues_window_glfw.dll
    zues_physics_box2d.dll
    glfw3.dll)

# Build the per-binary copy commands as a list so we can splat them into
# the custom_target below.
set(_zues_bin_copy_cmds "")
foreach(bin IN LISTS ZUES_DEPLOY_BINS)
    list(APPEND _zues_bin_copy_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${bin}
            ${ZUES_DIST_DIR}/${bin})
endforeach()

add_custom_target(zues_deploy
    COMMENT "Packaging Zues into ${ZUES_DIST_DIR}"
    # Wipe + recreate dist (idempotent). Use rm -rf so a stale zip from a
    # previous run doesn't get re-zipped into itself.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${ZUES_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}/lync
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}/lync/include
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}/templates/starter/src
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}/templates/starter/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZUES_DIST_DIR}/templates/starter/worlds
    # Engine binaries: only the whitelisted ones above. Skips PDBs / .lib /
    # .broken-* backups / editor state files.
    ${_zues_bin_copy_cmds}
    # Editor assets (icons + the default font ui_render_system loads).
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets ${ZUES_DIST_DIR}/assets
    # In-engine docs.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Editor/src/docs ${ZUES_DIST_DIR}/docs
    # Lync toolchain: prelude + ProjectAPI headers + plugin DLL.
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/LyncPlugin/zues_api.lync ${ZUES_DIST_DIR}/lync/zues_api.lync
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/ProjectAPI/include ${ZUES_DIST_DIR}/lync/include
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/zues_lync_plugin.dll
        ${ZUES_DIST_DIR}/lync/zues_lync_plugin.dll
    DEPENDS zues_editor zues_launcher zues_runtime zues_lync_plugin)

# Bundle the Lync stdlib next to lync.exe in the dist if the LyncLang
# tree is present. lync.exe falls back to `<exe_dir>/stdlib/` when no
# `--stdlib=` flag is set, so this layout makes `using std.math.*;`
# work out-of-the-box on a fresh install.
if(EXISTS "${CMAKE_SOURCE_DIR}/../LyncLang/stdlib")
    add_custom_command(TARGET zues_deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/../LyncLang/stdlib
            ${ZUES_DIST_DIR}/lync/stdlib
        COMMENT "Bundling Lync stdlib -> dist/lync/stdlib/")
endif()

# Optional: zip the dist into a single sendable file. Run with:
#   cmake --build <build> --target zues_zip
# Produces <build>/zues-<version>.zip alongside dist/. Uses cmake -E tar
# so it works on every platform without extra tools.
set(ZUES_ZIP_FILE ${CMAKE_BINARY_DIR}/zues-${PROJECT_VERSION}.zip)
add_custom_target(zues_zip
    COMMENT "Zipping ${ZUES_DIST_DIR} -> ${ZUES_ZIP_FILE}"
    COMMAND ${CMAKE_COMMAND} -E rm -f ${ZUES_ZIP_FILE}
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}
        ${CMAKE_COMMAND} -E tar cf zues-${PROJECT_VERSION}.zip --format=zip
        dist
    DEPENDS zues_deploy)

# Bundle lync.exe + a starter project template if the user pointed us at one.
if(LYNC_EXE_PATH AND EXISTS "${LYNC_EXE_PATH}")
    add_custom_command(TARGET zues_deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${LYNC_EXE_PATH} ${ZUES_DIST_DIR}/lync/lync.exe
        COMMENT "Bundling ${LYNC_EXE_PATH} -> dist/lync/lync.exe")
endif()

# Drop a minimal .zuesproject + starter src/ that uses the bundled defaults
# (empty lync_compiler/plugin/include/prelude -> editor falls back to the
# files in dist/lync/). Friend can copy templates/starter/ anywhere and
# Open Project on it.
file(WRITE ${CMAKE_BINARY_DIR}/_starter.zuesproject
[=[{
  "name": "MyGame",
  "engine_version": "0.1.0",
  "default_world": "worlds/main.zworld",
  "build": {
    "dll_path": "build/mygame.dll",
    "lync_main": ""
  }
}
]=])
add_custom_command(TARGET zues_deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/_starter.zuesproject
        ${ZUES_DIST_DIR}/templates/starter/MyGame.zuesproject)

# ---- Config summary ---------------------------------------------------------
message(STATUS "")
message(STATUS "==== Zues Engine ${PROJECT_VERSION} ====")
message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Editor        : ${ZUES_BUILD_EDITOR}")
message(STATUS "  Sample game   : ${ZUES_BUILD_SAMPLE_GAME}")
message(STATUS "  Reflection    : ${ZUES_USE_REFLECTION}")
message(STATUS "  Output dir    : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
