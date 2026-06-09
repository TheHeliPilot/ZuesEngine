#include "editor.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace Engine::editor {

// Forward decl — defined further down. Called from draw_source_panel.
void draw_source_template_modal(EditorState& s, const std::filesystem::path& root);

namespace {
    namespace fs = std::filesystem;

    // Expand/Collapse All support. 0 = no pending op; 1 = expand; -1 = collapse.
    static int s_tree_open_override = 0;  // consumed by draw_dir each frame

    bool ext_eq_lc(const fs::path& p, const char* e) {
        std::string s = p.extension().string();
        for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
        return s == e;
    }
    bool is_lync_file(const fs::path& p) { return ext_eq_lc(p, ".lync"); }

#if defined(_WIN32)
    void shell_reveal(const fs::path& p) {
        const fs::path parent = p.parent_path();
        std::string s = parent.string();
        ShellExecuteA(nullptr, "open", s.c_str(), nullptr, nullptr, SW_SHOW);
    }
#else
    void shell_reveal(const fs::path&) {}
#endif

    // Resolve project_dir/src (or whatever source_root_relative is). Empty
    // path means "no project loaded".
    fs::path resolve_source_root(EditorState& s) {
        if (s.project_loaded && !s.project_dir.empty()) {
            return fs::path(s.project_dir) / s.source_root_relative;
        }
        return {};
    }

    // Idempotent — fine to call when dir already exists.
    bool ensure_dir(const fs::path& p) {
        std::error_code ec;
        if (fs::exists(p, ec)) return fs::is_directory(p, ec);
        return fs::create_directories(p, ec);
    }

