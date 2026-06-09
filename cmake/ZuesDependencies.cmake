# Dependency fetchers. Called lazily from the module that needs each dep.
# Modules that don't need external libs add zero overhead here.
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

function(zues_fetch_glm)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glm)
endfunction()

function(zues_fetch_glfw)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    # Stick to X11 on Linux for now. Wayland's wayland-scanner pipeline is
    # extra deps we don't need yet.
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11      ON  CACHE BOOL "" FORCE)

    # CRITICAL: build glfw as a SHARED library so window_glfw.dll and the
    # editor (which links imgui+glfw transitively) end up sharing one GLFW
    # state. With static linkage each DLL gets its own _glfw window registry,
    # making `glfwGetWin32Window(handle_from_other_dll)` return null and
    # crashing imgui's wndproc hook setup. See docs/04-dll-safety.md for the
    # general rule.
    set(BUILD_SHARED_LIBS   ON  CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)

    # Restore default. Other fetches (glm) build static again.
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
endfunction()

function(zues_fetch_box2d)
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG        v3.0.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(box2d)
endfunction()

function(zues_fetch_imgui_color_text_edit)
    # Source-code editor widget for ImGui (used by the Lync Editor panel).
    # santaclose's fork tracks modern ImGui (1.91 docking) and is a single
    # TextEditor.cpp/h drop-in. MIT-licensed.
    if(TARGET imgui_color_text_edit)
        return()
    endif()
    # BalazsJako's original (std::regex; no boost). Stable API, hasn't moved
    # in years -- which is fine for what we need (Lync syntax + line edit).
    FetchContent_Declare(
        imgui_color_text_edit
        GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui_color_text_edit)

    add_library(imgui_color_text_edit STATIC
        ${imgui_color_text_edit_SOURCE_DIR}/TextEditor.cpp)
    target_include_directories(imgui_color_text_edit SYSTEM PUBLIC
        ${imgui_color_text_edit_SOURCE_DIR})
    target_link_libraries(imgui_color_text_edit PUBLIC imgui)
    set_target_properties(imgui_color_text_edit PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

function(zues_fetch_imgui)
    # imgui ships no CMakeLists.txt; FetchContent just downloads and we
    # build a static lib ourselves with the GLFW + OpenGL3 backends bundled.
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5-docking
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)

    if(NOT TARGET imgui)
        # find_package's IMPORTED targets are scoped to the calling directory.
        # Ensure OpenGL::GL is visible from the directory that calls us.
        find_package(OpenGL REQUIRED)

        add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
            ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
        target_include_directories(imgui SYSTEM PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
            ${imgui_SOURCE_DIR}/misc/cpp)
        # imgui's OpenGL3 backend has its own internal GL loader; we just
        # need to link the platform GL library + glfw for the GLFW backend.
        target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
        set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endfunction()

# Macro (not function) so ${stb_SOURCE_DIR} escapes back to the caller's
# scope — stb has no CMakeLists.txt, so we expose its source dir directly
# rather than via a target.
macro(zues_fetch_stb)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(stb)
endmacro()

# miniaudio: single-header cross-platform audio (WASAPI/DirectSound/CoreAudio
# /ALSA + WAV/MP3/FLAC decoders + 3D engine + spatializer). MIT/Unlicense
# dual licensed. The engine pins to the v0.11.x line via a known-good tag.
# Macro (not function) so the source dir escapes to the caller's scope.
macro(zues_fetch_miniaudio)
    if(NOT TARGET miniaudio)
        FetchContent_Declare(
            miniaudio
            GIT_REPOSITORY https://github.com/mackron/miniaudio.git
            GIT_TAG        0.11.21
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(miniaudio)

        # miniaudio ships no CMakeLists; expose it as an INTERFACE target so
        # consumers can `target_link_libraries(... miniaudio)` and pick up
        # the include path automatically. The .c implementation file is
        # compiled by exactly ONE consumer (HostShared) via MA_IMPLEMENTATION.
        add_library(miniaudio INTERFACE)
        target_include_directories(miniaudio SYSTEM INTERFACE
            ${miniaudio_SOURCE_DIR})
        # Win32: link against the OS audio + COM libs miniaudio's WASAPI
        # backend needs. Other platforms link their own libs as needed.
        if(WIN32)
            target_link_libraries(miniaudio INTERFACE ole32 winmm)
        elseif(APPLE)
            target_link_libraries(miniaudio INTERFACE
                "-framework CoreFoundation"
                "-framework CoreAudio"
                "-framework AudioToolbox")
        else()
            # Linux: pthread + dl are usually already linked; ALSA optional.
            find_package(Threads REQUIRED)
            target_link_libraries(miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
        endif()
    endif()
endmacro()

function(zues_fetch_json)
    set(JSON_BuildTests       OFF CACHE BOOL "" FORCE)
    set(JSON_Install          OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()
