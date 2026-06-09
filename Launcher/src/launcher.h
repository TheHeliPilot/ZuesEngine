#pragma once

// Internal launcher-only headers. The launcher is a small standalone exe;
// nothing here is part of the engine's public API.

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Engine::launcher {

// ---- theme ------------------------------------------------------------------

void apply_theme();

// ---- recents ----------------------------------------------------------------

struct RecentEntry {
    std::string path;     // absolute path to .zuesproject
    std::string name;
};

std::vector<RecentEntry> load_recents();
void                     save_recents(const std::vector<RecentEntry>& list);
void                     promote_recent(std::vector<RecentEntry>& list,
                                        const std::string& path,
                                        const std::string& name);

// ---- native dialogs ---------------------------------------------------------
//
// On Windows: uses comdlg32 / shell32 native dialogs. On Linux/macOS:
// returns false (caller falls back to a text-input flow). All paths are
// returned as UTF-8.

bool pick_open_file (std::string& out_path,
                     const char* title       = "Open Project",
                     const char* filter_label = "Zues Project",
                     const char* filter_exts  = "*.zuesproject");

bool pick_folder    (std::string& out_path,
                     const char* title = "Select Folder");

// ---- editor spawn -----------------------------------------------------------

bool spawn_editor(const std::string& project_path);

// ---- project creation -------------------------------------------------------

// Generate a Lync-flavoured project skeleton under `full` (created if absent).
bool create_lync_skeleton(const std::filesystem::path& full, const std::string& name);

// Run build.bat inside `project_dir` synchronously; capture combined stdout/
// stderr into `out_output`. Returns true iff exit code is 0.
bool run_build_sync(const std::filesystem::path& project_dir, std::string& out_output);

}  // namespace Engine::launcher
