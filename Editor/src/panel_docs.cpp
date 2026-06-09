#include "editor.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Engine::editor {

namespace {

// One topic = a markdown file under <exe>/docs (packaged) or
// <CMAKE_SOURCE_DIR>/Editor/src/docs (dev). Title is derived from the
// first '# ' line of the body; id is the filename stem.
struct DocsTopic {
    std::string id;        // filename stem - used as URL/key
    std::string title;     // first H1 in the file
    std::string body;      // raw markdown
};

// Resolve the docs root. Lookup order:
//   1. <exe_dir>/docs                         (packaged build)
//   2. ZUES_ASSETS_DIR_DEFAULT/../Editor/src/docs   (dev build)
//   3. plain "docs"                           (cwd fallback)
std::filesystem::path docs_root() {
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(_WIN32)
    char buf[260];
    DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        const auto p = fs::path(buf).parent_path() / "docs";
        if (fs::exists(p, ec)) return p;
    }
#endif
#if defined(ZUES_ASSETS_DIR_DEFAULT)
    {
        const auto p = fs::path(ZUES_ASSETS_DIR_DEFAULT).parent_path()
                       / "Editor" / "src" / "docs";
        if (fs::exists(p, ec)) return p;
    }
#endif
    return fs::path("docs");
}

const std::vector<DocsTopic>& topics() {
    static std::vector<DocsTopic> g;
    static bool loaded = false;
    if (loaded) return g;
    loaded = true;

    namespace fs = std::filesystem;
    const fs::path root = docs_root();
    std::error_code ec;
    if (!fs::exists(root, ec)) return g;

    // Manual order so the sidebar reads as a tour, not alphabetical noise.
    static const char* const order[] = {
        "quickstart",
        "lync",                 // language intro -- types, ref/own, match, attrs
        "components",
        "systems",
        "lifecycle",
        "templates",
        "physics",
        "timers_random",
        "prefabs",
        "sprites",              // slicing, 9-slice, animator + clip table
        "particles",            // VFX + swarm unified emitter component
        "audio",                // 2D + 3D audio (AudioSource, AudioListener)
        "ui",                   // UIAnchor + Text HUD components
        "worlds",
        "editor",
        "distributing",         // ship a game / ship the engine
    };
    for (auto* id : order) {
        const fs::path p = root / (std::string(id) + ".md");
        if (!fs::exists(p, ec)) continue;
        std::ifstream in(p);
        std::ostringstream ss; ss << in.rdbuf();
        DocsTopic t;
        t.id   = id;
        t.body = ss.str();
        // Title = first '# ' line; fall back to id if absent.
        std::istringstream lss(t.body);
        std::string line;
        while (std::getline(lss, line)) {
            if (line.size() >= 2 && line[0] == '#' && line[1] == ' ') {
                t.title = line.substr(2);
                break;
            }
        }
        if (t.title.empty()) t.title = id;
        g.push_back(std::move(t));
    }
    return g;
}

// ---- Inline markdown segment renderer ----------------------------------
// Splits a line into runs of plain / **bold** / `code` and emits each
// with the matching colour. Stays on one ImGui horizontal row via SameLine
// after every segment except the last.
void render_inline(const std::string& line) {
    if (line.empty()) { ImGui::Spacing(); return; }
    const ImU32 col_plain = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 col_code  = IM_COL32(255, 198, 109, 255);   // amber
    const ImU32 col_bold  = IM_COL32(220, 220, 235, 255);   // brighter

    auto emit = [&](const std::string& s, ImU32 c, bool last) {
        if (s.empty()) return;
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        ImGui::TextUnformatted(s.c_str());
        ImGui::PopStyleColor();
        if (!last) ImGui::SameLine(0.0f, 0.0f);
    };

    // Build segment list first so we know "last".
    struct Seg { std::string text; ImU32 col; };
    std::vector<Seg> segs;
    std::string cur;
    auto flush = [&](ImU32 c) {
        if (!cur.empty()) { segs.push_back({std::move(cur), c}); cur.clear(); }
    };

    for (size_t i = 0; i < line.size(); ) {
        // `inline code` ... ` is the closing tick.
        if (line[i] == '`') {
            flush(col_plain);
            const size_t end = line.find('`', i + 1);
            if (end == std::string::npos) {
                cur += line[i++];   // unterminated - treat as plain
                continue;
            }
            segs.push_back({line.substr(i + 1, end - i - 1), col_code});
            i = end + 1;
            continue;
        }
        // **bold** ... ** is the closer.
        if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
            flush(col_plain);
            const size_t end = line.find("**", i + 2);
            if (end == std::string::npos) {
                cur += line[i++];
                continue;
            }
            segs.push_back({line.substr(i + 2, end - i - 2), col_bold});
            i = end + 2;
            continue;
        }
        cur += line[i++];
    }
    flush(col_plain);

    for (size_t i = 0; i < segs.size(); ++i) {
        emit(segs[i].text, segs[i].col, i + 1 == segs.size());
    }
    if (segs.empty()) ImGui::TextUnformatted("");
}

