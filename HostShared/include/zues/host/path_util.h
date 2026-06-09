#pragma once

// Tiny path helpers that the host-side code (project loader, prefab,
// asset loader) uses to keep log/state strings forward-slashed regardless
// of the underlying filesystem. The editor + runtime both consume these.

#include <filesystem>
#include <string>

namespace Engine::host {

inline std::string normalize_path(const std::string& s) {
    std::string out = s;
    for (auto& c : out) if (c == '\\') c = '/';
    return out;
}

inline std::string path_str(const std::filesystem::path& p) {
    return normalize_path(p.string());
}

}  // namespace Engine::host
