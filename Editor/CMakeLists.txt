zues_fetch_imgui()
# stb_image (header-only) for the sprite cutter's alpha-bounds detection
# (it reads pixels straight off disk to find connected non-transparent
# regions). Already fetched by Renderer_GL; expose its source dir to
# the editor too.
zues_fetch_stb()
# nlohmann_json is already fetched by Core - just link the target below.
# TextEditor (ImGuiColorTextEdit by BalazsJako, MIT) is vendored directly into
# src/external/ so we can apply small Zues-side patches (e.g. exposing
# GetCharScreenPos for ghost-text overlays). The upstream repo is fine; we
# just want diff freedom.

add_executable(zues_editor
    src/main.cpp
    src/imgui_layer.cpp
    src/theme.cpp
    src/log_sink.cpp
    src/panel_dockspace.cpp
    src/panel_hierarchy.cpp
    src/panel_inspector.cpp
    src/panel_console.cpp
    src/panel_scene.cpp
    src/editor_systems.cpp
    src/gizmos.cpp
    src/panel_systems.cpp
    src/panel_game.cpp
    src/assets.cpp
    src/native_dialog.cpp
    src/world_io.cpp
    src/panel_assets.cpp
    src/panel_lync_editor.cpp
    src/panel_source.cpp
    src/panel_docs.cpp
    src/panel_todos.cpp
    src/panel_project_settings.cpp
    src/panel_search.cpp
    src/undo.cpp
    src/prefab.cpp
    src/build_export.cpp
    src/panel_sprite_cutter.cpp
    src/panel_animation_editor.cpp
    src/panel_hotkeys.cpp
    src/panel_audio.cpp
    src/panel_cue_editor.cpp
    src/debug_draw_service.cpp
    src/external/TextEditor.cpp)
add_executable(zues::editor ALIAS zues_editor)

target_include_directories(zues_editor PRIVATE src/external ${stb_SOURCE_DIR})

# host_api / project_loader / sprite_render_system / render_camera_service
# moved into HostShared (shared with zues_runtime). The editor adds its UI
# overlay (panels, undo, prefab save wrappers) on top.
target_link_libraries(zues_editor PRIVATE
    zues::core zues::host_shared zues::project_api imgui
    nlohmann_json::nlohmann_json
    comdlg32)

# Asset directory baked at build time. Lets the editor find icons + fonts
# from C:/ZuesEngine/assets/... without an env var. Production packaging
# will override this to a relative path next to the executable.
target_compile_definitions(zues_editor PRIVATE
    ZUES_ASSETS_DIR_DEFAULT="${CMAKE_SOURCE_DIR}/assets")

set_target_properties(zues_editor PROPERTIES
    OUTPUT_NAME "editor")

zues_set_target_flags(zues_editor)
