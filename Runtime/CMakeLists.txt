# zues_runtime — the standalone game runner. The editor's "Build → Export"
# menu copies this exe into <project>/dist/<name>/<name>.exe alongside the
# engine module DLLs + the project DLL + assets/ . Tiny: ~250 lines of
# main loop. All the host-side work (HostApi vtable, project DLL load,
# sprite render, render-camera service) lives in HostShared.

add_executable(zues_runtime
    src/main.cpp)
add_executable(zues::runtime ALIAS zues_runtime)

target_link_libraries(zues_runtime PRIVATE
    zues::core zues::host_shared zues::project_api)

set_target_properties(zues_runtime PROPERTIES
    OUTPUT_NAME "zues_runtime")

zues_set_target_flags(zues_runtime)

# Debug vs Release split:
#   Debug   - keeps the console subsystem so `printf` + log lines stream to
#             a terminal when launched from one (and a console pops up when
#             double-clicked). Useful while iterating on a project.
#   Release - /SUBSYSTEM:WINDOWS so no terminal ever appears. /ENTRY keeps
#             the existing `int main(int,char**)` valid (no need for
#             WinMain). Logs are stripped to no-ops via ZUES_STRIP_LOGS so
#             stray log_write calls don't even hit the DLL.
if(WIN32)
    # Use the LINKER: prefix so clang++ (which drives lld-link via -fuse-ld)
    # forwards these as raw linker args. Plain `/SUBSYSTEM:WINDOWS` looks
    # like a path to clang and breaks the build.
    target_link_options(zues_runtime PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:LINKER:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:LINKER:/ENTRY:mainCRTStartup>)
endif()
target_compile_definitions(zues_runtime PRIVATE
    $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:ZUES_STRIP_LOGS>)
