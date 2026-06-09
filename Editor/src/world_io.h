#pragma once
// Helpers for the recent-worlds list persisted next to the editor binary.

#include <filesystem>
#include <string>
#include <vector>

namespace Engine::editor {

// Returns the list in most-recent-first order.
// Reads <exe_dir>/recent_worlds.json; returns empty on any error.
std::vector<std::string> load_recent_worlds(const std::filesystem::path& exe_dir);

// Prepends `path` to `recents`, deduplicates, trims to 5, and writes the
// updated list back to <exe_dir>/recent_worlds.json. Silently ignores I/O
// errors so a read-only exe dir never crashes the editor.
void record_recent_world(const std::filesystem::path& exe_dir,
                         std::vector<std::string>&    recents,
                         const std::string&           path);

}  // namespace Engine::editor
