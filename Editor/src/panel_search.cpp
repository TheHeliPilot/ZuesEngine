#include "editor.h"

#include <imgui.h>
#include <TextEditor.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace Engine::editor {

namespace {
    namespace fs = std::filesystem;

    struct SearchResult {
        std::string file_abs;
        std::string file_rel;   // relative to project_dir for display
        int         line_1;     // 1-based
        std::string line_text;
    };

    // Per-session search state, static so it survives panel close/reopen.
    static char   s_query[256]        = {};
    static char   s_prev_query[256]   = {};
    static bool   s_whole_word        = false;
    static bool   s_case_sensitive    = false;
    static bool   s_use_regex         = false;
    static bool   s_results_valid     = false;   // false -> needs rescan
    static bool   s_scan_pending      = false;   // set when query changed
    static std::vector<SearchResult> s_results;
    static std::string s_last_project_dir;

    // Returns true if `name` matches any of the skip patterns.
    bool should_skip_dir(const std::string& name) {
        if (name == "build")            return true;
        if (name == ".zues")            return true;
        if (name.rfind("_zombie_bin",0) == 0)  return true;
        if (name.rfind("_zues_",0)      == 0)  return true;
        return false;
    }

    bool has_searchable_ext(const fs::path& p) {
        std::string e = p.extension().string();
        for (auto& c : e) c = (char)std::tolower((unsigned char)c);
        return e == ".lync" || e == ".cpp" || e == ".h" || e == ".hpp";
    }

    // Simple whole-word check: the match at [start, start+len) must be
    // surrounded by non-alphanumeric / non-underscore chars (or boundaries).
    bool whole_word_check(const std::string& line, size_t start, size_t len) {
        if (start > 0) {
            char c = line[start - 1];
            if (std::isalnum((unsigned char)c) || c == '_') return false;
        }
        size_t end = start + len;
        if (end < line.size()) {
            char c = line[end];
            if (std::isalnum((unsigned char)c) || c == '_') return false;
        }
        return true;
    }

    void run_search(const std::string& project_dir, const std::string& query,
                    bool whole_word, bool case_sens, bool use_regex) {
        s_results.clear();
        if (query.empty() || project_dir.empty()) return;

        // Pre-compile regex if needed.
        std::regex compiled;
        bool regex_ok = false;
        if (use_regex) {
            try {
                auto flags = std::regex::ECMAScript;
                if (!case_sens) flags |= std::regex::icase;
                compiled = std::regex(query, flags);
                regex_ok = true;
            } catch (...) {
                // Bad regex - show no results rather than crash.
                return;
            }
        }

        std::error_code ec;
        bool result_cap_hit = false;   // 2000-result cap; ends both loops
        for (auto& entry : fs::recursive_directory_iterator(
                 project_dir,
                 fs::directory_options::skip_permission_denied,
                 ec)) {
            // Skip directories by name.
            if (entry.is_directory(ec)) {
                const std::string dname = path_str(entry.path().filename());
                if (should_skip_dir(dname)) {
                    // Prune: skip_permission_denied doesn't help with our
                    // custom skip list - we disable recursion by clearing.
                    // recursive_directory_iterator exposes disable_recursion_pending.
                    // Access via the iterator directly is tricky here, so we
                    // use a simple workaround: just continue and skip files inside.
                    // (The iterator visits children - we skip them in the file check.)
                }
                continue;
            }

            if (!entry.is_regular_file(ec)) continue;

            // Skip files inside our blocked dirs by inspecting the relative path.
            {
                std::error_code rel_ec;
                const fs::path rel = fs::relative(entry.path(),
                                                   fs::path(project_dir), rel_ec);
                if (!rel_ec && !rel.empty()) {
                    bool skip = false;
                    for (auto it = rel.begin(); it != rel.end(); ++it) {
                        const std::string part = path_str(*it);
                        if (should_skip_dir(part)) { skip = true; break; }
                        // Skip live-check temp files.
                        if (part.find(".__live.") != std::string::npos) { skip = true; break; }
                    }
                    if (skip) continue;
                }
            }

            if (!has_searchable_ext(entry.path())) continue;

            std::ifstream f(entry.path(), std::ios::binary);
            if (!f) continue;

            std::string file_rel;
            {
                std::error_code rel_ec;
                file_rel = path_str(fs::relative(entry.path(),
                                                   fs::path(project_dir), rel_ec));
                if (rel_ec) file_rel = path_str(entry.path());
            }

            if (result_cap_hit) break;
            std::string line;
            int lineno = 0;
            while (std::getline(f, line)) {
                ++lineno;
                bool found = false;
                if (use_regex && regex_ok) {
                    found = std::regex_search(line, compiled);
                    if (found && whole_word) {
                        // Recheck first match for whole-word.
                        std::smatch m;
                        if (std::regex_search(line, m, compiled)) {
                            found = whole_word_check(line, (size_t)m.position(0),
                                                     (size_t)m.length(0));
                        }
                    }
                } else {
                    // Plain text search.
                    auto find_in = [&](const std::string& hay, const std::string& ndl) -> size_t {
                        if (case_sens) return hay.find(ndl);
                        // Case-insensitive.
                        const size_t hn = hay.size(), nn = ndl.size();
                        for (size_t i = 0; i + nn <= hn; ++i) {
                            bool ok = true;
                            for (size_t k = 0; k < nn; ++k) {
                                if (std::tolower((unsigned char)hay[i+k]) !=
                                    std::tolower((unsigned char)ndl[k])) { ok = false; break; }
                            }
                            if (ok) return i;
                        }
                        return std::string::npos;
                    };
                    const size_t pos = find_in(line, query);
                    if (pos != std::string::npos) {
                        found = !whole_word || whole_word_check(line, pos, query.size());
                    }
                }
                if (found) {
                    SearchResult r;
                    r.file_abs  = path_str(entry.path());
                    r.file_rel  = file_rel;
                    r.line_1    = lineno;
                    // Trim to a reasonable display width.
                    std::string text = line;
                    // Strip leading whitespace for display.
                    size_t lead = 0;
                    while (lead < text.size() &&
                           (text[lead] == ' ' || text[lead] == '\t')) ++lead;
                    if (lead > 0) text = text.substr(lead);
                    if (text.size() > 120) text = text.substr(0, 117) + "...";
                    r.line_text = std::move(text);
                    s_results.push_back(std::move(r));
                    if (s_results.size() >= 2000) {
                        result_cap_hit = true;
                        break;   // exits the per-line loop; outer loop checks the flag
                    }
                }
            }
            if (result_cap_hit) break;
        }
    }

}  // namespace

