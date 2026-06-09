#include "world_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Engine::editor {

static constexpr int        MAX_RECENTS  = 5;
static constexpr const char RECENTS_FILE[] = "recent_worlds.json";

std::vector<std::string> load_recent_worlds(const std::filesystem::path& exe_dir) {
    std::vector<std::string> result;
    try {
        std::ifstream f(exe_dir / RECENTS_FILE);
        if (!f) return result;
        nlohmann::json j;
        f >> j;
        if (j.is_array()) {
            for (auto& item : j)
                if (item.is_string())
                    result.push_back(item.get<std::string>());
        }
    } catch (...) {}
    return result;
}

void record_recent_world(const std::filesystem::path& exe_dir,
                         std::vector<std::string>&    recents,
                         const std::string&           path) {
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
    recents.insert(recents.begin(), path);
    if (static_cast<int>(recents.size()) > MAX_RECENTS)
        recents.resize(MAX_RECENTS);

    try {
        nlohmann::json j = recents;
        std::ofstream f(exe_dir / RECENTS_FILE);
        f << j.dump(2);
    } catch (...) {}
}

}  // namespace Engine::editor