// True when `line` looks like a markdown table row: starts and ends with '|'
// and contains at least one inner '|'.
bool is_table_row(const std::string& l) {
    if (l.size() < 3) return false;
    if (l.front() != '|' || l.back() != '|') return false;
    return l.find('|', 1) < l.size() - 1;
}
// Separator row: pipes around dashes/colons only.
bool is_table_sep(const std::string& l) {
    if (!is_table_row(l)) return false;
    for (char c : l) {
        if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t')
            return false;
    }
    return true;
}
// Split "| a | b | c |" into ["a","b","c"] (trimmed).
std::vector<std::string> split_table_row(const std::string& l) {
    std::vector<std::string> out;
    size_t i = 1;     // skip leading '|'
    while (i < l.size()) {
        size_t j = l.find('|', i);
        if (j == std::string::npos) j = l.size();
        std::string cell = l.substr(i, j - i);
        // trim
        size_t a = cell.find_first_not_of(" \t");
        size_t b = cell.find_last_not_of(" \t");
        if (a == std::string::npos) cell.clear();
        else cell = cell.substr(a, b - a + 1);
        out.push_back(std::move(cell));
        i = j + 1;
    }
    if (!out.empty() && out.back().empty()) out.pop_back();   // trailing '|'
    return out;
}

// Render a markdown body. Subset:
//   "# H1"    -> amber, separator
//   "## H2"   -> light blue
//   "### H3"  -> dim
//   "- item"  -> bullet
//   "1. item" -> bullet (we don't preserve numbering; cheap)
//   "| ... |" -> table (with separator row stripped)
//   "```LANG" -> code fence; LANG cpp/lync swaps with docs_lang, others always render
//   inline    -> **bold** + `code` segments coloured via render_inline
void render_markdown(EditorState& s, const std::string& body) {
    // Slurp all lines up front so we can peek (for table-separator detection).
    std::vector<std::string> lines;
    {
        std::istringstream ss(body);
        std::string l;
        while (std::getline(ss, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(std::move(l));
        }
    }

    auto wanted_lang_str = [&](){
        return s.docs_lang == EditorState::DocsLang::Cpp ? "cpp" : "lync";
    };

    int fence_id = 0;
    int table_id = 0;
    bool in_fence = false;
    std::string fence_lang;
    std::string fence_buf;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];

        // ---- Code fence ------------------------------------------------
        if (line.size() >= 3 && line.substr(0, 3) == "```") {
            if (!in_fence) {
                in_fence    = true;
                fence_lang  = line.substr(3);
                fence_buf.clear();
            } else {
                const bool toggle_fence = (fence_lang == "cpp" || fence_lang == "lync");
                const bool show         = !toggle_fence || fence_lang == wanted_lang_str();
                if (show) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImVec4(0.10f, 0.10f, 0.11f, 1.0f));
                    const int nl = (int)std::count(fence_buf.begin(),
                                                    fence_buf.end(), '\n');
                    ImGui::BeginChild(("##fence_" + std::to_string(fence_id++)).c_str(),
                        ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * (1 + nl)
                                   + 28.0f),
                        true);
                    if (ImGui::SmallButton(("Copy##" + std::to_string(fence_id)).c_str()))
                        ImGui::SetClipboardText(fence_buf.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s",
                        fence_lang.empty() ? "code" : fence_lang.c_str());
                    ImGui::Separator();
                    ImGui::TextUnformatted(fence_buf.c_str(),
                        fence_buf.c_str() + fence_buf.size());
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                in_fence = false;
                fence_lang.clear();
                fence_buf.clear();
            }
            continue;
        }
        if (in_fence) {
            fence_buf += line;
            fence_buf += '\n';
            continue;
        }

        // ---- Table block: row + separator + more rows -----------------
        if (is_table_row(line) &&
                li + 1 < lines.size() && is_table_sep(lines[li + 1])) {
            const std::vector<std::string> header = split_table_row(line);
            const int cols = (int)header.size();
            // Walk forward to collect body rows.
            std::vector<std::vector<std::string>> rows;
            size_t end = li + 2;
            while (end < lines.size() && is_table_row(lines[end])) {
                rows.push_back(split_table_row(lines[end]));
                ++end;
            }
            const std::string id = "##tbl" + std::to_string(table_id++);
            if (cols > 0 && ImGui::BeginTable(id.c_str(), cols,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp)) {
                for (auto& h : header)
                    ImGui::TableSetupColumn(h.c_str());
                ImGui::TableHeadersRow();
                for (auto& r : rows) {
                    ImGui::TableNextRow();
                    for (int c = 0; c < cols; ++c) {
                        ImGui::TableNextColumn();
                        if (c < (int)r.size()) render_inline(r[c]);
                    }
                }
                ImGui::EndTable();
            }
            li = end - 1;   // for-loop increment lands on the row after
            continue;
        }

        // ---- Headings -------------------------------------------------
        if (line.size() >= 4 && line[0]=='#' && line[1]=='#' && line[2]=='#' && line[3]==' ') {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.82f, 1.0f));
            ImGui::TextUnformatted(line.c_str() + 4);
            ImGui::PopStyleColor();
            continue;
        }
        // Anchor scroll helper: when a heading text matches the pending
        // anchor, snap-scroll to put it near the top of the body view.
        auto check_anchor = [&](const std::string& heading_text) {
            if (!s.docs_pending_anchor.empty() &&
                    heading_text == s.docs_pending_anchor) {
                ImGui::SetScrollHereY(0.1f);
                s.docs_pending_anchor.clear();
            }
        };
        if (line.size() >= 3 && line[0]=='#' && line[1]=='#' && line[2]==' ') {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.95f, 1.0f));
            ImGui::TextUnformatted(line.c_str() + 3);
            check_anchor(line.substr(3));
            ImGui::PopStyleColor();
            continue;
        }
        if (line.size() >= 2 && line[0]=='#' && line[1]==' ') {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.40f, 1.0f));
            ImGui::TextUnformatted(line.c_str() + 2);
            check_anchor(line.substr(2));
            ImGui::PopStyleColor();
            ImGui::Separator();
            continue;
        }

        // ---- Bullet list ---------------------------------------------
        if (line.size() >= 2 && line[0]=='-' && line[1]==' ') {
            ImGui::Bullet();
            render_inline(line.substr(2));
            continue;
        }
        // ---- Numbered list (any digits + ". ") -----------------------
        {
            size_t k = 0;
            while (k < line.size() && std::isdigit((unsigned char)line[k])) ++k;
            if (k > 0 && k + 1 < line.size() && line[k]=='.' && line[k+1]==' ') {
                ImGui::TextUnformatted(line.substr(0, k + 1).c_str());
                ImGui::SameLine();
                render_inline(line.substr(k + 2));
                continue;
            }
        }

        // ---- Plain (with inline ** + ` parsing) ----------------------
        if (line.empty()) {
            ImGui::Spacing();
        } else {
            render_inline(line);
        }
    }
}