void draw_search_panel(EditorState& s) {
    if (!s.show_search) return;
    if (!ImGui::Begin("Search", &s.show_search)) {
        ImGui::End();
        return;
    }

    // Ctrl+Shift+F focus request from draw_main_menu_bar shortcut.
    const bool focus_requested = s.search_focus_pending;
    if (focus_requested) {
        s.search_focus_pending = false;
        ImGui::SetNextWindowFocus();
    }

    // ---- Toolbar -----------------------------------------------------------
    bool trigger_search = false;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
    if (focus_requested) ImGui::SetKeyboardFocusHere();
    const ImGuiInputTextFlags qflags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("##search_query", s_query, sizeof(s_query), qflags)) {
        trigger_search = true;
    }
    // Detect query change each frame - rescan lazily.
    if (std::strcmp(s_query, s_prev_query) != 0) {
        s_scan_pending = true;
        std::strncpy(s_prev_query, s_query, sizeof(s_prev_query) - 1);
        s_prev_query[sizeof(s_prev_query) - 1] = 0;
    }
    if (s_scan_pending) trigger_search = true;

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) trigger_search = true;

    ImGui::Checkbox("Whole word",     &s_whole_word);
    ImGui::SameLine();
    ImGui::Checkbox("Case sensitive", &s_case_sensitive);
    ImGui::SameLine();
    ImGui::Checkbox("Regex",          &s_use_regex);

    ImGui::Separator();

    // If the project changed, invalidate.
    if (s.project_dir != s_last_project_dir) {
        s_results_valid = false;
        s_last_project_dir = s.project_dir;
    }

    if (trigger_search) {
        s_scan_pending = false;
        s_results_valid = true;
        run_search(s.project_dir, s_query, s_whole_word, s_case_sensitive, s_use_regex);
    }

    // ---- Results -----------------------------------------------------------
    if (!s_results_valid) {
        ImGui::TextDisabled("Type a query and press Enter, or click Refresh.");
    } else if (s_results.empty()) {
        ImGui::TextDisabled("No results.");
    } else {
        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "%d result%s",
                      (int)s_results.size(),
                      s_results.size() >= 2000 ? " (capped at 2000)" : "");
        ImGui::TextDisabled("%s", hdr);
        ImGui::Separator();

        if (ImGui::BeginChild("##search_results", ImVec2(0, 0), false,
                               ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

            std::string cur_file;
            for (const auto& r : s_results) {
                if (r.file_rel != cur_file) {
                    cur_file = r.file_rel;
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                                       "%s", r.file_rel.c_str());
                }

                // Indented result row.
                ImGui::Indent(12.0f);
                char row_id[32];
                std::snprintf(row_id, sizeof(row_id), "##sr_%p_%d",
                              (void*)r.file_abs.c_str(), r.line_1);
                char row_label[256];
                std::snprintf(row_label, sizeof(row_label), "%4d:  %s",
                              r.line_1, r.line_text.c_str());

                if (ImGui::Selectable(row_label, false,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                    // Single or double click -> open file at that line.
                    if (s.project_loaded) {
                        open_lync_doc(s, r.file_abs);
                        // Jump to the line. The doc was just opened/focused;
                        // find it and set cursor.
                        for (auto& d : s.lync_docs) {
                            if (d.path == r.file_abs && d.editor) {
                                d.editor->SetCursorPosition({r.line_1 - 1, 0});
                                break;
                            }
                        }
                    }
                }
                ImGui::Unindent(12.0f);
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

}  // namespace Engine::editor
