// TODO tracker panel. Walks the project source tree for TODO/FIXME/XXX/HACK
// comments and lists them. Click a row to jump to the file at that line in
// the Lync editor. Cached: re-scan only every few seconds (or on Refresh).

#include "editor.h"
#include "external/TextEditor.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Engine::editor {

namespace {

namespace fs = std::filesystem;

enum class TodoMarker { Todo, Fixme, Xxx, Hack };

struct TodoEntry {
    TodoMarker  marker;
    std::string rel_path;     // for display
    std::string abs_path;     // for the jump
    int         line;         // 1-based
    std::string text;         // payload after the marker keyword
};

const char* marker_label(TodoMarker m) {
    switch (m) {
        case TodoMarker::Todo:  return "TODO";
        case TodoMarker::Fixme: return "FIXME";
        case TodoMarker::Xxx:   return "XXX";
        case TodoMarker::Hack:  return "HACK";
    }
    return "?";
}

ImVec4 marker_color(TodoMarker m) {
    switch (m) {
        case TodoMarker::Todo:  return ImVec4(1.00f, 0.85f, 0.40f, 1.0f);
        case TodoMarker::Fixme: return ImVec4(1.00f, 0.50f, 0.30f, 1.0f);
        case TodoMarker::Xxx:   return ImVec4(0.95f, 0.50f, 0.95f, 1.0f);
        case TodoMarker::Hack:  return ImVec4(1.00f, 0.65f, 0.20f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

// Cache lives in panel scope so the panel can re-render without re-scanning.
struct TodoCache {
    std::vector<TodoEntry> entries;
    std::string            scanned_root;
    double                 next_rescan_s = 0.0;
};
TodoCache g_cache;

double now_s() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

bool ieq_ascii(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        if (ieq_ascii(hay.c_str() + i, needle.c_str(), needle.size())) return true;
    }
    return false;
}

// Try to detect one of the four markers immediately after a `//` comment
// opener. `s` points right after the `//`. Whitespace before the marker is
// allowed; the marker must be followed by a separator (`:`, space, tab) or
// end-of-line so we don't false-match identifiers like "TODOs_done".
//
// On match, sets *kind, returns the offset INTO the line where the payload
// text starts (skipping the marker + one optional `:` + leading whitespace).
// Returns -1 if no marker found.
int match_marker(const char* s, size_t len, TodoMarker& kind) {
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= len) return -1;

    struct M { const char* word; size_t n; TodoMarker k; };
    static const M markers[] = {
        {"TODO",  4, TodoMarker::Todo},
        {"FIXME", 5, TodoMarker::Fixme},
        {"XXX",   3, TodoMarker::Xxx},
        {"HACK",  4, TodoMarker::Hack},
    };
    for (const auto& m : markers) {
        if (i + m.n > len) continue;
        if (!ieq_ascii(s + i, m.word, m.n)) continue;
        const char tail = (i + m.n < len) ? s[i + m.n] : '\0';
        if (tail != '\0' && tail != ' ' && tail != '\t' && tail != ':') continue;
        kind = m.k;
        size_t j = i + m.n;
        if (j < len && s[j] == ':') ++j;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) ++j;
        return (int)j;
    }
    return -1;
}

// Scan one file for marker comments. Errors silently skipped.
void scan_file(const fs::path& abs, const fs::path& project_root,
               std::vector<TodoEntry>& out) {
    std::ifstream f(abs);
    if (!f) return;
    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        ++line_no;
        // Find `//` and try to match a marker right after it.
        size_t pos = line.find("//");
        if (pos == std::string::npos) continue;
        const char* after = line.c_str() + pos + 2;
        const size_t after_len = line.size() - pos - 2;
        TodoMarker k;
        const int payload_off = match_marker(after, after_len, k);
        if (payload_off < 0) continue;

        TodoEntry e;
        e.marker = k;
        e.line   = line_no;
        std::error_code ec;
        e.rel_path = fs::relative(abs, project_root, ec).generic_string();
        if (ec || e.rel_path.empty()) e.rel_path = abs.filename().string();
        e.abs_path = abs.string();
        for (auto& c : e.abs_path) if (c == '\\') c = '/';
        e.text = std::string(after + payload_off, after_len - payload_off);
        // Trim trailing CR / whitespace.
        while (!e.text.empty() && (e.text.back() == '\r' ||
                                    e.text.back() == ' '  ||
                                    e.text.back() == '\t')) e.text.pop_back();
        out.push_back(std::move(e));
    }
}

bool path_should_skip(const fs::path& p) {
    // Skip output dirs + autogen.
    for (const auto& part : p) {
        const std::string s = part.string();
        if (s == "build" || s == ".zues" || s == "_zombie_bin") return true;
    }
    const std::string fn = p.filename().string();
    if (fn.rfind("_zues_", 0) == 0) return true;
    if (fn.find(".__live.") != std::string::npos) return true;
    return false;
}

