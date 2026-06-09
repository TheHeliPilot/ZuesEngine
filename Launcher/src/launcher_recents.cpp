#include "launcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Engine::launcher {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    constexpr size_t MAX_RECENTS = 10;

    fs::path config_dir() {
#if defined(_WIN32)
        char buf[1024] = "";
        size_t len = sizeof(buf);
        getenv_s(&len, buf, sizeof(buf), "APPDATA");
        if (len == 0) return fs::current_path() / ".zues";
        return fs::path(buf) / "Zues";
#else
        if (const char* home = std::getenv("HOME"); home && *home) {
            return fs::path(home) / ".config" / "zues";
        }
        return fs::current_path() / ".zues";
#endif
    }

    fs::path recents_path() { return config_dir() / "recents.json"; }
}

std::vector<RecentEntry> load_recents() {
    std::vector<RecentEntry> out;
    auto p = recents_path();
    std::error_code ec;
    if (!fs::exists(p, ec)) return out;

    try {
        std::ifstream in(p);
        json j; in >> j;
        if (j.contains("recents") && j["recents"].is_array()) {
            for (const auto& e : j["recents"]) {
                RecentEntry r;
                r.path = e.value("path", std::string{});
                r.name = e.value("name", std::string{});
                if (!r.path.empty()) out.push_back(r);
            }
        }
    } catch (...) {
        // Corrupt recents — silently ignore, return empty list.
    }
    return out;
}

void save_recents(const std::vector<RecentEntry>& list) {
    std::error_code ec;
    fs::create_directories(config_dir(), ec);
    json j;
    j["recents"] = json::array();
    for (const auto& r : list) j["recents"].push_back({{"path", r.path}, {"name", r.name}});

    try {
        std::ofstream out(recents_path());
        out << j.dump(2);
    } catch (...) {}
}

void promote_recent(std::vector<RecentEntry>& list,
                    const std::string& path,
                    const std::string& name) {
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const RecentEntry& r) { return r.path == path; }),
               list.end());
    list.insert(list.begin(), {path, name});
    if (list.size() > MAX_RECENTS) list.resize(MAX_RECENTS);
}

}  // namespace Engine::launcher