// Symbol -> (topic id, anchor heading) map. Matches identifiers we have
// docs for. Auto-injected per-component helpers (AddX / HasX / EachX / ...)
// are matched by prefix; everything else is exact.
struct DocsLink { const char* topic; const char* anchor; };
const DocsLink* lookup_symbol_link(const std::string& sym) {
    static const std::unordered_map<std::string, DocsLink> map = {
        // ---- Attributes ----------------------------------------------
        {"Component",      {"components", "Components"}},
        {"Singleton",      {"components", "[Singleton] components"}},
        {"Category",       {"components", "Components"}},
        {"System",         {"systems",    "Systems"}},
        {"OnLoad",         {"lifecycle",  "Lifecycle hooks"}},
        {"OnUpdate",       {"lifecycle",  "Lifecycle hooks"}},
        {"OnUnload",       {"lifecycle",  "Lifecycle hooks"}},
        {"OnCollision",    {"physics",    "Collision + trigger hooks"}},
        {"OnTriggerEnter", {"physics",    "Collision + trigger hooks"}},
        {"OnTriggerExit",  {"physics",    "Collision + trigger hooks"}},

        // ---- Language keywords --------------------------------------
        {"def",       {"lync", "Functions"}},
        {"struct",    {"lync", "Structs"}},
        {"ref",       {"lync", "Ownership: `ref` and `own`"}},
        {"own",       {"lync", "Ownership: `ref` and `own`"}},
        {"match",     {"lync", "Match expressions"}},
        {"some",      {"lync", "Match expressions"}},
        {"null",      {"lync", "Match expressions"}},
        {"if",        {"lync", "Control flow"}},
        {"else",      {"lync", "Control flow"}},
        {"while",     {"lync", "Control flow"}},
        {"for",       {"lync", "Control flow"}},
        {"return",    {"lync", "Functions"}},

        // ---- Built-in types -----------------------------------------
        {"int",       {"lync", "Types"}},
        {"float",     {"lync", "Types"}},
        {"bool",      {"lync", "Types"}},
        {"ptr",       {"lync", "Types"}},
        {"Vec2",      {"lync", "Types"}},
        {"Vec3",      {"lync", "Types"}},
        {"Vec4",      {"lync", "Types"}},
        {"Color",     {"lync", "Types"}},
        {"EntityRef", {"lync", "Types"}},
        {"PrefabRef", {"prefabs", "Asset GUIDs"}},

        // ---- Engine components --------------------------------------
        {"Transform2D",    {"components", "Components"}},
        {"Sprite",         {"components", "Components"}},
        {"Camera2D",       {"components", "Components"}},
        {"RigidBody",      {"physics",    "Components"}},
        {"BoxCollider",    {"physics",    "Components"}},
        {"CircleCollider", {"physics",    "Components"}},
        {"Text",           {"ui",         "Components"}},
        {"UIAnchor",       {"ui",         "Components"}},

        // ---- Input + KeyCode ----------------------------------------
        {"KeyCode",       {"lync", "Prelude built-ins"}},
        {"IsKeyDown",     {"lync", "Prelude built-ins"}},
        {"IsKeyPressed",  {"lync", "Prelude built-ins"}},
        {"IsKeyReleased", {"lync", "Prelude built-ins"}},

        // ---- Logging ------------------------------------------------
        {"LogDebug",  {"lifecycle", "Logging"}},
        {"LogInfo",   {"lifecycle", "Logging"}},
        {"LogWarn",   {"lifecycle", "Logging"}},
        {"LogError",  {"lifecycle", "Logging"}},
        {"Log",       {"lifecycle", "Logging"}},

        // ---- Timers + random ----------------------------------------
        {"SetTimeout",   {"timers_random", "Timers"}},
        {"SetInterval",  {"timers_random", "Timers"}},
        {"CancelTimer",  {"timers_random", "Timers"}},
        {"RandomFloat",  {"timers_random", "Random"}},
        {"RandomRange",  {"timers_random", "Random"}},
        {"RandomInt",    {"timers_random", "Random"}},
        {"RandomSeed",   {"timers_random", "Random"}},

        // ---- Entity + prefab API ------------------------------------
        {"CreateEntity",       {"systems",  "Spawning + destroying entities"}},
        {"DestroyEntity",      {"systems",  "Spawning + destroying entities"}},
        {"Instantiate",        {"prefabs",  "Instantiating"}},
        {"InstantiatePrefab",  {"prefabs",  "Instantiating"}},
        {"SetTransform",          {"systems", "Spawning + destroying entities"}},
        {"SetTransformPosition",  {"systems", "Spawning + destroying entities"}},

        // ---- Physics API --------------------------------------------
        {"ApplyImpulse",     {"physics", "Engine API"}},
        {"ApplyForce",       {"physics", "Engine API"}},
        {"SetVelocity",      {"physics", "Engine API"}},
        {"SetBodyPosition",  {"physics", "Engine API"}},
        {"WakeBody",         {"physics", "Engine API"}},

        // ---- Text helpers -------------------------------------------
        {"SetText",       {"ui", "Updating text from a system"}},
        {"SetTextColor",  {"ui", "Updating text from a system"}},
        {"AddTextDefault",{"ui", "Components"}},
    };
    if (auto it = map.find(sym); it != map.end()) return &it->second;

    // Auto-injected helper prefixes -- catch Add<T>, Each<T>, etc. plus
    // their snake-case aliases (AddHealth, EachVelocity, ...).
    static const DocsLink helper{"components",
                                  "Auto-injected helper family"};
    static const char* const helper_prefixes[] = {
        "Add", "Each", "Has", "Get", "Remove", "QueryEach"
    };
    for (auto* p : helper_prefixes) {
        const size_t n = std::strlen(p);
        if (sym.size() > n && sym.compare(0, n, p) == 0
                && std::isupper((unsigned char)sym[n]))
            return &helper;
    }
    return nullptr;
}

}  // namespace