void rescan(const std::string& project_dir) {
    g_cache.entries.clear();
    g_cache.scanned_root = project_dir;
    if (project_dir.empty()) return;
    const fs::path root(project_dir);
    std::error_code ec;
    if (!fs::exists(root, ec)) return;

    auto opts = fs::directory_options::skip_permission_denied;
    for (auto& e : fs::recursive_directory_iterator(root, opts, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const auto ext = e.path().extension().string();
        if (ext != ".lync" && ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
        if (path_should_skip(e.path())) continue;
        scan_file(e.path(), root, g_cache.entries);
    }

    std::sort(g_cache.entries.begin(), g_cache.entries.end(),
              [](const TodoEntry& a, const TodoEntry& b) {
        if (a.rel_path != b.rel_path) return a.rel_path < b.rel_path;
        return a.line < b.line;
    });
}

// Open the matching Lync tab and place the caret at `line` (1-based).
void jump_to(EditorState& s, const TodoEntry& e) {
    const std::string ext_lower = [&]{
        std::string ext = fs::path(e.abs_path).extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return ext;
    }();
    if (ext_lower != ".lync") {
        // Non-lync: leave the click as a no-op for now (could ShellExecute).
        return;
    }
    open_lync_doc(s, e.abs_path);
    // Find the doc we just opened and seat the caret on its line.
    for (size_t i = 0; i < s.lync_docs.size(); ++i) {
        const auto& d = s.lync_docs[i];
        if (d.path == e.abs_path && d.editor) {
            s.lync_active_doc = (int)i;
            d.editor->SetCursorPosition({e.line - 1, 0});
            // focus_next_frame is the existing pattern other jumps use.
            const_cast<EditorState::LyncDoc&>(d).focus_next_frame = true;
            break;
        }
    }
}

}  // namespace

void draw_todos_panel(EditorState& s) {
    if (!s.show_todos) return;
    if (!ImGui::Begin("TODOs", &s.show_todos)) { ImGui::End(); return; }

    // Re-scan periodically OR when the project_dir changed under us.
    const double t = now_s();
    const bool   project_changed = (g_cache.scanned_root != s.project_dir);
    if (project_changed || t >= g_cache.next_rescan_s) {
        rescan(s.project_dir);
        g_cache.next_rescan_s = t + 4.0;   // 4s cadence; cheap for small trees
    }

    // ---- Toolbar -----------------------------------------------------------
    if (ImGui::Button("Refresh")) { rescan(s.project_dir); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##todos_filter", "filter text...",
                              s.todos_filter, sizeof(s.todos_filter));
    ImGui::SameLine();
    ImGui::Checkbox("TODO",  &s.todos_show_todo);  ImGui::SameLine();
    ImGui::Checkbox("FIXME", &s.todos_show_fixme); ImGui::SameLine();
    ImGui::Checkbox("XXX",   &s.todos_show_xxx);   ImGui::SameLine();
    ImGui::Checkbox("HACK",  &s.todos_show_hack);

    // Counts (post-filter).
    int n_todo = 0, n_fixme = 0, n_xxx = 0, n_hack = 0;
    const std::string filter = s.todos_filter;
    auto is_visible = [&](const TodoEntry& e) -> bool {
        switch (e.marker) {
            case TodoMarker::Todo:  if (!s.todos_show_todo)  return false; break;
            case TodoMarker::Fixme: if (!s.todos_show_fixme) return false; break;
            case TodoMarker::Xxx:   if (!s.todos_show_xxx)   return false; break;
            case TodoMarker::Hack:  if (!s.todos_show_hack)  return false; break;
        }
        if (!filter.empty() && !icontains(e.text, filter) &&
            !icontains(e.rel_path, filter)) return false;
        return true;
    };
    for (const auto& e : g_cache.entries) {
        if (!is_visible(e)) continue;
        switch (e.marker) {
            case TodoMarker::Todo:  ++n_todo;  break;
            case TodoMarker::Fixme: ++n_fixme; break;
            case TodoMarker::Xxx:   ++n_xxx;   break;
            case TodoMarker::Hack:  ++n_hack;  break;
        }
    }
    ImGui::TextDisabled("%d TODO  %d FIXME  %d XXX  %d HACK", n_todo, n_fixme, n_xxx, n_hack);

    ImGui::Separator();

    // ---- List --------------------------------------------------------------
    if (ImGui::BeginChild("##todos_scroll", ImVec2(0, 0))) {
        for (size_t i = 0; i < g_cache.entries.size(); ++i) {
            const auto& e = g_cache.entries[i];
            if (!is_visible(e)) continue;

            ImGui::PushID(static_cast<int>(i));

            // Marker badge (colored).
            const ImVec4 col = marker_color(e.marker);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(marker_label(e.marker));
            ImGui::PopStyleColor();
            ImGui::SameLine(80.0f);

            // file:line, clickable.
            char loc[512];
            std::snprintf(loc, sizeof(loc), "%s:%d", e.rel_path.c_str(), e.line);
            // Use a Selectable that spans only the location text width so
            // the row's text portion stays selectable as plain text.
            if (ImGui::Selectable(loc, false, ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(280.0f, 0.0f))) {
                jump_to(s, e);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open %s at line %d", e.rel_path.c_str(), e.line);
            }
            ImGui::SameLine(370.0f);
            ImGui::TextUnformatted(e.text.c_str());

            ImGui::PopID();
        }
        if (g_cache.entries.empty()) {
            ImGui::TextDisabled("(no TODO / FIXME / XXX / HACK comments found in src tree)");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace Engine::editor
