#include "editor.h"

#include <imgui.h>

#include <TextEditor.h>       // vendored ImGuiColorTextEdit (BalazsJako)

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace Engine::editor {

namespace {
    namespace fs = std::filesystem;

    bool read_file(const fs::path& p, std::string& out) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    bool write_file(const fs::path& p, const std::string& s) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        return out.good();
    }

    // Forward decl - lync_docs() is the canonical map of identifier->docstring,
    // shared between the LanguageDefinition (hover tooltips) and the docs
    // strip (caret-following). Defined later in the same anon namespace.
    const std::unordered_map<std::string, std::string>& lync_docs();

    // ---- Lync language definition for ::TextEditor ---------------------------
    // C-like syntax: //, /* */, identifiers, numbers. Keywords cover both
    // language built-ins and the Zues plugin attribute / built-in helper
    // surface so they all colour consistently.
    const ::TextEditor::LanguageDefinition& lync_language_def() {
        static bool init = false;
        static ::TextEditor::LanguageDefinition def;
        if (init) return def;
        init = true;

        // Core Lync keywords + types + control flow.
        static const char* const keywords[] = {
            "def", "struct", "extern", "return", "if", "else",
            "while", "for", "do", "to", "in", "match",
            "true", "false", "null", "let", "mut", "own",
            "int", "float", "double", "char", "bool", "void", "string", "ptr",
        };
        for (auto* k : keywords) def.mKeywords.insert(k);

        // Zues plugin attributes + built-in helpers: highlight as known
        // identifiers AND wire each one's mDeclaration to its docstring -
        // TextEditor surfaces this as the hover tooltip when the user
        // mouses over the symbol. The docs strip above the editor uses
        // the same map (lync_docs() defined below).
        const auto& docs = lync_docs();
        for (const auto& [name, doc] : docs) {
            ::TextEditor::Identifier i;
            i.mDeclaration = doc;
            def.mIdentifiers.insert({name, i});
        }

        // Engine built-in component / math type names. Routed to the
        // TypeName slot so they render in the type-specific color (cool
        // sky blue), distinct from the function-name pale yellow.
        static const char* const engine_types[] = {
            "Transform2D", "Sprite", "RigidBody",
            "BoxCollider", "CircleCollider",
            "Vec2", "Vec3", "Color",
            "Entity",
        };
        for (auto* t : engine_types) {
            ::TextEditor::Identifier i;
            i.mDeclaration = std::string("(engine type) ") + t;
            def.mTypes.insert({t, i});
        }

        def.mTokenize = nullptr;   // fall back to the regex-based tokenizer
        def.mCommentStart       = "/*";
        def.mCommentEnd         = "*/";
        def.mSingleLineComment  = "//";
        def.mCaseSensitive      = true;
        def.mAutoIndentation    = true;
        def.mPreprocChar        = '#';
        def.mName               = "Lync";

        // Token regexes (ordered): numeric literals, strings, idents, punct.
        // Borrowed from the C/C++ definition shipped with ::TextEditor.
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "[ \\t]*//[^\\n]*",  ::TextEditor::PaletteIndex::Comment));
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "L?\\\"(\\\\.|[^\\\"])*\\\"",  ::TextEditor::PaletteIndex::String));
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "\\'\\\\?[^\\']\\'",  ::TextEditor::PaletteIndex::CharLiteral));
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fFlL]?",
                ::TextEditor::PaletteIndex::Number));
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "[a-zA-Z_][a-zA-Z0-9_]*",  ::TextEditor::PaletteIndex::Identifier));
        def.mTokenRegexStrings.push_back(
            std::make_pair<std::string, ::TextEditor::PaletteIndex>(
                "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]",
                ::TextEditor::PaletteIndex::Punctuation));
        return def;
    }

    // Lync palette: keep the original near-black TextEditor background
    // (which fits the rest of the editor) and replace just the syntax
    // accent slots with a JetBrains-inspired warm scheme - amber keywords,
    // muted blue numbers, sage green strings, soft amber for known idents
    // (Zues plugin helpers). Colors are ABGR (imgui ImU32 packing).
    const ::TextEditor::Palette& lync_palette() {
        auto rgba = [](unsigned r, unsigned g, unsigned b, unsigned a = 0xFF) -> ImU32 {
            return (a << 24) | (b << 16) | (g << 8) | r;
        };
        // VS Code Dark+ inspired palette (back from the Rider experiment).
        // Distinct hues per identifier kind:
        //   - Keywords:    cool blue
        //   - Functions:   pale yellow (lemon)
        //   - Types:       warm amber-orange  ← related to functions but
        //                                       slightly warmer/more
        //                                       saturated, NOT blue/teal
        //   - Variables:   sky blue
        //   - Numbers:     pale sage
        //   - Strings:     warm coral
        //   - Comments:    muted olive
        // The fn-yellow / type-amber pairing keeps both warm tones in the
        // same family so the gutter feels cohesive, while still letting
        // the eye separate call sites from type usages.
        static ::TextEditor::Palette p = {{
            rgba(0xD4, 0xD4, 0xD4),       // Default
            rgba(0x56, 0x9C, 0xD6),       // Keyword         - VS Code blue
            rgba(0xB5, 0xCE, 0xA8),       // Number          - pale sage
            rgba(0xCE, 0x91, 0x78),       // String          - warm coral
            rgba(0xCE, 0x91, 0x78),       // CharLiteral
            rgba(0xC0, 0xC0, 0xC0),       // Punctuation     - neutral gray
            rgba(0xC5, 0x86, 0xC0),       // Preprocessor    - magenta
            rgba(0x9C, 0xDC, 0xFE),       // Identifier      - sky blue (vars)
            rgba(0xDC, 0xDC, 0xAA),       // KnownIdentifier - pale lemon (fns)
            rgba(0xC5, 0x86, 0xC0),       // PreprocIdent    - magenta
            rgba(0x6A, 0x99, 0x55),       // Comment         - muted olive
            rgba(0x6A, 0x99, 0x55),       // MultiLine cmt   - muted olive
            0xff1e1e1e,                   // Background      - VS Code editor.bg
            0xffe0e0e0,                   // Cursor
            0xa0a06030,                   // Selection
            0xc00040ff,                   // ErrorMarker
            0x40f08000,                   // Breakpoint
            0xff858585,                   // LineNumber
            0x00000000,                   // CurrentLineFill
            0x00000000,                   // CurrentLineFillInactive
            0x00000000,                   // CurrentLineEdge
            // Rainbow brackets: cool-modern cycle.
            rgba(0xFF, 0xD7, 0x00),       // RainbowBracket0 - gold
            rgba(0xDA, 0x70, 0xD6),       // RainbowBracket1 - orchid
            rgba(0x17, 0x9F, 0xFF),       // RainbowBracket2 - bright blue
            rgba(0xFF, 0x66, 0x99),       // RainbowBracket3 - hot pink
            rgba(0x4E, 0xC9, 0xB0),       // RainbowBracket4 - teal
            // TypeName: warm amber-orange, sibling of function pale-lemon
            // but more saturated. Not blue, not teal - sits in the same
            // warm family as the existing fn / kw colors so types feel
            // part of the palette instead of a foreign accent.
            rgba(0xFF, 0xCB, 0x6B),       // TypeName        - warm amber
        }};
        return p;
    }

    // ---- Symbol docs lookup (caret-based, JetBrains-style "quick docs") ----
    // Maps a Lync identifier to a one-line description. Covers Lync built-in
    // helpers + the auto-injected per-component helpers + plugin attributes.
    // Falls back to "(no docs)" when the word isn't known. The docs strip
    // renders just above the editor body when the caret is on a known symbol.
    const std::unordered_map<std::string, std::string>& lync_docs() {
        static const std::unordered_map<std::string, std::string> m = {
            // Plugin attributes
            {"Component",        "[Component] - tag a struct as an ECS component (auto-emits AddX + QueryEachX)"},
            {"System",           "[System(\"Phase\", \"Domain\")] - register a function as an ECS system"},
            {"OnLoad",           "[OnLoad] - tag a fn as the project's load callback (eng, host)->int"},
            {"OnUpdate",         "[OnUpdate] - tag a fn as the project's per-frame update (eng, dt, user)->void"},
            {"OnUnload",         "[OnUnload] - tag a fn as the project's unload callback (eng)->int"},
            {"OnCollision",      "[OnCollision] - called when two physics bodies overlap (eng, a: EntityRef, b: EntityRef)->void"},
            {"OnTriggerEnter",   "[OnTriggerEnter] - called when `other` enters trigger on `self` (eng, self: EntityRef, other: EntityRef)->void"},
            {"OnTriggerExit",    "[OnTriggerExit] - called when `other` exits trigger on `self` (eng, self: EntityRef, other: EntityRef)->void"},
            {"Category",         "[Category(\"Path/Sub\")] - editor menu group for the Add Component picker"},
            // Built-in API. Entity-first APIs are documented in their UFCS form
            // (`e.Foo(...)` rewrites to `Foo(e, ...)` at compile time) since
            // that's the idiomatic way to call them.
            {"CreateEntity",          "CreateEntity() -> EntityRef : create a new entity, returns its handle"},
            {"SetTransform",          "e.SetTransform(x, y, rot, sx, sy) : write Transform2D  (== SetTransform(e, ...))"},
            {"SetTransformPosition",  "e.SetTransformPosition(x, y) : update position only  (== SetTransformPosition(e, ...))"},
            {"AddSpriteDefault",      "e.AddSpriteDefault(w, h, r, g, b, a) : white quad with tint  (== AddSpriteDefault(e, ...))"},
            {"IsKeyDown",             "IsKeyDown(key) -> int : 1 while held, 0 otherwise"},
            {"IsKeyPressed",          "IsKeyPressed(key) -> int : 1 on the frame the key is pressed"},
            {"LogInfo",               "LogInfo(msg: string) : route to engine console"},
            {"SetTimeout",            "SetTimeout(seconds: float, cb) -> int : fire cb once after seconds; returns handle"},
            {"SetInterval",           "SetInterval(seconds: float, cb) -> int : fire cb every seconds; returns handle"},
            {"CancelTimer",           "CancelTimer(handle: int) -> int : 1 if a live timer was cancelled, 0 otherwise"},
            {"RandomFloat",           "RandomFloat() -> float : uniform [0, 1)"},
            {"RandomRange",           "RandomRange(lo: float, hi: float) -> float : uniform [lo, hi)"},
            {"RandomInt",             "RandomInt(lo: int, hi: int) -> int : uniform [lo, hi] inclusive"},
            {"RandomSeed",            "RandomSeed(seed: int) : reseed the engine PRNG"},
            {"GetParent",             "e.GetParent() -> EntityRef : parent of e, or null EntityRef if e is a root"},
            {"GetFirstChild",         "e.GetFirstChild() -> EntityRef : head of e's child list, or null"},
            {"GetNextSibling",        "e.GetNextSibling() -> EntityRef : next sibling in parent's child list, or null"},
            {"ChildCount",            "e.ChildCount() -> int : number of direct children of e"},
            {"GetChild",              "e.GetChild(idx) -> EntityRef : idx-th child of e, or null EntityRef if out of range"},
            {"IsNull",                "IsNull(e: EntityRef) -> bool : true if e is the null EntityRef"},
            {"AddTextDefault",        "AddTextDefault(e, utf8, size_px, r, g, b, a) : attach a Text component to e (HUD label)"},
            {"SetText",               "SetText(e, utf8) : update an existing Text component's string"},
            {"SetTextColor",          "SetTextColor(e, r, g, b, a) : update an existing Text component's color"},
            // Template-form ECS API (type parameter T is a registered component).
            // Same UFCS rewrite applies: `e.Add<T>(v)` == `Add<T>(e, v)`.
            {"Add",    "e.Add<T>(T{...}) : attach component T to entity e"},
            {"Has",    "e.Has<T>() -> int : 1 if entity e has component T, else 0"},
            {"Get",    "e.Get<T>() -> ref T? : nullable borrow to component T on entity e (match-unwrap)"},
            {"Each",   "Each<T>(cb) : iterate every entity that has T; cb: (EntityRef, ref T, dt: float) -> void"},
            {"Remove", "e.Remove<T>() : detach component T from entity e"},
            // Legacy aliases
            {"zues_create_entity",    "(legacy) -> CreateEntity"},
            {"zues_set_transform",    "(legacy) -> SetTransform"},
            {"zues_log_info",         "(legacy) -> LogInfo"},
            // Language keywords - one-line reminders.
            {"def",     "def NAME(arg: type, ...) : RetType { ... }   - function decl"},
            {"struct",  "struct { field: type, ... }   - aggregate type"},
            {"extern",  "extern <header.h> { def name(...): ret; ... }   - declare C imports"},
            {"for",     "for (i: a to b) { ... }   - inclusive range loop"},
            {"if",      "if (cond) { ... } else { ... }"},
            {"return",  "return EXPR;"},
        };
        return m;
    }

    // ---- Autocomplete suggestion source --------------------------------------
    // All known symbols a Lync programmer can complete. Built from the same
    // sources as the docs map + tokeniser so there's one truth.
    const std::vector<std::string>& autocomplete_pool() {
        static std::vector<std::string> pool;
        if (!pool.empty()) return pool;
        // Keywords + types - kept in sync with lync_language_def() above.
        static const char* const seed[] = {
            "def", "struct", "extern", "return", "if", "else",
            "while", "for", "do", "to", "in", "match",
            "true", "false", "null", "let", "mut", "own",
            "int", "float", "double", "char", "bool", "void", "string", "ptr",
            // Engine-provided types. These live in the lync prelude
            // (zues_api.lync) which isn't under <project>/src, so the
            // file-harvest pass below misses them. Hardcoding them here
            // keeps `e: EntityRef`, `bullet: PrefabRef`, etc. completable
            // from a fresh project. Updates to the prelude struct list
            // should mirror here.
            "EntityRef", "PrefabRef", "SpriteRef", "TextureRef",
            "AudioRef", "FontRef",
            "Vec2", "Vec3", "Color",
            "Transform2D", "Sprite", "RigidBody",
            "BoxCollider", "CircleCollider",
            "KeyCodeT", "TextureHandle", "Singleton",
            "Component", "System", "OnLoad", "OnUpdate", "OnUnload",
            "OnCollision", "OnTriggerEnter", "OnTriggerExit", "Category",
            "CreateEntity", "DestroyEntity",
            "SetTransform", "SetTransformPosition",
            "AddSpriteDefault", "IsKeyDown", "IsKeyPressed",
            "LogInfo", "LogWarn", "LogError", "LogDebug",
            "ApplyImpulse", "ApplyForce", "SetVelocity",
            "SetBodyPosition", "WakeBody",
            "Instantiate", "InstantiatePrefab", "KeyCode",
            "SetTimeout", "SetInterval", "CancelTimer",
            "RandomFloat", "RandomRange", "RandomInt", "RandomSeed",
            "GetParent", "GetFirstChild", "GetNextSibling",
            "ChildCount", "GetChild", "IsNull",
            "AddTextDefault", "SetText", "SetTextColor",
            "UIAnchor", "Text",
            // Template-form ECS helpers. The <T> placeholder is NOT included
            // here so these short names appear in normal word-prefix completion.
            // When the user types "Add<" a separate template-mode path takes
            // over and shows registered component names (see template_trigger_at_cursor).
            "Add", "Has", "Get", "Each", "Remove",
        };
        for (auto* s : seed) pool.emplace_back(s);
        return pool;
    }

    // ---- Template-mode trigger detection ------------------------------------
    // If the text immediately before the caret ends with one of the five
    // template operators followed by '<', return the operator name and the
    // characters typed after '<' (the component-name prefix so far).
    // Returns false when the caret is not right after one of those patterns.
    //
    // E.g.  "Add<Hea|"   -> op="Add",    tpl_prefix="Hea"
    //       "Each<|"     -> op="Each",   tpl_prefix=""
    //       "Get<Transform2|" -> op="Get", tpl_prefix="Transform2"
    bool template_trigger_at_cursor(::TextEditor& ed,
                                    std::string& out_op,
                                    std::string& out_tpl_prefix,
                                    int*         out_tpl_start_col = nullptr) {
        const auto pos = ed.GetCursorPosition();
        if (pos.mLine < 0) return false;
        const auto lines = ed.GetTextLines();
        if (pos.mLine >= (int)lines.size()) return false;
        const std::string& line = lines[pos.mLine];
        // Clamp column to line length. After a file switch the editor's caret
        // can briefly point at a column past the new line's end - reading
        // line[col-1] there walks off the std::string buffer and crashes.
        int col = pos.mColumn;
        if (col <= 0) return false;
        if (col > (int)line.size()) col = (int)line.size();

        // Walk backward from the caret past identifier chars to find '<'.
        auto is_word = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        int tpl_start = col;  // column right after the '<'
        // The chars before the caret may be a partial component name.
        int scan = col - 1;
        while (scan >= 0 && is_word(line[scan])) --scan;
        // scan now points at the first non-word char or -1. Must be '<'.
        if (scan < 0 || line[scan] != '<') return false;
        tpl_start = scan + 1;
        const std::string tpl_pfx = line.substr(tpl_start, col - tpl_start);

        // The chars before '<' must be exactly one of the five operators.
        int op_end = scan;  // exclusive
        int op_start = op_end - 1;
        while (op_start >= 0 && is_word(line[op_start])) --op_start;
        ++op_start;  // now [op_start, op_end) is the word before '<'
        if (op_start >= op_end) return false;
        const std::string op = line.substr(op_start, op_end - op_start);
        if (op != "Add" && op != "Has" && op != "Get" &&
            op != "Each" && op != "Remove") return false;

        out_op        = op;
        out_tpl_prefix = tpl_pfx;
        if (out_tpl_start_col) *out_tpl_start_col = tpl_start;
        return true;
    }

    // Harvest registered component names from the world's component registry.
    // Returns a vector of ASCII name strings, filtered (case-insensitively)
    // against `prefix`. If the world pointer is null, falls back to an
    // empty list (the caller will show the placeholder entry).
    // TODO: if the world API changes to expose registered_component_count()
    // or similar, prefer that over iterate_component_types.
    std::vector<std::string> component_names_from_world(ecs::World* world,
                                                         const std::string& prefix) {
        std::vector<std::string> names;
        if (!world) return names;

        auto starts_with_ci = [](const std::string& s, const std::string& pfx) {
            if (pfx.size() > s.size()) return false;
            for (size_t i = 0; i < pfx.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(s[i]))
                    != std::tolower(static_cast<unsigned char>(pfx[i])))
                    return false;
            }
            return true;
        };

        world->iterate_component_types([&](ecs::ComponentId /*id*/,
                                           const ecs::ComponentType& t) {
            if (!t.name) return;
            const std::string n(t.name);
            if (prefix.empty() || starts_with_ci(n, prefix))
                names.push_back(n);
        });

        std::sort(names.begin(), names.end());
        return names;
    }

    // Refresh the project-wide symbol index for any .lync file whose mtime
    // changed (plus any new files). Cheap: a directory listing + per-file
    // regex scan; only re-scans on actual filesystem change. Called once
    // per autocomplete suggestion build, but the inner scan is gated on
    // mtime so steady-state cost is essentially zero.
    void refresh_project_symbols(EditorState& s) {
        if (s.project_dir.empty()) return;
        const std::filesystem::path src_dir =
            std::filesystem::path(s.project_dir) /
            (s.project_source_dir.empty() ? "src" : s.project_source_dir);
        std::error_code ec;
        if (!std::filesystem::exists(src_dir, ec)) return;

        // Collect on-disk file set + mtimes. Drop stale entries from the
        // index (file deleted) at the end.
        std::unordered_map<std::string, std::filesystem::file_time_type> seen;
        for (auto& it : std::filesystem::directory_iterator(src_dir, ec)) {
            if (!it.is_regular_file(ec)) continue;
            if (it.path().extension() != ".lync") continue;
            const std::string name = Engine::editor::path_str(it.path().filename());
            if (name.rfind("_zues_", 0) == 0)              continue;
            if (name.find(".__live.") != std::string::npos) continue;
            const std::string key = Engine::editor::path_str(it.path());
            const auto t = std::filesystem::last_write_time(it.path(), ec);
            seen[key] = t;
            auto prev = s.project_symbols.mtime.find(key);
            if (prev != s.project_symbols.mtime.end() && prev->second == t)
                continue;   // unchanged - reuse the cached scan

            // Re-scan: pull out struct names + def names + synthesise the
            // 5 per-component helpers (Add/Each/Has/Get/Remove).
            std::vector<std::string> names;
            std::vector<EditorState::LyncFileSym> locs;
            std::ifstream f(it.path());
            std::stringstream ss; ss << f.rdbuf();
            const std::string text = ss.str();
            static const std::regex st_re(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*struct\b)");
            static const std::regex fn_re(R"(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
            std::istringstream lss(text);
            std::string ln;
            int line_no = 0;
            while (std::getline(lss, ln)) {
                std::smatch m;
                if (std::regex_search(ln, m, fn_re)) {
                    names.push_back(m[1].str());
                    locs.push_back({m[1].str(), line_no, false, false, {}});
                } else if (std::regex_search(ln, m, st_re)) {
                    const std::string n = m[1].str();
                    names.push_back(n);
                    locs.push_back({n, line_no, true, false, {}});
                    const char* pfx[] = {"Add","Each","Has","Get","Remove","QueryEach"};
                    for (auto* p : pfx) {
                        names.push_back(std::string(p) + n);
                        locs.push_back({std::string(p) + n, line_no, false, true, n});
                    }
                }
                ++line_no;
            }
            s.project_symbols.by_file[key]      = std::move(names);
            s.project_symbols.by_file_locs[key] = std::move(locs);
            s.project_symbols.mtime[key]        = t;
            s.project_symbols.generation++;
        }
        // Drop deleted files from the index.
        std::vector<std::string> dead;
        for (auto& [k, _] : s.project_symbols.by_file)
            if (!seen.count(k)) dead.push_back(k);
        for (auto& k : dead) {
            s.project_symbols.by_file.erase(k);
            s.project_symbols.by_file_locs.erase(k);
            s.project_symbols.mtime.erase(k);
            s.project_symbols.generation++;
        }
    }

    // Re-set the doc's TextEditor language def so user-declared struct +
    // def names render in the KnownIdentifier color (soft amber). Cheap
    // because each doc only re-applies when project_symbols.generation
    // moves - usually once per file save.
    // Forward declarations for hover-tooltip signature builders defined
    // alongside the goto-def helpers further down in this anon namespace.
    std::string format_func_signature(const EditorState::LyncFuncSymbol& fn);
    std::string format_struct_signature(const EditorState::LyncStructSymbol& st);

    void apply_live_language_def(EditorState& s, EditorState::LyncDoc& d) {
        if (!d.editor) return;
        if (d.applied_symbol_generation == s.project_symbols.generation) return;
        ::TextEditor::LanguageDefinition def = lync_language_def();
        // Project struct types -> mTypes (cool sky blue, type slot).
        // Project function names -> mIdentifiers (pale yellow, fn slot).
        // Different colors so the eye can immediately tell call sites
        // from type usages at a glance.
        if (s.project_symbols.rich_loaded) {
            for (const auto& st : s.project_symbols.structs) {
                if (def.mTypes.count(st.name) || def.mIdentifiers.count(st.name))
                    continue;
                ::TextEditor::Identifier id;
                id.mDeclaration = format_struct_signature(st);
                def.mTypes[st.name] = id;
                // Synthesise hover docs for the auto-generated component
                // helpers so AddX / EachX / GetX show up in the tooltip
                // with the right component name.
                bool is_component = false;
                for (const auto& a : st.attrs)
                    if (a == "Component") { is_component = true; break; }
                if (is_component) {
                    auto add = [&](const std::string& n, const std::string& doc) {
                        if (def.mIdentifiers.count(n) || def.mTypes.count(n)) return;
                        ::TextEditor::Identifier h;
                        h.mDeclaration = doc;
                        def.mIdentifiers[n] = h;
                    };
                    add("Add"      + st.name, "Add"      + st.name + "(e: EntityRef, ...) — attach " + st.name);
                    add("Each"     + st.name, "Each"     + st.name + "(cb) — iterate every entity with " + st.name);
                    add("Has"      + st.name, "Has"      + st.name + "(e: EntityRef) -> bool");
                    add("Get"      + st.name, "Get"      + st.name + "(e: EntityRef) -> " + st.name + "?");
                    add("Remove"   + st.name, "Remove"   + st.name + "(e: EntityRef) — detach " + st.name);
                    add("QueryEach"+ st.name, "QueryEach"+ st.name + "(cb) — legacy alias of Each" + st.name);
                }
            }
            for (const auto& fn : s.project_symbols.funcs) {
                if (def.mIdentifiers.count(fn.name) || def.mTypes.count(fn.name))
                    continue;
                ::TextEditor::Identifier id;
                id.mDeclaration = format_func_signature(fn);
                def.mIdentifiers[fn.name] = id;
            }
        }
        // Per-file fallback when rich symbols aren't loaded yet (project
        // hasn't built since editor start). Names land in mIdentifiers
        // without a type/fn split; they'll re-classify on next build.
        for (const auto& [path, names] : s.project_symbols.by_file) {
            for (const auto& n : names) {
                if (def.mIdentifiers.count(n) || def.mTypes.count(n)) continue;
                ::TextEditor::Identifier id;
                id.mDeclaration = "(project)";
                def.mIdentifiers[n] = id;
            }
        }
        d.editor->SetLanguageDefinition(def);
        d.applied_symbol_generation = s.project_symbols.generation;
    }

    // Build a per-file dynamic suggestion list: identifiers parsed out of
    // the doc's text (variable / function names) + per-component synthesised
    // helpers (AddX / QueryEachX) discovered by scanning [Component] decls
    // + project-wide symbols from every .lync in src/. Filtered against
    // `prefix`. The whole-project scan is gated on mtime so it's free in
    // steady-state.
    std::vector<std::string> autocomplete_suggestions(EditorState& s,
                                                       ::TextEditor& ed,
                                                       const std::string& prefix) {
        std::vector<std::string> out;
        if (prefix.empty()) return out;

        auto starts_with_ci = [](const std::string& s, const std::string& pfx) {
            if (pfx.size() > s.size()) return false;
            for (size_t i = 0; i < pfx.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(s[i]))
                    != std::tolower(static_cast<unsigned char>(pfx[i])))
                    return false;
            }
            return true;
        };
        // Subsequence match: every char of `q` appears in `n`, in order,
        // case-insensitively. e.g. "gtE" matches "getEntity". Only used
        // as a fallback when starts_with_ci fails — subseq matches
        // score lower so prefix matches always sort first. Skipped when
        // the query is a single char (too noisy).
        auto subseq_match_ci = [](const std::string& n, const std::string& q) {
            if (q.size() < 2)         return false;
            if (q.size() > n.size()) return false;
            size_t qi = 0;
            for (size_t i = 0; i < n.size() && qi < q.size(); ++i) {
                if (std::tolower((unsigned char)n[i])
                        == std::tolower((unsigned char)q[qi]))
                    ++qi;
            }
            return qi == q.size();
        };
        // Subseq score: lower base + a small bonus per consecutive pair
        // matched in order (so "getE" ranks higher than "gE" for
        // "getEntity"). Returns -1 when no match.
        auto subseq_score = [&](const std::string& n, const std::string& q) -> int {
            if (!subseq_match_ci(n, q)) return -1;
            int bonus = 0;
            size_t qi = 0;
            int last_match = -2;
            for (int i = 0; i < (int)n.size() && qi < q.size(); ++i) {
                if (std::tolower((unsigned char)n[i])
                        == std::tolower((unsigned char)q[qi])) {
                    if (i == last_match + 1) bonus += 2;
                    last_match = i;
                    ++qi;
                }
            }
            return bonus;
        };

        std::unordered_map<std::string, int> seen;  // name -> score (higher first)

        // 1) Static pool: keywords, types, built-in API. Highest score.
        for (const auto& kw : autocomplete_pool()) {
            if (kw == prefix) continue;
            if (starts_with_ci(kw, prefix))      seen[kw] = 100;
            else {
                const int sb = subseq_score(kw, prefix);
                if (sb >= 0) {
                    auto e = seen.find(kw);
                    const int sc = 50 + sb;
                    if (e == seen.end() || e->second < sc) seen[kw] = sc;
                }
            }
        }

        // 2) Active doc: in-file identifiers + per-component synthesised
        // helpers (Add/Each/Has/Get/Remove + legacy QueryEach).
        const auto lines = ed.GetTextLines();
        static const std::regex comp_re(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*struct\b)");
        static const std::regex ident_re(R"([A-Za-z_][A-Za-z0-9_]{1,})");
        for (const auto& l : lines) {
            std::smatch m;
            if (std::regex_search(l, m, comp_re)) {
                const std::string name = m[1].str();
                const char* prefixes[] = {"Add", "Each", "Has", "Get", "Remove", "QueryEach"};
                for (auto* p : prefixes) {
                    const std::string sym = std::string(p) + name;
                    if (sym == prefix) continue;
                    if (starts_with_ci(sym, prefix))     seen[sym] = 90;
                    else {
                        const int sb = subseq_score(sym, prefix);
                        if (sb >= 0) {
                            auto e = seen.find(sym);
                            const int sc = 45 + sb;
                            if (e == seen.end() || e->second < sc) seen[sym] = sc;
                        }
                    }
                }
            }
            for (auto it = std::sregex_iterator(l.begin(), l.end(), ident_re);
                     it != std::sregex_iterator(); ++it) {
                const std::string id = it->str();
                if (id == prefix) continue;
                if (starts_with_ci(id, prefix)) {
                    auto e = seen.find(id);
                    if (e == seen.end()) seen[id] = 50;
                } else {
                    const int sb = subseq_score(id, prefix);
                    if (sb >= 0) {
                        auto e = seen.find(id);
                        const int sc = 25 + sb;
                        if (e == seen.end() || e->second < sc) seen[id] = sc;
                    }
                }
            }
        }

        // 3) Project-wide pool: every .lync file in src/ contributes its
        // public identifiers (struct + def names + the 5 per-component
        // helpers each one synthesises). Refreshed lazily per file mtime.
        refresh_project_symbols(s);
        for (const auto& [path, names] : s.project_symbols.by_file) {
            for (const auto& n : names) {
                if (n == prefix) continue;
                if (starts_with_ci(n, prefix)) {
                    auto e = seen.find(n);
                    if (e == seen.end()) seen[n] = 70;
                } else {
                    const int sb = subseq_score(n, prefix);
                    if (sb >= 0) {
                        auto e = seen.find(n);
                        const int sc = 35 + sb;
                        if (e == seen.end() || e->second < sc) seen[n] = sc;
                    }
                }
            }
        }

        out.reserve(seen.size());
        for (auto& [k, _] : seen) out.push_back(k);
        std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
            const int sa = seen[a], sb = seen[b];
            if (sa != sb) return sa > sb;
            return a < b;
        });
        if (out.size() > 12) out.resize(12);    // popup cap
        return out;
    }

    // The "prefix being typed" - chars from the last word-boundary up to but
    // not past the caret. Different from word_at_cursor which also walks
    // forward into the word. Used for autocomplete filtering: as the user
    // types "Crea|teEntity", the prefix is "Crea", not "CreateEntity".
    std::string prefix_at_cursor(::TextEditor& ed, int* out_word_start_col = nullptr) {
        const auto pos = ed.GetCursorPosition();
        if (pos.mLine < 0) return {};
        auto lines = ed.GetTextLines();
        if (pos.mLine >= (int)lines.size()) return {};
        const std::string& line = lines[pos.mLine];
        // Convert the caret's VISUAL column to a character index (tabs in
        // the line make these diverge). Walk word chars in char-space,
        // then convert the resulting char index BACK to a visual column
        // for the caller (so they can pass it to TextEditor::Coordinates
        // without a double-conversion bug).
        const int col_idx = ed.VisualColumnToCharacterIndex(pos.mLine, pos.mColumn);
        if (col_idx <= 0 || col_idx > (int)line.size()) return {};
        auto is_word = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        if (!is_word(line[col_idx - 1])) return {};
        int start_idx = col_idx - 1;
        while (start_idx > 0 && is_word(line[start_idx - 1])) --start_idx;
        if (out_word_start_col) {
            *out_word_start_col =
                ed.CharacterIndexToVisualColumn(pos.mLine, start_idx);
        }
        return line.substr(start_idx, col_idx - start_idx);
    }

    // Replace the [start, end) range of text on the caret's line with `text`
    // and put the caret at the end of the inserted text. Uses the clipboard
    // round-trip (TextEditor lacks a public InsertText). Saves/restores the
    // user's clipboard so we don't trash it.
    void replace_range_with(::TextEditor& ed, int line, int start, int end,
                             const std::string& text) {
        // Use TextEditor's native Delete + InsertText. The previous clipboard
        // round-trip occasionally exploded on Windows when the OS clipboard
        // was held by another app ("Failed to convert clipboard to string:
        // Element not found" - GLFW error 65545).
        ed.SetSelection({line, start}, {line, end},
                         ::TextEditor::SelectionMode::Normal);
        ed.Delete();
        ed.InsertText(text);
    }

    // Insert `text` at the caret's current position, leaving the caret AT
    // the end of the insertion. TextEditor's InsertText already does this.
    void insert_at_caret(::TextEditor& ed, const std::string& text) {
        ed.InsertText(text);
    }

    // ---- Auto-close braces / smart Enter -----------------------------------
    // After Render(), inspect the last line edit and apply VS-style helpers:
    //   - User typed '{' and the next char isn't '}' -> insert '}' AFTER
    //     the caret, leave caret immediately after '{'. So `{|` becomes
    //     `{|}`.
    //   - User pressed Enter inside `{|}` -> after Render the editor has
    //     produced `{\n|}` with caret on a fresh line. Expand to
    //     `{\n  IND|\n}` with caret indented one level past the opening line.
    //
    // Implementation tracks (prev_line_text, prev_caret) per doc across
    // frames so we can diff what the editor just did.
    struct EditTrace {
        int         prev_caret_line  = -1;
        int         prev_caret_col   = -1;
        std::string prev_line_text;     // text of prev_caret_line BEFORE Render
        std::string prev_above_text;    // text of prev_caret_line - 1 BEFORE
    };

    // Returns leading whitespace (spaces + tabs) of `s`.
    std::string leading_indent(const std::string& s) {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return s.substr(0, i);
    }

    // Map an opening bracket char to its closer; returns 0 for non-brackets.
    char closer_for(char open) {
        switch (open) {
            case '{': return '}';
            case '(': return ')';
            case '[': return ']';
            case '"': return '"';
            case '\'': return '\'';
            default:  return 0;
        }
    }
    char opener_for(char close) {
        switch (close) {
            case '}': return '{';
            case ')': return '(';
            case ']': return '[';
            case '"': return '"';
            case '\'': return '\'';
            default:  return 0;
        }
    }

    // Apply auto-close + smart Enter using the trace from before Render
    // and the editor's current state after Render.
    void apply_brace_helpers(::TextEditor& ed, const EditTrace& tr,
                              bool auto_close_enabled) {
        if (tr.prev_caret_line < 0) return;
        const auto pos = ed.GetCursorPosition();
        auto lines = ed.GetTextLines();
        if (pos.mLine >= (int)lines.size()) return;

        // ---- Case A: opening bracket just typed on the same line ---------
        // Editor inserted '{' / '(' / '[' / '"' / '\'' at prev caret ->
        // caret moved one column right. Insert the matching closer right
        // after, leaving caret between the pair.
        //
        // Heuristics that suppress the auto-close (match VS Code / JetBrains):
        //   - Quote (" or '): preceding char is alphanumeric or _.
        //     Avoids `don't` / `it's` becoming `don''t`. Also avoids
        //     wrapping when the quote is part of an identifier-adjacent
        //     literal like `foo"bar` (rare, but cheap to skip).
        //   - Bracket (( [ {): the immediate NEXT char is a word char.
        //     Typing `(` before `foo` typically means "wrap into a call";
        //     auto-closing turns `foo` into `()foo` which is rarely the
        //     intent. The user can type the closer manually when they
        //     want it.
        //   - The next char is already the matching closer (handled by
        //     Case A.5's smart-skip path for `()`, `[]`, `{}`). For
        //     symmetric pairs (`"`, `'`) we skip only when smart-skip
        //     can verify the closer was just auto-inserted; otherwise
        //     we let auto-close run so the user can wrap into an
        //     existing string.
        auto is_word_char = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        if (auto_close_enabled &&
            pos.mLine == tr.prev_caret_line &&
            pos.mColumn == tr.prev_caret_col + 1) {
            const std::string& line = lines[pos.mLine];
            if (pos.mColumn > 0 && pos.mColumn <= (int)line.size()) {
                const char typed = line[pos.mColumn - 1];
                const char close = closer_for(typed);
                if (close) {
                    const bool is_quote = (typed == '"' || typed == '\'');
                    // Suppress when the preceding char is a word char
                    // (contractions, identifier-adjacent quotes).
                    bool suppress = false;
                    if (is_quote && pos.mColumn >= 2 &&
                            is_word_char(line[pos.mColumn - 2])) {
                        suppress = true;
                    }
                    // Suppress for ( [ { when the next char is a word
                    // char (typing `(` before `foo` shouldn't wrap).
                    if (!is_quote && !suppress &&
                            pos.mColumn < (int)line.size() &&
                            is_word_char(line[pos.mColumn])) {
                        suppress = true;
                    }
                    // Existing matching closer right after the typed
                    // char — let smart-skip (Case A.5) handle it.
                    const bool next_is_close =
                        pos.mColumn < (int)line.size() &&
                        line[pos.mColumn] == close;
                    if (!suppress && !next_is_close) {
                        insert_at_caret(ed, std::string(1, close));
                        ed.SetCursorPosition({pos.mLine, pos.mColumn});
                    }
                    if (!suppress) return;
                }
            }
        }

        // ---- Case A.5: closing bracket typed where one already sits ------
        // Smart skip: user typed ')' but caret was already in front of ')'.
        // The editor inserted a duplicate '))'. Detect and remove the dupe.
        // Only fires for asymmetric pairs (() [] {}); symmetric pairs
        // ("" '') need a tighter check than "matching char to the right"
        // because two quotes in a row legitimately mean "open a new
        // string immediately after the previous one".
        if (auto_close_enabled &&
            pos.mLine == tr.prev_caret_line &&
            pos.mColumn == tr.prev_caret_col + 1) {
            const std::string& line = lines[pos.mLine];
            if (pos.mColumn > 0 && pos.mColumn < (int)line.size()) {
                const char typed = line[pos.mColumn - 1];
                const bool is_asym_closer =
                    (typed == ')' || typed == ']' || typed == '}');
                if (is_asym_closer && line[pos.mColumn] == typed) {
                    ed.SetSelection({pos.mLine, pos.mColumn},
                                     {pos.mLine, pos.mColumn + 1},
                                     ::TextEditor::SelectionMode::Normal);
                    ed.Delete();
                    return;
                }
            }
        }

        // ---- Case A.6: auto-de-indent when '}' is the only non-whitespace
        // typed on the line. Editor's auto-indent keeps the previous line's
        // indent on Enter, so a typed `}` lands at +1 indent. Snap it back
        // to the indent of the matching '{'. Same for ')' and ']'.
        if (pos.mLine == tr.prev_caret_line &&
            pos.mColumn == tr.prev_caret_col + 1) {
            const std::string& line = lines[pos.mLine];
            if (pos.mColumn > 0 && pos.mColumn <= (int)line.size()) {
                const char typed = line[pos.mColumn - 1];
                const char open  = opener_for(typed);
                if (open) {
                    // Confirm everything BEFORE the typed char on this line
                    // is whitespace (so the user's intent is "close-only").
                    bool ws_only = true;
                    for (int ci = 0; ci < pos.mColumn - 1; ++ci) {
                        if (line[ci] != ' ' && line[ci] != '\t') { ws_only = false; break; }
                    }
                    if (ws_only) {
                        // Walk upward to find the matching opener and grab
                        // its line's leading whitespace.
                        int depth = 1;
                        std::string match_indent;
                        bool found = false;
                        for (int li = pos.mLine - 1; li >= 0 && !found; --li) {
                            const std::string& ln = lines[li];
                            for (int ci = (int)ln.size() - 1; ci >= 0; --ci) {
                                if      (ln[ci] == typed) ++depth;
                                else if (ln[ci] == open)  {
                                    --depth;
                                    if (depth == 0) {
                                        match_indent = leading_indent(ln);
                                        found = true; break;
                                    }
                                }
                            }
                        }
                        if (found) {
                            // Replace [0, pos.mColumn-1) (the leading
                            // whitespace) with match_indent, then re-position
                            // caret right after the closer.
                            ed.SetSelection({pos.mLine, 0},
                                             {pos.mLine, pos.mColumn - 1},
                                             ::TextEditor::SelectionMode::Normal);
                            ed.Delete();
                            ed.InsertText(match_indent);
                            ed.SetCursorPosition({pos.mLine,
                                                   (int)match_indent.size() + 1});
                            return;
                        }
                    }
                }
            }
        }

        // ---- Case B / C: Enter after `{` --------------------------------
        // TextEditor has mAutoIndentation = true; after the user presses
        // Enter it copies the previous line's leading whitespace to the
        // new line and parks the caret right after the auto-indent (NOT
        // at column 0). Both cases below assume that.
        //
        // Case B: prev caret was between `{` and `}` (auto-pair shape).
        //         After auto-indent the new line reads `<base>}` with
        //         caret at col=base.size(). We want to end up with:
        //             L:   <base>{
        //             L+1: <base>    <caret>
        //             L+2: <base>}
        //
        // Case C: prev line ENDS in `{` (no `}` after). After auto-indent
        //         the new line is just whitespace; we just push the
        //         caret one level deeper:
        //             L:   <base>{
        //             L+1: <base>    <caret>
        //
        // Both fire when:
        //   - caret moved exactly one line down
        //   - the new line is whitespace-only up to the caret column
        //   - prev line's last non-ws char is `{`
        if (pos.mLine == tr.prev_caret_line + 1) {
            // Verify line up to caret is whitespace only - that's how we
            // know we're sitting on TextEditor's auto-indent and not in
            // the middle of code that was carried over.
            const std::string& cur = lines[pos.mLine];
            int caret_idx = pos.mColumn;
            if (caret_idx > (int)cur.size()) caret_idx = (int)cur.size();
            bool ws_only = true;
            for (int i = 0; i < caret_idx; ++i) {
                if (cur[i] != ' ' && cur[i] != '\t') { ws_only = false; break; }
            }
            if (ws_only) {
                // Was the char IMMEDIATELY LEFT of the caret a `{`?
                // The previous logic checked "last non-ws of prev_line_text"
                // which broke for `{|}` (the line ends in `}`, not `{`,
                // so smart-Enter never fired and the user got the bare
                // TextEditor split with the `}` orphaned at column 0).
                // Walk back over trailing whitespace from the original
                // caret position to find the real anchor char.
                const std::string& prev = tr.prev_line_text;
                int anchor = std::min(tr.prev_caret_col, (int)prev.size()) - 1;
                while (anchor >= 0 &&
                       (prev[anchor] == ' ' || prev[anchor] == '\t')) --anchor;
                const bool ends_in_brace = (anchor >= 0 && prev[anchor] == '{');
                if (ends_in_brace) {
                    const std::string base = leading_indent(prev);
                    // Case B: was the char IMMEDIATELY RIGHT of the
                    // caret a `}`? After the split that closer is now
                    // sitting on the new line at the caret column,
                    // typically as the FIRST non-ws char of cur (because
                    // there's nothing else after it). Check both prev's
                    // post-caret slot AND the new line, so we handle
                    // both the auto-paired `{|}` case and the user-
                    // typed `{ |  }` case.
                    bool is_case_b = false;
                    if (tr.prev_caret_col >= 0 &&
                            tr.prev_caret_col < (int)prev.size()) {
                        int after = tr.prev_caret_col;
                        while (after < (int)prev.size() &&
                               (prev[after] == ' ' || prev[after] == '\t')) ++after;
                        if (after < (int)prev.size() && prev[after] == '}')
                            is_case_b = true;
                    }
                    if (!is_case_b && caret_idx < (int)cur.size() &&
                            cur[caret_idx] == '}') {
                        is_case_b = true;
                    }
                    if (is_case_b) {
                        // Insert "    \n<base>" at caret. After the
                        // insertion line N+1 reads "<base>    " (caret
                        // here at col base+4) and line N+2 reads
                        // "<base>}" (the `}` got pushed down with the
                        // newline split).
                        insert_at_caret(ed, "    \n" + base);
                        ed.SetCursorPosition({pos.mLine, (int)(base.size() + 4)});
                    } else {
                        // Case C: just push the caret one level deeper.
                        insert_at_caret(ed, "    ");
                    }
                    return;
                }
            }
        }

        // ---- Case D: Backspace deleted an opener; symmetric pair ---------
        // User typed `(` (auto-closed to `()`), then immediately Backspace.
        // After Render the caret moved one column LEFT and the next char
        // is the dangling closer. Delete it too so the pair vanishes
        // together — feels like the auto-close never happened.
        if (auto_close_enabled &&
            pos.mLine == tr.prev_caret_line &&
            pos.mColumn == tr.prev_caret_col - 1) {
            const std::string& prev = tr.prev_line_text;
            const int pc = tr.prev_caret_col;
            // What used to be at prev_caret_col-1 (now deleted)?
            if (pc > 0 && pc <= (int)prev.size()) {
                const char was = prev[pc - 1];
                const char close = closer_for(was);
                // Current line reflects the post-delete state. Caret sits
                // where the opener was; the closer should still be there.
                const std::string& line_now = lines[pos.mLine];
                if (close && pos.mColumn < (int)line_now.size() &&
                        line_now[pos.mColumn] == close) {
                    ed.SetSelection({pos.mLine, pos.mColumn},
                                     {pos.mLine, pos.mColumn + 1},
                                     ::TextEditor::SelectionMode::Normal);
                    ed.Delete();
                }
            }
        }

        // ---- Case E: Smart backspace - whole-indent + line-join ----------
        // User hit Backspace at the start of a line that contained ONLY
        // whitespace before the caret. After Render TextEditor has eaten
        // one char. Replace that with: delete the rest of the leading
        // whitespace AND join with the previous line's end.
        if (pos.mLine == tr.prev_caret_line &&
            pos.mColumn == tr.prev_caret_col - 1 &&
            tr.prev_caret_col > 0) {
            const std::string& prev = tr.prev_line_text;
            // Was the prev line all-whitespace up to the caret column?
            bool all_ws = true;
            for (int i = 0; i < tr.prev_caret_col && all_ws; ++i) {
                if (prev[i] != ' ' && prev[i] != '\t') all_ws = false;
            }
            if (all_ws && tr.prev_caret_col > 1) {
                // Select from col 0 of THIS line back to the end of the
                // previous line, and Delete - that joins the lines.
                if (pos.mLine > 0) {
                    const auto above_len = (int)lines[pos.mLine - 1].size();
                    ed.SetSelection({pos.mLine - 1, above_len},
                                     {pos.mLine,     pos.mColumn},
                                     ::TextEditor::SelectionMode::Normal);
                    ed.Delete();
                    // Caret lands at the join point - that's where we want it.
                }
            }
        }
    }

    // ---- Attribute-snippet expansion ----------------------------------------
    // Called once per frame, after Render() and apply_brace_helpers().
    //
    // Trigger condition (all must hold):
    //   1. The caret just moved to a NEW line this frame
    //      (current line != snippet_prev_caret_line).
    //   2. The line the caret just moved to is empty (or only whitespace).
    //   3. The line immediately above matches a recognised hook attribute.
    //   4. The line above is NOT inside a string literal or block comment
    //      (heuristic: the trimmed line must start with '[').
    //
    // On a match the function:
    //   - Builds the snippet body with the attribute's indentation prepended
    //     to every line of the template.
    //   - Deletes the empty current line (which was created by the Enter press)
    //     by inserting text that replaces it.
    //   - Places the caret at the $CURSOR position inside the body.
    //
    // For [System(...)], the placeholder function name is "system" and is left
    // as-is for the user to overwrite; TextEditor's SetSelection is used to
    // pre-select it so a single keypress replaces it (if selection doesn't
    // work in the caller's flow, the user can just double-click + type).
    //
    // Updates d.snippet_prev_caret_line at the END so subsequent frames do not
    // re-trigger on the same line.
    // Given a trimmed line content (e.g. `[OnLoad]` or `[System("PreUpdate","Game")]`),
    // return a one-line preview of what the snippet expander would insert,
    // or an empty string if the line isn't a recognized attribute. Used by
    // the inline preview popup so the user sees what Tab/Enter will do.
    const char* attribute_preview_signature(const std::string& trimmed) {
        // Strip trailing whitespace from the candidate.
        std::string a = trimmed;
        while (!a.empty() && (a.back() == ' ' || a.back() == '\t'))
            a.pop_back();
        if      (a == "[OnLoad]")          return "def on_load(eng: ptr, host: ptr): int { ... return 0; }";
        else if (a == "[OnUnload]")        return "def on_unload(eng: ptr): int { ... return 0; }";
        else if (a == "[OnUpdate]")        return "def on_update(eng: ptr, dt: float, user: ptr): void { ... }";
        else if (a == "[OnCollision]")     return "def on_collision(eng: ptr, a: EntityRef, b: EntityRef): void { ... }";
        else if (a == "[OnTriggerEnter]")  return "def on_trigger_enter(eng: ptr, self: EntityRef, other: EntityRef): void { ... }";
        else if (a == "[OnTriggerExit]")   return "def on_trigger_exit(eng: ptr, self: EntityRef, other: EntityRef): void { ... }";
        else if (a.size() > 8 && a.rfind("[System(", 0) == 0 && a.back() == ']')
            return "def system(eng: ptr, dt: float, user: ptr): void { ... }";
        return nullptr;
    }

    void apply_attribute_snippet(::TextEditor& ed,
                                  EditorState::LyncDoc& d) {
        const auto pos = ed.GetCursorPosition();
        const int cur_line = pos.mLine;

        // Always update the tracker at the end; capture old value first.
        const int prev_line = d.snippet_prev_caret_line;

        // Guard: only fire on the frame the caret moved to a new line.
        // Also skip the very first frame (prev_line == -1).
        auto update_and_return = [&]() {
            d.snippet_prev_caret_line = cur_line;
        };

        if (prev_line < 0 || cur_line == prev_line) {
            update_and_return();
            return;
        }

        const auto lines = ed.GetTextLines();
        if (cur_line <= 0 || cur_line >= (int)lines.size()) {
            update_and_return();
            return;
        }

        // Current line must be empty (only whitespace).
        const std::string& cur_text = lines[cur_line];
        for (char c : cur_text) {
            if (c != ' ' && c != '\t') { update_and_return(); return; }
        }

        // Line above is the attribute line.
        const std::string& above = lines[cur_line - 1];

        // Strip leading whitespace to get the trimmed attribute text and
        // also record the indentation to mirror onto the snippet.
        size_t indent_end = 0;
        while (indent_end < above.size() &&
               (above[indent_end] == ' ' || above[indent_end] == '\t'))
            ++indent_end;
        const std::string indent = above.substr(0, indent_end);
        const std::string trimmed = above.substr(indent_end);

        // Heuristic string/comment guard: a line inside a comment/string won't
        // start with '[' once trimmed. This is not foolproof (multiline strings
        // aren't tracked here) but covers the common cases without needing
        // full parser state.
        if (trimmed.empty() || trimmed[0] != '[') {
            update_and_return();
            return;
        }

        // --- Match attribute and select template ----------------------------
        // Returns the snippet lines (WITHOUT leading indent; we add it below).
        // $CURSOR_LINE / $CURSOR_COL indicate where to place the caret.
        // For System, name_sel_{start,len} mark the placeholder to select.
        struct SnippetSpec {
            std::vector<std::string> lines;  // template lines
            int cursor_line = 0;             // 0-based within snippet
            int cursor_col  = 0;             // column within that line
            int name_sel_start = -1;         // col of name to pre-select (-1 = none)
            int name_sel_len   = 0;
        };

        // Helper: build a simple snippet from a multiline literal.
        // Uses $CURSOR to mark where the caret ends up.
        // Returns false if no match found (out left unchanged).
        auto make_spec = [](const std::vector<std::string>& tpl_lines,
                            SnippetSpec& out) {
            out.lines.clear();
            out.cursor_line = 0;
            out.cursor_col  = 0;
            for (int i = 0; i < (int)tpl_lines.size(); ++i) {
                const std::string& t = tpl_lines[i];
                const size_t cp = t.find("$CURSOR");
                if (cp != std::string::npos) {
                    out.cursor_line = i;
                    out.cursor_col  = (int)cp;
                    // Strip the $CURSOR marker from the inserted text.
                    std::string clean = t.substr(0, cp) + t.substr(cp + 7);
                    out.lines.push_back(clean);
                } else {
                    out.lines.push_back(t);
                }
            }
        };

        SnippetSpec spec;
        bool matched = false;

        // Exact-match attributes (trimmed, strip trailing whitespace too).
        std::string attr = trimmed;
        while (!attr.empty() && (attr.back() == ' ' || attr.back() == '\t'))
            attr.pop_back();

        if (attr == "[OnLoad]") {
            make_spec({
                "def on_load(eng: ptr, host: ptr): int {",
                "    $CURSOR",
                "    return 0;",
                "}"
            }, spec);
            matched = true;
        } else if (attr == "[OnUnload]") {
            make_spec({
                "def on_unload(eng: ptr): int {",
                "    $CURSOR",
                "    return 0;",
                "}"
            }, spec);
            matched = true;
        } else if (attr == "[OnUpdate]") {
            make_spec({
                "def on_update(eng: ptr, dt: float, user: ptr): void {",
                "    $CURSOR",
                "}"
            }, spec);
            matched = true;
        } else if (attr == "[OnCollision]") {
            make_spec({
                "def on_collision(eng: ptr, a: EntityRef, b: EntityRef): void {",
                "    $CURSOR",
                "}"
            }, spec);
            matched = true;
        } else if (attr == "[OnTriggerEnter]") {
            make_spec({
                "def on_trigger_enter(eng: ptr, self: EntityRef, other: EntityRef): void {",
                "    $CURSOR",
                "}"
            }, spec);
            matched = true;
        } else if (attr == "[OnTriggerExit]") {
            make_spec({
                "def on_trigger_exit(eng: ptr, self: EntityRef, other: EntityRef): void {",
                "    $CURSOR",
                "}"
            }, spec);
            matched = true;
        } else if (attr.size() > 8 &&
                   attr.rfind("[System(", 0) == 0 &&
                   attr.back() == ']') {
            // [System("Phase", "Domain")] or any [System(...)] variant.
            // Placeholder function name is "system" - pre-selected for
            // immediate replacement.
            const std::string placeholder = "system";
            make_spec({
                "def " + placeholder + "(eng: ptr, dt: float, user: ptr): void {",
                "    $CURSOR",
                "}"
            }, spec);
            // Mark the "system" name for selection (starts at col 4 on line 0).
            spec.name_sel_start = 4;
            spec.name_sel_len   = (int)placeholder.size();
            matched = true;
        }

        if (!matched) {
            update_and_return();
            return;
        }

        // --- Build the text to insert on the current (empty) line -----------
        // We insert all snippet lines joined by '\n', each prefixed with
        // the attribute's indentation. The current line is currently empty
        // (caret is at its start, col 0), so we just insert - TextEditor
        // will push subsequent content down correctly.
        std::string insertion;
        for (int i = 0; i < (int)spec.lines.size(); ++i) {
            if (i > 0) insertion += '\n';
            insertion += indent + spec.lines[i];
        }

        // Use insert_at_caret which goes through the clipboard round-trip
        // (same mechanism as the rest of this file uses for programmatic edits).
        insert_at_caret(ed, insertion);

        // --- Place caret at $CURSOR ----------------------------------------
        const int target_line = cur_line + spec.cursor_line;
        const int target_col  = (int)indent.size() + spec.cursor_col;
        ed.SetCursorPosition({target_line, target_col});

        // --- Pre-select the placeholder name for [System(...)] -------------
        if (spec.name_sel_start >= 0) {
            const int sel_line = cur_line;   // always line 0 of snippet = cur_line
            const int sel_start = (int)indent.size() + spec.name_sel_start;
            const int sel_end   = sel_start + spec.name_sel_len;
            ed.SetSelection(
                {sel_line, sel_start},
                {sel_line, sel_end},
                ::TextEditor::SelectionMode::Normal);
            // Cursor on the selection end so a keypress replaces immediately.
            ed.SetCursorPosition({sel_line, sel_end});
        }

        update_and_return();
    }

    // ---- Generic keyword snippet expansion (Tab trigger) --------------------
    // Detection (BEFORE Render): editor focused, Tab (no mods), caret after
    // one of: for if while match def struct, no non-ws before kw on the line.
    // Sets d.snippet_tab_pending; apply_generic_snippet (AFTER Render) does
    // the actual insertion.
    static const char* const SNIPPET_KEYWORDS[] = {
        "for", "if", "while", "match", "def", "struct", nullptr
    };

    void detect_keyword_snippet(EditorState::LyncDoc& d, bool editor_focused,
                                bool ac_tab_pending) {
        d.snippet_tab_pending = false;
        if (!d.editor || !editor_focused) return;
        if (ac_tab_pending) return;  // autocomplete steals Tab first
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift || io.KeyAlt) return;
        if (!ImGui::IsKeyPressed(ImGuiKey_Tab, false)) return;
        const auto pos = d.editor->GetCursorPosition();
        if (pos.mLine < 0) return;
        const auto lines = d.editor->GetTextLines();
        if (pos.mLine >= (int)lines.size()) return;
        const std::string& line = lines[pos.mLine];
        // Clamp the caret column to the line's actual length. TextEditor
        // can briefly hold a caret past EOL (e.g. after a multi-line
        // InsertText leaves the cursor at the new content's end). Reading
        // line[col-1] without clamping trips the MSVC string-subscript
        // assertion - exactly the crash users see on the second Tab after
        // a postfix-match expansion.
        int col = pos.mColumn;
        if (col <= 0) return;
        if (col > (int)line.size()) col = (int)line.size();
        if (col == 0) return;
        auto is_word_c = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        int kw_end   = col;
        int kw_start = col - 1;
        while (kw_start > 0 && is_word_c(line[kw_start - 1])) --kw_start;
        if (kw_start < 0 || kw_start >= (int)line.size()) return;
        if (!is_word_c(line[kw_start])) return;
        const std::string candidate = line.substr(kw_start, kw_end - kw_start);
        bool found = false;
        for (int ki = 0; SNIPPET_KEYWORDS[ki]; ++ki)
            if (candidate == SNIPPET_KEYWORDS[ki]) { found = true; break; }
        if (!found) return;
        for (int c2 = 0; c2 < kw_start; ++c2)
            if (line[c2] != ' ' && line[c2] != '\t') return;
        d.snippet_tab_pending  = true;
        d.snippet_kw           = candidate;
        d.snippet_kw_line      = pos.mLine;
        d.snippet_kw_col_start = kw_start;
        d.snippet_kw_indent    = leading_indent(line);
    }

    // Postfix template: detect `<expr>.match` followed by Tab. The whole
    // chain gets replaced post-Render with a match-block scaffold.
    // `<expr>` is captured by walking back from the `.` over balanced
    // identifier/call chains: dotted names, paren-pairs, bracket-pairs.
    // Stops at the first non-chain char (whitespace / operator / `,` / `;`).
    void detect_postfix_match(EditorState::LyncDoc& d, bool editor_focused,
                                bool ac_tab_pending,
                                bool keyword_snippet_pending) {
        d.postfix_match_pending = false;
        if (!d.editor || !editor_focused) return;
        if (ac_tab_pending || keyword_snippet_pending) return;
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift || io.KeyAlt) return;
        if (!ImGui::IsKeyPressed(ImGuiKey_Tab, false)) return;

        const auto pos = d.editor->GetCursorPosition();
        if (pos.mLine < 0) return;
        const auto lines = d.editor->GetTextLines();
        if (pos.mLine >= (int)lines.size()) return;
        const std::string& line = lines[pos.mLine];
        const int col = std::min((int)line.size(), pos.mColumn);
        if (col < 6) return;   // need ".match" minimum

        // The 5 chars before caret must be exactly "match" with a `.` before.
        if (line.compare(col - 5, 5, "match") != 0) return;
        const int dot_col = col - 6;
        if (dot_col < 0 || line[dot_col] != '.') return;

        // Walk back from dot_col over a chain expression. Accept identifier
        // chars, `.`, `<`/`>` (template args), and balanced `()` / `[]`.
        int sc = dot_col - 1;
        auto is_id = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        while (sc >= 0) {
            const char c = line[sc];
            if (is_id(c) || c == '.') { --sc; continue; }
            if (c == ')' || c == ']' || c == '>') {
                // Walk back to matching opener.
                const char open = (c == ')') ? '(' : (c == ']') ? '[' : '<';
                int depth = 1;
                --sc;
                while (sc >= 0 && depth > 0) {
                    if (line[sc] == c)        ++depth;
                    else if (line[sc] == open) --depth;
                    --sc;
                }
                continue;
            }
            break;
        }
        const int expr_start = sc + 1;
        if (expr_start >= dot_col) return;
        std::string expr = line.substr(expr_start, dot_col - expr_start);
        // Trim trailing whitespace (paranoia).
        while (!expr.empty() && (expr.back() == ' ' || expr.back() == '\t'))
            expr.pop_back();
        if (expr.empty()) return;

        d.postfix_match_pending    = true;
        d.postfix_match_expr       = std::move(expr);
        d.postfix_match_line       = pos.mLine;
        d.postfix_match_expr_start = expr_start;
        d.postfix_match_caret_end  = col;
    }

    // Called AFTER Render. Replaces `<expr>.match` (consumed by Tab) with a
    // full match-block, caret placed inside the some-arm body.
    void apply_postfix_match(EditorState::LyncDoc& d) {
        if (!d.postfix_match_pending || !d.editor) return;
        d.postfix_match_pending = false;
        const int line = d.postfix_match_line;
        const auto lines_now = d.editor->GetTextLines();
        if (line < 0 || line >= (int)lines_now.size()) return;
        const std::string& cur = lines_now[line];

        // The range [postfix_match_expr_start, pos_now.mColumn) on `line`
        // covers `<expr>.match` plus whatever Tab-induced whitespace the
        // editor just inserted. Delete it as one unit; we re-insert the
        // entire match block from scratch. Bounds-clamp expr_start to the
        // line's actual length so a stale value (rare but possible if
        // anything else mutated the buffer between detect and apply)
        // can't drive a negative-length selection.
        const auto pos_now = d.editor->GetCursorPosition();
        if (pos_now.mLine != line) return;
        int sel_start = d.postfix_match_expr_start;
        int sel_end   = pos_now.mColumn;
        if (sel_start < 0)              sel_start = 0;
        if (sel_start > (int)cur.size()) sel_start = (int)cur.size();
        if (sel_end < sel_start)        sel_end   = sel_start;
        if (sel_end > (int)cur.size())  sel_end   = (int)cur.size();
        d.editor->SetSelection({line, sel_start},
                               {line, sel_end},
                               ::TextEditor::SelectionMode::Normal);
        d.editor->Delete();
        // Re-bind expr_start to the bounds-clamped value since the
        // indent computation below reads `cur` slice [0, expr_start).
        d.postfix_match_expr_start = sel_start;

        // Indent = whatever leads the line up to the expression start.
        std::string ind = cur.substr(0, d.postfix_match_expr_start);
        // Keep only leading-ws portion (drop any non-ws if the expr wasn't
        // at column 0 of indentation).
        size_t i = 0;
        while (i < ind.size() && (ind[i] == ' ' || ind[i] == '\t')) ++i;
        ind = ind.substr(0, i);

        const std::string& e = d.postfix_match_expr;
        // Build: match <expr> {\n    some(x): {\n        |\n    }\n    null: {\n    }\n}
        std::string ins;
        ins  = "match " + e + " {\n";
        ins += ind + "    some(x): {\n";
        ins += ind + "        ";
        const int caret_line = line + 2;
        const int caret_col  = (int)(ind.size() + 8);
        ins += "\n";
        ins += ind + "    }\n";
        ins += ind + "    null: {\n";
        ins += ind + "    }\n";
        // Lync match-as-statement: the closing `}` is followed by a `;`
        // because the whole match is a statement that needs to terminate.
        // Without it lync emits "expected ; but found }" on the next token.
        ins += ind + "};";
        d.editor->InsertText(ins);
        d.editor->SetCursorPosition({caret_line, caret_col});
    }

    // Match-on-Enter: detect Enter pressed when the current line ends with
    // `match <expr> {`. Set a pending flag; apply after Render inserts the
    // newline so we know exactly which line the brace ended up on.
    void detect_match_enter(EditorState::LyncDoc& d, bool editor_focused) {
        d.match_enter_pending = false;
        if (!d.editor || !editor_focused) return;
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift || io.KeyAlt) return;
        const bool entered =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (!entered) return;

        const auto pos = d.editor->GetCursorPosition();
        if (pos.mLine < 0 || pos.mColumn <= 0) return;
        const auto lines = d.editor->GetTextLines();
        if (pos.mLine >= (int)lines.size()) return;
        const std::string& line = lines[pos.mLine];
        const int col = std::min((int)line.size(), pos.mColumn);
        // The char immediately before the caret must be `{`. Two real
        // cases this catches:
        //   1. `match X {|`        — user just typed `{`, no auto-pair
        //   2. `match X {|}`       — auto-pair already inserted closing `}`
        // Both put `{` at line[col-1]; the trailing `}` (if any) is fine.
        if (col == 0 || line[col - 1] != '{') return;
        // Line must start with `match ` (after its own indent).
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (line.compare(i, 6, "match ") != 0) return;

        d.match_enter_pending     = true;
        d.match_enter_brace_line  = pos.mLine;
        d.match_enter_indent      = leading_indent(line);
    }

    void apply_match_enter(EditorState::LyncDoc& d) {
        if (!d.match_enter_pending || !d.editor) return;
        d.match_enter_pending = false;
        // After Render, the editor has inserted a newline + (auto-)indented
        // the new caret line. We want to expand into:
        //     <indent>    some(x): {
        //     <indent>        $CURSOR
        //     <indent>    }
        //     <indent>    null: {
        //     <indent>    }
        //     <indent>}
        // and leave the closing `}` matching. Insert text starting at the
        // caret's current line; we don't try to also insert the closing
        // brace because TextEditor's auto-pair likely already added one
        // when the user typed `{`. We just splice the body.
        const auto pos = d.editor->GetCursorPosition();
        const auto lines_now = d.editor->GetTextLines();
        if (pos.mLine < 0 || pos.mLine >= (int)lines_now.size()) return;

        const std::string& ind = d.match_enter_indent;
        // Replace whatever auto-indent put on this fresh line with our skel.
        const std::string& cur = lines_now[pos.mLine];
        d.editor->SetSelection({pos.mLine, 0},
                               {pos.mLine, (int)cur.size()},
                               ::TextEditor::SelectionMode::Normal);
        d.editor->Delete();

        std::string ins;
        ins  = ind + "    some(x): {\n";
        ins += ind + "        ";
        const int caret_line = pos.mLine + 1;
        const int caret_col  = (int)(ind.size() + 8);
        ins += "\n";
        ins += ind + "    }\n";
        ins += ind + "    null: {\n";
        ins += ind + "    }";
        d.editor->InsertText(ins);
        d.editor->SetCursorPosition({caret_line, caret_col});

        // Auto-pair already inserted the closing `}` of the match block on
        // a line right below our skeleton; lync needs that `}` followed by
        // `;` to terminate the match statement. Find it and append `;` if
        // it's missing - safe to skip if the line shape isn't what we
        // expect (user may have edited around it).
        const auto post_lines = d.editor->GetTextLines();
        // The closing `}` should now be at the line right after our last
        // inserted line. We inserted 5 lines (some/body/}/null/}) starting
        // at pos.mLine, so the auto-paired `}` is at pos.mLine + 5.
        const int close_line = pos.mLine + 5;
        if (close_line >= 0 && close_line < (int)post_lines.size()) {
            const std::string& cl = post_lines[close_line];
            // Trim leading ws to find the first non-ws char.
            size_t ci = 0;
            while (ci < cl.size() && (cl[ci] == ' ' || cl[ci] == '\t')) ++ci;
            if (ci < cl.size() && cl[ci] == '}') {
                // Already followed by `;`? Check the next non-ws char.
                size_t cj = ci + 1;
                while (cj < cl.size() && (cl[cj] == ' ' || cl[cj] == '\t')) ++cj;
                const bool already_terminated =
                    (cj < cl.size() && cl[cj] == ';');
                if (!already_terminated) {
                    // Insert `;` right after the `}`.
                    d.editor->SetSelection({close_line, (int)(ci + 1)},
                                           {close_line, (int)(ci + 1)},
                                           ::TextEditor::SelectionMode::Normal);
                    d.editor->SetCursorPosition({close_line, (int)(ci + 1)});
                    d.editor->InsertText(";");
                    // Restore caret to the some-arm body where the user
                    // expects to type next.
                    d.editor->SetCursorPosition({caret_line, caret_col});
                }
            }
        }
    }

    // Called AFTER Render. Removes kw+tab, inserts snippet, places cursor/sel.
    // Snippets (indent = keyword line leading ws):
    //   for    -> for (i: 0 to [N]) {\n    $CURSOR\n}   (N pre-selected)
    //   if     -> if ($CURSOR) {\n    \n}
    //   while  -> while ($CURSOR) {\n    \n}
    //   match  -> match $CURSOR {\n    null=>{ },\n    some(x)=>{ }\n}
    //   def    -> def [NAME](): void {\n    $CURSOR\n}  (NAME pre-selected)
    //   struct -> [Foo]: struct {\n    field: int\n}    (Foo pre-selected)
    void apply_generic_snippet(EditorState::LyncDoc& d) {
        if (!d.snippet_tab_pending || !d.editor) return;
        d.snippet_tab_pending = false;
        const int         snap_line = d.snippet_kw_line;
        const int         kw_start  = d.snippet_kw_col_start;
        const std::string kw        = d.snippet_kw;
        const std::string ind       = d.snippet_kw_indent;
        const auto pos_now   = d.editor->GetCursorPosition();
        const auto lines_now = d.editor->GetTextLines();
        if (snap_line >= (int)lines_now.size()) return;
        if (pos_now.mLine != snap_line) return;
        const int del_end = pos_now.mColumn;
        if (del_end <= kw_start) return;
        d.editor->SetSelection({snap_line, kw_start},
                                {snap_line, del_end},
                                ::TextEditor::SelectionMode::Normal);
        d.editor->Delete();

        // Template: $SEL_START/$SEL_END = region to pre-select, $CURSOR = caret.
        int snip_cursor_line = 0, snip_cursor_col  = 0;
        int snip_sel_s_line  = -1, snip_sel_s_col  = -1;
        int snip_sel_e_line  = -1, snip_sel_e_col  = -1;
        struct SnipLine { std::string text; };
        std::vector<SnipLine> tpl;
        auto parse_tpl = [&](const std::vector<std::string>& raw_lines) {
            tpl.clear();
            snip_cursor_line = snip_cursor_col = 0;
            snip_sel_s_line  = snip_sel_s_col  = -1;
            snip_sel_e_line  = snip_sel_e_col  = -1;
            for (int ri = 0; ri < (int)raw_lines.size(); ++ri) {
                const std::string& r = raw_lines[ri];
                std::string clean; clean.reserve(r.size());
                for (size_t ci = 0; ci < r.size(); ) {
                    if (r.compare(ci, 10, "$SEL_START") == 0) {
                        snip_sel_s_line = ri; snip_sel_s_col = (int)clean.size(); ci += 10;
                    } else if (r.compare(ci, 8, "$SEL_END") == 0) {
                        snip_sel_e_line = ri; snip_sel_e_col = (int)clean.size(); ci += 8;
                    } else if (r.compare(ci, 7, "$CURSOR") == 0) {
                        snip_cursor_line = ri; snip_cursor_col = (int)clean.size(); ci += 7;
                    } else { clean.push_back(r[ci++]); }
                }
                tpl.push_back({std::move(clean)});
            }
        };
        if (kw == "for") {
            parse_tpl({"for (i: 0 to $SEL_STARTN$SEL_END) {",
                        "    $CURSOR",
                        "}"});
        } else if (kw == "if") {
            parse_tpl({"if ($CURSOR) {",
                        "    ",
                        "}"});
        } else if (kw == "while") {
            parse_tpl({"while ($CURSOR) {",
                        "    ",
                        "}"});
        } else if (kw == "match") {
            // Lync match-arm syntax is `pattern: { stmts }` (colon, not =>).
            // Skeleton matches the canonical nullable form so users can
            // Tab into it and immediately fill the some-arm.
            parse_tpl({"match $CURSOR {",
                        "    some(x): {",
                        "        ",
                        "    }",
                        "    null: {",
                        "    }",
                        "}"});
        } else if (kw == "def") {
            parse_tpl({"def $SEL_STARTNAME$SEL_END(): void {",
                        "    $CURSOR",
                        "}"});
        } else if (kw == "struct") {
            parse_tpl({"$SEL_STARTFoo$SEL_END: struct {",
                        "    field: int",
                        "}"});
        } else { return; }

        std::string insertion;
        for (int li = 0; li < (int)tpl.size(); ++li) {
            if (li > 0) insertion += '\n';
            insertion += ind + tpl[li].text;
        }
        d.editor->InsertText(insertion);
        d.editor->SetCursorPosition(
            {snap_line + snip_cursor_line,
             (int)ind.size() + snip_cursor_col});
        if (snip_sel_s_line >= 0 && snip_sel_e_line >= 0) {
            const int sl = snap_line + snip_sel_s_line;
            const int sc = (int)ind.size() + snip_sel_s_col;
            const int el = snap_line + snip_sel_e_line;
            const int ec = (int)ind.size() + snip_sel_e_col;
            d.editor->SetSelection({sl, sc}, {el, ec},
                                    ::TextEditor::SelectionMode::Normal);
            d.editor->SetCursorPosition({el, ec});
        }
    }

    // ---- Surround-with-bracket on selection ---------------------------------
    // Called BEFORE Render. Selection non-empty + ( [ { " ' in input queue:
    // consume the char, delete selection, insert wrapped form, re-select body.
    // For '{' + multi-line: {\n<indented body>\n<base_indent>} pattern.
    void apply_surround_with_bracket(EditorState::LyncDoc& d, bool editor_focused) {
        if (!d.editor || !editor_focused) return;
        if (!d.editor->HasSelection()) return;
        ImGuiIO& io = ImGui::GetIO();
        char opener = 0;
        int  opener_qi = -1;
        for (int qi = 0; qi < io.InputQueueCharacters.Size; ++qi) {
            const ImWchar ch = io.InputQueueCharacters[qi];
            if (ch == '(' || ch == '[' || ch == '{' ||
                ch == '"' || ch == '\'') {
                opener = static_cast<char>(ch); opener_qi = qi; break;
            }
        }
        if (!opener) return;
        io.InputQueueCharacters.erase(io.InputQueueCharacters.Data + opener_qi);

        const char        close     = closer_for(opener);
        const std::string sel_text  = d.editor->GetSelectedText();
        // TextEditor has no public GetSelectionStart/End; use cursor (end of
        // selection) and the post-Delete cursor (which lands at sel_start).
        const auto        sel_end_c = d.editor->GetCursorPosition();
        d.editor->Delete();  // caret lands at sel_start
        const auto        sel_start = d.editor->GetCursorPosition();
        const bool        multi_ln  = (sel_start.mLine != sel_end_c.mLine);

        if (opener == '{' && multi_ln) {
            const size_t first_nl  = sel_text.find('\n');
            const std::string base_ind = leading_indent(
                sel_text.substr(0, first_nl == std::string::npos
                                       ? sel_text.size() : first_nl));
            std::vector<std::string> sel_lines;
            {
                std::istringstream iss(sel_text);
                std::string ln;
                while (std::getline(iss, ln)) sel_lines.push_back(ln);
            }
            std::string body;
            for (size_t li = 0; li < sel_lines.size(); ++li) {
                if (li > 0) body += '\n';
                std::string stripped = sel_lines[li];
                if (stripped.size() >= base_ind.size() &&
                    stripped.compare(0, base_ind.size(), base_ind) == 0)
                    stripped = stripped.substr(base_ind.size());
                body += base_ind + "    " + stripped;
            }
            const std::string wrapped = "{\n" + body + "\n" + base_ind + "}";
            d.editor->InsertText(wrapped);
            const int bs_line = sel_start.mLine + 1;
            const int bs_col  = (int)(base_ind.size() + 4);
            const int be_line = sel_start.mLine + (int)sel_lines.size();
            int be_col = bs_col;
            if (!sel_lines.empty()) {
                std::string last = sel_lines.back();
                if (last.size() >= base_ind.size() &&
                    last.compare(0, base_ind.size(), base_ind) == 0)
                    last = last.substr(base_ind.size());
                be_col = (int)(base_ind.size() + 4 + last.size());
            }
            d.editor->SetSelection({bs_line, bs_col}, {be_line, be_col},
                                    ::TextEditor::SelectionMode::Normal);
        } else {
            const std::string wrapped =
                std::string(1, opener) + sel_text + std::string(1, close);
            d.editor->InsertText(wrapped);
            const int start_col = sel_start.mColumn + 1;
            const int nl_count  = (int)std::count(
                sel_text.begin(), sel_text.end(), '\n');
            int end_line, end_col;
            if (nl_count == 0) {
                end_line = sel_start.mLine;
                end_col  = start_col + (int)sel_text.size();
            } else {
                end_line = sel_start.mLine + nl_count;
                const auto last_nl = sel_text.rfind('\n');
                end_col = (last_nl == std::string::npos)
                    ? (int)sel_text.size()
                    : (int)(sel_text.size() - last_nl - 1);
            }
            d.editor->SetSelection({sel_start.mLine, start_col},
                                    {end_line, end_col},
                                    ::TextEditor::SelectionMode::Normal);
        }
    }

    // Word-under-cursor helper. TextEditor's GetWordUnderCursor is private,
    // so we compute it from the public cursor pos + text lines. Word chars =
    // [A-Za-z0-9_]. Returns "" when caret is on whitespace/punct.
    std::string word_at_cursor(::TextEditor& ed) {
        const auto pos = ed.GetCursorPosition();
        if (pos.mLine < 0) return {};
        auto lines = ed.GetTextLines();
        if (pos.mLine >= (int)lines.size()) return {};
        const std::string& line = lines[pos.mLine];
        const int col = pos.mColumn;
        if (col < 0 || col > (int)line.size()) return {};
        auto is_word = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        // Caret may be just past the word's end - back off one if needed.
        int probe = (col == (int)line.size() || !is_word(line[col]))
                    ? col - 1 : col;
        if (probe < 0 || !is_word(line[probe])) return {};
        int start = probe;
        while (start > 0 && is_word(line[start - 1])) --start;
        int end = probe;
        while (end < (int)line.size() && is_word(line[end])) ++end;
        return line.substr(start, end - start);
    }

    // Per-component QueryEach<Name> / Add<Name> aren't in the static map -
    // recognise them by prefix and synthesize a docstring on the fly.
    bool synthetic_doc_for(const std::string& word, std::string& out) {
        if (word.rfind("QueryEach", 0) == 0 && word.size() > 9) {
            out = word + "(cb: ptr) - iterate every entity with the "
                + word.substr(9) + " component";
            return true;
        }
        if (word.rfind("Add", 0) == 0 && word.size() > 3
                && std::isupper(static_cast<unsigned char>(word[3]))) {
            out = word + "(e, ...fields) - attach " + word.substr(3)
                + " component to entity e";
            return true;
        }
        if (word.rfind("zues_add_", 0) == 0 && word.size() > 9) {
            out = "(legacy) " + word + "(e, ...fields) - attach "
                + word.substr(9) + " component to entity e";
            return true;
        }
        return false;
    }

    // ---- Per-doc ::TextEditor lazy-init -------------------------------------
    void ensure_editor(EditorState::LyncDoc& d) {
        if (d.editor) return;
        d.editor = std::make_shared<::TextEditor>();
        d.editor->SetLanguageDefinition(lync_language_def());
        d.editor->SetShowWhitespaces(false);
        d.editor->SetTabSize(4);
        d.editor->SetPalette(lync_palette());
        d.editor->SetMatchPairsHighlight(true);
        // JetBrains / VS Code use a roomier line height than ImGui's
        // default 1.0× — bumping makes long files less wall-of-text.
        d.editor->SetLineSpacing(1.25f);
    }

    // Apply mutable per-frame settings (Settings popup writes EditorState
    // fields; we mirror them onto every doc's TextEditor before Render).
    void sync_settings_to_doc(EditorState::LyncDoc& d, const EditorState& s) {
        if (!d.editor) return;
        if (d.editor->IsRainbowBrackets() != s.lync_rainbow)
            d.editor->SetRainbowBrackets(s.lync_rainbow);
        if (d.editor->IsWordWrap() != s.lync_word_wrap)
            d.editor->SetWordWrap(s.lync_word_wrap);
    }

    // Reformat braces according to the chosen style. Conservative: only
    // touches lines where the open '{' sits at end-of-line OR on its own.
    // SameLine mode normalises `\n{` blocks back to `... {` on the prior
    // non-empty line. NewLine mode normalises `... {\n` to `...\n{\n` with
    // matching indent. No-op for inline braces / data literals.
    std::string reformat_braces(const std::string& src,
                                 EditorState::LyncBraceStyle style) {
        std::vector<std::string> lines;
        {
            std::string cur;
            for (char c : src) {
                if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); }
                else            cur.push_back(c);
            }
            lines.push_back(std::move(cur));
        }
        auto trim_right = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        auto leading = [](const std::string& s) -> std::string {
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            return s.substr(0, i);
        };
        auto only_open_brace = [&](const std::string& s) -> bool {
            // Strip leading ws; remainder must be exactly '{' (perhaps with
            // trailing ws). Avoids touching `{ field: 1 }` data literals.
            std::string t = s.substr(leading(s).size());
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
            return t == "{";
        };
        auto ends_with_open = [&](const std::string& s) -> bool {
            std::string t = s;
            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
            return !t.empty() && t.back() == '{' && t.size() >= 2;
        };

        if (style == EditorState::LyncBraceStyle::SameLine) {
            // Pull `{` up onto the previous non-empty line.
            std::vector<std::string> out;
            out.reserve(lines.size());
            for (size_t i = 0; i < lines.size(); ++i) {
                if (only_open_brace(lines[i]) && !out.empty()) {
                    // Append to last non-empty out line.
                    int j = (int)out.size() - 1;
                    while (j >= 0 && out[j].find_first_not_of(" \t") == std::string::npos)
                        --j;
                    if (j >= 0) {
                        trim_right(out[j]);
                        out[j] += " {";
                        continue;   // skip emitting this line
                    }
                }
                out.push_back(lines[i]);
            }
            lines = std::move(out);
        } else {
            // NewLine: split `... {` into `...` then `<indent>{`.
            std::vector<std::string> out;
            out.reserve(lines.size() * 2);
            for (auto& l : lines) {
                if (ends_with_open(l)) {
                    std::string head = l.substr(0, l.size() - 1);
                    while (!head.empty() && (head.back() == ' ' || head.back() == '\t'))
                        head.pop_back();
                    out.push_back(head);
                    out.push_back(leading(l) + "{");
                } else {
                    out.push_back(l);
                }
            }
            lines = std::move(out);
        }

        std::string joined;
        for (size_t i = 0; i < lines.size(); ++i) {
            joined += lines[i];
            if (i + 1 < lines.size()) joined += '\n';
        }
        return joined;
    }

    // --- Item 1 + 2: trim trailing whitespace and expand leading tabs --------
    // Returns the processed text. No multi-line string awareness (v1: trim
    // everything -- acceptable per spec). Also expands leading tabs -> 4 spaces.
    std::string trim_and_format_lines(const std::string& text) {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else           { cur += c; }
        }
        lines.push_back(cur);

        for (auto& line : lines) {
            // Step 1: expand leading tabs to 4 spaces each.
            std::string expanded;
            expanded.reserve(line.size());
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                if (line[i] == '\t') expanded += "    ";
                else                 expanded += ' ';
                ++i;
            }
            expanded += line.substr(i);

            // Step 2: strip trailing spaces and tabs.
            size_t end = expanded.size();
            while (end > 0 && (expanded[end-1] == ' ' || expanded[end-1] == '\t'))
                --end;
            line = expanded.substr(0, end);
        }

        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            out += lines[i];
            if (i + 1 < lines.size()) out += '\n';
        }
        return out;
    }

    void save_doc(EditorState& s, int idx) {
        if (idx < 0 || idx >= static_cast<int>(s.lync_docs.size())) return;
        auto& d = s.lync_docs[idx];
        if (!d.editor) return;
        // Apply brace-style reformat just before write. The editor buffer is
        // also updated so the user sees the formatted result. SetText resets
        // the cursor; restore it post-format.
        const std::string raw = d.editor->GetText();
        const std::string fmt = reformat_braces(raw, s.lync_brace_style);
        if (fmt != raw) {
            const auto cursor = d.editor->GetCursorPosition();
            d.editor->SetText(fmt);
            d.editor->SetCursorPosition(cursor);
        }
        // Items 1+2: trim trailing whitespace and expand leading tabs, then
        // reload into the editor so disk and buffer stay identical.
        {
            const std::string before_trim = d.editor->GetText();
            const std::string trimmed     = trim_and_format_lines(before_trim);
            if (trimmed != before_trim) {
                const auto cur = d.editor->GetCursorPosition();
                d.editor->SetText(trimmed);
                const auto new_lines = d.editor->GetTextLines();
                int new_col = cur.mColumn;
                if (cur.mLine >= 0 && cur.mLine < (int)new_lines.size()) {
                    const int max_col = (int)new_lines[cur.mLine].size();
                    if (new_col > max_col) new_col = max_col;
                }
                d.editor->SetCursorPosition({cur.mLine, new_col});
            }
        }
        // Strip trailing empty lines before write. Many editors leave a
        // single trailing newline which is a POSIX convention; we keep
        // exactly one trailing newline at most. Multiple blank lines at
        // EOF feel like cruft and bloat diffs.
        {
            std::string text = d.editor->GetText();
            // Drop trailing whitespace lines: keep stripping while the
            // last char(s) are blank-line content. Stop when we hit a
            // non-newline / non-whitespace char.
            while (!text.empty()) {
                const char c = text.back();
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                    text.pop_back();
                } else {
                    break;
                }
            }
            // Add back a single trailing newline (POSIX convention).
            if (!text.empty()) text.push_back('\n');
            // Re-sync the editor buffer ONLY if changes were applied so
            // the cursor doesn't jump around on every save.
            if (text != d.editor->GetText()) {
                const auto cur = d.editor->GetCursorPosition();
                d.editor->SetText(text);
                const auto new_lines = d.editor->GetTextLines();
                int new_line = std::min(cur.mLine, std::max(0, (int)new_lines.size() - 1));
                int new_col  = cur.mColumn;
                if (new_line >= 0 && new_line < (int)new_lines.size()) {
                    const int max_col = (int)new_lines[new_line].size();
                    if (new_col > max_col) new_col = max_col;
                }
                d.editor->SetCursorPosition({new_line, new_col});
            }
        }
        if (write_file(d.path, d.editor->GetText())) {
            d.dirty = false;
            // Tell the lync source watcher to compile NOW (instead of
            // waiting up to 5s for its safety-net mtime poll).
            s.lync_watch.force_fire = true;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Saved %s", d.display.c_str());
            show_toast(s, buf, 1.5f, false);
        } else {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Save failed: %s", d.path.c_str());
            show_toast(s, buf, 3.0f, true);
        }
    }

    // Vertical drag-handle between the source pane and the editor body.
    // Built on InvisibleButton + mouse delta - ImGui::SplitterBehavior
    // expects two pane sizes (size1*, size2*); we only track one (the left
    // pane width) so this hand-rolled version keeps the math honest.
    // Returns true while actively dragged.
    bool vertical_splitter(const char* id, float* width_left,
                            float min_left, float max_left) {
        const float thickness = 6.0f;
        // ImGui::InvisibleButton asserts on zero-size; the available height
        // can be 0 when the Lync window is collapsed or on a first-frame
        // layout race. Force a minimum so the splitter widget is always
        // valid, even if the user can't visibly interact this frame.
        float h = ImGui::GetContentRegionAvail().y;
        if (h < 1.0f) h = 1.0f;
        ImGui::InvisibleButton(id, ImVec2(thickness, h));
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active) {
            *width_left += ImGui::GetIO().MouseDelta.x;
            if (*width_left < min_left) *width_left = min_left;
            if (*width_left > max_left) *width_left = max_left;
            return true;
        }
        return false;
    }

    // Apply Ctrl+C / Ctrl+X "whole line if no selection" semantics. Called
    // before the editor's own Render() consumes the keypress so our pre-set
    // selection is what gets copied/cut. Returns true if we mutated state.
    void apply_line_clipboard_semantics(::TextEditor& ed, bool editor_focused) {
        if (!editor_focused) return;
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl) return;
        const bool wants_copy = ImGui::IsKeyPressed(ImGuiKey_C, false);
        const bool wants_cut  = ImGui::IsKeyPressed(ImGuiKey_X, false);
        if (!wants_copy && !wants_cut) return;
        if (ed.HasSelection()) return;     // user already selected something

        const auto pos = ed.GetCursorPosition();
        const int total = ed.GetTotalLines();
        // Select the current line + its trailing newline. SelectionMode::Line
        // is wrong here: it snaps the END coord to line N+1, copying TWO
        // lines. Normal mode with end at (line+1, 0) gives "this line and
        // the newline that terminates it" - exactly one line of content.
        // For the very last line (no trailing newline) we use INT_MAX so
        // TextEditor clamps to the actual line length.
        ::TextEditor::Coordinates start{pos.mLine, 0};
        ::TextEditor::Coordinates end;
        if (pos.mLine + 1 < total) {
            end = ::TextEditor::Coordinates{pos.mLine + 1, 0};
        } else {
            end = ::TextEditor::Coordinates{pos.mLine, INT_MAX};
        }
        ed.SetSelection(start, end, ::TextEditor::SelectionMode::Normal);
    }

    // Per-frame autocomplete computation for the active document. Returns
    // the best suggestion (empty if none) plus the prefix range so the
    // top status bar can render a hint and Tab-accept can do the swap.
    //
    // When `is_template_mode` is true the caret is right after one of the
    // five template operators (Add<, Has<, Get<, Each<, Remove<). In that
    // case `alternatives` holds ALL matching component names (not capped at 4)
    // for the dropdown, `tpl_prefix` is what the user has typed after '<',
    // and `tpl_insert_col` is the column right after the '<' (start of the
    // fragment to replace on accept).
    struct AutoComplete {
        bool        active           = false;
        std::string prefix;
        std::string best;
        std::vector<std::string> alternatives;   // up to 4 for normal; all for template mode
        int         line             = 0;
        int         word_start       = 0;

        // Template-mode fields.
        bool        is_template_mode  = false;
        std::string tpl_op;            // "Add" / "Has" / "Get" / "Each" / "Remove"
        std::string tpl_prefix;        // chars typed after '<'
        int         tpl_insert_col    = 0;  // column right after '<'

        // Dot-trigger context. Set when the AC opens via `<expr>.<prefix>`.
        // The accept handler routes `match` (and other postfix-template
        // names) through the chain-replacement path instead of a plain
        // identifier insertion.
        bool        dot_triggered     = false;
    };
    AutoComplete compute_autocomplete(EditorState& s, EditorState::LyncDoc& d) {
        AutoComplete ac;
        if (!d.editor) return ac;

        // ---- Template-mode check: caret is right after "Op<[partial]" ------
        {
            std::string op, tpl_pfx;
            int tpl_col = 0;
            if (template_trigger_at_cursor(*d.editor, op, tpl_pfx, &tpl_col)) {
                const auto pos = d.editor->GetCursorPosition();
                // Skip if there's already a word char to the right of the caret
                // (user is editing inside an existing name — same rule as normal mode).
                const auto lines = d.editor->GetTextLines();
                bool mid_word = false;
                if (pos.mLine >= 0 && pos.mLine < (int)lines.size()) {
                    const std::string& line = lines[pos.mLine];
                    if (pos.mColumn >= 0 && pos.mColumn < (int)line.size()) {
                        const char c = line[pos.mColumn];
                        if (std::isalnum((unsigned char)c) || c == '_') mid_word = true;
                    }
                }
                if (!mid_word) {
                    std::vector<std::string> comps =
                        component_names_from_world(s.world, tpl_pfx);

                    ac.active           = true;
                    ac.is_template_mode = true;
                    ac.tpl_op           = op;
                    ac.tpl_prefix       = tpl_pfx;
                    ac.tpl_insert_col   = tpl_col;
                    ac.line             = pos.mLine;
                    ac.word_start       = tpl_col;  // used for ghost-text anchor
                    ac.prefix           = tpl_pfx;

                    if (!comps.empty()) {
                        ac.best         = comps.front();
                        ac.alternatives = std::vector<std::string>(comps.begin() + 1,
                                                                    comps.end());
                    }
                    // If no components: best stays empty; the popup will show
                    // the placeholder. active stays true so the popup opens.
                    return ac;
                }
            }
        }

        // ---- Normal word-prefix mode ----------------------------------------
        int word_start = 0;
        const std::string prefix = prefix_at_cursor(*d.editor, &word_start);

        // Dot-trigger: if the char right before the prefix start is `.` (and
        // the char before THAT is a word/`)`/`]` char), the user is doing a
        // member access. Force a field-name dropdown - bypassing the normal
        // builtin-suggestions path - so `user.Add` doesn't suggest
        // AddSpriteDefault. Fires regardless of prefix length.
        // prefix_at_cursor doesn't write `word_start` when the prefix is
        // empty (caret right after a non-word char like `.`), so we fall
        // back to the caret column there.
        bool dot_triggered = false;
        {
            const auto pos = d.editor->GetCursorPosition();
            const auto lines_now = d.editor->GetTextLines();
            // pos.mColumn is a VISUAL column (tabs count as up to mTabSize
            // empty cells). word_start (returned by prefix_at_cursor) is
            // ALSO visual. Indexing the raw `line` string by visual column
            // breaks the moment a tab character lives before the caret -
            // that's the "indent breaks autofill" bug. Convert to a char
            // index at the boundary, then keep the original word_start
            // around (still visual) for downstream Coordinates-based
            // operations like SetSelection / SetCursorPosition.
            if (prefix.empty()) word_start = pos.mColumn;
            const int word_start_idx =
                d.editor->VisualColumnToCharacterIndex(pos.mLine, word_start);
            if (pos.mLine >= 0 && pos.mLine < (int)lines_now.size()) {
                const std::string& line = lines_now[pos.mLine];
                const int dot_idx = word_start_idx - 1;
                if (dot_idx >= 0 && dot_idx < (int)line.size() &&
                        line[dot_idx] == '.' &&
                        dot_idx > 0 &&
                        (std::isalnum((unsigned char)line[dot_idx - 1]) ||
                         line[dot_idx - 1] == '_' ||
                         line[dot_idx - 1] == ')' ||
                         line[dot_idx - 1] == ']')) {
                    dot_triggered = true;
                }
            }
        }
        if (!dot_triggered && prefix.size() < 2) return ac;

        // Skip when caret is mid-word: if the char immediately to the right
        // of the caret is also a word char, the user is editing inside an
        // existing identifier and a ghost-text completion would just paint
        // duplicate glyphs on top of the real ones. Also skip when the
        // char to the LEFT is a word char that already extends past the
        // caret -- the regex-based prefix detection above can otherwise
        // pick up the *whole* identifier the caret is parked in the
        // middle of and offer to "complete" it to itself.
        //
        // pos.mColumn is a VISUAL column. Convert to a character index
        // before peeking at the line bytes -- a tab at the line start
        // would otherwise let mColumn point past the line's end.
        {
            const auto pos = d.editor->GetCursorPosition();
            auto lines = d.editor->GetTextLines();
            if (pos.mLine >= 0 && pos.mLine < (int)lines.size()) {
                const std::string& line = lines[pos.mLine];
                const int char_idx =
                    d.editor->VisualColumnToCharacterIndex(pos.mLine, pos.mColumn);
                if (char_idx >= 0 && char_idx < (int)line.size()) {
                    const char c = line[char_idx];
                    if (std::isalnum((unsigned char)c) || c == '_') return ac;
                }
                // Also bail when the caret sits between two word chars
                // (e.g. "Pla|yerData") -- the prefix walker may report
                // "Pla" but the char to the right is a word char too;
                // the check above already catches it. Catch the trailing
                // case too where mColumn == line.size() but the prior
                // char is a word char AND we're inside a longer literal
                // chain (rare; conservative no-op when in doubt).
            }
        }

        // ---- Dot-trigger smart resolution -----------------------------------
        // For `lhs.<prefix>`, try to figure out lhs's TYPE and only suggest
        // fields of that type. Falls back to "all field names anywhere"
        // only when we can't resolve the type. Heuristic, not full
        // symbol-table aware - but enough to stop suggesting `value` when
        // `dt: float` is on the LHS.
        std::string dot_lhs_name;     // identifier left of the `.`
        std::string dot_lhs_type;     // resolved type name (struct or primitive)
        bool        dot_lhs_nullable  = false;  // LHS evaluates to T? (must unwrap)
        if (dot_triggered) {
            const auto pos2 = d.editor->GetCursorPosition();
            const auto lines_now2 = d.editor->GetTextLines();
            if (pos2.mLine >= 0 && pos2.mLine < (int)lines_now2.size()) {
                const std::string& line = lines_now2[pos2.mLine];
                // Walk in char-space, not visual-column-space, so a tab
                // before the LHS identifier doesn't slide the indices off
                // the end of the line. word_start_idx is the char index
                // for the LHS's word_start.
                const int word_start_idx =
                    d.editor->VisualColumnToCharacterIndex(pos2.mLine, word_start);
                int wc = word_start_idx - 2;  // skip the `.`
                int we = wc + 1;
                while (wc >= 0 &&
                       (std::isalnum((unsigned char)line[wc]) || line[wc] == '_')) --wc;
                ++wc;
                if (wc >= 0 && we > wc) dot_lhs_name = line.substr(wc, we - wc);

                // ---- Chain-ending template-call pattern -------------------
                // When the LHS ends with `Op<T>()` (e.g. `i.Get<Name>()`),
                // we can read the component type directly from the call's
                // type arg. Walks the substring `[0, word_start - 1]`
                // (everything before the trailing `.`) and looks for
                // `<TypeName> ( )` immediately preceding the dot. Op is
                // any of Get / Mut / MutGet / Has — they all yield T-typed
                // values for AC purposes (we suggest fields of T even if
                // the actual return is T?).
                if (dot_lhs_type.empty() && word_start_idx >= 2) {
                    const std::string head =
                        line.substr(0, word_start_idx - 1);   // strip trailing `.`
                    // Accept both empty and non-empty parens: `Get<T>()` is
                    // the UFCS form (`e.Get<T>()` rewrites to that), and
                    // `Get<T>(e)` is the explicit-call form. We don't care
                    // what's inside - the type comes from the `<T>` slot.
                    static const std::regex tplcall_re(
                        R"((Get|Mut|MutGet|Has)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>\s*\([^)]*\)\s*$)");
                    std::smatch m;
                    if (std::regex_search(head, m, tplcall_re)) {
                        dot_lhs_type = m[2].str();
                        // Get<T>(...) returns T?, so the LHS is nullable.
                        // Mut / MutGet (when added) return non-nullable T.
                        // Has returns int (not relevant - no struct fields).
                        const std::string op = m[1].str();
                        if (op == "Get") dot_lhs_nullable = true;
                    }
                }
                // ---- Plain function-call pattern: `Name().` --------------
                // Look up Name in the engine extern surface to get its
                // return type. Hardcoded for known engine zero-arg
                // factories (KeyCode, GameManager()-style singleton getters
                // would land here too once symbols.json round-trips).
                // Without this, `KeyCode().Space` falls into the entity-like
                // fallback and offers Add/Has/Get/etc. instead of key
                // constants.
                if (dot_lhs_type.empty() && word_start_idx >= 2) {
                    const std::string head =
                        line.substr(0, word_start_idx - 1);
                    static const std::regex plain_call_re(
                        R"(([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*$)");
                    std::smatch m;
                    if (std::regex_search(head, m, plain_call_re)) {
                        const std::string fn_name = m[1].str();
                        // Engine factories with known struct returns. The
                        // [Singleton] plugin emits a getter named after
                        // the component type that returns `T?` (typed
                        // nullable), so any user-declared singleton flows
                        // through here too — fall through to the rich
                        // symbols pool for those.
                        if (fn_name == "KeyCode") {
                            dot_lhs_type = "KeyCodeT";
                        } else {
                            // Try the rich symbols if available — covers
                            // user-declared singletons and any extern that
                            // returns a typed struct.
                            for (const auto& fs : s.project_symbols.funcs) {
                                if (fs.name == fn_name && !fs.ret_type.empty()) {
                                    dot_lhs_type = fs.ret_type;
                                    if (fs.ret_nullable) dot_lhs_nullable = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (!dot_lhs_name.empty() && dot_lhs_type.empty()) {
                // Scan upward in the current doc for `<name>: <type>` or
                // `<name>: <ownership>? <type>` declarations whose name
                // matches dot_lhs_name. Stop at function boundary (`def `
                // line) since locals don't escape. Capture group 1 is
                // either "?" or "" - tells us whether the var is nullable.
                const auto& doc_lines = lines_now2;
                const std::string esc_name = std::regex_replace(dot_lhs_name,
                    std::regex(R"([\.\$\^\\\+\*\?\(\)\[\]\{\}\|])"), "\\$&");
                std::regex pat("\\b" + esc_name +
                              R"(\s*:\s*(?:own\s+|ref\s+)?(\??)([A-Za-z_][A-Za-z0-9_]*))");
                // Match-arm binding: `some(<name>):` introduces <name> as a
                // narrowed (non-nullable) reference to the matched
                // expression. Resolve by finding the enclosing
                // `match <expr> {` and reading the type out of <expr>.
                std::regex some_re("\\bsome\\s*\\(\\s*" + esc_name + R"(\s*\)\s*:)");
                std::regex match_re(R"(^\s*match\s+([^{]+)\{)");
                for (int li = pos2.mLine; li >= 0; --li) {
                    const std::string& l = doc_lines[li];
                    std::smatch m;
                    if (std::regex_search(l, m, pat)) {
                        if (m[1].str() == "?") dot_lhs_nullable = true;
                        dot_lhs_type = m[2].str();
                        break;
                    }
                    if (std::regex_search(l, m, some_re)) {
                        // Walk further up for the matching `match <expr> {`.
                        // Naive (no nesting tracking) - good enough for the
                        // common case where the user is editing inside the
                        // some-arm of the most-recent match.
                        for (int mj = li - 1; mj >= 0; --mj) {
                            std::smatch mm;
                            if (std::regex_search(doc_lines[mj], mm, match_re)) {
                                const std::string expr = mm[1].str();
                                // Trailing whitespace already excluded by the
                                // regex's `[^{]+`. Try the template-call
                                // pattern first, then a bare-identifier `name`
                                // which we'd resolve via another decl scan.
                                // Accept both `Get<T>()` (UFCS) and
                                // `Get<T>(<args>)` (explicit-call).
                                static const std::regex tplcall_re(
                                    R"((Get|Mut|MutGet|Has)\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>\s*\([^)]*\)\s*$)");
                                std::smatch tm;
                                if (std::regex_search(expr, tm, tplcall_re)) {
                                    dot_lhs_type = tm[2].str();
                                    // Inside `some(x):` the binding is
                                    // narrowed to non-nullable T regardless
                                    // of whether the match expr was T?.
                                }
                                break;
                            }
                            if (doc_lines[mj].find("def ") != std::string::npos) break;
                        }
                        if (!dot_lhs_type.empty()) break;
                    }
                    if (l.find("def ") != std::string::npos) break;  // out of scope
                }
            }
            // Primitive types have no struct members. BUT `int` is the
            // engine's entity convention — `i: int = CreateEntity(); i.X(...)`
            // is the canonical UFCS-call form, so we must NOT bail here for
            // int. Other primitives genuinely have no members; emit the
            // empty dropdown for them.
            static const std::set<std::string> primitives_no_methods{
                "float", "double", "char", "bool", "string", "ptr", "void"
            };
            if (primitives_no_methods.count(dot_lhs_type)) {
                // Empty dropdown - the user knows there's nothing to suggest.
                ac.active           = true;
                ac.is_template_mode = false;
                ac.line             = d.editor->GetCursorPosition().mLine;
                ac.word_start       = word_start;
                ac.prefix           = prefix;
                return ac;   // ac_items will be empty -> placeholder shows
            }
        }

        std::vector<std::string> sugg;
        if (dot_triggered) {
            // Rich-symbols fast path: when we have an authoritative table
            // from --emit-symbols, prefer it over the regex scan. Drops in
            // exactly the right struct fields + UFCS-callable free
            // functions for the resolved LHS type.
            if (s.project_symbols.rich_loaded) {
                std::set<std::string> seen_rich;
                auto starts_with_ci2 = [](const std::string& a, const std::string& b) {
                    if (b.size() > a.size()) return false;
                    for (size_t i = 0; i < b.size(); ++i)
                        if (std::tolower((unsigned char)a[i]) !=
                            std::tolower((unsigned char)b[i])) return false;
                    return true;
                };
                auto try_add = [&](const std::string& n) {
                    if (n.empty()) return;
                    if (prefix.empty() || starts_with_ci2(n, prefix))
                        seen_rich.insert(n);
                };

                // Struct fields for the resolved LHS type.
                if (!dot_lhs_type.empty()) {
                    for (const auto& st : s.project_symbols.structs) {
                        if (st.name != dot_lhs_type) continue;
                        for (const auto& [fn, ft] : st.fields) try_add(fn);
                        break;
                    }
                }
                // UFCS-callable: any function whose first param type matches
                // the LHS. When LHS type is unknown, fall back to "first
                // param is `EntityRef`" — the post-migration convention.
                // Pre-migration projects might still see `int` first-params
                // for entity-style functions; we match those too only when
                // the user's `e.` resolves to an `int` (back-compat).
                for (const auto& fs : s.project_symbols.funcs) {
                    if (fs.params.empty()) continue;
                    const std::string& t0 = fs.params.front().type;
                    bool match = false;
                    if (!dot_lhs_type.empty()) {
                        match = (t0 == dot_lhs_type);
                    } else {
                        match = (t0 == "EntityRef");
                    }
                    if (match) try_add(fs.name);
                }
                if (!seen_rich.empty()) {
                    sugg.assign(seen_rich.begin(), seen_rich.end());
                    if (!sugg.empty()) {
                        ac.active     = true;
                        ac.prefix     = prefix;
                        ac.best       = sugg.front();
                        ac.line       = d.editor->GetCursorPosition().mLine;
                        ac.word_start = word_start;
                        for (size_t i = 1; i < sugg.size() &&
                                ac.alternatives.size() < sugg.size() - 1; ++i)
                            ac.alternatives.push_back(sugg[i]);
                        return ac;
                    }
                }
            }

            // Harvest two sources, with no symbol-table lookup (would need
            // full type info first):
            //   1. Every identifier seen AFTER a `.` in any project .lync.
            //   2. Every struct-field name from any `<Name>: struct { ... }`
            //      block - so freshly-declared fields are suggestable even
            //      before they're used.
            // Both filtered by the user's partial prefix.
            //
            // We don't restrict to OPEN editor tabs - anything on disk in
            // the project's src tree counts. Otherwise a single-tab session
            // misses fields declared in sibling files.
            std::set<std::string> seen;
            auto starts_with_ci = [](const std::string& a, const std::string& b) {
                if (b.size() > a.size()) return false;
                for (size_t i = 0; i < b.size(); ++i) {
                    if (std::tolower((unsigned char)a[i]) !=
                        std::tolower((unsigned char)b[i])) return false;
                }
                return true;
            };
            // Language keywords are not valid dot-completions (you can't
            // write `e.match`, `e.if`, `e.for`...). Excluding them keeps
            // postfix-template triggers (like `.match` Tab) from being
            // pre-empted by the autocomplete-Tab path.
            static const std::set<std::string> kw_blacklist{
                "match", "if", "else", "for", "while", "do", "return",
                "def", "struct", "extern", "let", "mut", "own", "ref",
                "to", "in", "true", "false", "null",
                "int", "float", "double", "char", "bool", "void", "string", "ptr"
            };
            auto consider = [&](std::string name) {
                if (name.empty()) return;
                if (kw_blacklist.count(name)) return;
                if (prefix.empty() || starts_with_ci(name, prefix)) {
                    seen.insert(std::move(name));
                }
            };

            // Build the set of source bodies to scan: open docs first
            // (live, may have unsaved edits), then anything else on disk.
            std::vector<std::vector<std::string>> sources;
            std::set<std::string> seen_paths;
            for (const auto& doc : s.lync_docs) {
                if (!doc.editor) continue;
                seen_paths.insert(doc.path);
                sources.push_back(doc.editor->GetTextLines());
            }
            if (!s.project_dir.empty()) {
                namespace fs = std::filesystem;
                std::error_code ec;
                const fs::path src_root = fs::path(s.project_dir);
                if (fs::exists(src_root, ec)) {
                    for (auto& e : fs::recursive_directory_iterator(
                             src_root,
                             fs::directory_options::skip_permission_denied,
                             ec)) {
                        if (!e.is_regular_file(ec)) continue;
                        if (e.path().extension() != ".lync") continue;
                        const auto p = e.path().string();
                        if (seen_paths.count(p)) continue;
                        std::ifstream f(p);
                        if (!f) continue;
                        std::vector<std::string> file_lines;
                        std::string ln;
                        while (std::getline(f, ln)) file_lines.push_back(ln);
                        sources.push_back(std::move(file_lines));
                    }
                }
            }
            // Also pull in the engine's prelude (zues_api.lync). Lives
            // outside <project>/src but declares EVERY engine struct
            // (KeyCodeT, Transform2D, Sprite, the *Ref family, ...) so
            // dot-completion against `KeyCode().` or `e.Get<Transform2D>().`
            // can harvest the right field set. Loaded once per
            // autocomplete invocation; small file (~300 lines), cheap.
            if (!s.lync_prelude_abs.empty()) {
                namespace fs = std::filesystem;
                std::error_code ec;
                if (fs::exists(s.lync_prelude_abs, ec)) {
                    if (!seen_paths.count(s.lync_prelude_abs)) {
                        std::ifstream f(s.lync_prelude_abs);
                        if (f) {
                            std::vector<std::string> lns;
                            std::string ln;
                            while (std::getline(f, ln)) lns.push_back(ln);
                            sources.push_back(std::move(lns));
                        }
                    }
                }
            }

            // If we resolved the LHS type, restrict harvesting to ONLY
            // that struct's fields. When the type is unknown (e.g. user
            // wrote `i.Get<NotAStruct>().`), keep restrict_to_type set
            // so we DON'T leak fields from other structs - the popup
            // shows just the postfix-template entries (e.g. `match`).
            const bool restrict_to_type = !dot_lhs_type.empty();

            for (const auto& lines_doc : sources) {
                bool in_struct = false;
                bool in_target_struct = false;
                for (const auto& line : lines_doc) {
                    // (1) Identifiers after `.` - only when NOT restricting
                    // to a specific type (otherwise we'd suggest random
                    // unrelated names).
                    if (!restrict_to_type) {
                        for (size_t i = 0; i + 1 < line.size(); ++i) {
                            if (line[i] != '.') continue;
                            size_t j = i + 1;
                            if (j >= line.size()) continue;
                            if (!std::isalpha((unsigned char)line[j]) && line[j] != '_') continue;
                            size_t k = j;
                            while (k < line.size() &&
                                   (std::isalnum((unsigned char)line[k]) || line[k] == '_')) ++k;
                            consider(line.substr(j, k - j));
                        }
                    }

                    // (2) Detect `<Name>: struct { ... }` block opener. The
                    // decl line itself is NOT scanned for field-shaped
                    // patterns - that's where the `<Name>:` would falsely
                    // match as a "field" otherwise. Field harvesting kicks
                    // in on subsequent lines until the closing `}`.
                    const size_t struct_kw = line.find(": struct");
                    if (struct_kw != std::string::npos &&
                        line.find('{') != std::string::npos) {
                        in_struct = true;
                        // Pull the struct's name out (the identifier just
                        // before `: struct`) so we know whether to harvest.
                        if (restrict_to_type) {
                            int e = (int)struct_kw - 1;
                            while (e >= 0 && (line[e] == ' ' || line[e] == '\t')) --e;
                            int sb = e;
                            while (sb >= 0 &&
                                   (std::isalnum((unsigned char)line[sb]) || line[sb] == '_')) --sb;
                            ++sb;
                            const std::string struct_name = line.substr(sb, e - sb + 1);
                            in_target_struct = (struct_name == dot_lhs_type);
                        } else {
                            in_target_struct = true;
                        }
                        if (line.find('}') != std::string::npos) {
                            // Single-line struct: harvest fields from inside
                            // the braces.
                            const size_t lb = line.find('{');
                            const size_t rb = line.find('}');
                            if (lb != std::string::npos && rb > lb && in_target_struct) {
                                std::string body = line.substr(lb + 1, rb - lb - 1);
                                // tokenize on commas + whitespace, take name
                                // before each `:`.
                                std::stringstream ss(body);
                                std::string field_decl;
                                while (std::getline(ss, field_decl, ',')) {
                                    const size_t cp = field_decl.find(':');
                                    if (cp == std::string::npos) continue;
                                    std::string name = field_decl.substr(0, cp);
                                    // trim
                                    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
                                    size_t a = 0;
                                    while (a < name.size() && (name[a] == ' ' || name[a] == '\t')) ++a;
                                    name = name.substr(a);
                                    consider(std::move(name));
                                }
                            }
                            in_struct = false;
                        }
                        continue;   // skip per-field scan on the decl line
                    }

                    if (in_struct && in_target_struct) {
                        // Field syntax: `<name>: <type>,`. Look for IDENT:
                        // anywhere on the line.
                        size_t i = 0;
                        while (i < line.size()) {
                            while (i < line.size() &&
                                   (line[i] == ' ' || line[i] == '\t' ||
                                    line[i] == ',' || line[i] == '{' ||
                                    line[i] == '}')) ++i;
                            if (i >= line.size()) break;
                            if (!std::isalpha((unsigned char)line[i]) && line[i] != '_') {
                                ++i; continue;
                            }
                            size_t k = i;
                            while (k < line.size() &&
                                   (std::isalnum((unsigned char)line[k]) || line[k] == '_')) ++k;
                            std::string name = line.substr(i, k - i);
                            size_t after = k;
                            while (after < line.size() &&
                                   (line[after] == ' ' || line[after] == '\t')) ++after;
                            if (after < line.size() && line[after] == ':') {
                                consider(std::move(name));
                            }
                            while (k < line.size() && line[k] != ',' && line[k] != '}') ++k;
                            i = k;
                        }
                    }
                    if (in_struct && line.find('}') != std::string::npos) {
                        in_struct = false;
                        in_target_struct = false;
                    }
                }
            }

            // ---- UFCS-callable free functions ---------------------------
            // When the LHS doesn't resolve to a struct (or resolves to one
            // we don't have a decl for), also surface free functions that
            // take an entity-shaped first arg. lync rewrites
            // `e.Foo(args)` -> `Foo(e, args)`, so any function whose first
            // param is the receiver is a valid completion after `<expr>.`.
            //
            // Run when LHS resolves to `EntityRef` (post-migration). Also
            // run on unknown types as a fallback so brand-new locals
            // without explicit annotations still get useful suggestions.
            // Plain `int` is NO longer entity-like — autocomplete won't
            // surface SetTransform / Add / Get etc. on integer variables.
            const bool is_entity_like =
                dot_lhs_type.empty() ||
                dot_lhs_type == "EntityRef";
            if (is_entity_like) {
                static const std::set<std::string> ufcs_engine_api{
                    "SetTransform", "SetTransformPosition",
                    "AddSpriteDefault",
                    "Add", "Has", "Get", "Each", "Remove",
                    "ApplyImpulse", "ApplyForce", "SetVelocity",
                    "SetBodyPosition", "WakeBody", "DestroyEntity",
                };
                for (const auto& name : ufcs_engine_api) consider(name);

                // Project funcs: scan for `def Name(<id>: EntityRef ...)`.
                static const std::regex def_first_re(
                    R"(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)(?:<[^>]*>)?\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*:\s*(?:own\s+|ref\s+)?\??(EntityRef)\b)");
                for (const auto& lines_doc : sources) {
                    for (const auto& l : lines_doc) {
                        std::smatch m;
                        if (std::regex_search(l, m, def_first_re)) {
                            consider(m[1].str());
                        }
                    }
                }
            }

            // Engine-component fallback: when restrict_to_type is set but
            // no fields were harvested from .lync source, the type might be
            // an engine built-in (Sprite, Transform2D, etc.) registered by
            // a host module. Query the world's component registry for the
            // field list - the renderer/transform/physics modules attach
            // FieldInfo arrays via ZUES_COMPONENT_FIELDS.
            if (restrict_to_type && seen.empty() && s.world) {
                struct WC { const std::string* type_name; std::set<std::string>* out; const std::string* prefix; bool (*starts_with_ci)(const std::string&, const std::string&); };
                auto starts_with_ci_fn = [](const std::string& a, const std::string& b) -> bool {
                    if (b.size() > a.size()) return false;
                    for (size_t i = 0; i < b.size(); ++i)
                        if (std::tolower((unsigned char)a[i]) !=
                            std::tolower((unsigned char)b[i])) return false;
                    return true;
                };
                static auto sw_ci = +[](const std::string& a, const std::string& b) -> bool {
                    if (b.size() > a.size()) return false;
                    for (size_t i = 0; i < b.size(); ++i)
                        if (std::tolower((unsigned char)a[i]) !=
                            std::tolower((unsigned char)b[i])) return false;
                    return true;
                };
                WC wc{&dot_lhs_type, &seen, &prefix, sw_ci};
                s.world->iterate_component_types(
                    [&](ecs::ComponentId /*id*/, const ecs::ComponentType& t) {
                        if (!t.name) return;
                        if (dot_lhs_type != t.name) return;
                        for (u32 i = 0; i < t.field_count; ++i) {
                            const auto* fi = &t.fields[i];
                            if (!fi || !fi->name) continue;
                            std::string fname = fi->name;
                            if (fname.empty()) continue;
                            // Skip engine-internal handle fields (start with `_`).
                            if (fname[0] == '_') continue;
                            if (prefix.empty() ||
                                starts_with_ci_fn(fname, prefix)) {
                                seen.insert(std::move(fname));
                            }
                        }
                    });
            }

            // Math-types fallback: Vec2, Vec3, Color etc. live in the
            // lync prelude (zues_api.lync) which isn't under project_dir,
            // so the source-file harvest above misses them. They aren't
            // registered as ECS components either, so the world fallback
            // doesn't catch them. Hardcode the field names here -
            // they're stable engine surface; updating them here is the
            // same cost as updating the prelude struct decl.
            if (restrict_to_type && seen.empty()) {
                auto add_if_match = [&](const char* fname) {
                    auto sw_ci = [](const std::string& a, const std::string& b) {
                        if (b.size() > a.size()) return false;
                        for (size_t i = 0; i < b.size(); ++i)
                            if (std::tolower((unsigned char)a[i]) !=
                                std::tolower((unsigned char)b[i])) return false;
                        return true;
                    };
                    if (prefix.empty() || sw_ci(fname, prefix))
                        seen.insert(fname);
                };
                if (dot_lhs_type == "Vec2") {
                    add_if_match("x"); add_if_match("y");
                } else if (dot_lhs_type == "Vec3") {
                    add_if_match("x"); add_if_match("y"); add_if_match("z");
                } else if (dot_lhs_type == "Vec4") {
                    add_if_match("x"); add_if_match("y");
                    add_if_match("z"); add_if_match("w");
                } else if (dot_lhs_type == "Color") {
                    add_if_match("r"); add_if_match("g");
                    add_if_match("b"); add_if_match("a");
                }
            }

            sugg.assign(seen.begin(), seen.end());
        } else {
            sugg = autocomplete_suggestions(s, *d.editor, prefix);
        }

        // Dot-trigger postfix templates: when the LHS evaluates to a
        // nullable value, surface `match` as a selectable completion.
        // Accepting it expands the whole chain into a match-block (handled
        // at the accept site via ac.dot_triggered + chosen == "match").
        if (dot_triggered && dot_lhs_nullable) {
            auto starts_with_ci = [](const std::string& a, const std::string& b) {
                if (b.size() > a.size()) return false;
                for (size_t i = 0; i < b.size(); ++i)
                    if (std::tolower((unsigned char)a[i]) !=
                        std::tolower((unsigned char)b[i])) return false;
                return true;
            };
            if (prefix.empty() || starts_with_ci("match", prefix)) {
                // Push to FRONT so the postfix-template entry is the
                // top suggestion when the LHS is nullable - that's almost
                // always what the user wants.
                bool already = false;
                for (const auto& s : sugg) if (s == "match") { already = true; break; }
                if (!already) sugg.insert(sugg.begin(), "match");
            }
        }
        if (sugg.empty()) return ac;
        ac.active         = true;
        ac.dot_triggered  = dot_triggered;
        ac.prefix         = prefix;
        ac.best           = sugg.front();
        ac.line           = d.editor->GetCursorPosition().mLine;
        ac.word_start     = word_start;
        // Cap normal-mode alternatives at 8 (was 4) so the new dropdown
        // surfaces meaningful options for big completion sets.
        const size_t max_alts = dot_triggered ? sugg.size() - 1 : 8;
        for (size_t i = 1; i < sugg.size() && ac.alternatives.size() < max_alts; ++i)
            ac.alternatives.push_back(sugg[i]);
        return ac;
    }

    // Tab-accept: TextEditor binds Tab to "indent" and we can't suppress that
    // before its Render(). Trick: detect Tab BEFORE Render, let TextEditor
    // insert "\t", then AFTER Render delete the spurious tab + paste the
    // suggestion in place of the prefix.
    //
    // In template mode `name` is an explicit override (chosen from the popup
    // dropdown); pass "" to use ac.best.
    void accept_autocomplete(::TextEditor& ed, const AutoComplete& ac,
                             const std::string& name_override = "") {
        // After the editor's Tab handler, the caret sits one column past
        // the inserted tab (still on `ac.line` as long as the caret was
        // mid-word). Find the tab and delete it via SetSelection + Delete.
        const auto pos = ed.GetCursorPosition();
        // Defensive: if something else moved the caret, bail.
        if (pos.mLine != ac.line) return;
        const int caret_col = pos.mColumn;
        if (caret_col <= 0) return;
        // Walk left from the caret eating any tab OR space chars TextEditor
        // inserted in response to the Tab key. Different builds insert
        // either a literal '\t' OR N spaces (default 4). Stop the walk
        // when we hit the end of the prefix the user actually typed
        // (ac.word_start + ac.prefix.size() for normal mode, or
        // ac.tpl_insert_col + ac.tpl_prefix.size() for template mode).
        const auto lines_now = ed.GetTextLines();
        if (ac.line < 0 || ac.line >= (int)lines_now.size()) return;
        const std::string& cur_line = lines_now[ac.line];
        const int prefix_end_col = ac.is_template_mode
            ? ac.tpl_insert_col + (int)ac.tpl_prefix.size()
            : ac.word_start      + (int)ac.prefix.size();
        int del_start = caret_col;
        // Walk back through Tab-inserted whitespace. Bounds: del_start - 1
        // must be a valid index into cur_line (>= 0 AND < size). The old
        // loop only checked the upper bound; on edge-case inputs where
        // prefix_end_col was somehow negative or stale, del_start could
        // reach 0 and `cur_line[-1]` would trip the MSVC-debug
        // "string subscript out of range" assertion.
        while (del_start > prefix_end_col &&
               del_start - 1 >= 0 &&
               del_start - 1 < (int)cur_line.size()) {
            const char c = cur_line[del_start - 1];
            if (c != '\t' && c != ' ') break;
            --del_start;
        }
        if (del_start < caret_col) {
            ed.SetSelection({ac.line, del_start}, {ac.line, caret_col},
                             ::TextEditor::SelectionMode::Normal);
            ed.Delete();   // consumes everything Tab inserted
        }

        if (ac.is_template_mode) {
            // In template mode we replace [tpl_insert_col, caret-1) with
            // "<Name>" (closing '>' included). The '<' itself was typed by
            // the user and stays; we only replace the partial name fragment
            // the user has typed so far (tpl_prefix) with "Name>".
            const std::string chosen = name_override.empty() ? ac.best : name_override;
            if (chosen.empty()) return;
            // Per-op tail: Get/Has/Remove take no extra args (entity is
            // implicit via UFCS), so close them as `>()` with caret AFTER
            // the `)` so the user can immediately chain `.field`. Add/Each
            // need an arg, so close as `>(` with caret BEFORE the missing
            // `)` for the user to fill in.
            std::string tail;
            int caret_back_offset = 0;
            if (ac.tpl_op == "Get" || ac.tpl_op == "Has" ||
                ac.tpl_op == "Remove") {
                tail = ">()";
                caret_back_offset = 0;   // caret lands at end of insertion
            } else if (ac.tpl_op == "Add" || ac.tpl_op == "Each") {
                tail = ">()";
                caret_back_offset = 1;   // caret lands inside the parens
            } else {
                tail = ">";              // generic / unknown op
            }
            // tpl_insert_col is the column right after '<', so we replace
            // [tpl_insert_col, tpl_insert_col + tpl_prefix.size()) with "Name<tail>".
            replace_range_with(ed, ac.line, ac.tpl_insert_col,
                               ac.tpl_insert_col + (int)ac.tpl_prefix.size(),
                               chosen + tail);
            if (caret_back_offset > 0) {
                const auto p = ed.GetCursorPosition();
                ed.SetCursorPosition({p.mLine, p.mColumn - caret_back_offset});
            }
        } else {
            // Normal mode: replace the prefix with the suggestion.
            const std::string chosen = name_override.empty() ? ac.best : name_override;
            // Template-op completions: append `<` so accepting drops the
            // user straight into template-mode AC (component picker fires
            // next frame because there's now a `<` immediately preceding
            // the caret). Same trick the engine docs recommend; saves a
            // keystroke + makes the right thing happen by default.
            static const std::set<std::string> template_ops{
                "Add", "Has", "Get", "Each", "Remove",
                "Mut", "MutGet"
            };
            const bool is_tpl_op = template_ops.count(chosen) > 0;
            const std::string tail = is_tpl_op ? "<" : "";
            replace_range_with(ed, ac.line, ac.word_start,
                               ac.word_start + (int)ac.prefix.size(),
                               chosen + tail);
        }
    }

    // ---- Symbol scanning (file-local; basis for go-to-def, rename, outline) -
    // Definitions we recognise (regex-based, no full AST):
    //   - `<Ident>: struct {`           component / struct decl
    //   - `def <Ident>(`                function decl
    // Returns a vector of (name, line) pairs in source order.
    struct LyncSymbol {
        std::string name;
        int         line = 0;     // 0-based
        char        kind = 'f';   // 'f' = func, 's' = struct/component
    };
    std::vector<LyncSymbol> scan_symbols(::TextEditor& ed) {
        std::vector<LyncSymbol> out;
        const auto lines = ed.GetTextLines();
        static const std::regex st_re(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*struct\b)");
        static const std::regex fn_re(R"(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
        for (int i = 0; i < (int)lines.size(); ++i) {
            std::smatch m;
            if (std::regex_search(lines[i], m, fn_re))
                out.push_back({m[1].str(), i, 'f'});
            else if (std::regex_search(lines[i], m, st_re))
                out.push_back({m[1].str(), i, 's'});
        }
        return out;
    }

    // Find the first line that defines `name`. -1 if not found.
    int find_definition_line(::TextEditor& ed, const std::string& name) {
        for (const auto& sym : scan_symbols(ed))
            if (sym.name == name) return sym.line;
        return -1;
    }

    // Project-wide goto-def lookup. Returned `line` is 0-based for the
    // editor; `file` is the absolute, forward-slash path. Empty file
    // string means "not found project-wide" (caller should fall back to
    // file-local search). Resolution order:
    //   1. Rich symbols (post-build): exact name match.
    //   2. Regex by_file_locs (covers files that haven't been built yet
    //      and the active doc before the user saves). Synthetic helpers
    //      (AddX/EachX/...) jump to the parent struct's line.
    struct GotoDefResult {
        std::string file;       // abs forward-slash path, "" = none
        int         line = 0;   // 0-based
        int         col  = 0;
    };
    GotoDefResult find_definition_global(EditorState& s, const std::string& name) {
        GotoDefResult out;
        if (name.empty()) return out;
        auto resolve = [&](const std::string& raw) -> std::string {
            if (raw.empty()) return {};
            std::filesystem::path p(raw);
            std::error_code ec;
            if (p.is_absolute() && std::filesystem::exists(p, ec))
                return Engine::editor::path_str(p);
            // Try project_dir / raw, project_dir/src/raw, raw as-is.
            if (!s.project_dir.empty()) {
                std::filesystem::path proj(s.project_dir);
                auto a = proj / raw;
                if (std::filesystem::exists(a, ec))
                    return Engine::editor::path_str(a);
                auto b = proj / (s.project_source_dir.empty() ? "src" : s.project_source_dir) / raw;
                if (std::filesystem::exists(b, ec))
                    return Engine::editor::path_str(b);
            }
            if (std::filesystem::exists(p, ec))
                return Engine::editor::path_str(p);
            return {};
        };
        if (s.project_symbols.rich_loaded) {
            for (const auto& fn : s.project_symbols.funcs) {
                if (fn.name != name) continue;
                std::string abs = resolve(fn.file);
                if (abs.empty()) break;   // fall through to regex pool
                out.file = std::move(abs);
                // Compiler emits 1-based lines; editor wants 0-based.
                out.line = std::max(0, fn.line - 1);
                out.col  = std::max(0, fn.col  - 1);
                return out;
            }
            for (const auto& st : s.project_symbols.structs) {
                if (st.name != name) continue;
                std::string abs = resolve(st.file);
                if (abs.empty()) break;
                out.file = std::move(abs);
                out.line = std::max(0, st.line - 1);
                out.col  = std::max(0, st.col  - 1);
                return out;
            }
        }
        // Regex fallback: by_file_locs is keyed by abs path already.
        for (const auto& [path, syms] : s.project_symbols.by_file_locs) {
            for (const auto& sym : syms) {
                if (sym.name != name) continue;
                out.file = path;
                out.line = sym.line;
                out.col  = 0;
                return out;
            }
        }
        return out;
    }

    // ---- Find references --------------------------------------------------
    // One match for the references popup.
    struct RefHit {
        std::string file;       // abs forward-slash
        int         line  = 0;  // 0-based
        int         col   = 0;  // 0-based, byte offset in the line
        std::string preview;    // the line, trimmed
    };
    // Scan every .lync file under <project>/<source_dir>/ for word-boundary
    // occurrences of `name`. Skips _zues_*.lync (codegen) and *.__live.*
    // (live-syntax-check temp files). For files currently open in the
    // editor with unsaved edits, we use the editor buffer instead of the
    // on-disk text so the user sees current state.
    std::vector<RefHit> find_references_in_project(EditorState& s,
                                                    const std::string& name) {
        std::vector<RefHit> hits;
        if (name.empty() || s.project_dir.empty()) return hits;
        // Map abs path -> editor buffer for any dirty doc, so we read the
        // live text rather than the stale on-disk version.
        std::unordered_map<std::string, std::string> live;
        for (const auto& d : s.lync_docs) {
            if (!d.editor) continue;
            live[Engine::editor::normalize_path(d.path)] = d.editor->GetText();
        }
        const std::regex re(std::string("\\b") + name + "\\b");
        auto scan_text = [&](const std::string& abs, const std::string& text) {
            std::istringstream iss(text);
            std::string ln;
            int line_no = 0;
            while (std::getline(iss, ln)) {
                auto it = std::sregex_iterator(ln.begin(), ln.end(), re);
                auto end = std::sregex_iterator();
                for (; it != end; ++it) {
                    RefHit h;
                    h.file = abs;
                    h.line = line_no;
                    h.col  = (int)it->position();
                    // Trim leading whitespace for the preview.
                    size_t p = ln.find_first_not_of(" \t");
                    h.preview = (p == std::string::npos) ? ln : ln.substr(p);
                    if (h.preview.size() > 120) h.preview.resize(120);
                    hits.push_back(std::move(h));
                }
                ++line_no;
            }
        };
        const std::filesystem::path src_dir =
            std::filesystem::path(s.project_dir) /
            (s.project_source_dir.empty() ? "src" : s.project_source_dir);
        std::error_code ec;
        if (std::filesystem::exists(src_dir, ec)) {
            for (auto& it : std::filesystem::recursive_directory_iterator(src_dir, ec)) {
                if (!it.is_regular_file(ec)) continue;
                if (it.path().extension() != ".lync") continue;
                const std::string fn = Engine::editor::path_str(it.path().filename());
                if (fn.rfind("_zues_", 0) == 0)              continue;
                if (fn.find(".__live.") != std::string::npos) continue;
                const std::string abs = Engine::editor::path_str(it.path());
                auto liveIt = live.find(abs);
                if (liveIt != live.end()) {
                    scan_text(abs, liveIt->second);
                } else {
                    std::ifstream f(it.path());
                    std::stringstream ss; ss << f.rdbuf();
                    scan_text(abs, ss.str());
                }
            }
        }
        // Also scan any open docs whose path lives outside project src
        // (e.g. user dragged a stray .lync from elsewhere).
        for (const auto& [abs, text] : live) {
            bool already = false;
            for (const auto& h : hits) {
                if (h.file == abs) { already = true; break; }
            }
            if (already) continue;
            scan_text(abs, text);
        }
        return hits;
    }

    // ---- Type-aware hover -------------------------------------------------
    // Format `Func(p1: int, p2: float) -> bool` from a LyncFuncSymbol.
    std::string format_func_signature(const EditorState::LyncFuncSymbol& fn) {
        std::string sig = fn.name + "(";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i) sig += ", ";
            sig += fn.params[i].name;
            if (!fn.params[i].type.empty()) {
                sig += ": ";
                sig += fn.params[i].type;
                if (fn.params[i].nullable) sig += "?";
            }
        }
        sig += ")";
        if (!fn.ret_type.empty() && fn.ret_type != "void") {
            sig += " -> ";
            sig += fn.ret_type;
            if (fn.ret_nullable) sig += "?";
        }
        if (fn.is_extern) sig = "extern " + sig;
        return sig;
    }
    std::string format_struct_signature(const EditorState::LyncStructSymbol& st) {
        std::string sig = st.name + " : struct {";
        for (size_t i = 0; i < st.fields.size(); ++i) {
            if (i) sig += ", ";
            sig += st.fields[i].first;
            if (!st.fields[i].second.empty()) {
                sig += ": ";
                sig += st.fields[i].second;
            }
        }
        sig += "}";
        return sig;
    }

    // Rename all occurrences of `old_name` -> `new_name`, word-boundary
    // matched. Returns count.
    int rename_in_doc(::TextEditor& ed, const std::string& old_name,
                       const std::string& new_name) {
        if (old_name.empty() || old_name == new_name) return 0;
        auto lines = ed.GetTextLines();
        int count = 0;
        const std::regex re(std::string("\\b") + old_name + "\\b");
        for (auto& line : lines) {
            std::string out_line;
            auto begin = std::sregex_iterator(line.begin(), line.end(), re);
            auto end   = std::sregex_iterator();
            size_t pos = 0;
            for (auto it = begin; it != end; ++it) {
                out_line.append(line, pos, it->position() - pos);
                out_line.append(new_name);
                pos = it->position() + it->length();
                ++count;
            }
            out_line.append(line, pos, std::string::npos);
            line.swap(out_line);
        }
        if (count) {
            std::string joined;
            for (size_t i = 0; i < lines.size(); ++i) {
                joined += lines[i];
                if (i + 1 < lines.size()) joined += '\n';
            }
            ed.SetText(joined);
        }
        return count;
    }

    // ---- Editor utility ops --------------------------------------------------
    // All operate on the editor's current text via GetTextLines / SetText.
    // Heavy on copy semantics for now (one full rebuild per op); fine for
    // human-paced editing. Cursor + selection are restored / set explicitly.

    // Duplicate the current line (or every line touched by selection).
    void op_duplicate_line(::TextEditor& ed) {
        auto lines = ed.GetTextLines();
        if (lines.empty()) return;
        const auto pos = ed.GetCursorPosition();
        const int  L = std::clamp(pos.mLine, 0, (int)lines.size() - 1);
        std::vector<std::string> out;
        out.reserve(lines.size() + 1);
        for (int i = 0; i < (int)lines.size(); ++i) {
            out.push_back(lines[i]);
            if (i == L) out.push_back(lines[i]);
        }
        std::string joined;
        for (size_t i = 0; i < out.size(); ++i) {
            joined += out[i];
            if (i + 1 < out.size()) joined += '\n';
        }
        ed.SetText(joined);
        ed.SetCursorPosition({L + 1, pos.mColumn});
    }

    // Move the current line up or down by one. Direction = -1 or +1.
    void op_move_line(::TextEditor& ed, int dir) {
        if (dir != -1 && dir != +1) return;
        auto lines = ed.GetTextLines();
        if (lines.size() < 2) return;
        const auto pos = ed.GetCursorPosition();
        const int  L = std::clamp(pos.mLine, 0, (int)lines.size() - 1);
        const int  N = L + dir;
        if (N < 0 || N >= (int)lines.size()) return;
        std::swap(lines[L], lines[N]);
        std::string joined;
        for (size_t i = 0; i < lines.size(); ++i) {
            joined += lines[i];
            if (i + 1 < lines.size()) joined += '\n';
        }
        ed.SetText(joined);
        ed.SetCursorPosition({N, pos.mColumn});
    }

    // Resolve the line range covered by the current selection (or the
    // caret line if there's no selection). TextEditor doesn't expose
    // selection start/end publicly; we recover them by counting newlines
    // in GetSelectedText() and using the caret as the "selection end".
    // Returns inclusive [start, end] line indices.
    void selection_line_range(::TextEditor& ed, int& out_start, int& out_end) {
        const auto pos = ed.GetCursorPosition();
        out_start = out_end = pos.mLine;
        if (!ed.HasSelection()) return;
        const std::string sel = ed.GetSelectedText();
        const int nls = (int)std::count(sel.begin(), sel.end(), '\n');
        // The caret sits at one end of the selection — typically the
        // anchor's *end* in TextEditor's selection model. Work back from
        // there. If the caret column is 0 and the selection ends with
        // a newline, the caret line itself isn't really part of the
        // body the user meant to comment / indent, so trim it.
        int end_line = pos.mLine;
        int start_line = end_line - nls;
        if (start_line < 0) start_line = 0;
        // Trailing-newline-only-on-last-line trim: matches the JetBrains
        // convention (Shift+Down then Tab indents the visually selected
        // lines, not the row below).
        if (pos.mColumn == 0 && end_line > start_line) end_line -= 1;
        out_start = start_line;
        out_end   = end_line;
    }

    // Replace [start_line, end_line] with `new_lines` and rebuild the
    // editor text. Caret is parked at the end of the replaced span.
    void apply_line_block_edit(::TextEditor& ed, int start_line, int end_line,
                                const std::vector<std::string>& new_lines) {
        auto lines = ed.GetTextLines();
        if (start_line < 0) start_line = 0;
        if (end_line >= (int)lines.size()) end_line = (int)lines.size() - 1;
        if (end_line < start_line) return;
        std::vector<std::string> out;
        out.reserve(lines.size() - (end_line - start_line + 1) + new_lines.size());
        for (int i = 0; i < start_line; ++i) out.push_back(std::move(lines[i]));
        for (auto& l : new_lines)            out.push_back(l);
        for (int i = end_line + 1; i < (int)lines.size(); ++i)
            out.push_back(std::move(lines[i]));
        std::string joined;
        for (size_t i = 0; i < out.size(); ++i) {
            joined += out[i];
            if (i + 1 < out.size()) joined += '\n';
        }
        ed.SetText(joined);
    }

    // Toggle a `// ` prefix on the current line OR every line in the
    // selection. If ANY targeted line is uncommented, comment them all
    // (using the minimum-indent column as the prefix anchor); otherwise
    // strip the `// ` (or `//` without trailing space) from each.
    void op_toggle_line_comment(::TextEditor& ed) {
        auto lines = ed.GetTextLines();
        if (lines.empty()) return;
        int start_line = 0, end_line = 0;
        selection_line_range(ed, start_line, end_line);
        if (end_line >= (int)lines.size()) end_line = (int)lines.size() - 1;
        // First pass: all lines already commented?
        bool all_commented = true;
        size_t min_indent = SIZE_MAX;
        for (int L = start_line; L <= end_line; ++L) {
            const std::string& line = lines[L];
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i == line.size()) continue;   // blank line ignored for the test
            if (i < min_indent) min_indent = i;
            const bool c = i + 2 <= line.size() && line[i] == '/' && line[i + 1] == '/';
            if (!c) all_commented = false;
        }
        if (min_indent == SIZE_MAX) min_indent = 0;
        std::vector<std::string> out;
        out.reserve(end_line - start_line + 1);
        for (int L = start_line; L <= end_line; ++L) {
            std::string line = lines[L];
            if (all_commented) {
                size_t i = 0;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                if (i + 2 <= line.size() && line[i] == '/' && line[i + 1] == '/') {
                    const size_t skip =
                        (i + 3 <= line.size() && line[i + 2] == ' ') ? 3 : 2;
                    line.erase(i, skip);
                }
            } else {
                // Skip blank lines so we don't pollute them with a stray
                // `// ` prefix.
                bool blank = true;
                for (char c : line) if (c != ' ' && c != '\t') { blank = false; break; }
                if (!blank) {
                    if (line.size() >= min_indent) line.insert(min_indent, "// ");
                    else                            line.insert(0, "// ");
                }
            }
            out.push_back(std::move(line));
        }
        const auto saved_caret = ed.GetCursorPosition();
        apply_line_block_edit(ed, start_line, end_line, out);
        ed.SetCursorPosition(saved_caret);
    }

    // Re-establish a selection covering [start_line..end_line] inclusive
    // so the user can chain Tab presses to keep indenting / dedenting
    // the same block. Anchors at column 0 of start_line and end of
    // end_line. No-op for single-line "selections" (caret moves only).
    void reselect_block(::TextEditor& ed, int start_line, int end_line,
                         bool had_selection) {
        if (!had_selection || start_line == end_line) return;
        const auto lines = ed.GetTextLines();
        if (end_line >= (int)lines.size()) end_line = (int)lines.size() - 1;
        const int end_col = (int)lines[end_line].size();
        ed.SetSelection({start_line, 0}, {end_line, end_col},
                         ::TextEditor::SelectionMode::Normal);
        ed.SetCursorPosition({end_line, end_col});
    }

    // Indent every line in the selection by one tab-step (4 spaces).
    // No selection -> indent the caret line. The selection (if any)
    // is restored after the edit so chaining Tab keeps stepping the
    // same block.
    void op_indent_selection(::TextEditor& ed) {
        auto lines = ed.GetTextLines();
        if (lines.empty()) return;
        const bool had_sel = ed.HasSelection();
        int start_line = 0, end_line = 0;
        selection_line_range(ed, start_line, end_line);
        const std::string ind(4, ' ');
        std::vector<std::string> out;
        out.reserve(end_line - start_line + 1);
        for (int L = start_line; L <= end_line; ++L)
            out.push_back(ind + lines[L]);
        const auto saved = ed.GetCursorPosition();
        apply_line_block_edit(ed, start_line, end_line, out);
        ed.SetCursorPosition({saved.mLine, saved.mColumn + 4});
        reselect_block(ed, start_line, end_line, had_sel);
    }
    // Dedent every line in the selection by up to one tab-step. A line
    // with less than 4 leading spaces (or 1 tab) is dedented to col 0.
    void op_dedent_selection(::TextEditor& ed) {
        auto lines = ed.GetTextLines();
        if (lines.empty()) return;
        const bool had_sel = ed.HasSelection();
        int start_line = 0, end_line = 0;
        selection_line_range(ed, start_line, end_line);
        std::vector<std::string> out;
        out.reserve(end_line - start_line + 1);
        int caret_dx_on_caret_line = 0;
        const auto saved = ed.GetCursorPosition();
        for (int L = start_line; L <= end_line; ++L) {
            const std::string& line = lines[L];
            int strip = 0;
            if (!line.empty() && line[0] == '\t')      strip = 1;
            else {
                while (strip < 4 && strip < (int)line.size() &&
                       line[strip] == ' ') ++strip;
            }
            if (L == saved.mLine) caret_dx_on_caret_line = -strip;
            out.push_back(line.substr(strip));
        }
        apply_line_block_edit(ed, start_line, end_line, out);
        const int new_col = std::max(0, saved.mColumn + caret_dx_on_caret_line);
        ed.SetCursorPosition({saved.mLine, new_col});
        reselect_block(ed, start_line, end_line, had_sel);
    }

    // Smart Home: toggle between column 0 and the first non-whitespace
    // column on the current line. Sublime / VS Code default. If the
    // caret is on the first non-ws column or to its right, Home goes
    // to col 0; otherwise (caret in the leading indent or already at
    // col 0) Home goes to the first non-ws column.
    void op_smart_home(::TextEditor& ed, bool extend_selection) {
        const auto pos = ed.GetCursorPosition();
        const auto lines = ed.GetTextLines();
        if (pos.mLine < 0 || pos.mLine >= (int)lines.size()) return;
        const std::string& line = lines[pos.mLine];
        int first = 0;
        while (first < (int)line.size() &&
               (line[first] == ' ' || line[first] == '\t')) ++first;
        const int target = (pos.mColumn == first) ? 0 : first;
        if (extend_selection) {
            // Anchor the selection at the existing caret, extend to target.
            // TextEditor's SetSelection takes (start, end, mode) — we use
            // current caret as start so the visible selection matches.
            ed.SetSelection(pos, {pos.mLine, target},
                             ::TextEditor::SelectionMode::Normal);
        }
        ed.SetCursorPosition({pos.mLine, target});
    }

    // Count total occurrences of `needle` in the editor (case toggle).
    // Used by the find bar to display "n of N" style match counts.
    int op_count_matches(::TextEditor& ed, const std::string& needle,
                          bool case_sensitive) {
        if (needle.empty()) return 0;
        const auto lines = ed.GetTextLines();
        int count = 0;
        for (const auto& line : lines) {
            const size_t hn = line.size(), nn = needle.size();
            for (size_t i = 0; i + nn <= hn; ) {
                bool ok = true;
                for (size_t k = 0; k < nn; ++k) {
                    const char a = case_sensitive ? line[i + k]
                        : (char)std::tolower((unsigned char)line[i + k]);
                    const char b = case_sensitive ? needle[k]
                        : (char)std::tolower((unsigned char)needle[k]);
                    if (a != b) { ok = false; break; }
                }
                if (ok) { ++count; i += std::max((size_t)1, nn); }
                else    { ++i; }
            }
        }
        return count;
    }

    // Find PREVIOUS occurrence of `needle` from the caret position. Wraps
    // to end of file. Mirror of op_find_next; selects the match the same
    // way so F3 / Shift+F3 produce a smooth cycle.
    bool op_find_prev(::TextEditor& ed, const std::string& needle, bool case_sensitive) {
        if (needle.empty()) return false;
        const auto lines = ed.GetTextLines();
        const auto pos = ed.GetCursorPosition();
        auto rfind = [&](const std::string& hay, size_t off) -> size_t {
            // off is the EXCLUSIVE upper bound (search [0, off) backwards).
            const size_t nn = needle.size();
            if (nn == 0 || off < nn) return std::string::npos;
            for (size_t i = off - nn; ; --i) {
                bool ok = true;
                for (size_t k = 0; k < nn; ++k) {
                    const char a = case_sensitive ? hay[i + k]
                        : (char)std::tolower((unsigned char)hay[i + k]);
                    const char b = case_sensitive ? needle[k]
                        : (char)std::tolower((unsigned char)needle[k]);
                    if (a != b) { ok = false; break; }
                }
                if (ok) return i;
                if (i == 0) break;
            }
            return std::string::npos;
        };
        for (int L = pos.mLine; L >= 0; --L) {
            const size_t cap = (L == pos.mLine)
                ? (size_t)std::max(0, pos.mColumn - 1) + 0   // exclusive of caret
                : lines[L].size();
            const size_t hit = rfind(lines[L], cap);
            if (hit != std::string::npos) {
                ed.SetSelection({L, (int)hit}, {L, (int)(hit + needle.size())},
                                 ::TextEditor::SelectionMode::Normal);
                ed.SetCursorPosition({L, (int)hit});
                return true;
            }
        }
        for (int L = (int)lines.size() - 1; L >= pos.mLine; --L) {
            const size_t hit = rfind(lines[L], lines[L].size());
            if (hit != std::string::npos) {
                ed.SetSelection({L, (int)hit}, {L, (int)(hit + needle.size())},
                                 ::TextEditor::SelectionMode::Normal);
                ed.SetCursorPosition({L, (int)hit});
                return true;
            }
        }
        return false;
    }

    // Find next occurrence of `needle` from caret. Wraps. Selects the match.
    // Returns true if a match was found.
    bool op_find_next(::TextEditor& ed, const std::string& needle, bool case_sensitive) {
        if (needle.empty()) return false;
        const auto lines = ed.GetTextLines();
        const auto pos = ed.GetCursorPosition();
        auto match = [&](const std::string& hay, size_t off) -> size_t {
            if (case_sensitive) return hay.find(needle, off);
            // Case-insensitive search.
            const size_t hn = hay.size(), nn = needle.size();
            if (off + nn > hn) return std::string::npos;
            for (size_t i = off; i + nn <= hn; ++i) {
                bool ok = true;
                for (size_t k = 0; k < nn; ++k) {
                    if (std::tolower((unsigned char)hay[i + k])
                        != std::tolower((unsigned char)needle[k])) { ok = false; break; }
                }
                if (ok) return i;
            }
            return std::string::npos;
        };
        // Search from caret onward, then wrap.
        for (int L = pos.mLine; L < (int)lines.size(); ++L) {
            const size_t start = (L == pos.mLine) ? pos.mColumn : 0;
            const size_t hit = match(lines[L], start);
            if (hit != std::string::npos) {
                ed.SetSelection({L, (int)hit}, {L, (int)(hit + needle.size())},
                                 ::TextEditor::SelectionMode::Normal);
                ed.SetCursorPosition({L, (int)(hit + needle.size())});
                return true;
            }
        }
        for (int L = 0; L <= pos.mLine; ++L) {
            const size_t hit = match(lines[L], 0);
            if (hit != std::string::npos) {
                ed.SetSelection({L, (int)hit}, {L, (int)(hit + needle.size())},
                                 ::TextEditor::SelectionMode::Normal);
                ed.SetCursorPosition({L, (int)(hit + needle.size())});
                return true;
            }
        }
        return false;
    }

    // Replace ALL occurrences of `needle` with `repl`. Returns count.
    int op_replace_all(::TextEditor& ed, const std::string& needle,
                        const std::string& repl, bool case_sensitive) {
        if (needle.empty()) return 0;
        auto lines = ed.GetTextLines();
        int count = 0;
        for (auto& line : lines) {
            size_t off = 0;
            while (off + needle.size() <= line.size()) {
                bool match = true;
                for (size_t k = 0; k < needle.size(); ++k) {
                    const char a = case_sensitive ? line[off + k]
                        : (char)std::tolower((unsigned char)line[off + k]);
                    const char b = case_sensitive ? needle[k]
                        : (char)std::tolower((unsigned char)needle[k]);
                    if (a != b) { match = false; break; }
                }
                if (match) {
                    line.replace(off, needle.size(), repl);
                    off += repl.size();
                    ++count;
                } else {
                    ++off;
                }
            }
        }
        if (count) {
            std::string joined;
            for (size_t i = 0; i < lines.size(); ++i) {
                joined += lines[i];
                if (i + 1 < lines.size()) joined += '\n';
            }
            ed.SetText(joined);
        }
        return count;
    }

    // ---- Find / Replace bar (per-doc, lives above the TextEditor) ----------
    void draw_find_replace_bar(EditorState::LyncDoc& d) {
        if (!d.find_open && !d.replace_open) return;
        ImGui::PushID(("##find_" + d.path).c_str());

        if (d.find_focus_pending) {
            ImGui::SetKeyboardFocusHere();
            d.find_focus_pending = false;
        }
        ImGui::SetNextItemWidth(220);
        const bool find_enter = ImGui::InputText("##find", d.find_buf,
                                                  sizeof(d.find_buf),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
        // Pressing Enter inside the find input = next match. Shift+Enter
        // = previous (matches VS Code / JetBrains).
        const ImGuiIO& fio = ImGui::GetIO();
        if (find_enter) {
            if (fio.KeyShift) op_find_prev(*d.editor, d.find_buf, d.find_case_sensitive);
            else              op_find_next(*d.editor, d.find_buf, d.find_case_sensitive);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("<##find_prev"))
            op_find_prev(*d.editor, d.find_buf, d.find_case_sensitive);
        ImGui::SameLine();
        if (ImGui::SmallButton(">##find_next"))
            op_find_next(*d.editor, d.find_buf, d.find_case_sensitive);
        ImGui::SameLine();
        ImGui::Checkbox("Aa", &d.find_case_sensitive);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Case-sensitive (Alt+C)");
        ImGui::SameLine();
        // Match count. Re-counted every frame; cheap for human-paced edits.
        const int total =
            d.find_buf[0] ? op_count_matches(*d.editor, d.find_buf,
                                              d.find_case_sensitive) : 0;
        if (d.find_buf[0])
            ImGui::TextDisabled("(%d match%s)", total, total == 1 ? "" : "es");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##find_close")) {
            d.find_open = false; d.replace_open = false;
        }

        if (d.replace_open) {
            ImGui::SetNextItemWidth(220);
            const bool repl_enter = ImGui::InputText("##repl", d.replace_buf,
                                                      sizeof(d.replace_buf),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Replace") || repl_enter) {
                // Replace the currently-selected match (if any) and
                // advance to the next. Mirrors VS Code's Replace.
                if (d.editor->HasSelection()) {
                    const std::string sel = d.editor->GetSelectedText();
                    bool eq = sel.size() == std::strlen(d.find_buf);
                    if (eq) {
                        for (size_t k = 0; k < sel.size(); ++k) {
                            const char a = d.find_case_sensitive ? sel[k]
                                : (char)std::tolower((unsigned char)sel[k]);
                            const char b = d.find_case_sensitive ? d.find_buf[k]
                                : (char)std::tolower((unsigned char)d.find_buf[k]);
                            if (a != b) { eq = false; break; }
                        }
                    }
                    if (eq) {
                        d.editor->Delete();
                        d.editor->InsertText(d.replace_buf);
                    }
                }
                op_find_next(*d.editor, d.find_buf, d.find_case_sensitive);
            }
            ImGui::SameLine();
            if (ImGui::Button("Replace All")) {
                const int n = op_replace_all(*d.editor, d.find_buf,
                                              d.replace_buf, d.find_case_sensitive);
                if (n > 0) d.dirty = true;
            }
        }
        // Esc closes the bar (only if it currently has keyboard focus,
        // so global Esc handlers elsewhere keep working). Alt+C toggles
        // the case-sensitive flag from anywhere in the bar.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            d.find_open = false; d.replace_open = false;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                fio.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            d.find_case_sensitive = !d.find_case_sensitive;
        }
        ImGui::PopID();
    }

    // ---- Go-to-line popup --------------------------------------------------
    void draw_goto_line_popup(EditorState::LyncDoc& d) {
        if (!d.goto_line_open) return;
        ImGui::OpenPopup("Go to line");
        d.goto_line_open = false;
    }
    void draw_goto_line_modal(EditorState::LyncDoc& d) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                       vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Go to line", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::SetNextItemWidth(140);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("Line", d.goto_line_buf,
                                                  sizeof(d.goto_line_buf),
                                                  ImGuiInputTextFlags_CharsDecimal |
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            const int total = d.editor ? d.editor->GetTotalLines() : 0;
            ImGui::TextDisabled("(1 - %d)", total);
            if (enter || ImGui::Button("Go", ImVec2(120, 0))) {
                int n = std::atoi(d.goto_line_buf);
                if (n < 1) n = 1;
                if (n > total) n = total;
                if (d.editor) {
                    d.editor->SetCursorPosition({n - 1, 0});
                }
                d.goto_line_buf[0] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                d.goto_line_buf[0] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- Live syntax check (debounced, background) -------------------------
    // Each frame we tick the per-doc idle timer. When >=600ms passes since
    // the last edit AND the buffer has changed since last check AND no
    // worker is in flight, kick a worker. The worker writes the buffer to
    // a temp .lync, runs the lync compiler against it (with the project's
    // plugin/include/prelude), and captures stderr+stdout. UI thread polls
    // the future each frame; when ready, parses + applies markers.
    struct LiveCheckJob {
        std::string source_path;          // user's real .lync (for marker mapping)
        std::string output;               // captured compiler output
        bool        ok = false;
    };
    static std::future<LiveCheckJob> g_live_future;

    std::string build_live_check_cmd(const EditorState& s,
                                      const std::string& temp_lync,
                                      const std::string& temp_dll) {
        if (s.lync_compiler_abs.empty()) return {};
        auto q = [](const std::string& v) { return std::string("\"") + v + "\""; };
        std::string cmd = q(s.lync_compiler_abs) + " " + q(temp_lync)
                        + " --target=dll";
        if (!s.lync_plugin_abs.empty())  cmd += " --plugin="  + q(s.lync_plugin_abs);
        if (!s.lync_include_abs.empty()) cmd += " --include=" + q(s.lync_include_abs);
        if (!s.lync_prelude_abs.empty()) cmd += " --prelude=" + q(s.lync_prelude_abs);
        cmd += " -o " + q(temp_dll);
        // No shell redirects / outer wrap — run_capture pipes stderr -> stdout
        // via CreateProcess handles directly.
        return cmd;
    }

    LiveCheckJob run_live_check(EditorState s_snapshot,
                                 std::string buf_text,
                                 std::string source_path) {
        LiveCheckJob job;
        job.source_path = source_path;

        // Write the buffer to a temp .lync next to the original (so includes
        // resolve relative to the same project structure).
        std::error_code ec;
        const std::filesystem::path orig(source_path);
        const std::filesystem::path tmp_lync =
            orig.parent_path() / (orig.stem().string() + ".__live.lync");
        const std::filesystem::path tmp_dll =
            std::filesystem::temp_directory_path(ec) / "zues_live_check.dll";
        {
            std::ofstream f(tmp_lync, std::ios::binary | std::ios::trunc);
            if (!f) { job.ok = false; return job; }
            f.write(buf_text.data(), (std::streamsize)buf_text.size());
        }
        const std::string cmd = build_live_check_cmd(s_snapshot,
            Engine::editor::path_str(tmp_lync), Engine::editor::path_str(tmp_dll));
        if (cmd.empty()) {
            std::filesystem::remove(tmp_lync, ec);
            return job;
        }
        // Spawn lync.exe with stdout+stderr captured into job.output via the
        // shared run_capture helper (CreateProcess + CREATE_NO_WINDOW on Win)
        // so live-check on every file switch doesn't flash a cmd window.
        const int rc = Engine::editor::run_capture(cmd, job.output);
        if (rc < 0) {
            std::filesystem::remove(tmp_lync, ec);
            return job;
        }
        job.ok = (rc == 0);
        // Cleanup. Compiler errors may keep an intermediate .c; rm it too.
        std::filesystem::remove(tmp_lync, ec);
        std::filesystem::remove(tmp_dll, ec);
        std::filesystem::remove(orig.parent_path() /
            (orig.stem().string() + ".__live.c"), ec);
        return job;
    }

    // Per-frame tick. Triggers a background check when conditions are met.
    void tick_live_check(EditorState& s, float dt) {
        if (!s.lync_live_check) return;
        if (s.lync_active_doc < 0 ||
            s.lync_active_doc >= (int)s.lync_docs.size()) return;
        auto& d = s.lync_docs[s.lync_active_doc];
        if (!d.editor) return;
        const std::string& path = d.path;

        const std::string text = d.editor->GetText();
        auto& prev = s.live_check.prev_frame_text[path];   // last frame
        auto& last = s.live_check.last_text[path];         // last checked
        auto& idle = s.live_check.idle_secs[path];

        // Idle accumulator: reset to zero only on a THIS-FRAME edit.
        // Previously we compared against `last` (the last-checked snapshot)
        // which is "" on doc open - that fired idle=0 every frame and the
        // trigger never reached its threshold. Comparing against the prev
        // frame's text keeps the timer ticking once typing pauses.
        if (text != prev) idle = 0.0f;
        else              idle += dt;
        prev = text;

        // Drain a finished worker (any path) and apply markers - only to
        // the file the worker checked, never to other docs.
        if (g_live_future.valid() &&
            g_live_future.wait_for(std::chrono::seconds(0))
                == std::future_status::ready) {
            LiveCheckJob job = g_live_future.get();
            apply_lync_diagnostics_for_file(s, job.output, job.source_path);
        }

        // Trigger a new worker when:
        //   - typing has been idle >= 600 ms
        //   - text differs from last checked snapshot
        //   - no worker currently in flight
        // Wait this long after the user stops typing before kicking off a
        // live syntax check. 0.6s was too eager - the compiler fired
        // mid-keystroke on long names and the editor felt jittery.
        // 1.5s is the sweet spot: typing pauses naturally cluster around
        // 1-2s, so realtime feedback still kicks in but not on every keystroke.
        constexpr float DEBOUNCE_S = 1.5f;
        const bool busy = g_live_future.valid() &&
            g_live_future.wait_for(std::chrono::seconds(0))
                != std::future_status::ready;
        if (idle >= DEBOUNCE_S && text != last && !busy) {
            last = text;
            EditorState snap;
            snap.lync_compiler_abs = s.lync_compiler_abs;
            snap.lync_plugin_abs   = s.lync_plugin_abs;
            snap.lync_include_abs  = s.lync_include_abs;
            snap.lync_prelude_abs  = s.lync_prelude_abs;
            g_live_future = std::async(std::launch::async,
                run_live_check, snap, text, path);
        }
    }

    // ---- F2 rename modal (per doc) -----------------------------------------
    void draw_rename_popup(EditorState::LyncDoc& d) {
        if (!d.rename_open) return;
        ImGui::OpenPopup("Rename");
        d.rename_open = false;
    }

    // Project-wide rename. For each .lync file in <project>/<src>/:
    //   - If a matching open doc exists, renames in the editor buffer
    //     (so live changes become the source of truth).
    //   - Otherwise rewrites the file on disk.
    // Returns total occurrences replaced. Caller is expected to have
    // confirmed the operation (it touches files on disk).
    int rename_in_project(EditorState& s,
                           const std::string& old_name,
                           const std::string& new_name) {
        if (old_name.empty() || old_name == new_name || s.project_dir.empty())
            return 0;
        const std::regex re(std::string("\\b") + old_name + "\\b");
        int total = 0;
        // 1) Open docs first (in-memory replace).
        std::unordered_set<std::string> handled;
        for (auto& d : s.lync_docs) {
            if (!d.editor) continue;
            const int n = rename_in_doc(*d.editor, old_name, new_name);
            if (n > 0) d.dirty = true;
            total += n;
            handled.insert(Engine::editor::normalize_path(d.path));
        }
        // 2) On-disk files not currently open.
        const std::filesystem::path src_dir =
            std::filesystem::path(s.project_dir) /
            (s.project_source_dir.empty() ? "src" : s.project_source_dir);
        std::error_code ec;
        if (!std::filesystem::exists(src_dir, ec)) return total;
        for (auto& it : std::filesystem::recursive_directory_iterator(src_dir, ec)) {
            if (!it.is_regular_file(ec)) continue;
            if (it.path().extension() != ".lync") continue;
            const std::string fn = Engine::editor::path_str(it.path().filename());
            if (fn.rfind("_zues_", 0) == 0)              continue;
            if (fn.find(".__live.") != std::string::npos) continue;
            const std::string abs = Engine::editor::path_str(it.path());
            if (handled.count(abs)) continue;
            std::ifstream in(it.path());
            std::stringstream ss; ss << in.rdbuf();
            const std::string text = ss.str();
            std::string out_text;
            int n = 0;
            auto bgn = std::sregex_iterator(text.begin(), text.end(), re);
            auto e2  = std::sregex_iterator();
            size_t pos = 0;
            for (auto i2 = bgn; i2 != e2; ++i2) {
                out_text.append(text, pos, i2->position() - pos);
                out_text.append(new_name);
                pos = i2->position() + i2->length();
                ++n;
            }
            if (n > 0) {
                out_text.append(text, pos, std::string::npos);
                std::ofstream of(it.path(), std::ios::binary | std::ios::trunc);
                if (of) of.write(out_text.data(), (std::streamsize)out_text.size());
                total += n;
            }
        }
        return total;
    }

    void draw_rename_modal(EditorState::LyncDoc& d, EditorState& s) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                       vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Rename", nullptr,
                ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled("rename '%s'", d.rename_old.c_str());
            ImGui::SetNextItemWidth(360);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("New", d.rename_buf,
                                                  sizeof(d.rename_buf),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Checkbox("across project (all .lync files)",
                            &s.lync_rename_across_project);
            ImGui::TextDisabled(s.lync_rename_across_project
                ? "writes other files to disk; undo is per-file"
                : "rename stays within this tab");
            ImGui::Separator();
            // Live preview: scan project for word-boundary matches of
            // the OLD name (the new name doesn't change which sites get
            // hit). Group by file. Same logic as Find references; cheap
            // for human-paced typing in the modal.
            ImGui::TextDisabled("Affected sites:");
            auto raw = s.lync_rename_across_project
                ? find_references_in_project(s, d.rename_old)
                : std::vector<RefHit>{};
            int total_in_doc = 0;
            if (d.editor) {
                const std::regex re(std::string("\\b") + d.rename_old + "\\b");
                const auto lines = d.editor->GetTextLines();
                for (const auto& l : lines) {
                    auto it = std::sregex_iterator(l.begin(), l.end(), re);
                    auto end = std::sregex_iterator();
                    for (; it != end; ++it) ++total_in_doc;
                }
            }
            ImGui::BeginChild("##rename_preview", ImVec2(0, 180), true);
            if (s.lync_rename_across_project) {
                std::string current_file;
                int per_file = 0;
                auto flush = [&]() {
                    if (current_file.empty()) return;
                    std::string label = current_file;
                    if (!s.project_dir.empty()) {
                        auto pd = Engine::editor::normalize_path(s.project_dir);
                        if (label.rfind(pd, 0) == 0) {
                            label = label.substr(pd.size());
                            if (!label.empty() && (label[0] == '/' || label[0] == '\\'))
                                label.erase(0, 1);
                        }
                    }
                    ImGui::Text("  %s  ", label.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d)", per_file);
                };
                for (const auto& h : raw) {
                    if (h.file != current_file) {
                        flush();
                        current_file = h.file;
                        per_file = 0;
                    }
                    ++per_file;
                }
                flush();
                if (raw.empty())
                    ImGui::TextDisabled("(no occurrences)");
            } else {
                ImGui::Text("  %s  ", d.display.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%d)", total_in_doc);
            }
            ImGui::EndChild();
            // Total count line.
            const int grand = s.lync_rename_across_project
                ? (int)raw.size() : total_in_doc;
            ImGui::TextDisabled("%d occurrence%s%s",
                grand, grand == 1 ? "" : "s",
                d.rename_buf[0] && std::strcmp(d.rename_buf, d.rename_old.c_str()) != 0
                    ? "" : "  (new name same as old — nothing will change)");
            const bool ok = d.rename_buf[0] != 0
                         && std::strcmp(d.rename_buf, d.rename_old.c_str()) != 0;
            if ((enter || ImGui::Button("Rename", ImVec2(140, 0))) && ok) {
                int n = 0;
                if (s.lync_rename_across_project) {
                    n = rename_in_project(s, d.rename_old, d.rename_buf);
                } else {
                    n = rename_in_doc(*d.editor, d.rename_old, d.rename_buf);
                    if (n > 0) d.dirty = true;
                }
                show_toast(s, (std::string("renamed ") + std::to_string(n)
                                + " occurrence" + (n == 1 ? "" : "s")).c_str());
                d.rename_buf[0] = 0;
                d.rename_old.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(140, 0)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                d.rename_buf[0] = 0;
                d.rename_old.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- Find references popup ---------------------------------------------
    // Modal listing every word-boundary hit for `s.lync_refs.query` across
    // .lync files in the project src dir. Click a row to jump (and push
    // a back-stack entry so Ctrl+- returns).
    void draw_lync_refs_popup(EditorState& s) {
        if (!s.lync_refs.open) return;
        ImGui::OpenPopup("Find references");
        s.lync_refs.open = false;
    }
    void draw_lync_refs_modal(EditorState& s) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                       vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Find references", nullptr,
                ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled("References to '%s'  (%d hit%s)",
                s.lync_refs.query.c_str(),
                (int)s.lync_refs.hits_.size(),
                s.lync_refs.hits_.size() == 1 ? "" : "s");
            ImGui::Separator();
            // Group hits by file for readability.
            std::string current_file;
            ImGui::BeginChild("##refs_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
            for (size_t i = 0; i < s.lync_refs.hits_.size(); ++i) {
                const auto& h = s.lync_refs.hits_[i];
                if (h.file != current_file) {
                    current_file = h.file;
                    ImGui::Spacing();
                    // Show the relative path from project_dir if possible.
                    std::string label = h.file;
                    if (!s.project_dir.empty()) {
                        auto pd = Engine::editor::normalize_path(s.project_dir);
                        if (label.rfind(pd, 0) == 0) {
                            label = label.substr(pd.size());
                            if (!label.empty() && (label[0] == '/' || label[0] == '\\'))
                                label.erase(0, 1);
                        }
                    }
                    ImGui::TextColored(ImVec4(0.6f,0.8f,1.0f,1.0f), "%s", label.c_str());
                }
                ImGui::PushID((int)i);
                char rowbuf[256];
                std::snprintf(rowbuf, sizeof(rowbuf),
                              "  %4d:%-3d  %s##r%zu",
                              h.line + 1, h.col + 1, h.preview.c_str(), i);
                if (ImGui::Selectable(rowbuf, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    // Push current caret onto back-stack before jumping.
                    if (s.lync_active_doc >= 0 &&
                        s.lync_active_doc < (int)s.lync_docs.size()) {
                        auto& cur = s.lync_docs[s.lync_active_doc];
                        if (cur.editor) {
                            EditorState::LyncJumpFrame fr;
                            fr.file = cur.path;
                            const auto cp = cur.editor->GetCursorPosition();
                            fr.line = cp.mLine; fr.col = cp.mColumn;
                            s.lync_jump_back.push_back(std::move(fr));
                            if (s.lync_jump_back.size() > 32)
                                s.lync_jump_back.erase(s.lync_jump_back.begin());
                        }
                    }
                    open_lync_doc(s, h.file);
                    if (s.lync_active_doc >= 0 &&
                        s.lync_active_doc < (int)s.lync_docs.size()) {
                        auto& nd = s.lync_docs[s.lync_active_doc];
                        if (nd.editor)
                            nd.editor->SetCursorPosition({h.line, h.col});
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            if (s.lync_refs.hits_.empty())
                ImGui::TextDisabled("(no occurrences in project src)");
            ImGui::EndChild();
            if (ImGui::Button("Close", ImVec2(120, 0)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- Settings popup -----------------------------------------------------
    void draw_lync_settings_popup(EditorState& s) {
        if (!s.lync_show_settings) return;
        ImGui::OpenPopup("Lync Editor Settings");
        s.lync_show_settings = false;
    }
    void draw_lync_settings_modal(EditorState& s) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                       vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Lync Editor Settings", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Checkbox("Rainbow brackets",       &s.lync_rainbow);
            ImGui::Checkbox("Word wrap",              &s.lync_word_wrap);
            ImGui::Checkbox("Auto-close () [] {} \" '", &s.lync_auto_close);
            ImGui::Checkbox("Bottom status bar",      &s.lync_status_bar);
            ImGui::Checkbox("Live syntax check (idle)",&s.lync_live_check);
            ImGui::Spacing();
            ImGui::TextDisabled("Brace style (used by smart-Enter / auto-format):");
            int bs = (int)s.lync_brace_style;
            ImGui::RadioButton("Same line  { ... }", &bs, 0); ImGui::SameLine();
            ImGui::RadioButton("New line\\n{ ... }", &bs, 1);
            s.lync_brace_style = (EditorState::LyncBraceStyle)bs;
            ImGui::Spacing();
            ImGui::TextDisabled("Visual cut-line column (always visible):");
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderInt("##wrap_col", &s.lync_wrap_col, 40, 200);
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Forward declaration -- wrlt_save_open_tabs is defined later in this
    // same anonymous namespace (after the draw functions that call it).
    void wrlt_save_open_tabs(const EditorState& s);

    void draw_lync_editor_body(EditorState& s) {
        if (s.lync_docs.empty()) {
            ImGui::TextDisabled("No file open. Pick a .lync from the Source pane.");
            return;
        }

        if (ImGui::BeginTabBar("##lync_tabs",
                ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            int close_idx = -1;
            for (int i = 0; i < static_cast<int>(s.lync_docs.size()); ++i) {
                auto& d = s.lync_docs[i];
                ensure_editor(d);
                // Refresh project symbols + the editor's known-identifier set
                // so user-declared struct types render colored.
                refresh_project_symbols(s);
                apply_live_language_def(s, d);

                // No " *" suffix on the label — the UnsavedDocument
                // flag below draws the modified-state indicator on its
                // own. Doubling up made the tab feel busy.
                char label[280];
                std::snprintf(label, sizeof(label), "%s###lync_%d",
                              d.display.c_str(), i);

                bool open = true;
                ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
                if (d.dirty) flags |= ImGuiTabItemFlags_UnsavedDocument;
                // Tell the tab bar to switch to this tab on the same
                // frame open_lync_doc / Ctrl+Tab requested focus.
                // SetNextWindowFocus alone only focuses the editor child
                // window; the tab bar would still display whichever tab
                // was previously selected. Without SetSelected the user
                // has to manually click the new tab — which is exactly
                // the "opening doesn't switch" symptom.
                if (d.focus_next_frame)
                    flags |= ImGuiTabItemFlags_SetSelected;

                if (ImGui::BeginTabItem(label, &open, flags)) {
                    s.lync_active_doc = i;
                    sync_settings_to_doc(d, s);

                    // Find / Replace bar (collapsed when find_open == false).
                    // The per-tab toolbar (path / Save / // / Find / status)
                    // was removed in the minimal pass — the menu bar and the
                    // breadcrumb already carry that information, and every
                    // action has a keyboard shortcut.
                    draw_find_replace_bar(d);

                    // Inline docs / suggestion strips intentionally removed -
                    // docs surface as native TextEditor hover tooltips; the
                    // autocomplete hint lives in the window-top status bar
                    // (see draw_lync_editor_panel below).

                    if (d.focus_next_frame) {
                        ImGui::SetNextWindowFocus();
                        d.focus_next_frame = false;
                    }

                    // Detect whether the editor child is focused this frame
                    // BEFORE Render() so we can adjust the selection for the
                    // line-aware Ctrl+C / Ctrl+X behaviour.
                    const bool editor_focused = ImGui::IsWindowFocused(
                        ImGuiFocusedFlags_RootAndChildWindows);
                    if (editor_focused) s.lync_editor_focused = true;
                    apply_line_clipboard_semantics(*d.editor, editor_focused);

                    // Autocomplete: compute candidates BEFORE Render so the
                    // window-top status bar (rendered earlier this frame) and
                    // the Tab-accept logic see the same snapshot. If the user
                    // hits Tab and we have a candidate, mark it pending - the
                    // editor will still insert "\t" during Render; we undo
                    // and replace below.
                    AutoComplete ac = compute_autocomplete(s, d);
                    // Apply dismissed-state immediately so arrow keys flow
                    // through to TextEditor when the user just pressed Esc.
                    if (d.ac_dismissed_for_prefix) ac.active = false;
                    if (ac.active &&
                        (ac.prefix != d.ac_last_prefix ||
                         ac.is_template_mode != d.ac_last_template_mode)) {
                        d.ac_selected_idx          = 0;
                        d.ac_last_prefix           = ac.prefix;
                        d.ac_last_template_mode    = ac.is_template_mode;
                        d.ac_dismissed_for_prefix  = false;
                    }
                    bool tab_accept_pending = false;
                    bool tab_indent_selection_pending = false;
                    bool tab_dedent_selection_pending = false;
                    if (editor_focused) {
                        const ImGuiIO& io = ImGui::GetIO();
                        const bool tab_pressed =
                            !io.KeyCtrl && !io.KeyAlt &&
                            ImGui::IsKeyPressed(ImGuiKey_Tab, false);
                        const bool has_sel = d.editor && d.editor->HasSelection();
                        if (tab_pressed) {
                            if (io.KeyShift) {
                                // Shift+Tab always dedents (selection or
                                // single line). Matches VS Code / JetBrains.
                                d.editor->SuppressNextTab();
                                tab_dedent_selection_pending = true;
                            } else if (has_sel) {
                                // Multi-line selection: indent the block.
                                // (TextEditor's default would replace the
                                // selection with a single tab — really not
                                // what the user wants.)
                                d.editor->SuppressNextTab();
                                tab_indent_selection_pending = true;
                            } else if (ac.active) {
                                // Autocomplete commit (no selection, no shift).
                                std::string would_choose;
                                if (!ac.best.empty()) would_choose = ac.best;
                                else if (!ac.alternatives.empty())
                                    would_choose = ac.alternatives.front();
                                if (!would_choose.empty()) {
                                    tab_accept_pending = true;
                                    d.editor->SuppressNextTab();
                                }
                            }
                        }
                    }
                    // Arrow-nav + Esc-dismiss for the dropdown. Done BEFORE
                    // Render so we can decide whether to restore the caret
                    // afterwards (TextEditor's Render will move the caret in
                    // response to the same Up/Down events otherwise).
                    bool ac_consumed_arrow_keys = false;
                    bool ac_commit_via_kbd      = false;
                    {
                        const size_t item_count =
                            (ac.active ? ((ac.best.empty() ? 0u : 1u) + ac.alternatives.size()) : 0u);
                        if (ac.active && editor_focused && item_count > 0) {
                            const int n = (int)item_count;
                            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
                                d.ac_selected_idx = (d.ac_selected_idx - 1 + n) % n;
                                ac_consumed_arrow_keys = true;
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
                                d.ac_selected_idx = (d.ac_selected_idx + 1) % n;
                                ac_consumed_arrow_keys = true;
                            }
                            // Enter commits the highlighted candidate when
                            // the dropdown shows multiple options. With a
                            // single suggestion Enter still falls through
                            // to a normal newline (matches VS Code's
                            // ghost-text behaviour where Tab is the
                            // accept key, Enter just inserts a newline).
                            if (n > 1 &&
                                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
                                ac_commit_via_kbd = true;
                                // Swallow Enter so TextEditor doesn't also
                                // insert a newline behind the commit.
                                d.editor->SuppressNextEnter();
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                                d.ac_dismissed_for_prefix = true;
                                ac.active = false;
                            }
                        }
                    }

                    // Numpad Enter as Enter. ImGui delivers KeypadEnter as a
                    // separate key; TextEditor's Render only listens for the
                    // main Enter. Push a synthetic event so the user gets the
                    // same line-break behaviour either way. Done BEFORE Render
                    // so TextEditor sees it. Skipped when autocomplete is
                    // committing via Enter — otherwise the synthesised '\n'
                    // goes through InputQueueCharacters (which the suppression
                    // flag doesn't gate) and the user gets a newline behind
                    // the committed identifier.
                    if (editor_focused &&
                        !ac_commit_via_kbd &&
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                        ImGuiIO& io = ImGui::GetIO();
                        io.AddInputCharacter('\n');
                    }

                    // Surround-with-bracket: intercept opener chars BEFORE Render.
                    // Must be after ac setup (so ac_tab_pending is known) but
                    // before Render so we can remove the char from the queue.
                    apply_surround_with_bracket(d, editor_focused);

                    // Keyword snippet: detect Tab after a snippet keyword.
                    // Pass tab_accept_pending so autocomplete wins if active.
                    detect_keyword_snippet(d, editor_focused, tab_accept_pending);
                    // Postfix `<expr>.match` Tab. Loses to autocomplete and
                    // to keyword snippets (the user types just `match` then
                    // Tab on a fresh line; postfix only fires when there's
                    // an expression chain ending in `.match`).
                    detect_postfix_match(d, editor_focused, tab_accept_pending,
                                          d.snippet_tab_pending);
                    // Match-on-Enter: catches the `match X {<Enter>` idiom
                    // and fills the some/null arms post-Render.
                    detect_match_enter(d, editor_focused);

                    // Snapshot pre-Render state so apply_brace_helpers can
                    // diff what TextEditor just did to the caret line.
                    EditTrace trace;
                    {
                        const auto pre = d.editor->GetCursorPosition();
                        trace.prev_caret_line = pre.mLine;
                        trace.prev_caret_col  = pre.mColumn;
                        const auto pre_lines = d.editor->GetTextLines();
                        if (pre.mLine >= 0 && pre.mLine < (int)pre_lines.size())
                            trace.prev_line_text = pre_lines[pre.mLine];
                        if (pre.mLine > 0 && pre.mLine - 1 < (int)pre_lines.size())
                            trace.prev_above_text = pre_lines[pre.mLine - 1];
                    }

                    // ---- Multi-cursor (ghost carets): pre-Render capture ----
                    // Snapshot the chars + key events that TextEditor is about
                    // to consume so we can replay them at each ghost position
                    // after Render. Ghost positions live in d.ghost_carets.
                    std::vector<unsigned int> mc_captured_chars;
                    bool mc_back_pressed = false;
                    bool mc_clear_request = false;
                    bool mc_add_below = false;
                    bool mc_add_above = false;
                    if (editor_focused) {
                        ImGuiIO& io = ImGui::GetIO();
                        for (int qi = 0; qi < io.InputQueueCharacters.Size; ++qi) {
                            const ImWchar w = io.InputQueueCharacters[qi];
                            // skip control chars; backspace handled separately
                            if (w >= 32 && w < 0x10000) mc_captured_chars.push_back(w);
                            if (w == '\n' || w == '\r') mc_clear_request = true;
                        }
                        mc_back_pressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, true);
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                            mc_clear_request = true;
                        // Arrow keys / Home / End / PgUp / PgDn / mouse click
                        // also dismiss to avoid surprising the user.
                        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_Home, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_End, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_PageUp, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_PageDown, false) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            mc_clear_request = true;
                        }
                        // Ctrl+Alt+Down / Up adds / removes a ghost caret.
                        // Use the MOD chord *with* the arrow as the trigger.
                        const bool chord = io.KeyCtrl && io.KeyAlt;
                        if (chord && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
                            mc_add_below = true;
                            mc_clear_request = false;   // Down is the chord, not a clear
                        }
                        if (chord && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
                            mc_add_above = true;
                            mc_clear_request = false;
                        }
                    }

                    // ---- Breadcrumbs: enclosing struct / def of caret -----
                    // Walks scan_symbols and picks the closest decl on or
                    // above the caret line. Click to jump back to that
                    // decl. Skipped silently when no decl precedes the
                    // caret (top of file, comment, etc.).
                    if (s.lync_show_breadcrumbs) {
                        const auto syms = scan_symbols(*d.editor);
                        const int caret_line = d.editor->GetCursorPosition().mLine;
                        int best = -1;
                        for (int i = 0; i < (int)syms.size(); ++i) {
                            if (syms[i].line <= caret_line &&
                                (best < 0 || syms[i].line > syms[best].line))
                                best = i;
                        }
                        // Single muted line — no separator below. The
                        // editor body's own border / background change
                        // is the real visual divider.
                        const ImVec4 muted(0.50f, 0.50f, 0.54f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Text, muted);
                        if (best >= 0) {
                            const auto& sym = syms[best];
                            ImGui::Text("%s  ›  %s",
                                d.display.c_str(), sym.name.c_str());
                            // Make the symbol name clickable: invisible
                            // overlay button so the text styling stays
                            // calm. Cheap hit test.
                            const float text_w =
                                ImGui::CalcTextSize(sym.name.c_str()).x;
                            const ImVec2 max = ImGui::GetItemRectMax();
                            const ImVec2 min(max.x - text_w,
                                             ImGui::GetItemRectMin().y);
                            if (ImGui::IsMouseHoveringRect(min, max) &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                d.editor->SetCursorPosition({sym.line, 0});
                            }
                        } else {
                            ImGui::Text("%s", d.display.c_str());
                        }
                        ImGui::PopStyleColor();
                    }

                    // Push the monospace code font for the editor body
                    // (and the find-match overlay below, which uses the
                    // same char advance). Outside this scope the UI
                    // proportional font is back in effect.
                    const bool use_code_font = (g_code_font != nullptr);
                    if (use_code_font) ImGui::PushFont(g_code_font);
                    d.editor->Render("##lync_text",
                                      ImGui::GetContentRegionAvail(), false);

                    // ---- Active-line highlight ---------------------------
                    // A subtle full-width tint on the caret's line so the
                    // eye snaps to it as you move the cursor. Quiet enough
                    // that it doesn't fight selection / find highlights.
                    {
                        const auto cp = d.editor->GetCursorPosition();
                        const ImVec2 advance = d.editor->GetCharAdvance();
                        if (advance.y > 0.0f) {
                            const ImVec2 line_start =
                                d.editor->GetCharScreenPos({cp.mLine, 0});
                            // Stretch from the start-of-line to the right
                            // edge of the editor child window.
                            const ImVec2 win_min = ImGui::GetItemRectMin();
                            const ImVec2 win_max = ImGui::GetItemRectMax();
                            if (line_start.y >= win_min.y &&
                                line_start.y + advance.y <= win_max.y &&
                                !d.editor->HasSelection()) {
                                ImDrawList* dl = ImGui::GetWindowDrawList();
                                dl->AddRectFilled(
                                    ImVec2(win_min.x, line_start.y),
                                    ImVec2(win_max.x, line_start.y + advance.y),
                                    IM_COL32(255, 255, 255, 8));
                            }
                        }
                    }

                    // ---- Word-under-cursor highlight ---------------------
                    // Subtle outline on every other instance of the word
                    // currently under the caret. Helps spot uses without
                    // having to pop the find bar. Skipped when there's
                    // an active selection (would just visually duplicate
                    // the selection's normal highlight) and when find is
                    // open (its highlight is louder and takes priority).
                    if (!d.editor->HasSelection() &&
                        !d.find_open && !d.replace_open) {
                        const std::string w = word_at_cursor(*d.editor);
                        if (w.size() >= 2) {
                            const auto lines = d.editor->GetTextLines();
                            const ImVec2 advance = d.editor->GetCharAdvance();
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            const ImU32 col = IM_COL32(255, 255, 255, 22);
                            const auto cp = d.editor->GetCursorPosition();
                            auto is_word = [](char c) {
                                return std::isalnum((unsigned char)c) || c == '_';
                            };
                            for (int L = 0; L < (int)lines.size(); ++L) {
                                const std::string& line = lines[L];
                                size_t pos = 0;
                                while ((pos = line.find(w, pos)) != std::string::npos) {
                                    // Word boundary on both sides.
                                    const bool left_ok =
                                        pos == 0 || !is_word(line[pos - 1]);
                                    const bool right_ok =
                                        pos + w.size() == line.size() ||
                                        !is_word(line[pos + w.size()]);
                                    if (left_ok && right_ok) {
                                        // Skip the instance the caret is sitting on.
                                        const bool is_caret_inst =
                                            L == cp.mLine &&
                                            (int)pos <= cp.mColumn &&
                                            cp.mColumn <= (int)(pos + w.size());
                                        if (!is_caret_inst) {
                                            const ImVec2 p1 =
                                                d.editor->GetCharScreenPos({L, (int)pos});
                                            const ImVec2 p2 =
                                                d.editor->GetCharScreenPos(
                                                    {L, (int)(pos + w.size())});
                                            if (p1.x != 0.0f || p1.y != 0.0f) {
                                                dl->AddRectFilled(
                                                    p1,
                                                    ImVec2(p2.x, p1.y + advance.y),
                                                    col);
                                            }
                                        }
                                    }
                                    pos += w.size();
                                }
                            }
                        }
                    }

                    // ---- Highlight all find matches -----------------------
                    // When the find bar is open and the user has a query,
                    // overlay a translucent rectangle on every match. Cheap:
                    // GetCharScreenPos uses the post-Render content origin
                    // so the rects line up with rendered glyphs even with
                    // tab-elastic columns. Skip when the editor isn't on
                    // screen yet (first frame).
                    if ((d.find_open || d.replace_open) && d.find_buf[0]) {
                        const auto lines = d.editor->GetTextLines();
                        const std::string needle = d.find_buf;
                        const ImVec2 advance = d.editor->GetCharAdvance();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImU32 fill    = IM_COL32(0xea, 0xc7, 0x4a, 0x44);
                        const ImU32 outline = IM_COL32(0xea, 0xc7, 0x4a, 0xc0);
                        auto eq_at = [&](const std::string& hay, size_t i) {
                            if (i + needle.size() > hay.size()) return false;
                            for (size_t k = 0; k < needle.size(); ++k) {
                                const char a = d.find_case_sensitive ? hay[i + k]
                                    : (char)std::tolower((unsigned char)hay[i + k]);
                                const char b = d.find_case_sensitive ? needle[k]
                                    : (char)std::tolower((unsigned char)needle[k]);
                                if (a != b) return false;
                            }
                            return true;
                        };
                        for (int L = 0; L < (int)lines.size(); ++L) {
                            const std::string& line = lines[L];
                            for (size_t i = 0; i + needle.size() <= line.size(); ) {
                                if (eq_at(line, i)) {
                                    const ImVec2 p1 = d.editor->GetCharScreenPos(
                                        {L, (int)i});
                                    const ImVec2 p2 = d.editor->GetCharScreenPos(
                                        {L, (int)(i + needle.size())});
                                    if (p1.x != 0.0f || p1.y != 0.0f) {
                                        dl->AddRectFilled(
                                            p1, ImVec2(p2.x, p1.y + advance.y),
                                            fill, 2.0f);
                                        dl->AddRect(
                                            p1, ImVec2(p2.x, p1.y + advance.y),
                                            outline, 2.0f);
                                    }
                                    i += std::max((size_t)1, needle.size());
                                } else {
                                    ++i;
                                }
                            }
                        }
                    }
                    // Code font is no longer needed past this point —
                    // popups, status bar, breadcrumbs are all UI font.
                    if (use_code_font) ImGui::PopFont();

                    // ---- Multi-cursor: replay typed input at each ghost ----
                    if (!d.ghost_carets.empty() && mc_clear_request) {
                        d.ghost_carets.clear();
                    } else if (!d.ghost_carets.empty() &&
                               (!mc_captured_chars.empty() || mc_back_pressed)) {
                        // Snapshot primary so we can restore it.
                        const auto primary_pos = d.editor->GetCursorPosition();
                        // Apply edits in REVERSE-line order so that an edit on
                        // a later line doesn't shift the column offsets of
                        // earlier ghosts. (Same line edits would still
                        // interfere; v1 keeps each ghost on its own line.)
                        std::sort(d.ghost_carets.begin(), d.ghost_carets.end(),
                                  [](const auto& a, const auto& b) {
                                      if (a.first != b.first) return a.first > b.first;
                                      return a.second > b.second;
                                  });
                        for (auto& [gl, gc] : d.ghost_carets) {
                            d.editor->SetCursorPosition({gl, gc});
                            for (unsigned int w : mc_captured_chars) {
                                if (w < 0x80) {
                                    char buf[2] = {(char)w, 0};
                                    d.editor->InsertText(buf);
                                } else {
                                    // UTF-8 encode (ImWchar is up to 0xFFFF).
                                    char buf[5] = {};
                                    if (w < 0x800) {
                                        buf[0] = (char)(0xC0 | (w >> 6));
                                        buf[1] = (char)(0x80 | (w & 0x3F));
                                    } else {
                                        buf[0] = (char)(0xE0 | (w >> 12));
                                        buf[1] = (char)(0x80 | ((w >> 6) & 0x3F));
                                        buf[2] = (char)(0x80 | (w & 0x3F));
                                    }
                                    d.editor->InsertText(buf);
                                }
                            }
                            if (mc_back_pressed) {
                                const auto p = d.editor->GetCursorPosition();
                                if (p.mColumn > 0) {
                                    d.editor->SetSelection({p.mLine, p.mColumn - 1},
                                                           {p.mLine, p.mColumn},
                                                           ::TextEditor::SelectionMode::Normal);
                                    d.editor->Delete();
                                }
                            }
                            const auto np = d.editor->GetCursorPosition();
                            gl = np.mLine;
                            gc = np.mColumn;
                        }
                        d.editor->SetCursorPosition(primary_pos);
                    }
                    // ---- Multi-cursor: add a ghost on chord ----
                    if (mc_add_below || mc_add_above) {
                        const auto pp = d.editor->GetCursorPosition();
                        const int total = (int)d.editor->GetTotalLines();
                        // Anchor column = primary's column. Anchor line is
                        // pp.mLine ± (existing ghosts + 1) so each press
                        // walks one further away.
                        int target = pp.mLine;
                        if (mc_add_below) target += (int)d.ghost_carets.size() + 1;
                        else              target -= (int)d.ghost_carets.size() + 1;
                        if (target >= 0 && target < total && target != pp.mLine) {
                            d.ghost_carets.emplace_back(target, pp.mColumn);
                        }
                    }

                    // ---- Visual overlays (drawn after Render so they sit on top) ----
                    {
                        ImDrawList* dl      = ImGui::GetWindowDrawList();
                        const ImVec2 ed_min = ImGui::GetItemRectMin();
                        const ImVec2 ed_max = ImGui::GetItemRectMax();
                        const ImVec2 adv    = d.editor->GetCharAdvance(); // x=char_w, y=line_h
                        const float  char_w = adv.x;
                        const float  line_h = adv.y;
                        const auto   all_lines   = d.editor->GetTextLines();
                        const int    total_lines  = (int)all_lines.size();

                        // Shared: column-0 screen x (left edge of text, past gutter).
                        const ImVec2 col0_origin = (total_lines > 0)
                            ? d.editor->GetCharScreenPos({0, 0})
                            : ImVec2(ed_min.x, ed_min.y);
                        const float  col0_x  = col0_origin.x;
                        const float  origin_y = col0_origin.y;

                        // -- Multi-cursor ghost caret bars --
                        // Vertical 1px bars at each ghost position so the
                        // user sees where their next typed character will be
                        // duplicated. Color matches the primary caret but
                        // dimmer to distinguish.
                        if (!d.ghost_carets.empty() &&
                            char_w > 0.0f && line_h > 0.0f) {
                            const ImU32 ghost_col = IM_COL32(220, 220, 220, 180);
                            for (const auto& [gl, gc] : d.ghost_carets) {
                                if (gl < 0 || gl >= total_lines) continue;
                                const ImVec2 p =
                                    d.editor->GetCharScreenPos({gl, gc});
                                if (p.x < ed_min.x || p.x > ed_max.x) continue;
                                if (p.y < ed_min.y - line_h ||
                                    p.y > ed_max.y) continue;
                                dl->AddRectFilled(
                                    ImVec2(p.x, p.y),
                                    ImVec2(p.x + 1.5f, p.y + line_h),
                                    ghost_col);
                            }
                        }

                        // -- Visual cut-line at column lync_wrap_col --
                        // Only drawn when lync_wrap_col > 0; setting it to 0
                        // in Settings turns the indicator off entirely.
                        if (s.lync_wrap_col > 0) {
                            const float cut_x = col0_x + (float)s.lync_wrap_col * char_w;
                            if (cut_x > ed_min.x && cut_x < ed_max.x) {
                                dl->AddLine(ImVec2(cut_x, ed_min.y),
                                            ImVec2(cut_x, ed_max.y),
                                            IM_COL32(102, 102, 115, 60), 1.0f);
                            }
                        }

                        // -- Item 1: indent guides --
                        if (s.lync_indent_guides &&
                            total_lines > 0 && char_w > 0.0f && line_h > 0.0f) {
                            const ImU32 guide_col = IM_COL32(77, 82, 92, 255);
                            const int first_vis = (int)std::max(0.0f,
                                (ed_min.y - origin_y) / line_h);
                            const int last_vis  = (int)std::min((float)(total_lines - 1),
                                (ed_max.y - origin_y) / line_h + 1.0f);
                            for (int li = first_vis; li <= last_vis; ++li) {
                                const std::string& ln = all_lines[li];
                                int spaces = 0;
                                for (char c : ln) {
                                    if      (c == ' ')  ++spaces;
                                    else if (c == '\t') spaces += 4;
                                    else                break;
                                }
                                const int depth = spaces / 4;
                                if (depth == 0) continue;
                                const float ly_top = origin_y + (float)li * line_h;
                                const float ly_bot = ly_top + line_h;
                                for (int lvl = 1; lvl <= depth && lvl <= 8; ++lvl) {
                                    const float gx = col0_x + (float)(lvl * 4) * char_w;
                                    if (gx < ed_min.x || gx > ed_max.x) continue;
                                    dl->AddLine(ImVec2(gx, ly_top),
                                                ImVec2(gx, ly_bot),
                                                guide_col, 1.0f);
                                }
                            }
                        }

                        // -- Bracket scope highlight (1px, dim) --
                        if (s.lync_scope_marker && total_lines > 0) {
                            const auto caret_pos = d.editor->GetCursorPosition();
                            const int  caret_ln  = caret_pos.mLine;
                            int open_ln = -1, close_ln = -1;
                            // Scan upward for enclosing '{'.
                            int up_safety = 0;
                            int depth = 0;
                            for (int li = caret_ln; li >= 0 && open_ln < 0 && up_safety++ < 4096; --li) {
                                if (li >= total_lines) continue;
                                const std::string& ln = all_lines[li];
                                for (int ci = (int)ln.size() - 1; ci >= 0; --ci) {
                                    if      (ln[ci] == '}') ++depth;
                                    else if (ln[ci] == '{') {
                                        if (depth == 0) { open_ln = li; break; }
                                        else --depth;
                                    }
                                }
                            }
                            if (open_ln >= 0) {
                                int dn_safety = 0;
                                depth = 0;
                                for (int li = open_ln;
                                     li < total_lines && close_ln < 0 && dn_safety++ < 4096; ++li) {
                                    for (char c : all_lines[li]) {
                                        if      (c == '{') ++depth;
                                        else if (c == '}') {
                                            --depth;
                                            if (depth == 0) { close_ln = li; break; }
                                        }
                                    }
                                }
                            }
                            if (open_ln >= 0 && close_ln > open_ln) {
                                const float gx = col0_x - 4.0f;
                                if (gx >= ed_min.x) {
                                    const float y_top = origin_y + (float)open_ln  * line_h;
                                    const float y_bot = origin_y + (float)close_ln * line_h + line_h;
                                    dl->AddLine(ImVec2(gx, y_top), ImVec2(gx, y_bot),
                                                IM_COL32(100, 160, 220, 70), 1.0f);
                                }
                            }
                        }
                        // -- Item 4: Sticky scroll (def headers pinned to top) --
                        // When a def line has scrolled above the visible area but its closing
                        // brace is still in-view, render the def line as a sticky header at
                        // the top of the editor. Capped at 3 stacked headers.
                        if (total_lines > 0 && char_w > 0.0f && line_h > 0.0f) {
                            static const std::regex sticky_def_re(R"(^\s*def\s+)");
                            const float scroll_oy = ed_min.y - origin_y;
                            const int first_vis_s = (int)std::max(0.0f, scroll_oy / line_h);
                            struct StickyHdr { int line_idx; std::string text; };
                            std::vector<StickyHdr> hdrs;
                            for (int li = first_vis_s - 1;
                                 li >= 0 && (int)hdrs.size() < 3; --li) {
                                const std::string& ltxt = all_lines[li];
                                std::smatch sm;
                                if (!std::regex_search(ltxt, sm, sticky_def_re)) continue;
                                int depth_s = 0;
                                bool close_in_view = false;
                                bool found_close   = false;
                                for (int ci = li; ci < total_lines && !found_close; ++ci) {
                                    for (char ch : all_lines[ci]) {
                                        if      (ch == '{') ++depth_s;
                                        else if (ch == '}') {
                                            --depth_s;
                                            if (depth_s == 0) {
                                                if (ci >= first_vis_s) close_in_view = true;
                                                found_close = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (!close_in_view) continue;
                                std::string disp = ltxt;
                                size_t lead_s = 0;
                                while (lead_s < disp.size() &&
                                       (disp[lead_s]==' '||disp[lead_s]=='\t')) ++lead_s;
                                if (lead_s) disp = disp.substr(lead_s);
                                hdrs.insert(hdrs.begin(), {li, std::move(disp)});
                            }
                            if (!hdrs.empty()) {
                                const ImU32 hdr_bg  = IM_COL32(28,  32,  40,  230);
                                const ImU32 hdr_fg  = IM_COL32(220, 185, 120, 255);
                                const ImU32 hdr_sep = IM_COL32(80,  90,  110, 200);
                                for (int hi = 0; hi < (int)hdrs.size(); ++hi) {
                                    const float hy0 = ed_min.y + (float)hi * line_h;
                                    const float hy1 = hy0 + line_h;
                                    dl->AddRectFilled(ImVec2(ed_min.x, hy0),
                                                      ImVec2(ed_max.x, hy1), hdr_bg);
                                    dl->AddLine(ImVec2(ed_min.x, hy1 - 1.0f),
                                                ImVec2(ed_max.x, hy1 - 1.0f), hdr_sep, 1.0f);
                                    dl->AddText(ImVec2(col0_x, hy0), hdr_fg,
                                                hdrs[hi].text.c_str());
                                }
                            }
                        }
                    }
// ---- End visual overlays ----------------------------------------

                    // Restore caret if we consumed Up/Down for the dropdown.
                    // TextEditor reads keys during Render and will have moved
                    // the caret already; snapping back to the pre-Render
                    // position makes the navigation feel exclusive.
                    if (ac_consumed_arrow_keys) {
                        d.editor->SetCursorPosition({trace.prev_caret_line,
                                                      trace.prev_caret_col});
                    }

                    // After Render: apply auto-close + smart Enter helpers.
                    // No-op when nothing structural happened on the trace's
                    // line (typical typing).
                    if (editor_focused && d.editor->IsTextChanged()) {
                        apply_brace_helpers(*d.editor, trace, s.lync_auto_close);
                    }

                    // After Render: attribute-snippet expansion.
                    // Fires only on the frame the caret transitions to a new
                    // empty line whose predecessor is a recognised hook
                    // attribute. No-op every other frame (cheap line-number
                    // compare). Runs unconditionally (not gated on
                    // IsTextChanged) so it also fires when the user presses
                    // Enter on an attribute line that was already in the file
                    // but the preceding brace-helper pass didn't alter text.
                    if (editor_focused) {
                        apply_attribute_snippet(*d.editor, d);
                    }

                    // After Render: generic keyword snippet expansion.
                    // Undoes the spurious tab TextEditor inserted and replaces
                    // [keyword_start, caret) with the snippet body.
                    if (editor_focused) {
                        apply_generic_snippet(d);
                        apply_postfix_match(d);
                        apply_match_enter(d);
                    }

                    // ---- Diagnostic hover tooltip + Alt+Enter quick-fix ---
                    // If the mouse hovers over a line we have an error marker
                    // for, surface the message in an ImGui tooltip. Avoids
                    // patching TextEditor.cpp itself - we mirror the marker
                    // map on EditorState::LyncDoc and read it from here.
                    if (!d.error_markers.empty()) {
                        const ImVec2 ed_min = ImGui::GetItemRectMin();
                        const ImVec2 ed_max = ImGui::GetItemRectMax();
                        const ImVec2 mp     = ImGui::GetMousePos();
                        if (mp.x >= ed_min.x && mp.x <= ed_max.x &&
                            mp.y >= ed_min.y && mp.y <= ed_max.y) {
                            const ImVec2 adv  = d.editor->GetCharAdvance();
                            if (adv.y > 0.0f) {
                                // Approximate the line under the mouse: line 0
                                // = top of editor; each line is adv.y tall. We
                                // anchor against the editor's content rect's
                                // top-left.
                                const ::TextEditor::Coordinates probe{0, 0};
                                const ImVec2 origin = d.editor->GetCharScreenPos(probe);
                                const int line_under =
                                    1 + (int)((mp.y - origin.y) / adv.y);
                                auto it = d.error_markers.find(line_under);
                                if (it != d.error_markers.end()) {
                                    ImGui::BeginTooltip();
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                        ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
                                    ImGui::TextUnformatted(it->second.c_str());
                                    ImGui::PopStyleColor();
                                    ImGui::EndTooltip();
                                }
                            }
                        }

                        // Alt+Enter - placeholder quick-fix popup. Just shows
                        // a "no fixes available yet" hint for now; the real
                        // fix actions need analyzer integration.
                        const ImGuiIO& aio = ImGui::GetIO();
                        if (editor_focused && aio.KeyAlt &&
                            ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                            const auto cp = d.editor->GetCursorPosition();
                            auto it = d.error_markers.find(cp.mLine + 1);
                            if (it != d.error_markers.end()) {
                                show_toast(s, ("Quick-fixes for: "
                                              + it->second.substr(0, 60)
                                              + "  (none implemented yet)").c_str(),
                                           3.0f, false);
                            }
                        }
                    }

                    // ---- Inline parameter hints --------------------------
                    // When the caret is inside a function call (after `(`
                    // or right after a `,`), show ghost text with the
                    // remaining param names harvested from `def Name(...)`
                    // declarations in any project .lync file. Heuristic
                    // (no real symbol table); keeps it cheap.
                    if (editor_focused) {
                        const auto pos = d.editor->GetCursorPosition();
                        const auto lines_ph = d.editor->GetTextLines();
                        if (pos.mLine >= 0 && pos.mLine < (int)lines_ph.size()) {
                            const std::string& line = lines_ph[pos.mLine];
                            const int col = std::min((int)line.size(), pos.mColumn);
                            // Find the enclosing `(` on this line by walking
                            // back, balancing nested parens.
                            int depth = 0;
                            int open_paren = -1;
                            int comma_count = 0;
                            for (int ci = col - 1; ci >= 0; --ci) {
                                const char c = line[ci];
                                if      (c == ')') ++depth;
                                else if (c == '(') {
                                    if (depth == 0) { open_paren = ci; break; }
                                    --depth;
                                } else if (c == ',' && depth == 0) {
                                    ++comma_count;
                                }
                            }
                            if (open_paren > 0) {
                                // Pull the function name immediately before `(`.
                                int ne = open_paren;
                                int ns = ne;
                                while (ns > 0 &&
                                       (std::isalnum((unsigned char)line[ns - 1]) ||
                                        line[ns - 1] == '_')) --ns;
                                // UFCS detection: if the char immediately before
                                // the function name is `.`, the call site is
                                // `<expr>.fn(...)` - lync rewrites that to
                                // `fn(<expr>, ...)`, so the user has already
                                // "supplied" the first argument implicitly.
                                // Skip param 0 in the hint.
                                const bool is_ufcs_call =
                                    ns > 0 && line[ns - 1] == '.';
                                if (ne > ns) {
                                    const std::string fn = line.substr(ns, ne - ns);
                                    // Look up signature. Preferred source: the
                                    // rich symbol table from --emit-symbols
                                    // (post-analysis, types resolved). Falls
                                    // back to a regex scan of open docs if
                                    // the table isn't loaded yet (no
                                    // successful build this session).
                                    std::string sig_params;
                                    if (s.project_symbols.rich_loaded) {
                                        for (const auto& fs : s.project_symbols.funcs) {
                                            if (fs.name != fn) continue;
                                            for (size_t pi = 0; pi < fs.params.size(); ++pi) {
                                                if (pi) sig_params += ", ";
                                                sig_params += fs.params[pi].name;
                                                sig_params += ": ";
                                                sig_params += fs.params[pi].type;
                                            }
                                            break;
                                        }
                                    }
                                    if (sig_params.empty()) {
                                        static const std::regex def_re(
                                            R"(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)(?:<[^>]*>)?\s*\(([^)]*)\))");
                                        for (const auto& doc : s.lync_docs) {
                                            if (!doc.editor) continue;
                                            const auto dl = doc.editor->GetTextLines();
                                            for (const auto& l : dl) {
                                                std::smatch m;
                                                if (std::regex_search(l, m, def_re)
                                                    && m[1].str() == fn) {
                                                    sig_params = m[2].str();
                                                    break;
                                                }
                                            }
                                            if (!sig_params.empty()) break;
                                        }
                                    }
                                    if (!sig_params.empty()) {
                                        // Split params by top-level commas + skip
                                        // already-supplied ones using comma_count.
                                        std::vector<std::string> ps;
                                        std::string cur;
                                        int pdepth = 0;
                                        for (char c : sig_params) {
                                            if      (c == '<' || c == '(' || c == '[') ++pdepth;
                                            else if (c == '>' || c == ')' || c == ']') --pdepth;
                                            if (c == ',' && pdepth == 0) {
                                                ps.push_back(cur); cur.clear();
                                            } else {
                                                cur += c;
                                            }
                                        }
                                        if (!cur.empty()) ps.push_back(cur);
                                        // Trim each.
                                        for (auto& p : ps) {
                                            size_t a = 0;
                                            while (a < p.size() && (p[a] == ' ' || p[a] == '\t')) ++a;
                                            p = p.substr(a);
                                            while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
                                        }
                                        // UFCS shifts the user-visible param
                                        // index by 1: the first param is the
                                        // receiver (`e` in `e.SetTransform(...)`),
                                        // already supplied implicitly.
                                        const int start_idx =
                                            comma_count + (is_ufcs_call ? 1 : 0);
                                        std::string remaining;
                                        for (int pi = start_idx; pi < (int)ps.size(); ++pi) {
                                            if (!remaining.empty()) remaining += ", ";
                                            remaining += ps[pi];
                                        }
                                        if (!remaining.empty()) {
                                            const ::TextEditor::Coordinates anc{
                                                pos.mLine, col};
                                            const ImVec2 sp =
                                                d.editor->GetCharScreenPos(anc);
                                            const ImVec2 ed_min =
                                                ImGui::GetItemRectMin();
                                            const ImVec2 ed_max =
                                                ImGui::GetItemRectMax();
                                            if (sp.x >= ed_min.x &&
                                                sp.x <= ed_max.x &&
                                                sp.y >= ed_min.y &&
                                                sp.y <= ed_max.y - 4) {
                                                ImDrawList* dl =
                                                    ImGui::GetWindowDrawList();
                                                const ImU32 col_dim =
                                                    IM_COL32(140, 145, 160, 200);
                                                dl->AddText(sp, col_dim,
                                                            remaining.c_str());
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- Inline attribute preview hint -------------------
                    // When the caret sits at the end of a line whose trimmed
                    // content is one of the recognized hook attributes,
                    // show a tiny popup hint with the function signature
                    // that Tab/Enter will insert. Tab on this state injects
                    // a newline so the existing snippet expander runs next
                    // frame.
                    bool attr_preview_active = false;
                    {
                        const auto pos = d.editor->GetCursorPosition();
                        const auto lines = d.editor->GetTextLines();
                        if (editor_focused && pos.mLine >= 0 &&
                                pos.mLine < (int)lines.size()) {
                            const std::string& line = lines[pos.mLine];
                            // Caret must be at the end of trimmed line content.
                            int trimmed_end = (int)line.size();
                            while (trimmed_end > 0 &&
                                   (line[trimmed_end - 1] == ' ' ||
                                    line[trimmed_end - 1] == '\t')) --trimmed_end;
                            if (pos.mColumn >= trimmed_end) {
                                int indent_end = 0;
                                while (indent_end < (int)line.size() &&
                                       (line[indent_end] == ' ' ||
                                        line[indent_end] == '\t')) ++indent_end;
                                const std::string trimmed = line.substr(
                                    indent_end, trimmed_end - indent_end);
                                const char* sig = attribute_preview_signature(trimmed);
                                if (sig) {
                                    attr_preview_active = true;
                                    // Anchor below the line.
                                    const ::TextEditor::Coordinates ac_anchor{
                                        pos.mLine, indent_end};
                                    const ImVec2 anchor_screen =
                                        d.editor->GetCharScreenPos(ac_anchor);
                                    const float line_h =
                                        ImGui::GetTextLineHeightWithSpacing();
                                    ImGui::SetNextWindowPos(
                                        ImVec2(anchor_screen.x,
                                                anchor_screen.y + line_h),
                                        ImGuiCond_Always);
                                    ImGui::SetNextWindowBgAlpha(0.85f);
                                    constexpr ImGuiWindowFlags HF =
                                        ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoMove        |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_AlwaysAutoResize;
                                    char hid[64];
                                    std::snprintf(hid, sizeof(hid),
                                                  "##attr_hint_%d", i);
                                    if (ImGui::Begin(hid, nullptr, HF)) {
                                        ImGui::TextDisabled("[Tab/Enter] ");
                                        ImGui::SameLine();
                                        ImGui::TextColored(
                                            ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                                            "%s", sig);
                                    }
                                    ImGui::End();

                                    // Tab interception: TextEditor already
                                    // ate Tab during Render and inserted "\t".
                                    // We delete that and inject "\n" instead -
                                    // the snippet expander runs next frame.
                                    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
                                        // Find the tab we just inserted (one
                                        // char before the caret) and replace.
                                        const auto pos2 = d.editor->GetCursorPosition();
                                        if (pos2.mColumn > 0) {
                                            const auto lns = d.editor->GetTextLines();
                                            if (pos2.mLine < (int)lns.size()) {
                                                const std::string& l2 = lns[pos2.mLine];
                                                if (pos2.mColumn - 1 < (int)l2.size() &&
                                                    (l2[pos2.mColumn - 1] == '\t' ||
                                                     l2[pos2.mColumn - 1] == ' ')) {
                                                    d.editor->SetSelection(
                                                        {pos2.mLine, pos2.mColumn - 1},
                                                        {pos2.mLine, pos2.mColumn},
                                                        ::TextEditor::SelectionMode::Normal);
                                                    d.editor->Delete();
                                                }
                                            }
                                        }
                                        d.editor->InsertText("\n");
                                    }
                                }
                            }
                        }
                    }

                    // ---- Autocomplete dropdown ---------------------------
                    // Renders for both modes:
                    //   - template mode: caret is after Add<, Has<, etc.;
                    //     anchored at the '<', items render as "Name>".
                    //   - normal mode: anchor at the start of the partial
                    //     word; items render as the plain identifier.
                    //
                    // Arrow keys move the selection, Tab/Enter commits, Esc
                    // dismisses (one frame: prefix changes -> ac rebuilt).
                    //
                    // Reset selected index when prefix or mode changes so the
                    // user always starts at the top of the new candidate list.
                    // (Selected-index reset + dismiss-state moved above the
                    // pre-Render arrow-nav block.)

                    // Build the unified candidate list.
                    std::vector<const std::string*> ac_items;
                    if (ac.active) {
                        if (!ac.best.empty()) ac_items.push_back(&ac.best);
                        for (const auto& alt : ac.alternatives) ac_items.push_back(&alt);
                        if (d.ac_selected_idx >= (int)ac_items.size())
                            d.ac_selected_idx = (int)ac_items.size() - 1;
                        if (d.ac_selected_idx < 0) d.ac_selected_idx = 0;
                    }

                    // (Arrow nav + Esc dismiss moved to before Render so we
                    // can restore the caret right after the editor renders.)

                    // Render the popup whenever ac is active. An empty
                    // candidate list still shows a placeholder ("no items")
                    // so the user knows the trigger fired - debugging hint
                    // when the world hasn't registered the relevant type
                    // yet (e.g. project DLL not loaded, no struct decls
                    // for dot-trigger).
                    if (ac.active) {
                        // Anchor: '<' for template mode, start of partial
                        // word for normal mode.
                        const int  anchor_col   = ac.is_template_mode
                                                  ? ac.tpl_insert_col
                                                  : ac.word_start;
                        const ::TextEditor::Coordinates anchor{ac.line, anchor_col};
                        const ImVec2 anchor_screen = d.editor->GetCharScreenPos(anchor);
                        const float  line_h = ImGui::GetTextLineHeightWithSpacing();

                        // Render unconditionally - clipping the popup based on
                        // whether the anchor was inside the editor rect was
                        // hiding it whenever GetCharScreenPos returned (0,0)
                        // (first frame, off-screen line). Just position and
                        // let ImGui handle clipping if it goes off the
                        // viewport.
                        {
                            // Clamp the popup to the main viewport so it
                            // doesn't render half-off-screen when the caret
                            // sits near the right or bottom edge. Flips
                            // above the caret line when there isn't room
                            // below.
                            const ImGuiViewport* mv = ImGui::GetMainViewport();
                            constexpr float popup_w = 260.0f;
                            constexpr float popup_h_cap = 220.0f;
                            float px = anchor_screen.x;
                            float py = anchor_screen.y + line_h;
                            if (px + popup_w > mv->WorkPos.x + mv->WorkSize.x - 8)
                                px = mv->WorkPos.x + mv->WorkSize.x - popup_w - 8;
                            if (px < mv->WorkPos.x + 8) px = mv->WorkPos.x + 8;
                            if (py + popup_h_cap > mv->WorkPos.y + mv->WorkSize.y - 8)
                                py = anchor_screen.y - popup_h_cap;
                            if (py < mv->WorkPos.y + 8) py = mv->WorkPos.y + 8;

                            ImGui::SetNextWindowPos(
                                ImVec2(px, py),
                                ImGuiCond_Always);
                            ImGui::SetNextWindowSizeConstraints(
                                ImVec2(popup_w, 0),
                                ImVec2(popup_w, popup_h_cap));
                            ImGui::SetNextWindowSize(ImVec2(popup_w, 0), ImGuiCond_Always);
                            ImGui::SetNextWindowBgAlpha(0.95f);

                            // Drop NoBringToFrontOnFocus so the popup sits
                            // above the editor child window.
                            constexpr ImGuiWindowFlags POPUP_FLAGS =
                                ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoMove        |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoFocusOnAppearing;

                            char popup_id[64];
                            std::snprintf(popup_id, sizeof(popup_id),
                                          "##ac_drop_%d", i);

                            // Helper to commit a selection: same logic for
                            // mouse click, kbd Enter, kbd Tab.
                            auto commit_index = [&](int idx) {
                                if (idx < 0 || idx >= (int)ac_items.size()) return;
                                const std::string& chosen = *ac_items[idx];
                                if (ac.is_template_mode) {
                                    replace_range_with(
                                        *d.editor, ac.line,
                                        ac.tpl_insert_col,
                                        ac.tpl_insert_col + (int)ac.tpl_prefix.size(),
                                        chosen + ">");
                                } else {
                                    replace_range_with(
                                        *d.editor, ac.line,
                                        ac.word_start,
                                        ac.word_start + (int)ac.prefix.size(),
                                        chosen);
                                }
                            };

                            if (ImGui::Begin(popup_id, nullptr, POPUP_FLAGS)) {
                                if (ac_items.empty()) {
                                    ImGui::TextDisabled(ac.is_template_mode
                                        ? "(no components registered)"
                                        : "(no candidates)");
                                }
                                // Vivid blue background for the selected row
                                // so the highlight is unmistakable. Default
                                // ImGui Selectable highlight is washed out on
                                // a dim popup background.
                                ImGui::PushStyleColor(ImGuiCol_Header,
                                    IM_COL32(0x2a, 0x6f, 0xc8, 0xff));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                    IM_COL32(0x35, 0x82, 0xe0, 0xff));
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                    IM_COL32(0x42, 0x68, 0x8a, 0xff));
                                for (int idx = 0; idx < (int)ac_items.size(); ++idx) {
                                    const std::string label = ac.is_template_mode
                                        ? (*ac_items[idx] + ">")
                                        : *ac_items[idx];
                                    const bool is_sel = (idx == d.ac_selected_idx);
                                    if (ImGui::Selectable(label.c_str(), is_sel)) {
                                        commit_index(idx);
                                    }
                                    if (is_sel && ac_consumed_arrow_keys) {
                                        ImGui::SetScrollHereY();
                                    }
                                }
                                ImGui::PopStyleColor(3);
                            }
                            ImGui::End();

                            // Keyboard commit (Enter/numpad-Enter when there
                            // are alternatives - single-suggestion Enter falls
                            // through to a normal newline).
                            if (ac_commit_via_kbd) commit_index(d.ac_selected_idx);
                        }
                    }

                    // ---- Inline ghost text at the caret ------------------
                    // After Render() the editor has updated mLastContentScreenPos
                    // (Zues patch), so we can compute the caret's exact screen
                    // pos and draw the suggestion's TAIL (suggestion - prefix)
                    // in dim grey - VS Code / Copilot style "press Tab to take
                    // this". Skipped if the suggestion isn't a strict extension
                    // of the typed prefix (e.g. case-only differences).
                    // Also skipped in template mode (the dropdown serves the same
                    // purpose and ghost text would be confusing there).
                    if (ac.active && !ac.is_template_mode) {
                        // Use the dropdown's currently-selected item, not
                        // ac.best (which is just index 0). This keeps the
                        // ghost text in sync with arrow-key navigation.
                        std::string ghost_chosen;
                        if (d.ac_selected_idx == 0) {
                            ghost_chosen = ac.best;
                        } else if (d.ac_selected_idx - 1 <
                                       (int)ac.alternatives.size()) {
                            ghost_chosen = ac.alternatives[d.ac_selected_idx - 1];
                        }
                        if (!ghost_chosen.empty() &&
                                ghost_chosen.size() > ac.prefix.size()) {
                        // Confirm the suggestion truly extends the prefix
                        // case-insensitively before drawing the tail.
                        bool extends = true;
                        for (size_t k = 0; k < ac.prefix.size(); ++k) {
                            const char a = (char)std::tolower((unsigned char)ghost_chosen[k]);
                            const char b = (char)std::tolower((unsigned char)ac.prefix[k]);
                            if (a != b) { extends = false; break; }
                        }
                        if (extends) {
                            // Suppress ghost when there's any non-whitespace
                            // char immediately after the caret - otherwise it
                            // overlaps the real text (e.g. ghost "Collision"
                            // painted over `]` in `[On|]`).
                            bool blocked_by_following_text = false;
                            {
                                const auto post_pos = d.editor->GetCursorPosition();
                                const auto post_lines = d.editor->GetTextLines();
                                if (post_pos.mLine >= 0 &&
                                    post_pos.mLine < (int)post_lines.size()) {
                                    const std::string& l = post_lines[post_pos.mLine];
                                    if (post_pos.mColumn >= 0 &&
                                            post_pos.mColumn < (int)l.size()) {
                                        const char nc = l[post_pos.mColumn];
                                        if (nc != ' ' && nc != '\t' && nc != '\r' && nc != '\n')
                                            blocked_by_following_text = true;
                                    }
                                }
                            }
                            if (blocked_by_following_text) {
                                // skip the ghost paint
                            } else {
                            const std::string tail = ghost_chosen.substr(ac.prefix.size());
                            // Caret cell: line `ac.line`, col after the prefix.
                            const ::TextEditor::Coordinates caret{
                                ac.line,
                                ac.word_start + (int)ac.prefix.size()};
                            const ImVec2 pos = d.editor->GetCharScreenPos(caret);
                            // Only draw if the caret pos is inside the editor
                            // rect we just rendered (avoids ghost trails when
                            // scrolled off-screen).
                            const ImVec2 ed_min = ImGui::GetItemRectMin();
                            const ImVec2 ed_max = ImGui::GetItemRectMax();
                            if (pos.x >= ed_min.x && pos.x <= ed_max.x &&
                                pos.y >= ed_min.y && pos.y <= ed_max.y - 4) {
                                // Paint a background rect to mask any text the
                                // editor painted to the right (e.g. trailing
                                // `]` on a bracket line). Then draw the ghost.
                                ImDrawList* dl = ImGui::GetWindowDrawList();
                                const float line_h = ImGui::GetTextLineHeight();
                                const float tail_w = ImGui::CalcTextSize(tail.c_str()).x;
                                dl->AddRectFilled(pos,
                                    ImVec2(pos.x + tail_w, pos.y + line_h),
                                    IM_COL32(16, 16, 16, 255));
                                const ImU32 col = IM_COL32(170, 170, 170, 200);
                                dl->AddText(pos, col, tail.c_str());
                            }
                            }
                        }
                        }
                    }

                    if (tab_indent_selection_pending) {
                        op_indent_selection(*d.editor);
                        d.dirty = true;
                    }
                    if (tab_dedent_selection_pending) {
                        op_dedent_selection(*d.editor);
                        d.dirty = true;
                    }
                    if (tab_accept_pending) {
                        // Tab commits the SELECTED item (ac.best is just the
                        // first one - the highlighted row in the dropdown
                        // may be a different index after Up/Down nav).
                        std::string chosen;
                        if (d.ac_selected_idx == 0) {
                            chosen = ac.best;   // accept_autocomplete uses ac.best when override is empty
                        } else if (d.ac_selected_idx - 1 <
                                       (int)ac.alternatives.size()) {
                            chosen = ac.alternatives[d.ac_selected_idx - 1];
                        }
                        // No real candidate to commit - the popup was an
                        // empty placeholder (e.g. template mode with no
                        // components, or dot-trigger with an unknown LHS
                        // type). Falling through to accept_autocomplete
                        // with chosen="" wastes a frame and risks unsafe
                        // string indexing inside SetSelection / Delete on
                        // edge-case columns. Just let Tab insert a tab.
                        if (chosen.empty()) {
                            // intentional no-op
                        } else
                        // Postfix-template: chosen == "match" in dot-trigger
                        // context means "expand the chain into a match block".
                        // Walks the chain back from `.` (the char at
                        // word_start - 1), then routes through the same
                        // logic as detect_postfix_match.
                        if (ac.dot_triggered &&
                                chosen == "match" &&
                                d.editor && ac.line >= 0) {
                            const auto lines_now = d.editor->GetTextLines();
                            if (ac.line < (int)lines_now.size()) {
                                const std::string& line = lines_now[ac.line];
                                const int dot_col = ac.word_start - 1;
                                if (dot_col >= 0 && dot_col < (int)line.size() &&
                                        line[dot_col] == '.') {
                                    // Walk back over chain (identifiers, `.`,
                                    // balanced () [] <>). Same logic as
                                    // detect_postfix_match.
                                    auto is_id = [](char c) {
                                        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                                    };
                                    int sc = dot_col - 1;
                                    while (sc >= 0) {
                                        const char c = line[sc];
                                        if (is_id(c) || c == '.') { --sc; continue; }
                                        if (c == ')' || c == ']' || c == '>') {
                                            const char open = (c == ')') ? '(' : (c == ']') ? '[' : '<';
                                            int depth = 1;
                                            --sc;
                                            while (sc >= 0 && depth > 0) {
                                                if (line[sc] == c)        ++depth;
                                                else if (line[sc] == open) --depth;
                                                --sc;
                                            }
                                            continue;
                                        }
                                        break;
                                    }
                                    const int expr_start = sc + 1;
                                    if (expr_start < dot_col) {
                                        std::string expr = line.substr(expr_start,
                                                                        dot_col - expr_start);
                                        // Stash + call apply_postfix_match
                                        // INLINE. The post-Render apply pass
                                        // already fired this frame, so a
                                        // pending-flag-only approach would
                                        // miss this frame and leave the user
                                        // staring at an inserted tab. Apply
                                        // the chain replacement here so the
                                        // change lands in the same frame the
                                        // user pressed Tab.
                                        d.postfix_match_pending    = true;
                                        d.postfix_match_expr       = std::move(expr);
                                        d.postfix_match_line       = ac.line;
                                        d.postfix_match_expr_start = expr_start;
                                        d.postfix_match_caret_end  =
                                            ac.word_start + (int)ac.prefix.size();
                                        apply_postfix_match(d);
                                        d.ac_selected_idx = 0;
                                        d.ac_last_prefix.clear();
                                    } else {
                                        accept_autocomplete(*d.editor, ac, chosen);
                                    }
                                } else {
                                    accept_autocomplete(*d.editor, ac, chosen);
                                }
                            } else {
                                accept_autocomplete(*d.editor, ac, chosen);
                            }
                        } else {
                            accept_autocomplete(*d.editor, ac, chosen);
                        }
                    }

                    if (d.editor->IsTextChanged()) d.dirty = true;

                    // ---- Keyboard shortcuts (active when editor focused) ----
                    const ImGuiIO& io = ImGui::GetIO();

                    // -- Item 4: Ctrl+scroll / Ctrl+= / Ctrl+- font zoom ----
                    // FontGlobalScale clamped to [0.5, 3.0]. Ctrl+0 resets.
                    // Ctrl+scroll works whenever the editor is hovered (not
                    // strictly focused) so it feels natural.
                    if (io.KeyCtrl) {
                        const float zoom_step = 0.1f;
                        float& scale = ImGui::GetIO().FontGlobalScale;
                        // Ctrl+scroll (hovered or focused).
                        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                                ImGui::GetIO().MouseWheel != 0.0f) {
                            scale += ImGui::GetIO().MouseWheel * zoom_step;
                            scale = std::max(0.5f, std::min(3.0f, scale));
                        }
                        if (editor_focused) {
                            // Ctrl+= zoom in, Ctrl+- zoom out, Ctrl+0 reset.
                            if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
                                scale = std::max(0.5f, std::min(3.0f, scale + zoom_step));
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
                                scale = std::max(0.5f, std::min(3.0f, scale - zoom_step));
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_0, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
                                scale = 1.0f;
                            }
                        }
                    }

                    if (editor_focused) {
                        // Smart Home: column 0 / first-non-ws toggle.
                        // ImGui's editor moves to col 0 on Home; we
                        // intercept and override BEFORE Render so
                        // TextEditor doesn't also do its own move.
                        // Shift+Home extends the selection.
                        if (!io.KeyCtrl && !io.KeyAlt &&
                                ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                            op_smart_home(*d.editor, io.KeyShift);
                        }
                        // Ctrl+W = close current tab (with dirty confirm
                        // routed through the same path as the X button).
                        if (io.KeyCtrl && !io.KeyShift &&
                                ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                            const int idx_to_close = i;
                            if (d.dirty) {
                                request_confirm(s,
                                    "Close '" + d.display + "' without saving?",
                                    [&s, idx_to_close]() {
                                        if (idx_to_close < (int)s.lync_docs.size()) {
                                            s.lync_docs.erase(s.lync_docs.begin() + idx_to_close);
                                            if (s.lync_active_doc >= (int)s.lync_docs.size())
                                                s.lync_active_doc = (int)s.lync_docs.size() - 1;
                                        }
                                    });
                            } else {
                                s.lync_docs.erase(s.lync_docs.begin() + idx_to_close);
                                if (s.lync_active_doc >= (int)s.lync_docs.size())
                                    s.lync_active_doc = (int)s.lync_docs.size() - 1;
                            }
                        }
                        // Ctrl+Tab / Ctrl+Shift+Tab = cycle tabs forward /
                        // back. Wraps around. Done by setting the next
                        // doc's focus_next_frame flag so its tab gets
                        // ImGuiTabItemFlags_SetSelected.
                        if (io.KeyCtrl && !io.KeyAlt &&
                                ImGui::IsKeyPressed(ImGuiKey_Tab, false) &&
                                s.lync_docs.size() > 1) {
                            const int n = (int)s.lync_docs.size();
                            const int dir = io.KeyShift ? -1 : +1;
                            const int next = (i + dir + n) % n;
                            s.lync_docs[next].focus_next_frame = true;
                        }
                        if (io.KeyCtrl && !io.KeyShift &&
                                ImGui::IsKeyPressed(ImGuiKey_S, false))
                            save_doc(s, i);
                        // Ctrl+Shift+S = Save All. Walks every open doc
                        // and saves the dirty ones in order.
                        if (io.KeyCtrl && io.KeyShift &&
                                ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                            int n = 0;
                            for (int k = 0; k < (int)s.lync_docs.size(); ++k) {
                                if (s.lync_docs[k].dirty) { save_doc(s, k); ++n; }
                            }
                            show_toast(s, ("saved " + std::to_string(n) + " file"
                                            + (n == 1 ? "" : "s")).c_str());
                        }
                        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                            d.find_open    = true;
                            d.replace_open = false;
                            d.find_focus_pending = true;
                            // Pre-fill from current selection if it's
                            // single-line (matches VS Code / Sublime).
                            if (d.editor->HasSelection()) {
                                const std::string sel = d.editor->GetSelectedText();
                                if (!sel.empty() && sel.find('\n') == std::string::npos
                                        && sel.size() < sizeof(d.find_buf)) {
                                    std::strncpy(d.find_buf, sel.c_str(),
                                                 sizeof(d.find_buf) - 1);
                                }
                            }
                        }
                        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
                            d.find_open    = true;
                            d.replace_open = true;
                            d.find_focus_pending = true;
                        }
                        // F3 / Shift+F3 navigate matches when the find
                        // buffer has content. Works whether the bar is
                        // open or not.
                        if (!io.KeyCtrl && !io.KeyAlt && d.find_buf[0]) {
                            if (!io.KeyShift &&
                                    ImGui::IsKeyPressed(ImGuiKey_F3, false))
                                op_find_next(*d.editor, d.find_buf,
                                              d.find_case_sensitive);
                            if (io.KeyShift &&
                                    ImGui::IsKeyPressed(ImGuiKey_F3, false))
                                op_find_prev(*d.editor, d.find_buf,
                                              d.find_case_sensitive);
                        }
                        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
                            d.goto_line_open = true;
                        }
                        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Slash, false))
                            op_toggle_line_comment(*d.editor);
                        // Ctrl+D = duplicate. (Multi-cursor select-next will
                        // bind to Ctrl+Alt+D in a follow-up.)
                        if (io.KeyCtrl && !io.KeyAlt &&
                                ImGui::IsKeyPressed(ImGuiKey_D, false))
                            op_duplicate_line(*d.editor);
                        // Alt+Up / Alt+Down move the line.
                        if (io.KeyAlt && !io.KeyCtrl && !io.KeyShift) {
                            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
                                op_move_line(*d.editor, -1);
                            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
                                op_move_line(*d.editor, +1);
                        }
                        // Push current caret onto the goto-def back-stack.
                        // Caps at 32 entries (drops oldest).
                        auto push_jump = [&]() {
                            EditorState::LyncJumpFrame fr;
                            fr.file = d.path;
                            const auto cp = d.editor->GetCursorPosition();
                            fr.line = cp.mLine;
                            fr.col  = cp.mColumn;
                            s.lync_jump_back.push_back(std::move(fr));
                            if (s.lync_jump_back.size() > 32)
                                s.lync_jump_back.erase(s.lync_jump_back.begin());
                        };
                        // Project-wide goto-def. Tries rich symbols first,
                        // then regex by_file_locs, then file-local. Opens
                        // the target file if it's not already a tab.
                        auto goto_def = [&](const std::string& w) {
                            if (w.empty()) return;
                            GotoDefResult r = find_definition_global(s, w);
                            if (!r.file.empty()) {
                                push_jump();
                                if (Engine::editor::normalize_path(r.file)
                                        != Engine::editor::normalize_path(d.path)) {
                                    open_lync_doc(s, r.file);
                                    // open_lync_doc focuses the matching tab;
                                    // the active doc index now points at it.
                                    if (s.lync_active_doc >= 0 &&
                                        s.lync_active_doc < (int)s.lync_docs.size()) {
                                        auto& nd = s.lync_docs[s.lync_active_doc];
                                        if (nd.editor) {
                                            nd.editor->SetCursorPosition({r.line, r.col});
                                        }
                                    }
                                } else {
                                    d.editor->SetCursorPosition({r.line, r.col});
                                }
                                return;
                            }
                            // File-local fallback.
                            const int ln = find_definition_line(*d.editor, w);
                            if (ln >= 0) {
                                push_jump();
                                d.editor->SetCursorPosition({ln, 0});
                            } else {
                                show_toast(s, ("no definition for '" + w + "'").c_str(),
                                            2.0f, true);
                            }
                        };
                        // Ctrl+Click: goto-def. If no def found AND the symbol
                        // has a docs entry, we fall through to docs in
                        // goto_def's toast path - but for known docs we
                        // prefer to honour the user's convention: hold
                        // Ctrl+Alt for docs explicitly.
                        if (io.KeyCtrl && !io.KeyAlt &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Left,
                                                      false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            goto_def(w);
                        }
                        if (io.KeyCtrl && io.KeyAlt &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Left,
                                                      false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            if (!w.empty()) jump_to_docs(s, w);
                        }
                        // F1 = docs (keyboard alternative).
                        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            if (!w.empty()) jump_to_docs(s, w);
                        }
                        // F12 = goto-def project-wide.
                        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            goto_def(w);
                        }
                        // Shift+F12 = find references (project-wide). Opens
                        // a modal listing every word-boundary hit.
                        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            if (!w.empty()) {
                                s.lync_refs.query = w;
                                auto raw = find_references_in_project(s, w);
                                s.lync_refs.hits_.clear();
                                s.lync_refs.hits_.reserve(raw.size());
                                for (auto& r : raw) {
                                    s.lync_refs.hits_.push_back(
                                        {std::move(r.file), r.line, r.col,
                                         std::move(r.preview)});
                                }
                                s.lync_refs.open = true;
                            }
                        }
                        // Ctrl+- (Ctrl+Minus) = pop the back-stack and
                        // restore the previous caret. JetBrains-style.
                        if (io.KeyCtrl &&
                                (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                                 ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))) {
                            if (!s.lync_jump_back.empty()) {
                                auto fr = s.lync_jump_back.back();
                                s.lync_jump_back.pop_back();
                                if (Engine::editor::normalize_path(fr.file)
                                        != Engine::editor::normalize_path(d.path)) {
                                    open_lync_doc(s, fr.file);
                                    if (s.lync_active_doc >= 0 &&
                                        s.lync_active_doc < (int)s.lync_docs.size()) {
                                        auto& nd = s.lync_docs[s.lync_active_doc];
                                        if (nd.editor)
                                            nd.editor->SetCursorPosition({fr.line, fr.col});
                                    }
                                } else {
                                    d.editor->SetCursorPosition({fr.line, fr.col});
                                }
                            }
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
                            const std::string w = word_at_cursor(*d.editor);
                            if (!w.empty()) {
                                d.rename_old = w;
                                std::strncpy(d.rename_buf, w.c_str(),
                                             sizeof(d.rename_buf) - 1);
                                d.rename_open = true;
                            }
                        }
                    }

                    // ---- Bottom status bar (per active tab) -----------------
                    // Minimal: just position. No separator above (the
                    // editor body's own bottom edge is enough). Wrap /
                    // rainbow flags moved to the Settings popup — they
                    // don't need permanent screen real estate.
                    if (s.lync_status_bar) {
                        const auto pos = d.editor->GetCursorPosition();
                        const int total = d.editor->GetTotalLines();
                        ImGui::TextDisabled(
                            "Ln %d, Col %d  ·  %d",
                            pos.mLine + 1, pos.mColumn + 1, total);
                    }

                    // ---- Per-doc popups (deferred OpenPopup invocations) ----
                    draw_goto_line_popup(d);
                    draw_goto_line_modal(d);
                    draw_rename_popup(d);
                    draw_rename_modal(d, s);
                    draw_lync_refs_popup(s);
                    draw_lync_refs_modal(s);

                    ImGui::EndTabItem();
                }
                if (!open) close_idx = i;
            }
            if (close_idx >= 0) {
                // Confirm before discarding unsaved edits. The user can
                // either cancel (the tab stays) or accept and lose the
                // changes. Save-then-close is one click away via the
                // explicit "save" path; we don't want auto-save here
                // because the user might be closing precisely to throw
                // the edits away.
                if (close_idx < (int)s.lync_docs.size() &&
                        s.lync_docs[close_idx].dirty) {
                    const std::string display = s.lync_docs[close_idx].display;
                    const int idx_to_close = close_idx;
                    request_confirm(s,
                        "Close '" + display + "' without saving?",
                        [&s, idx_to_close]() {
                            if (idx_to_close < (int)s.lync_docs.size()) {
                                s.lync_docs.erase(s.lync_docs.begin() + idx_to_close);
                                if (s.lync_active_doc >= (int)s.lync_docs.size())
                                    s.lync_active_doc = (int)s.lync_docs.size() - 1;
                            }
                        });
                } else {
                    s.lync_docs.erase(s.lync_docs.begin() + close_idx);
                    if (s.lync_active_doc >= static_cast<int>(s.lync_docs.size()))
                        s.lync_active_doc = static_cast<int>(s.lync_docs.size()) - 1;
                    wrlt_save_open_tabs(s);   // Item 3: persist on close
                }
            }
            ImGui::EndTabBar();
        }
    }

    // ---- Item 3: Tab persistence helpers (private to this TU) ---------------
    // Hand-rolled JSON: { "tabs": ["abs1","abs2"], "active": "abs2" }
    // Stored at %APPDATA%/Zues/recent_lync_tabs.json (same dir as imgui.ini).

    std::string recent_lync_tabs_path() {
#if defined(_WIN32)
        const char* base = std::getenv("APPDATA");
        if (!base) base = std::getenv("USERPROFILE");
#elif defined(__APPLE__)
        const char* base = std::getenv("HOME");
#else
        const char* base = std::getenv("XDG_CONFIG_HOME");
        if (!base) base = std::getenv("HOME");
#endif
        if (!base || !*base) return {};
        namespace fs2 = std::filesystem;
        fs2::path dir = base;
#if defined(_WIN32)
        dir /= "Zues";
#elif defined(__APPLE__)
        dir /= "Library";
        dir /= "Application Support";
        dir /= "Zues";
#else
        if (std::getenv("XDG_CONFIG_HOME") == nullptr) dir /= ".config";
        dir /= "zues";
#endif
        std::error_code ec;
        fs2::create_directories(dir, ec);
        if (ec) return {};
        return (dir / "recent_lync_tabs.json").string();
    }

    // Escape a string for JSON (ASCII only -- backslash and quote).
    std::string json_escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            if      (c == '\\') out += "\\\\";
            else if (c == '"')  out += "\\\"";
            else                out += c;
        }
        return out;
    }

    void wrlt_save_open_tabs(const EditorState& s) {
        const std::string path = recent_lync_tabs_path();
        if (path.empty()) return;
        std::ofstream f(path, std::ios::trunc);
        if (!f) return;
        f << "{\n  \"tabs\": [";
        for (int i = 0; i < (int)s.lync_docs.size(); ++i) {
            if (i > 0) f << ", ";
            f << "\"" << json_escape(s.lync_docs[i].path) << "\"";
        }
        f << "],\n  \"active\": \"";
        if (s.lync_active_doc >= 0 && s.lync_active_doc < (int)s.lync_docs.size())
            f << json_escape(s.lync_docs[s.lync_active_doc].path);
        f << "\"\n}\n";
    }

}  // namespace

// Parse the simple one-liner JSON produced above. Tolerant: ignores unknown
// fields, returns empty on any structural problem.
static void read_recent_lync_tabs(const std::string& path,
                                   std::vector<std::string>& out_tabs,
                                   std::string& out_active) {
    out_tabs.clear();
    out_active.clear();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    // Extract "tabs": [...] array of quoted strings.
    auto find_array = [&](const std::string& key) -> std::vector<std::string> {
        std::vector<std::string> result;
        const std::string search = "\"" + key + "\"";
        auto kp = text.find(search);
        if (kp == std::string::npos) return result;
        auto ab = text.find('[', kp + search.size());
        auto ae = text.find(']', ab == std::string::npos ? 0 : ab);
        if (ab == std::string::npos || ae == std::string::npos) return result;
        const std::string arr = text.substr(ab + 1, ae - ab - 1);
        // Walk quoted strings.
        size_t i = 0;
        while (i < arr.size()) {
            auto q1 = arr.find('"', i);
            if (q1 == std::string::npos) break;
            std::string item;
            size_t j = q1 + 1;
            while (j < arr.size()) {
                if (arr[j] == '\\' && j+1 < arr.size()) { item += arr[j+1]; j += 2; }
                else if (arr[j] == '"') { ++j; break; }
                else { item += arr[j]; ++j; }
            }
            result.push_back(item);
            i = j;
        }
        return result;
    };
    auto find_string = [&](const std::string& key) -> std::string {
        const std::string search = "\"" + key + "\"";
        auto kp = text.find(search);
        if (kp == std::string::npos) return {};
        auto q1 = text.find('"', kp + search.size() + 1);
        if (q1 == std::string::npos) return {};
        std::string val;
        size_t i = q1 + 1;
        while (i < text.size()) {
            if (text[i] == '\\' && i+1 < text.size()) { val += text[i+1]; i += 2; }
            else if (text[i] == '"') { break; }
            else { val += text[i]; ++i; }
        }
        return val;
    };

    out_tabs   = find_array("tabs");
    out_active = find_string("active");
}

void load_recent_lync_tabs(EditorState& s) {
    const std::string path = recent_lync_tabs_path();
    std::vector<std::string> tabs;
    std::string active;
    read_recent_lync_tabs(path, tabs, active);
    for (const auto& t : tabs) {
        namespace fs2 = std::filesystem;
        std::error_code ec;
        if (!fs2::exists(t, ec) || ec) continue;
        open_lync_doc(s, t);  // sets lync_active_doc to new tab
    }
    // Restore the previously active tab.
    if (!active.empty()) {
        for (int i = 0; i < (int)s.lync_docs.size(); ++i) {
            if (s.lync_docs[i].path == active) {
                s.lync_active_doc = i;
                s.lync_docs[i].focus_next_frame = true;
                break;
            }
        }
    }
}

// Promote `abs_path` to the front of the recent-files list. Caps at
// 12 entries; older ones fall off the back. De-dups so the same file
// doesn't appear twice. Called from open_lync_doc.
static void bump_recent_lync(EditorState& s, const std::string& abs_path) {
    auto& rec = s.lync_recent_files;
    rec.erase(std::remove(rec.begin(), rec.end(), abs_path), rec.end());
    rec.insert(rec.begin(), abs_path);
    if (rec.size() > 12) rec.resize(12);
}

void open_lync_doc(EditorState& s, const std::string& abs_path) {
    // De-dup: focus existing tab if already open.
    for (int i = 0; i < static_cast<int>(s.lync_docs.size()); ++i) {
        if (s.lync_docs[i].path == abs_path) {
            s.lync_active_doc = i;
            s.lync_docs[i].focus_next_frame = true;
            s.show_lync_editor = true;
            bump_recent_lync(s, abs_path);
            wrlt_save_open_tabs(s);   // Item 3: persist on focus
            return;
        }
    }
    EditorState::LyncDoc d;
    d.path    = abs_path;
    d.display = Engine::editor::path_str(fs::path(abs_path).filename());
    std::string body;
    if (!read_file(abs_path, body)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Open failed: %s", abs_path.c_str());
        show_toast(s, buf, 3.0f, true);
        return;
    }
    s.lync_docs.push_back(std::move(d));
    auto& back = s.lync_docs.back();
    ensure_editor(back);
    back.editor->SetText(body);
    back.focus_next_frame = true;
    s.lync_active_doc  = static_cast<int>(s.lync_docs.size()) - 1;
    s.show_lync_editor = true;
    bump_recent_lync(s, abs_path);
    wrlt_save_open_tabs(s);   // Item 3: persist on open
}

void draw_lync_editor_panel(EditorState& s) {
    if (!s.show_lync_editor) return;
    if (!ImGui::Begin("Lync Editor", &s.show_lync_editor,
                      ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    // ---- Window menu bar -------------------------------------------------
    if (ImGui::BeginMenuBar()) {
        const bool has_doc = s.lync_active_doc >= 0
                          && s.lync_active_doc < static_cast<int>(s.lync_docs.size());
        if (ImGui::MenuItem("Save", "Ctrl+S", false, has_doc)) {
            save_doc(s, s.lync_active_doc);
        }
        if (ImGui::MenuItem("Save All", nullptr, false, !s.lync_docs.empty())) {
            for (int i = 0; i < static_cast<int>(s.lync_docs.size()); ++i)
                if (s.lync_docs[i].dirty) save_doc(s, i);
        }
        if (ImGui::MenuItem("Close", "Ctrl+W", false, has_doc)) {
            s.lync_docs.erase(s.lync_docs.begin() + s.lync_active_doc);
            if (s.lync_active_doc >= static_cast<int>(s.lync_docs.size()))
                s.lync_active_doc = static_cast<int>(s.lync_docs.size()) - 1;
            wrlt_save_open_tabs(s);   // Item 3: persist on close
        }
        // Open Recent submenu — promotes recent-file workflow without
        // needing the user to navigate the source tree. Skipped (greyed)
        // when there's no history yet. Stale paths (file deleted from
        // disk between sessions) are filtered out at draw time.
        if (ImGui::BeginMenu("Open Recent",
                              !s.lync_recent_files.empty())) {
            std::vector<size_t> dead;
            for (size_t i = 0; i < s.lync_recent_files.size(); ++i) {
                const std::string& p = s.lync_recent_files[i];
                std::error_code ec;
                if (!std::filesystem::exists(p, ec)) { dead.push_back(i); continue; }
                std::string label = p;
                if (!s.project_dir.empty()) {
                    auto pd = Engine::editor::normalize_path(s.project_dir);
                    if (label.rfind(pd, 0) == 0) {
                        label = label.substr(pd.size());
                        if (!label.empty() && (label[0] == '/' || label[0] == '\\'))
                            label.erase(0, 1);
                    }
                }
                if (ImGui::MenuItem(label.c_str())) open_lync_doc(s, p);
            }
            // Drop dead entries lazily (after the iteration completes).
            for (auto it = dead.rbegin(); it != dead.rend(); ++it)
                s.lync_recent_files.erase(s.lync_recent_files.begin() + *it);
            if (!s.lync_recent_files.empty()) {
                ImGui::Separator();
                if (ImGui::MenuItem("Clear list"))
                    s.lync_recent_files.clear();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Edit ops on the active doc (also have keyboard shortcuts).
        if (ImGui::MenuItem("Find", "Ctrl+F", false, has_doc)) {
            s.lync_docs[s.lync_active_doc].find_open    = true;
            s.lync_docs[s.lync_active_doc].replace_open = false;
        }
        if (ImGui::MenuItem("Replace", "Ctrl+H", false, has_doc)) {
            s.lync_docs[s.lync_active_doc].find_open    = true;
            s.lync_docs[s.lync_active_doc].replace_open = true;
        }
        if (ImGui::MenuItem("Go to line...", "Ctrl+G", false, has_doc)) {
            s.lync_docs[s.lync_active_doc].goto_line_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle comment", "Ctrl+/", false, has_doc)) {
            op_toggle_line_comment(*s.lync_docs[s.lync_active_doc].editor);
        }
        if (ImGui::MenuItem("Duplicate line", "Ctrl+D", false, has_doc)) {
            op_duplicate_line(*s.lync_docs[s.lync_active_doc].editor);
        }
        if (ImGui::MenuItem("Move line up",   "Alt+Up",   false, has_doc)) {
            op_move_line(*s.lync_docs[s.lync_active_doc].editor, -1);
        }
        if (ImGui::MenuItem("Move line down", "Alt+Down", false, has_doc)) {
            op_move_line(*s.lync_docs[s.lync_active_doc].editor, +1);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Find references", "Shift+F12", false, has_doc)) {
            auto& d = s.lync_docs[s.lync_active_doc];
            const std::string w = word_at_cursor(*d.editor);
            if (!w.empty()) {
                s.lync_refs.query = w;
                auto raw = find_references_in_project(s, w);
                s.lync_refs.hits_.clear();
                s.lync_refs.hits_.reserve(raw.size());
                for (auto& r : raw) {
                    s.lync_refs.hits_.push_back(
                        {std::move(r.file), r.line, r.col, std::move(r.preview)});
                }
                s.lync_refs.open = true;
            }
        }
        if (ImGui::MenuItem("Go to definition", "F12 / Ctrl+Click", false, has_doc)) {
            auto& d = s.lync_docs[s.lync_active_doc];
            const std::string w = word_at_cursor(*d.editor);
            if (!w.empty()) {
                GotoDefResult r = find_definition_global(s, w);
                if (!r.file.empty()) {
                    EditorState::LyncJumpFrame fr;
                    fr.file = d.path;
                    const auto cp = d.editor->GetCursorPosition();
                    fr.line = cp.mLine; fr.col = cp.mColumn;
                    s.lync_jump_back.push_back(std::move(fr));
                    open_lync_doc(s, r.file);
                    if (s.lync_active_doc >= 0 &&
                        s.lync_active_doc < (int)s.lync_docs.size()) {
                        auto& nd = s.lync_docs[s.lync_active_doc];
                        if (nd.editor)
                            nd.editor->SetCursorPosition({r.line, r.col});
                    }
                } else {
                    show_toast(s, ("no definition for '" + w + "'").c_str(),
                                2.0f, true);
                }
            }
        }
        if (ImGui::MenuItem("Back", "Ctrl+-", false,
                            !s.lync_jump_back.empty() && has_doc)) {
            auto fr = s.lync_jump_back.back();
            s.lync_jump_back.pop_back();
            open_lync_doc(s, fr.file);
            if (s.lync_active_doc >= 0 &&
                s.lync_active_doc < (int)s.lync_docs.size()) {
                auto& nd = s.lync_docs[s.lync_active_doc];
                if (nd.editor)
                    nd.editor->SetCursorPosition({fr.line, fr.col});
            }
        }
        ImGui::Separator();
        ImGui::MenuItem("Source pane", nullptr, &s.lync_show_source_pane);
        ImGui::MenuItem("Breadcrumbs",  nullptr, &s.lync_show_breadcrumbs);
        if (ImGui::MenuItem("Settings...")) s.lync_show_settings = true;
        ImGui::EndMenuBar();
    }

    // (No top autocomplete strip - the ghost text at the caret + Tab cue
    // covers the same UX without pushing the editor body around when it
    // appears/disappears. Hover docs are native TextEditor tooltips.)

    // ---- Two-pane layout: Source tree on left, editor body on right ------
    const float total_w = ImGui::GetContentRegionAvail().x;
    if (s.lync_show_source_pane) {
        const float min_left = 140.0f;
        const float max_left = total_w - 220.0f;
        if (s.lync_source_pane_w < min_left) s.lync_source_pane_w = min_left;
        if (s.lync_source_pane_w > max_left) s.lync_source_pane_w = max_left;

        ImGui::BeginChild("##lync_source_pane",
            ImVec2(s.lync_source_pane_w, 0), true);
        // Outline of the active doc - clickable jump-to-decl entries. Skipped
        // when no doc is open. Keeps the section tight (max ~140 px) so the
        // file tree below isn't squeezed.
        if (s.lync_active_doc >= 0 &&
            s.lync_active_doc < (int)s.lync_docs.size() &&
            s.lync_docs[s.lync_active_doc].editor) {
            auto& d = s.lync_docs[s.lync_active_doc];
            if (ImGui::CollapsingHeader("Outline",
                    ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##outline_filter", "filter...",
                    s.lync_outline_filter, sizeof(s.lync_outline_filter));
                ImGui::BeginChild("##outline", ImVec2(0, 220), false);
                const auto syms = scan_symbols(*d.editor);
                if (syms.empty()) {
                    ImGui::TextDisabled("(no defs found)");
                } else {
                    // Pre-build a name -> rich-symbol map so we can append
                    // `(p1, p2) -> ret` to def rows when symbols.json is
                    // loaded. O(N) per outline draw, fine for any sane file.
                    std::unordered_map<std::string, const EditorState::LyncFuncSymbol*> fns;
                    if (s.project_symbols.rich_loaded) {
                        for (const auto& fn : s.project_symbols.funcs)
                            fns[fn.name] = &fn;
                    }
                    auto match = [&](const std::string& n) {
                        if (s.lync_outline_filter[0] == 0) return true;
                        std::string lc = n, fl = s.lync_outline_filter;
                        for (auto& c : lc) c = (char)std::tolower((unsigned char)c);
                        for (auto& c : fl) c = (char)std::tolower((unsigned char)c);
                        return lc.find(fl) != std::string::npos;
                    };
                    for (const auto& sym : syms) {
                        if (!match(sym.name)) continue;
                        const ImVec4 col = sym.kind == 's'
                            ? ImVec4(0.55f, 0.85f, 1.00f, 1.0f)   // struct = sky blue
                            : ImVec4(0.95f, 0.85f, 0.55f, 1.0f);  // fn     = pale yellow
                        ImGui::PushStyleColor(ImGuiCol_Text, col);
                        ImGui::Text("%c", sym.kind == 's' ? 'S' : 'f');
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        // Use Selectable spanning the rest of the line.
                        std::string label = sym.name;
                        auto fnIt = fns.find(sym.name);
                        if (fnIt != fns.end()) {
                            label += "(";
                            label += std::to_string(fnIt->second->params.size());
                            label += ")";
                            if (!fnIt->second->ret_type.empty()
                                    && fnIt->second->ret_type != "void") {
                                label += " -> ";
                                label += fnIt->second->ret_type;
                            }
                        }
                        ImGui::PushID(sym.line);
                        if (ImGui::Selectable(label.c_str()))
                            d.editor->SetCursorPosition({sym.line, 0});
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();
            }
        }
        draw_source_tree_inline(s);
        ImGui::EndChild();

        ImGui::SameLine();
        vertical_splitter("##lync_splitter", &s.lync_source_pane_w,
                           min_left, max_left);
        ImGui::SameLine();

        ImGui::BeginChild("##lync_editor_body", ImVec2(0, 0), false);
        draw_lync_editor_body(s);
        ImGui::EndChild();
    } else {
        // Source pane hidden - editor body fills the window.
        draw_lync_editor_body(s);
    }

    // Live syntax check tick (debounced; uses s.last_dt for the idle timer).
    tick_live_check(s, s.last_dt);

    // Settings popup (deferred OpenPopup + modal renderer).
    draw_lync_settings_popup(s);
    draw_lync_settings_modal(s);

    ImGui::End();
}

// ----------------------------------------------------------------------------
// Compile-error diagnostics: parse the lync compiler's stderr output and
// push per-line markers into the matching open editor tabs. TextEditor draws
// these as red wavy underlines on the gutter and shows the message on hover.
// ----------------------------------------------------------------------------
void apply_lync_diagnostics(EditorState& s,
                             const std::string& compiler_output,
                             bool compile_ok) {
    // Compile OK -> wipe every doc's markers and bail.
    if (compile_ok) {
        for (auto& d : s.lync_docs) {
            if (d.editor) d.editor->SetErrorMarkers({});
            d.error_markers.clear();
        }
        return;
    }

    // Two error formats land in the watcher's captured output:
    //
    //   Lync native:   [<path>:<line>:<col>] <stage>:<sev>: <msg>
    //   C backend:     <path>:<line>:<col>: <sev>: <msg>     (GCC-style)
    //
    // Path tolerates Windows drive letters (C:/...). For the C-backend
    // errors against the auto-generated .c file we map back to the
    // matching .lync file (same dir, same stem) and pin the marker on
    // line 1 - the line numbers in the .c don't correspond to the .lync.
    // Lync native:   [<path>:<line>:<col>] <stage>:<sev>: <msg>
    // C backend:     <path>:<line>:<col>: <sev>: <msg>     (GCC-style)
    //
    // Path may contain mixed slashes and a Windows drive letter colon, so we
    // anchor on `:line:col]` (lync) / `:line:col:` (gcc) at the end and let
    // a greedy `.+` swallow the rest as the path. Both line + col are
    // required - both formats always emit them.
    static const std::regex re_lync(
        R"(^\s*\[(.+):(\d+):(\d+)\]\s+\w+:(error|warning|note):\s*(.*)$)");
    static const std::regex re_gcc(
        R"(^\s*(.+):(\d+):(\d+):\s+(error|warning|note):\s*(.*)$)");

    std::unordered_map<std::string,
        std::map<int, std::string>> per_file;

    auto record = [&](std::string file, int ln, const std::string& kind,
                       const std::string& msg) {
        for (auto& c : file) if (c == '\\') c = '/';

        // Lync's parser emits "expected ; but found <X>" at the line of the
        // OFFENDING token (typically the `}` or next-statement keyword), not
        // the line that's actually missing the `;`. Bump those markers up
        // one line so the squiggle lands on the line the user needs to fix.
        // Same trick for "expected , but found <X>" inside arg lists / struct
        // literals.
        if (ln > 1 &&
            (msg.rfind("expected ;", 0) == 0 ||
             msg.rfind("expected ,", 0) == 0)) {
            --ln;
        }
        // Note: the old prelude-offset compensation was here. It's been
        // removed because lync tags each token with its source filename, so
        // an error at `[user_file:5:1]` means line 5 of the user file
        // directly - no offset needed. The compensation was only ever
        // correct for errors that surfaced inside the prelude itself, where
        // the path in the error message would be the prelude's filename.

        // Map the live syntax check's temp file back to the real source.
        // We write `<stem>.__live.lync` for the in-memory buffer; lync reports
        // errors against that path, but the open doc is `<stem>.lync`. Strip
        // the `.__live` suffix before path matching so markers actually land.
        {
            const std::string mark = ".__live.lync";
            if (file.size() > mark.size() &&
                file.compare(file.size() - mark.size(), mark.size(), mark) == 0) {
                file = file.substr(0, file.size() - mark.size()) + ".lync";
            }
        }

        // Map *.c -> *.lync (generated C file -> source) when it exists.
        // Also collapse the live-check generated `<stem>.__live.c` back to
        // `<stem>.lync`. Pin on line 1 since line numbers don't translate.
        if (file.size() > 2 && file.compare(file.size() - 2, 2, ".c") == 0) {
            std::string stem = file.substr(0, file.size() - 2);
            const std::string live_mark = ".__live";
            if (stem.size() > live_mark.size() &&
                stem.compare(stem.size() - live_mark.size(), live_mark.size(), live_mark) == 0) {
                stem = stem.substr(0, stem.size() - live_mark.size());
            }
            std::string lync_file = stem + ".lync";
            std::error_code ec;
            if (std::filesystem::exists(lync_file, ec)) {
                file = std::move(lync_file);
                ln = 1;
            }
        }
        auto& bucket = per_file[file];
        std::string formatted = "[" + kind + "] " + msg;
        auto it = bucket.find(ln);
        if (it == bucket.end()) bucket.emplace(ln, std::move(formatted));
        else it->second += "\n" + formatted;
    };

    std::istringstream ss(compiler_output);
    std::string line;
    while (std::getline(ss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re_lync)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
        } else if (std::regex_search(line, m, re_gcc)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
        }
    }

    // Apply (or clear) markers on every open doc. We always set so previously-
    // erroring docs that are now clean lose their markers.
    //
    // Path matching is fuzzy: lync may report errors with a path that's
    // (a) the absolute editor-side path, (b) a relative path resolved
    // against the manifest dir, OR (c) just the filename. For each open
    // doc we try those forms in order so a relative-path error still
    // applies to the correctly-opened doc.
    auto match_doc_path = [&](const std::string& doc_norm)
        -> std::map<int, std::string>* {
        // 1. Exact normalized.
        auto it = per_file.find(doc_norm);
        if (it != per_file.end()) return &it->second;
        // 2. Filename suffix - any per_file key whose normalized form is the
        //    suffix of doc_norm (or vice versa).
        for (auto& kv : per_file) {
            const std::string& k = kv.first;
            if (k.empty()) continue;
            // doc_norm ends with k, separated by '/'.
            if (doc_norm.size() > k.size() &&
                doc_norm.compare(doc_norm.size() - k.size(), k.size(), k) == 0 &&
                doc_norm[doc_norm.size() - k.size() - 1] == '/') {
                return &kv.second;
            }
            // basename match (last path component).
            auto last_slash_k   = k.find_last_of('/');
            auto last_slash_doc = doc_norm.find_last_of('/');
            const std::string base_k =
                (last_slash_k == std::string::npos) ? k : k.substr(last_slash_k + 1);
            const std::string base_doc =
                (last_slash_doc == std::string::npos) ? doc_norm : doc_norm.substr(last_slash_doc + 1);
            if (!base_k.empty() && base_k == base_doc) return &kv.second;
        }
        return nullptr;
    };

    for (auto& d : s.lync_docs) {
        if (!d.editor) continue;
        std::string norm = d.path;
        for (auto& c : norm) if (c == '\\') c = '/';
        auto* m = match_doc_path(norm);
        if (!m) {
            d.editor->SetErrorMarkers({});
            d.error_markers.clear();
        } else {
            // Clamp marker line numbers to the doc's actual line count.
            // Lync reports EOF errors as "line N+1" where N is the last
            // line - without clamping, those markers would be invisible.
            const int max_line = (int)d.editor->GetTextLines().size();
            std::map<int, std::string> clamped;
            for (auto& kv : *m) {
                int ln = kv.first;
                if (ln > max_line) ln = max_line;
                if (ln < 1)        ln = 1;
                auto it = clamped.find(ln);
                if (it == clamped.end()) clamped.emplace(ln, kv.second);
                else it->second += "\n" + kv.second;
            }
            d.editor->SetErrorMarkers(clamped);
            d.error_markers = std::move(clamped);
        }
    }
}

// Watcher-side push. Differs from apply_lync_diagnostics in two ways:
//   - On success, only the primary file's markers get cleared (others stay).
//   - On failure, only files mentioned in the output get markers; files
//     untouched by this compile pass keep whatever they had (typically from
//     the live syntax check).
void load_lync_symbols_json(EditorState& s, const std::string& json_path) {
    std::ifstream in(json_path);
    if (!in) return;
    nlohmann::json j;
    try { in >> j; }
    catch (...) {
        Engine::log_write(Engine::LogLevel::Warn, "lync.symbols",
            "failed to parse symbols.json — falling back to regex pool");
        return;
    }
    const int ver = j.value("version", 0);
    if (ver != 1) return;

    auto& ps = s.project_symbols;
    ps.funcs.clear();
    ps.structs.clear();

    if (j.contains("functions") && j["functions"].is_array()) {
        for (const auto& jf : j["functions"]) {
            EditorState::LyncFuncSymbol fn;
            fn.name         = jf.value("name", "");
            fn.ret_type     = jf.value("ret_type", "void");
            fn.ret_nullable = jf.value("ret_nullable", false);
            fn.is_extern    = jf.value("is_extern", false);
            if (jf.contains("params") && jf["params"].is_array()) {
                for (const auto& jp : jf["params"]) {
                    EditorState::LyncParamSymbol p;
                    p.name      = jp.value("name", "");
                    p.type      = jp.value("type", "");
                    p.nullable  = jp.value("nullable", false);
                    p.ownership = jp.value("ownership", "none");
                    fn.params.push_back(std::move(p));
                }
            }
            if (jf.contains("attrs") && jf["attrs"].is_array()) {
                for (const auto& ja : jf["attrs"])
                    fn.attrs.push_back(ja.get<std::string>());
            }
            if (jf.contains("loc") && jf["loc"].is_object()) {
                fn.file = jf["loc"].value("file", "");
                fn.line = jf["loc"].value("line", 0);
                fn.col  = jf["loc"].value("col",  0);
            }
            ps.funcs.push_back(std::move(fn));
        }
    }
    if (j.contains("structs") && j["structs"].is_array()) {
        for (const auto& js : j["structs"]) {
            EditorState::LyncStructSymbol st;
            st.name = js.value("name", "");
            if (js.contains("fields") && js["fields"].is_array()) {
                for (const auto& jfl : js["fields"]) {
                    st.fields.emplace_back(jfl.value("name", ""),
                                            jfl.value("type", ""));
                }
            }
            if (js.contains("attrs") && js["attrs"].is_array()) {
                for (const auto& ja : js["attrs"])
                    st.attrs.push_back(ja.get<std::string>());
            }
            if (js.contains("loc") && js["loc"].is_object()) {
                st.file = js["loc"].value("file", "");
                st.line = js["loc"].value("line", 0);
                st.col  = js["loc"].value("col",  0);
            }
            ps.structs.push_back(std::move(st));
        }
    }
    ps.rich_loaded = true;
    ps.generation++;   // forces per-doc identifier-color refresh next frame
    Engine::log_write(Engine::LogLevel::Info, "lync.symbols",
        ("loaded " + std::to_string(ps.funcs.size()) + " funcs, "
         + std::to_string(ps.structs.size()) + " structs").c_str());
}

void apply_lync_diagnostics_watcher(EditorState& s,
                                     const std::string& compiler_output,
                                     bool compile_ok,
                                     const std::string& primary_file) {
    auto norm = [](std::string p) {
        for (auto& c : p) if (c == '\\') c = '/';
        return p;
    };
    const std::string primary = norm(primary_file);

    if (compile_ok) {
        for (auto& d : s.lync_docs) {
            if (!d.editor) continue;
            if (norm(d.path) == primary) {
                d.editor->SetErrorMarkers({});
                d.error_markers.clear();
            }
        }
        return;
    }

    static const std::regex re_lync(
        R"(^\s*\[([A-Za-z]:[\\/][^:]+|[^:\s][^:]*):(\d+)(?::(\d+))?\]\s+\w+:(error|warning|note):\s*(.*)$)");
    static const std::regex re_gcc(
        R"(^\s*([A-Za-z]:[\\/][^:]+|[^:\s][^:]*):(\d+)(?::(\d+))?:\s*(error|warning|note):\s*(.*)$)");

    std::unordered_map<std::string, std::map<int, std::string>> per_file;
    auto record = [&](std::string file, int ln, const std::string& kind,
                       const std::string& msg) {
        for (auto& c : file) if (c == '\\') c = '/';
        // Prelude-offset compensation. See live-check path for full rationale:
        // lync emits combined-buffer line numbers when the error is in the
        // file passed as input. Find the doc whose path matches `file` and
        // subtract prelude_lines if the reported line is past the file's
        // real length.
        if (s.lync_prelude_lines > 0) {
            for (const auto& doc : s.lync_docs) {
                if (!doc.editor) continue;
                std::string dn = doc.path;
                for (auto& c : dn) if (c == '\\') c = '/';
                if (dn == file) {
                    const int total = (int)doc.editor->GetTotalLines();
                    if (ln > total && ln > s.lync_prelude_lines) {
                        ln -= s.lync_prelude_lines;
                    }
                    break;
                }
            }
        }

        // "expected ; / ," errors point at the next-token line; bump up so
        // the squiggle lands on the line that actually needs the punctuation.
        if (ln > 1 &&
            (msg.rfind("expected ;", 0) == 0 ||
             msg.rfind("expected ,", 0) == 0)) {
            --ln;
        }

        const std::string mark = ".__live.lync";
        if (file.size() > mark.size() &&
            file.compare(file.size() - mark.size(), mark.size(), mark) == 0) {
            file = file.substr(0, file.size() - mark.size()) + ".lync";
        }
        if (file.size() > 2 && file.compare(file.size() - 2, 2, ".c") == 0) {
            std::string stem = file.substr(0, file.size() - 2);
            const std::string lm = ".__live";
            if (stem.size() > lm.size() &&
                stem.compare(stem.size() - lm.size(), lm.size(), lm) == 0) {
                stem = stem.substr(0, stem.size() - lm.size());
            }
            std::string lync_file = stem + ".lync";
            std::error_code ec;
            if (std::filesystem::exists(lync_file, ec)) { file = std::move(lync_file); ln = 1; }
        }
        auto& bucket = per_file[file];
        std::string formatted = "[" + kind + "] " + msg;
        auto it = bucket.find(ln);
        if (it == bucket.end()) bucket.emplace(ln, std::move(formatted));
        else it->second += "\n" + formatted;
    };
    std::istringstream ss(compiler_output);
    std::string line;
    int parsed_lines = 0, total_lines = 0;
    std::string first_unparsed;
    while (std::getline(ss, line)) {
        ++total_lines;
        // Trim trailing CR (Windows line endings) so $ anchors work cleanly.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, re_lync)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
            ++parsed_lines;
        } else if (std::regex_search(line, m, re_gcc)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
            ++parsed_lines;
        } else if (first_unparsed.empty() && !line.empty()) {
            first_unparsed = line;
        }
    }
    if (!first_unparsed.empty() && parsed_lines == 0) {
        char dbg[800];
        std::snprintf(dbg, sizeof(dbg),
            "diag: regex did NOT match line: <<<%s>>> (len=%zu)",
            first_unparsed.c_str(), first_unparsed.size());
        log_write(LogLevel::Warn, "lync.diag", dbg);
    }
    if (total_lines > 0) {
        char dbg[256];
        std::snprintf(dbg, sizeof(dbg),
            "diag: %d/%d compiler lines parsed -> %zu file bucket(s)",
            parsed_lines, total_lines, per_file.size());
        log_write(LogLevel::Info, "lync.diag", dbg);
        for (auto& kv : per_file) {
            char p[512];
            std::snprintf(p, sizeof(p), "  -> '%s' has %zu marker(s)",
                          kv.first.c_str(), kv.second.size());
            log_write(LogLevel::Info, "lync.diag", p);
        }
    }

    // Apply markers ONLY to files mentioned in this compile's output.
    // Untouched files keep their markers (live-check ownership).
    //
    // Same fuzzy match as apply_lync_diagnostics: lync may report a path
    // that's relative to the manifest dir (or just the filename) so we
    // also match by filename suffix / basename.
    auto match_doc_path = [&](const std::string& doc_norm)
        -> std::map<int, std::string>* {
        auto it = per_file.find(doc_norm);
        if (it != per_file.end()) return &it->second;
        for (auto& kv : per_file) {
            const std::string& k = kv.first;
            if (k.empty()) continue;
            if (doc_norm.size() > k.size() &&
                doc_norm.compare(doc_norm.size() - k.size(), k.size(), k) == 0 &&
                doc_norm[doc_norm.size() - k.size() - 1] == '/') {
                return &kv.second;
            }
            auto last_slash_k   = k.find_last_of('/');
            auto last_slash_doc = doc_norm.find_last_of('/');
            const std::string base_k =
                (last_slash_k == std::string::npos) ? k : k.substr(last_slash_k + 1);
            const std::string base_doc =
                (last_slash_doc == std::string::npos) ? doc_norm : doc_norm.substr(last_slash_doc + 1);
            if (!base_k.empty() && base_k == base_doc) return &kv.second;
        }
        return nullptr;
    };

    int matches_applied = 0;
    for (auto& d : s.lync_docs) {
        if (!d.editor) continue;
        const std::string norm_p = norm(d.path);
        auto* m = match_doc_path(norm_p);
        char dl_msg[512];
        std::snprintf(dl_msg, sizeof(dl_msg),
            "  doc '%s' -> %s",
            norm_p.c_str(), m ? "MATCHED" : "(no marker)");
        log_write(LogLevel::Info, "lync.diag", dl_msg);
        if (m) {
            // Clamp lines past EOF (lync reports them as N+1) so the
            // squiggle still lands on the last actual line.
            const int max_line = (int)d.editor->GetTextLines().size();
            std::map<int, std::string> clamped;
            for (auto& kv : *m) {
                int ln = kv.first;
                if (ln > max_line) ln = max_line;
                if (ln < 1)        ln = 1;
                auto it = clamped.find(ln);
                if (it == clamped.end()) clamped.emplace(ln, kv.second);
                else it->second += "\n" + kv.second;
            }
            d.editor->SetErrorMarkers(clamped);
            d.error_markers = std::move(clamped);
            ++matches_applied;
        }
    }

    // Diagnostics: log a hint when the compile reported errors but none
    // landed on any open doc. Usually means the path-matching heuristic
    // failed for a path format we don't recognize.
    if (!per_file.empty() && matches_applied == 0) {
        char msg[512];
        std::string sample;
        for (auto& kv : per_file) { sample = kv.first; break; }
        std::snprintf(msg, sizeof(msg),
            "diagnostics: %d error file(s) reported but no open doc matched "
            "(sample path: '%s'). Open the file to see markers.",
            (int)per_file.size(), sample.c_str());
        log_write(LogLevel::Warn, "lync", msg);
    }
}

// Per-file marker push for the live syntax check. Same parser as the full
// version, but only ever writes to the doc whose path matches `source_path`.
// Other docs' existing markers are NEVER touched - so a successful live run
// on file A doesn't wipe errors on file B.
void apply_lync_diagnostics_for_file(EditorState& s,
                                      const std::string& compiler_output,
                                      const std::string& source_path) {
    // Find the target doc up front; bail if it's not open.
    std::string target = source_path;
    for (auto& c : target) if (c == '\\') c = '/';
    EditorState::LyncDoc* dst = nullptr;
    for (auto& d : s.lync_docs) {
        if (!d.editor) continue;
        std::string norm = d.path;
        for (auto& c : norm) if (c == '\\') c = '/';
        if (norm == target) { dst = &d; break; }
    }
    if (!dst) return;

    // Reuse the same parsing logic as apply_lync_diagnostics - kept inline
    // so the .__live -> .lync remap path applies here too.
    static const std::regex re_lync(
        R"(^\s*\[([A-Za-z]:[\\/][^:]+|[^:\s][^:]*):(\d+)(?::(\d+))?\]\s+\w+:(error|warning|note):\s*(.*)$)");
    static const std::regex re_gcc(
        R"(^\s*([A-Za-z]:[\\/][^:]+|[^:\s][^:]*):(\d+)(?::(\d+))?:\s*(error|warning|note):\s*(.*)$)");

    std::map<int, std::string> markers;
    auto record = [&](std::string file, int ln, const std::string& kind,
                       const std::string& msg) {
        for (auto& c : file) if (c == '\\') c = '/';
        // Prelude-offset compensation: lync's lexer concatenates the prelude
        // with the user file and reports errors using the user file's name
        // but COMBINED-buffer line numbers. So an error in the user's source
        // arrives as `prelude_lines + actual_user_line`. Subtract the offset
        // when the reported line is past the file's real length and we have
        // a non-zero prelude.
        if (s.lync_prelude_lines > 0 && dst && dst->editor) {
            const int total = (int)dst->editor->GetTotalLines();
            if (ln > total && ln > s.lync_prelude_lines) {
                ln -= s.lync_prelude_lines;
            }
        }
        if (ln > 1 &&
            (msg.rfind("expected ;", 0) == 0 ||
             msg.rfind("expected ,", 0) == 0)) {
            --ln;
        }
        // Strip the .__live.lync suffix the worker writes for in-memory text.
        const std::string mark = ".__live.lync";
        if (file.size() > mark.size() &&
            file.compare(file.size() - mark.size(), mark.size(), mark) == 0) {
            file = file.substr(0, file.size() - mark.size()) + ".lync";
        }
        // Only record diagnostics for THIS file. Cross-file errors during
        // a live check are ignored here to keep the contract single-file.
        if (file != target) return;
        std::string formatted = "[" + kind + "] " + msg;
        auto it = markers.find(ln);
        if (it == markers.end()) markers.emplace(ln, std::move(formatted));
        else it->second += "\n" + formatted;
    };

    std::istringstream ss(compiler_output);
    std::string line;
    while (std::getline(ss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re_lync)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
        } else if (std::regex_search(line, m, re_gcc)) {
            record(m[1].str(), std::atoi(m[2].str().c_str()),
                   m[4].str(), m[5].str());
        }
    }

    // Clamp any line numbers past the actual file size to the last line so
    // the squiggle is always visible (lync sometimes reports EOF errors at
    // file_size+1; without clamp the marker is silently dropped).
    {
        const int max_line = (int)dst->editor->GetTextLines().size();
        std::map<int, std::string> clamped;
        for (auto& kv : markers) {
            int ln = kv.first;
            if (ln > max_line) ln = max_line;
            if (ln < 1)        ln = 1;
            auto it = clamped.find(ln);
            if (it == clamped.end()) clamped.emplace(ln, kv.second);
            else it->second += "\n" + kv.second;
        }
        markers = std::move(clamped);
    }
    // Set or clear markers on the single target doc only.
    dst->editor->SetErrorMarkers(markers);
    dst->error_markers = markers;   // mirror for hover tooltip
}

}  // namespace Engine::editor