void jump_to_docs(EditorState& s, const std::string& symbol) {
    const DocsLink* link = lookup_symbol_link(symbol);
    if (!link) {
        // Toast briefly so the user knows the Ctrl+click was registered
        // but the symbol just isn't in the docs index. Cuts the
        // "is the keybinding broken?" ambiguity.
        if (!symbol.empty()) {
            const std::string msg = std::string("No docs for `") +
                                     symbol + "`";
            show_toast(s, msg.c_str(), 1.5f, false);
        }
        return;
    }
    s.show_docs           = true;
    s.docs_active         = link->topic;
    s.docs_pending_anchor = link->anchor;
    ImGui::SetWindowFocus("Docs");   // bring it to the front
}

void draw_docs_panel(EditorState& s) {
    if (!s.show_docs) return;
    if (!ImGui::Begin("Docs", &s.show_docs, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        ImGui::TextDisabled("Language:");
        ImGui::SameLine();
        int li = (s.docs_lang == EditorState::DocsLang::Cpp) ? 0 : 1;
        if (ImGui::RadioButton("cpp",  &li, 0))
            s.docs_lang = EditorState::DocsLang::Cpp;
        ImGui::SameLine();
        if (ImGui::RadioButton("lync", &li, 1))
            s.docs_lang = EditorState::DocsLang::Lync;
        ImGui::EndMenuBar();
    }

    const auto& list = topics();
    if (list.empty()) {
        ImGui::TextDisabled("(no docs found - check Editor/src/docs/)");
        ImGui::End();
        return;
    }

    // Two-column layout: 180px sidebar, rest = body.
    ImGui::BeginChild("##docs_nav", ImVec2(180, 0), true);
    for (const auto& t : list) {
        const bool sel = (s.docs_active == t.id);
        if (ImGui::Selectable(t.title.c_str(), sel))
            s.docs_active = t.id;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##docs_body", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const DocsTopic* active = nullptr;
    for (const auto& t : list) if (t.id == s.docs_active) { active = &t; break; }
    if (!active) active = &list.front();
    render_markdown(s, active->body);
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace Engine::editor
