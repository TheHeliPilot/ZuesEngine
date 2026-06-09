#include <zues/project.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace Engine {

using json = nlohmann::json;
namespace fs = std::filesystem;

Result load_project(const char* path, Project& out) {
    if (!path) return Result::InvalidArgument;

    fs::path p(path);
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return Result::NotFound;

    json j;
    try {
        std::ifstream in(p);
        if (!in) return Result::Error;
        in >> j;
    } catch (const std::exception&) {
        return Result::Error;
    }

    out = {};
    out.name           = j.value("name",           std::string{});
    out.engine_version = j.value("engine_version", std::string{"0.1.0"});
    out.default_world  = j.value("default_world",  std::string{});
    out.worlds_dir       = j.value("worlds_dir",       std::string{"assets/worlds"});
    out.source_dir       = j.value("source_dir",       std::string{"src"});
    out.default_language = j.value("default_language", std::string{"lync"});

    if (j.contains("settings") && j["settings"].is_object()) {
        const auto& s = j["settings"];
        out.settings.window_width  = s.value("window_width",  1280);
        out.settings.window_height = s.value("window_height", 720);
        out.settings.window_title  = s.value("window_title",  std::string{});
        out.settings.fixed_size    = s.value("fixed_size",    false);
        out.settings.fullscreen    = s.value("fullscreen",    false);
    }
    if (out.settings.window_title.empty()) out.settings.window_title = out.name;

    if (j.contains("build") && j["build"].is_object()) {
        const auto& b = j["build"];
        out.build.dll_path      = b.value("dll_path",      std::string{});
        out.build.lync_main     = b.value("lync_main",     std::string{});
        out.build.lync_compiler = b.value("lync_compiler", std::string{});
        out.build.lync_plugin   = b.value("lync_plugin",   std::string{});
        out.build.lync_include  = b.value("lync_include",  std::string{});
        out.build.lync_prelude  = b.value("lync_prelude",  std::string{});
    }

    out.project_dir = p.parent_path().string();
    return Result::Ok;
}

Result save_project(const char* path, const Project& p) {
    if (!path) return Result::InvalidArgument;

    json j;
    j["name"]           = p.name;
    j["engine_version"] = p.engine_version;
    j["default_world"]  = p.default_world;
    j["source_dir"]     = p.source_dir;
    j["settings"]["window_width"]  = p.settings.window_width;
    j["settings"]["window_height"] = p.settings.window_height;
    j["settings"]["window_title"]  =
        p.settings.window_title.empty() ? p.name : p.settings.window_title;
    if (p.settings.fixed_size) j["settings"]["fixed_size"] = true;
    if (p.settings.fullscreen) j["settings"]["fullscreen"] = true;

    // Persist every build-related field so re-saving the project doesn't
    // wipe the lync toolchain paths the launcher set up at create time.
    if (!p.build.dll_path.empty())     j["build"]["dll_path"]     = p.build.dll_path;
    if (!p.build.lync_main.empty())    j["build"]["lync_main"]    = p.build.lync_main;
    if (!p.build.lync_compiler.empty())j["build"]["lync_compiler"]= p.build.lync_compiler;
    if (!p.build.lync_plugin.empty())  j["build"]["lync_plugin"]  = p.build.lync_plugin;
    if (!p.build.lync_include.empty()) j["build"]["lync_include"] = p.build.lync_include;
    if (!p.build.lync_prelude.empty()) j["build"]["lync_prelude"] = p.build.lync_prelude;

    try {
        std::ofstream out(path);
        if (!out) return Result::Error;
        out << j.dump(2);
    } catch (const std::exception&) {
        return Result::Error;
    }
    return Result::Ok;
}

Result create_project_skeleton(const char* root_dir, const char* name) {
    if (!root_dir || !*root_dir || !name || !*name) return Result::InvalidArgument;

    fs::path root(root_dir);
    std::error_code ec;
    fs::create_directories(root, ec);
    // Lowercase dir names match the editor's default project schema
    // (worlds_dir/source_dir/assets_root_relative). Keep them in sync with
    // Project's defaults in zues/project.h.
    fs::create_directories(root / "assets",        ec);
    fs::create_directories(root / "worlds",        ec);
    fs::create_directories(root / "src",           ec);
    fs::create_directories(root / ".zues" / "cache", ec);

    Project p;
    p.name           = name;
    p.engine_version = "0.1.0";
    p.default_world  = "worlds/main.zworld";
    p.settings.window_title = name;

    fs::path proj_file = root / (std::string(name) + ".zuesproject");
    return save_project(proj_file.string().c_str(), p);
}

}  // namespace Engine