    // Sanitize a candidate filename. Keeps things printable, single-segment,
    // not empty/dotfile. We don't enforce extensions — the user can name
    // files anything; convention is .lync but they may want .json etc.
    bool valid_name(const char* n) {
        if (!n || !*n) return false;
        if (std::strpbrk(n, "/\\:*?\"<>|")) return false;
        if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0))) return false;
        return true;
    }

    // Commit a pending New File / New Folder. Returns true if committed (and
    // resets the pending state); false if invalid/canceled (caller decides
    // whether to cancel).
    void commit_pending_new(EditorState& s) {
        if (s.pending_new_kind == EditorState::SourceNewKind::None) return;
        const std::string name = s.pending_new_buf;
        if (!valid_name(name.c_str())) {
            show_toast(s, "Invalid name", 2.0f, true);
            return;
        }
        const fs::path target = fs::path(s.pending_new_parent) / name;
        std::error_code ec;
        if (fs::exists(target, ec)) {
            show_toast(s, "Already exists", 2.0f, true);
            return;
        }
        if (s.pending_new_kind == EditorState::SourceNewKind::Folder) {
            if (!fs::create_directory(target, ec)) {
                show_toast(s, "Create folder failed", 2.5f, true);
                return;
            }
        } else {
            std::ofstream f(target, std::ios::binary);
            if (!f) {
                show_toast(s, "Create file failed", 2.5f, true);
                return;
            }
            // Empty file. The user can fill it from the Lync editor.
        }
        s.pending_new_kind = EditorState::SourceNewKind::None;
        s.pending_new_parent.clear();
        s.pending_new_buf[0] = 0;
    }

    void cancel_pending_new(EditorState& s) {
        s.pending_new_kind = EditorState::SourceNewKind::None;
        s.pending_new_parent.clear();
        s.pending_new_buf[0] = 0;
    }

    void commit_rename(EditorState& s) {
        if (s.source_rename_target.empty()) return;
        const fs::path src = s.source_rename_target;
        const std::string name = s.source_rename_buf;
        if (!valid_name(name.c_str())) {
            show_toast(s, "Invalid name", 2.0f, true);
            return;
        }
        const fs::path dst = src.parent_path() / name;
        std::error_code ec;
        if (fs::exists(dst, ec)) {
            show_toast(s, "Target name already exists", 2.5f, true);
            return;
        }
        fs::rename(src, dst, ec);
        if (ec) {
            show_toast(s, "Rename failed", 2.5f, true);
            return;
        }
        s.source_rename_target.clear();
        s.source_rename_buf[0] = 0;
    }

    void cancel_rename(EditorState& s) {
        s.source_rename_target.clear();
        s.source_rename_buf[0] = 0;
    }

    // Render the inline new-file/folder InputText row inside `parent`.
    // Returns true if it rendered (so caller can suppress duplicate rows).
    bool render_pending_new_row(EditorState& s, const fs::path& parent) {
        if (s.pending_new_kind == EditorState::SourceNewKind::None) return false;
        if (s.pending_new_parent != parent.string()) return false;

        ImGui::PushID("##pending_new_row");
        const bool is_folder = (s.pending_new_kind == EditorState::SourceNewKind::Folder);
        ImGui::TextDisabled(is_folder ? "[D]" : "[F]");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetKeyboardFocusHere();
        const ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
        if (ImGui::InputText("##new_name", s.pending_new_buf,
                              sizeof(s.pending_new_buf), flags)) {
            commit_pending_new(s);
        }
        // Cancel on Escape or losing keyboard focus to a click elsewhere.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancel_pending_new(s);
        } else if (ImGui::IsItemDeactivated()
                   && !ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            // Treat blur-without-enter as commit-if-nonempty, else cancel.
            if (s.pending_new_buf[0]) commit_pending_new(s);
            else                       cancel_pending_new(s);
        }
        ImGui::PopID();
        return true;
    }

    void draw_dir(EditorState& s, const fs::path& abs, const fs::path& root) {
        std::error_code ec;
        if (!fs::exists(abs, ec) || !fs::is_directory(abs, ec)) return;

        struct Entry { fs::path path; bool is_dir; };
        std::vector<Entry> entries;
        for (auto& it : fs::directory_iterator(abs, ec)) {
            const std::string name = Engine::editor::path_str(it.path().filename());
            // Hide auto-generated `_zues_*` files (e.g. _zues_main.lync) and
            // `.__live.*` temp files from the live syntax check. Both are
            // editor internals, not user code.
            if (name.rfind("_zues_", 0) == 0)               continue;
            if (name.find(".__live.") != std::string::npos) continue;
            entries.push_back({it.path(), it.is_directory(ec)});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return Engine::editor::path_str(a.path.filename()) < Engine::editor::path_str(b.path.filename());
        });

        // Inline "new" row at the top of this directory if applicable.
        render_pending_new_row(s, abs);

        for (const auto& e : entries) {
            const std::string filename = Engine::editor::path_str(e.path.filename());
            ImGui::PushID(filename.c_str());

            // Inline rename row?
            const bool renaming = (s.source_rename_target == Engine::editor::path_str(e.path));
            if (renaming) {
                ImGui::TextDisabled(e.is_dir ? "[D]" : "[F]");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SetKeyboardFocusHere();
                const ImGuiInputTextFlags flags =
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                if (ImGui::InputText("##rename", s.source_rename_buf,
                                      sizeof(s.source_rename_buf), flags)) {
                    commit_rename(s);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    cancel_rename(s);
                } else if (ImGui::IsItemDeactivated()
                           && !ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                    if (s.source_rename_buf[0] &&
                        std::strcmp(s.source_rename_buf, filename.c_str()) != 0)
                        commit_rename(s);
                    else
                        cancel_rename(s);
                }
                ImGui::PopID();
                continue;
            }

            if (e.is_dir) {
                const ImGuiTreeNodeFlags flags =
                    (e.path.parent_path() == root)
                        ? ImGuiTreeNodeFlags_DefaultOpen
                        : 0;
                if (s_tree_open_override != 0)
                    ImGui::SetNextItemOpen(s_tree_open_override > 0, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(filename.c_str(), flags);

                // TODO: drag-to-reorder within the same parent folder (skip for now - needs ImGui DragDrop per-item payload + insert-position tracking).
        // Folder context menu — IDE-style.
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("New File...")) {
                        s.pending_new_kind   = EditorState::SourceNewKind::File;
                        s.pending_new_parent = Engine::editor::path_str(e.path);
                        s.pending_new_buf[0] = 0;
                    }
                    if (ImGui::MenuItem("New Folder...")) {
                        s.pending_new_kind   = EditorState::SourceNewKind::Folder;
                        s.pending_new_parent = Engine::editor::path_str(e.path);
                        s.pending_new_buf[0] = 0;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename...")) {
                        s.source_rename_target = Engine::editor::path_str(e.path);
                        std::strncpy(s.source_rename_buf, filename.c_str(),
                                     sizeof(s.source_rename_buf) - 1);
                    }
                    if (ImGui::MenuItem("Delete (recursive)")) {
                        const fs::path target = e.path;
                        request_confirm(s,
                            std::string("Delete folder '") + filename +
                            "' and ALL its contents? This cannot be undone.",
                            [&s, target]() {
                                std::error_code rec;
                                const auto removed = fs::remove_all(target, rec);
                                if (rec) show_toast(s, "Delete failed", 2.5f, true);
                                else {
                                    char buf[160];
                                    std::snprintf(buf, sizeof(buf),
                                        "Deleted %llu items",
                                        (unsigned long long)removed);
                                    show_toast(s, buf, 2.0f, false);
                                }
                            });
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reveal in Explorer")) shell_reveal(e.path);
                    ImGui::EndPopup();
                }

                if (open) {
                    draw_dir(s, e.path, root);
                    ImGui::TreePop();
                }
            } else {
                // File row. Full-width Selectable for hover highlight + click;
                // a coloured leading dot keys file type without text noise.
                // .lync = amber, .cpp/.h = blue, .json = green, other = grey.
                ImVec4 dot_color{0.55f, 0.55f, 0.58f, 1.0f};
                const std::string ext = Engine::editor::path_str(e.path.extension());
                if      (ext == ".lync")                       dot_color = {0.95f, 0.65f, 0.30f, 1.0f};
                else if (ext == ".cpp" || ext == ".h"
                      || ext == ".hpp" || ext == ".cc")        dot_color = {0.40f, 0.65f, 0.95f, 1.0f};
                else if (ext == ".json"  || ext == ".zworld"
                      || ext == ".zuesproject")                dot_color = {0.55f, 0.85f, 0.45f, 1.0f};
                else if (ext == ".png"  || ext == ".jpg"
                      || ext == ".jpeg")                       dot_color = {0.85f, 0.55f, 0.85f, 1.0f};

                // Render a tinted bullet, then the filename, with the whole
                // row clickable. SpanAvailWidth keeps the hover highlight
                // covering the full pane width.
                const ImVec2 row_pos = ImGui::GetCursorScreenPos();
                const float  text_h  = ImGui::GetTextLineHeight();
                // SpanAllColumns gives full-width hover even outside Tables,
                // which is what we want here (no actual table in use).
                ImGui::Selectable("", false,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick);
                const bool hovered_now = ImGui::IsItemHovered();
                const bool dblclicked  = hovered_now &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                // Overlay the bullet + filename on top of the Selectable so
                // hover styling still works.
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 dot_c{row_pos.x + 6.0f, row_pos.y + text_h * 0.5f};
                dl->AddCircleFilled(dot_c, 3.5f, ImGui::ColorConvertFloat4ToU32(dot_color));
                dl->AddText(ImVec2(row_pos.x + 18.0f, row_pos.y),
                            ImGui::GetColorU32(ImGuiCol_Text), filename.c_str());

                if (dblclicked && is_lync_file(e.path))
                    open_lync_doc(s, Engine::editor::path_str(e.path));

                if (ImGui::BeginPopupContextItem()) {
                    if (is_lync_file(e.path)) {
                        if (ImGui::MenuItem("Open in Lync Editor"))
                            open_lync_doc(s, Engine::editor::path_str(e.path));
                    }
                    if (ImGui::MenuItem("Rename...")) {
                        s.source_rename_target = Engine::editor::path_str(e.path);
                        std::strncpy(s.source_rename_buf, filename.c_str(),
                                     sizeof(s.source_rename_buf) - 1);
                    }
                    if (ImGui::MenuItem("Delete")) {
                        const fs::path target = e.path;
                        request_confirm(s,
                            std::string("Delete file '") + filename + "'?",
                            [&s, target]() {
                                std::error_code rec;
                                if (!fs::remove(target, rec) || rec)
                                    show_toast(s, "Delete failed", 2.5f, true);
                            });
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reveal in Explorer")) shell_reveal(e.path);
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
    }
}

// Renders the source-tree body directly into the current ImGui window.
// Caller controls Begin/End (so this can live inside the Lync Editor's
// left pane or any other host window).
void draw_source_tree_inline(EditorState& s) {
    const fs::path root = resolve_source_root(s);
    if (root.empty()) {
        ImGui::TextDisabled("(no project loaded)");
        return;
    }

    // Header - template buttons, plain-file fallback, root path.
    auto open_template = [&](EditorState::SourceTemplate kind) {
        ensure_dir(root);
        s.template_kind = kind;
        s.template_name_buf[0] = 0;
        const std::string& lang = s.project_default_language.empty()
                                  ? std::string("lync") : s.project_default_language;
        std::strncpy(s.template_lang_buf, lang.c_str(),
                     sizeof(s.template_lang_buf) - 1);
        s.template_lang_buf[sizeof(s.template_lang_buf) - 1] = 0;
        s.template_just_opened = true;
    };
    // Compact toolbar: single "+" button opens a popup with all four create
    // actions. Keeps the header clean even on narrow source panes.
    if (ImGui::Button("+", ImVec2(28, 0))) ImGui::OpenPopup("##new_menu");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New file / folder / template");
    if (ImGui::BeginPopup("##new_menu")) {
        if (ImGui::MenuItem("New Component...")) open_template(EditorState::SourceTemplate::Component);
        if (ImGui::MenuItem("New System..."))    open_template(EditorState::SourceTemplate::System);
        ImGui::Separator();
        if (ImGui::MenuItem("New File...")) {
            ensure_dir(root);
            s.pending_new_kind   = EditorState::SourceNewKind::File;
            s.pending_new_parent = Engine::editor::path_str(root);
            s.pending_new_buf[0] = 0;
        }
        if (ImGui::MenuItem("New Folder...")) {
            ensure_dir(root);
            s.pending_new_kind   = EditorState::SourceNewKind::Folder;
            s.pending_new_parent = Engine::editor::path_str(root);
            s.pending_new_buf[0] = 0;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("v v")) s_tree_open_override =  1;  // Expand All
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Expand All");
    ImGui::SameLine();
    if (ImGui::SmallButton("^ ^")) s_tree_open_override = -1;  // Collapse All
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse All");
    ImGui::SameLine();
    // Path strip: show project name only on narrow panes; full root on wide.
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        std::string label;
        if (avail > 280.0f) {
            label = Engine::editor::path_str(root);
            if (label.size() > 60) label = "..." + label.substr(label.size() - 57);
        } else {
            label = Engine::editor::path_str(fs::path(root).filename());
            if (label.empty()) label = Engine::editor::path_str(root);
        }
        // Vertical centering relative to the button next to it.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label.c_str());
    }
    ImGui::Separator();

    std::error_code ec;
    if (!fs::exists(root, ec)) {
        ImGui::TextDisabled("(src directory missing - created on first New)");
        ImGui::TextDisabled("Path: %s", Engine::editor::path_str(root).c_str());
        return;
    }

    if (ImGui::BeginChild("##source_tree", ImVec2(0, 0), false,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        // Tighten vertical rhythm so rows feel like an IDE file tree, not
        // like a form. Restore original spacing on exit.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(4, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,  14.0f);
        draw_dir(s, root, root);
        s_tree_open_override = 0;   // consumed; reset for next frame
        ImGui::PopStyleVar(3);
    }
    ImGui::EndChild();

    // Template modal lives at the same scope as the tree (its lifetime is
    // tied to the host window).
    draw_source_template_modal(s, root);
}

// ----------------------------------------------------------------------------
// Component / System pre-gen. Skeletons are intentionally minimal — enough to
// compile, with TODOs marking what the user should fill in.
// ----------------------------------------------------------------------------
namespace {

// Note on file layout: a Lync source file can hold any mix of [component],
// [system], [on_load], [on_unload] decls plus helpers. These templates put
// one item per file as an organisational default, not a language requirement.
// You can paste a system into a component file and it will compile fine.

std::string lync_component_template(const std::string& name) {
    std::string s;
    s += "[Component]\n";
    s += "[Category(\"Project\")]\n";
    s += name + ": struct {\n";
    s += "    value: float\n";
    s += "}\n";
    return s;
}

std::string lync_system_template(const std::string& name) {
    std::string s;
    s += "[System(\"PreUpdate\", \"Game\")]\n";
    s += "def " + name + "(eng: ptr, dt: float, user: ptr): void {\n";
    s += "}\n";
    return s;
}

std::string cpp_component_template(const std::string& name) {
    std::string s;
    s += "#pragma once\n";
    s += "#include <zues/project_api.h>\n\n";
    s += "struct " + name + " {\n";
    s += "    float value{};\n";
    s += "};\n";
    s += "ZUES_PROJECT_FIELDS(" + name + ", value);\n";
    return s;
}

std::string cpp_system_template(const std::string& name) {
    std::string s;
    s += "#include <zues/project_api.h>\n\n";
    s += "extern \"C\" void " + name + "(ZuesEngine* eng, float dt, void* user) {\n";
    s += "    (void)eng; (void)dt; (void)user;\n";
    s += "}\n";
    return s;
}

}  // namespace

void draw_source_template_modal(EditorState& s, const fs::path& root) {
    if (s.template_kind == EditorState::SourceTemplate::None) return;

    const bool is_component = (s.template_kind == EditorState::SourceTemplate::Component);
    const char* title = is_component ? "New Component" : "New System";

    if (s.template_just_opened) {
        ImGui::OpenPopup(title);
        s.template_just_opened = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::BeginPopupModal(title, &open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

        ImGui::SetNextItemWidth(280);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", s.template_name_buf,
                                              sizeof(s.template_name_buf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled(is_component
            ? "PascalCase recommended (e.g. Velocity, Health)"
            : "lower_snake recommended (e.g. movement_system)");

        // Language picker — defaults to project's default_language.
        const char* langs[] = {"lync", "cpp"};
        int lang_idx = (std::strcmp(s.template_lang_buf, "cpp") == 0) ? 1 : 0;
        if (ImGui::Combo("Language", &lang_idx, langs, IM_ARRAYSIZE(langs))) {
            std::strncpy(s.template_lang_buf, langs[lang_idx],
                         sizeof(s.template_lang_buf) - 1);
            s.template_lang_buf[sizeof(s.template_lang_buf) - 1] = 0;
        }

        const bool name_ok = s.template_name_buf[0] != 0
                          && valid_name(s.template_name_buf);

        bool commit = enter;
        if (ImGui::Button("Create", ImVec2(120, 0))) commit = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.template_kind = EditorState::SourceTemplate::None;
            ImGui::CloseCurrentPopup();
        }

        if (commit && name_ok) {
            const std::string name = s.template_name_buf;
            const bool is_cpp = (lang_idx == 1);
            const std::string ext = is_cpp
                ? (is_component ? ".h" : ".cpp")
                : ".lync";
            const fs::path target = root / (name + ext);

            std::error_code ec;
            if (fs::exists(target, ec)) {
                show_toast(s, "File already exists", 2.5f, true);
            } else {
                std::string body;
                if (is_component) body = is_cpp ? cpp_component_template(name)
                                                : lync_component_template(name);
                else              body = is_cpp ? cpp_system_template(name)
                                                : lync_system_template(name);
                {
                    std::ofstream f(target, std::ios::binary | std::ios::trunc);
                    if (!f) {
                        show_toast(s, "Create failed", 2.5f, true);
                    } else {
                        f.write(body.data(), static_cast<std::streamsize>(body.size()));
                        f.flush();
                    }
                    // Stream closes here at scope end — guarantees the bytes
                    // are on disk before open_lync_doc() does its ifstream read.
                }
                std::error_code stat_ec;
                if (fs::file_size(target, stat_ec) > 0) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "Created %s",
                                  Engine::editor::path_str(target.filename()).c_str());
                    show_toast(s, buf, 2.0f, false);
                    // Auto-open .lync files in the integrated editor.
                    if (!is_cpp) open_lync_doc(s, Engine::editor::path_str(target));
                }
                s.template_kind = EditorState::SourceTemplate::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (!open) s.template_kind = EditorState::SourceTemplate::None;
}

}  // namespace Engine::editor
