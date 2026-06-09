# HostShared — runtime-side glue that the project DLL talks to.
#
# Both editor.exe and runtime.exe link this. It contains:
#   - The ZuesHostApi vtable + thunks (host_api.cpp)
#   - ProjectDllLoader (shadow-copy + LoadLibrary the project DLL)
#   - Default sprite render system (Phase::Render)
#   - IRenderCamera_v1 service implementation
#   - HostContext (small struct of "what the host knows")
#   - Runtime prefab instantiate (no editor undo / selection)
#
# Editor adds its UI overlay on top: panels, undo stacks, prefab save
# wrappers, toast notifications.

zues_fetch_miniaudio()

add_library(zues_host_shared STATIC
    src/host_api.cpp
    src/host_context.cpp
    src/prefab_runtime.cpp
    src/project_loader.cpp
    src/render_camera_service.cpp
    src/sprite_render_system.cpp
    src/ui_render_system.cpp
    src/animator_system.cpp
    src/particle_system.cpp
    src/audio_system.cpp
    src/task_runner.cpp)
add_library(zues::host_shared ALIAS zues_host_shared)

target_include_directories(zues_host_shared PUBLIC include)

target_link_libraries(zues_host_shared PUBLIC
    zues::core
    zues::project_api
    miniaudio
    nlohmann_json::nlohmann_json)

zues_set_target_flags(zues_host_shared)
