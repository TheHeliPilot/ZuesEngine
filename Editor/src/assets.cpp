#include "assets.h"

#include <zues/log.h>
#include <zues/services/renderer_2d.h>

#include <cstdlib>     // getenv
#include <filesystem>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#ifndef ZUES_ASSETS_DIR_DEFAULT
#define ZUES_ASSETS_DIR_DEFAULT "assets"
#endif

namespace Engine::editor {

namespace {
    // Directory containing the running editor executable. Used to resolve
    // relocatable asset/lync paths in packaged builds.
    std::filesystem::path exe_dir() {
#if defined(_WIN32)
        char buf[MAX_PATH];
        DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return {};
        return std::filesystem::path(buf).parent_path();
#else
        return std::filesystem::current_path();
#endif
    }
}

const std::string& assets_dir() {
    // Resolved once. Lookup order (first hit wins):
    //   1. ZUES_ASSETS environment variable                    (override)
    //   2. <exe_dir>/assets                                    (packaged dist)
    //   3. ZUES_ASSETS_DIR_DEFAULT compile-time default        (dev build)
    static const std::string g_dir = []{
        if (const char* env = std::getenv("ZUES_ASSETS"); env && *env) {
            return std::string(env);
        }
        std::error_code ec;
        const auto e = exe_dir();
        if (!e.empty()) {
            const auto p = e / "assets";
            if (std::filesystem::exists(p, ec)) return p.string();
        }
        return std::string(ZUES_ASSETS_DIR_DEFAULT);
    }();
    return g_dir;
}

std::string asset_path(const char* relative) {
    auto p = std::filesystem::path(assets_dir()) / relative;
    return p.string();
}

namespace {
    u32 load_one(::IRenderer_2D_v1* r, const char* relative) {
        if (!r || !r->load_texture_from_file) return 0;
        const auto p = asset_path(relative);
        const u32 h = r->load_texture_from_file(r, p.c_str());
        if (h == 0) {
            char buf[400];
            std::snprintf(buf, sizeof(buf),
                          "icon load failed: %s", p.c_str());
            Engine::log_write(Engine::LogLevel::Warn, "editor.assets", buf);
        }
        return h;
    }
}

void icons_load(EditorIcons& out, ::IRenderer_2D_v1* r) {
    out.play  = load_one(r, "custom-icons/Media/Play.png");
    out.pause = load_one(r, "custom-icons/Media/Pause.png");
    out.stop  = load_one(r, "custom-icons/Media/Stop.png");

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "icons loaded - play=%u pause=%u stop=%u (assets at %s)",
                  out.play, out.pause, out.stop, assets_dir().c_str());
    Engine::log_write(Engine::LogLevel::Info, "editor.assets", buf);
}

void icons_free(EditorIcons& icons, ::IRenderer_2D_v1* r) {
    if (!r || !r->free_texture) return;
    auto drop = [&](u32& h) { if (h) { r->free_texture(r, h); h = 0; } };
    drop(icons.play);
    drop(icons.pause);
    drop(icons.stop);
}

}  // namespace Engine::editor
