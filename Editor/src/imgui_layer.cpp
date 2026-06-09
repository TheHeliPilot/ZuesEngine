#include "editor.h"
#include "assets.h"

#include <zues/log.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace Engine::editor {

// Globally-accessible monospace font for code views (the Lync editor
// pushes this around its TextEditor::Render so code lines up). Set
// during imgui_init; nullptr means "fallback to default font".
ImFont* g_code_font = nullptr;

namespace {

// Resolve a per-user prefs directory and ensure it exists. Layout/window
// state, recent worlds, and any future global UI prefs go here so they
// persist across sessions AND across projects (Unity-style).
//
// Windows: %APPDATA%/Zues
// macOS:   $HOME/Library/Application Support/Zues
// Linux:   ${XDG_CONFIG_HOME or ~/.config}/zues
//
// We hold the resolved imgui.ini path in a static string so the C-string
// pointer ImGui keeps in IO.IniFilename stays valid for the lifetime of
// the context.
static std::string g_imgui_ini_path;

const char* prefs_imgui_ini_path() {
#if defined(_WIN32)
    const char* base = std::getenv("APPDATA");
    if (!base) base = std::getenv("USERPROFILE");
#elif defined(__APPLE__)
    const char* base = std::getenv("HOME");
#else
    const char* base = std::getenv("XDG_CONFIG_HOME");
    if (!base) base = std::getenv("HOME");
#endif
    if (!base || !*base) return nullptr;

    std::filesystem::path dir = base;
#if defined(_WIN32)
    dir /= "Zues";
#elif defined(__APPLE__)
    dir /= "Library";
    dir /= "Application Support";
    dir /= "Zues";
#else
    if (std::getenv("XDG_CONFIG_HOME") == nullptr) dir /= ".config";
    dir /= "zues";
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return nullptr;
    g_imgui_ini_path = (dir / "imgui.ini").string();
    return g_imgui_ini_path.c_str();
}

}  // namespace

bool imgui_init(GLFWwindow* window) {
    if (!window) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // Persist dock layout + window positions to the per-user prefs dir, not
    // to the working directory. Falls back to the ImGui default ("imgui.ini"
    // next to the exe) if the env vars don't resolve. Setting IniFilename
    // BEFORE the first NewFrame is required - ImGui reads the ini at the
    // first frame and won't re-read on a later swap.
    if (const char* ini = prefs_imgui_ini_path()) {
        io.IniFilename = ini;
        char msg[512];
        std::snprintf(msg, sizeof(msg), "imgui: layout persisted at %s", ini);
        ZUES_LOG_INFO(msg);
    }
    // Note: NavEnableKeyboard is intentionally OFF. ImGui's nav system
    // intercepts arrow keys to move focus between widgets, which fights
    // the Lync TextEditor (TextEditor only sets WantCaptureKeyboard from
    // INSIDE its Render(), one frame too late to suppress that frame's
    // nav input). Without nav, mouse + explicit SetKeyboardFocusHere are
    // enough to drive the editor and modal dialogs.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Load Exo2 (variable-weight TTF) so the editor doesn't sit on ImGui's
    // bitmap default. Must happen BEFORE ImGui_ImplOpenGL3_Init — that's
    // the call that uploads the font atlas to the GPU. Falling back to the
    // default keeps the editor bootable even when the assets dir is missing.
    {
        const auto font_path = asset_path("fonts/Exo2-VariableFont_wght.ttf");
        std::error_code ec;
        if (std::filesystem::exists(font_path, ec)) {
            // Extended glyph range: Latin-1 + common typography (em dash,
            // arrows, multiplication sign, bullet, ellipsis, check/cross).
            // Without this, the docs panel renders any non-ASCII char as
            // a missing-glyph blank — the markdown text uses em-dash and
            // → arrows liberally. Range list ends with 0.
            //
            // ImGui copies the array internally on AddFontFromFileTTF so
            // a stack-local is fine; but we keep it static for clarity.
            static const ImWchar ranges[] = {
                0x0020, 0x00FF,   // Basic Latin + Latin-1 supplement
                0x2010, 0x205E,   // General Punctuation: dashes, quotes,
                                  //   bullet, ellipsis
                0x2190, 0x21FF,   // Arrows
                0x2200, 0x22FF,   // Mathematical operators
                0x2300, 0x23FF,   // Miscellaneous technical
                0x2500, 0x257F,   // Box drawing
                0x2580, 0x259F,   // Block elements
                0x25A0, 0x25FF,   // Geometric shapes
                0x2600, 0x26FF,   // Miscellaneous symbols (warning, etc.)
                0x2700, 0x27BF,   // Dingbats (check, cross)
                0,
            };
            ImFontConfig cfg{};
            // Higher oversampling = sharper, less shimmery text. The
            // atlas memory cost is small at the sizes we use.
            cfg.OversampleH = 3;
            cfg.OversampleV = 1;
            cfg.PixelSnapH  = false;   // sub-pixel alignment for smoother glyphs
            io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f, &cfg, ranges);
            ZUES_LOG_INFO("imgui: loaded Exo2 font (extended glyph range)");
        } else {
            ZUES_LOG_WARN("imgui: Exo2 font not found, using ImGui default");
        }
    }

    // ---- Code font (monospace) for the Lync editor ------------------------
    // Code reads infinitely better in a monospace face — column-aligned
    // operators, indented blocks line up, ASCII art works. Try in order:
    //   1. Cascadia Mono   (Windows 10+ system font)
    //   2. Cascadia Code   (Windows 10+ system font, with ligatures)
    //   3. Consolas        (older Windows)
    //   4. Menlo           (macOS)
    //   5. DejaVu Sans Mono (most Linux distros)
    // Falls back to the default sans face if none found, but at this
    // point the user has bigger problems than typography.
    {
        const char* candidates[] = {
        #if defined(_WIN32)
            "C:/Windows/Fonts/CascadiaMono.ttf",
            "C:/Windows/Fonts/CascadiaCode.ttf",
            "C:/Windows/Fonts/consola.ttf",
        #elif defined(__APPLE__)
            "/System/Library/Fonts/Menlo.ttc",
            "/System/Library/Fonts/Monaco.ttf",
        #else
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        #endif
        };
        // Reuse the same extended glyph range as the UI font so any
        // code that contains arrows / dashes / etc. doesn't glitch.
        static const ImWchar code_ranges[] = {
            0x0020, 0x00FF,
            0x2010, 0x205E,
            0x2190, 0x21FF,
            0x2200, 0x22FF,
            0x2500, 0x257F,
            0,
        };
        for (const char* path : candidates) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            ImFontConfig cfg{};
            cfg.OversampleH = 3;
            cfg.OversampleV = 1;
            cfg.PixelSnapH  = false;
            g_code_font = io.Fonts->AddFontFromFileTTF(
                path, 15.0f, &cfg, code_ranges);
            if (g_code_font) {
                char msg[512];
                std::snprintf(msg, sizeof(msg),
                              "imgui: loaded code font %s", path);
                ZUES_LOG_INFO(msg);
            }
            break;
        }
        if (!g_code_font)
            ZUES_LOG_WARN("imgui: no monospace code font found — "
                          "Lync editor will use the UI font");
    }

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) return false;

    return true;
}

void imgui_shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void imgui_begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void imgui_end_frame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace Engine::editor
