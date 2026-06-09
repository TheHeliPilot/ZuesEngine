# Standalone launcher exe. Picks/creates a project, spawns editor.exe with
# --project=<path>, then exits. No engine modules — just glfw + imgui + Core
# (for the .zuesproject loader).

zues_fetch_imgui()
zues_fetch_json()

add_executable(zues_launcher
    src/main.cpp
    src/launcher_theme.cpp
    src/launcher_dialogs.cpp
    src/launcher_recents.cpp
    src/launcher_spawn.cpp
    src/launcher_project.cpp)
add_executable(zues::launcher ALIAS zues_launcher)

target_link_libraries(zues_launcher PRIVATE
    zues::core
    imgui
    glfw
    nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(zues_launcher PRIVATE comdlg32 shell32 ole32)
    # Tell the linker to bake an asInvoker manifest into the PE.
    # Without this, Windows' legacy installer-detection heuristic flags
    # the "launcher" name + missing UAC manifest and silently elevates
    # the process every launch (the "elevator popup"). lld-link
    # supports /MANIFESTUAC natively -- no mt.exe / no .rc file needed.
    target_link_options(zues_launcher PRIVATE
        "LINKER:/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()

set_target_properties(zues_launcher PROPERTIES OUTPUT_NAME "launcher")
zues_set_target_flags(zues_launcher)
