#pragma once

// Editor public surface. Internal headers kept to a minimum — the panels are
// free functions that take an EditorState&.

#include "assets.h"

#include <zues/animation.h>
#include <zues/asset.h>
#include <zues/audio_cue.h>
#include <zues/ecs/world.h>
#include <zues/guid.h>
#include <zues/log.h>
#include <zues/math/vec2.h>
#include <zues/types.h>

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
struct ImFont;       // ImGui — full def in <imgui.h>
class  TextEditor;   // ImGuiColorTextEdit (BalazsJako) — defined in <TextEditor.h>
// Forward decl of the C-linkage IWindow_v1 service struct so the
// editor can take pointers to it without pulling <zues/services/window.h>
// into every translation unit that includes this header.
extern "C" struct IWindow_v1;

namespace Engine::editor {

// One captured log line, copied into the editor's ring buffer so the
// Console panel can render it without touching the global log infrastructure
// directly.
struct LogLine {
    LogLevel    level;
    std::string source;
    std::string message;
    // Wall-clock seconds since process start, captured at push time. Used by
    // the Console panel to render an "HH:MM:SS" stamp without dragging in
    // <chrono> formatting at draw time.
    double      time_s;
};

class LogRingBuffer {
public:
    void push(LogLevel lvl, const char* source, const char* msg);
    const std::vector<LogLine>& entries() const { return m_entries; }
    void clear() { m_entries.clear(); }

    // Filter flags (one bit per LogLevel). Default: all on except Trace.
    bool show_level[6] = {false, true, true, true, true, true};

private:
    static constexpr size_t MAX_ENTRIES = 4096;
    std::vector<LogLine> m_entries;
};

// Editor scene-view camera. Plain struct, not an ECS entity — sidesteps
// "hide it from Hierarchy", "make it undeletable", and serialization. The
// editor's camera-publish system copies these fields into IRenderCamera_v1
// each frame; the sprite render system reads from there. Swap the publish
// system out (or replace it with one that follows a Camera2D entity) to
// switch camera sources without touching the render pipeline.
//
// World units: 1 unit = 1 cm. `pixels_per_unit` defaults to 100.
struct EditorCamera {
    Engine::math::vec2 pan             = {0.0f, 0.0f};   // world-space, cm
    float              zoom            = 1.0f;
    float              pixels_per_unit = 100.0f;         // 100 px = 1 cm at zoom 1
};

// Shared editor state. Single instance owned by main(). Panels read/write
// fields directly — no opacity until we actually need it.
// Export config — declared above EditorState so the menu-driven flag
// (build_export_kind) can use it. Definition + behaviour comment lives at
// the build_export() declaration further down.
enum class ExportKind { Debug, Release };

struct EditorState {
    ecs::World*       world          = nullptr;
    ecs::Entity       selected_entity = {};

    LogRingBuffer     log;
    // Console panel text filter. Persisted in EditorState so it survives
    // panel close/reopen and doesn't reset when the log ring wraps.
    char              log_filter[128] = {};

    // Project context (set by main from --project=<path>).
    bool        project_loaded = false;
    std::string project_name;
    std::string project_dir;

    // Renderer service pointer + Scene panel render target. Phase 4.3.
    // `renderer` is `Engine::IRenderer_2D_v1*` (typed in panel_scene.cpp).
    // `scene_rt` is opaque — 0 means "not yet created".
    void*       renderer        = nullptr;
    u32         scene_rt        = 0;
    int         scene_rt_w      = 0;
    int         scene_rt_h      = 0;
    int         editor_frame    = 0;     // monotonic, used by demo content
    u32         demo_texture    = 0;     // 64x64 checker, lazy-created
    float       last_dt         = 1.0f / 60.0f;

    EditorCamera camera{};

    // Help overlay (F1). Lists every editor-wide keyboard shortcut so
    // users can discover them without hunting the docs. Opens / closes
    // with F1 from anywhere in the editor (modal-non-blocking).
    bool show_hotkeys = false;

    // ABI mismatch recovery prompt. Set by main() after the initial
    // project load when the project DLL was built against a different
    // engine API version. Drawn as a modal that offers "Rebuild" --
    // which kicks off a Lync compile (the watcher's force_fire path);
    // a successful compile produces a fresh DLL, the existing source-
    // dirty poll picks it up, and project_loader.reload() runs. The
    // versions are stored so the modal text can read e.g. "got 14,
    // expected 15."
    bool         abi_mismatch_open       = false;
    Engine::u32  abi_mismatch_observed   = 0;
    Engine::u32  abi_mismatch_expected   = 0;
    // After a successful auto-rebuild + reload triggered by the prompt,
    // the editor should also re-attempt the world autoload that we
    // skipped on the original failure. Set to the world path captured
    // at boot; cleared once the autoload runs.
    std::string  abi_pending_world_autoload;

    // Stock icon textures (PNGs from assets/custom-icons). Populated by
    // icons_load() once the renderer is ready. 0 means "icon missing or
    // failed to load" — UI code should fall back to text labels.
    EditorIcons  icons{};

    // ---- Hot reload control + toast feedback ------------------------------
    // `auto_reload_enabled` is on by default — Edit-mode mtime polling will
    // trigger reload(). Disable from the menu if you'd rather press Ctrl+R
    // manually. `reload_poll_accum` accumulates dt; once over the interval
    // (~0.5s) we do one stat check and reset.
    bool   auto_reload_enabled  = true;
    float  reload_poll_accum    = 0.0f;
    bool   want_manual_reload   = false;   // set by File→Reload Project / Ctrl+R

    // Toast notification overlay (top-right of editor viewport). Set via
    // show_toast(); the dockspace renderer draws it and ticks the timer.
    // amber=warning/error, white=info — chosen by show_toast caller.
    std::string toast_message;
    float       toast_remaining_s = 0.0f;
    bool        toast_is_warning  = false;

    // Component removals queued during Inspector iteration; applied after
    // the iteration finishes so we don't mutate the type registry mid-walk.
    struct PendingComponentRemoval { ecs::Entity e; ecs::ComponentId id; };
    std::vector<PendingComponentRemoval> pending_component_removals;

    // Hierarchy mutations queued during draw_hierarchy_panel's iterate_alive
    // walk; drained after the walk so mid-iteration destroys/reparents are safe.
    struct PendingHierarchyOp {
        // Reparent           = make `target` a child of `new_parent`
        //                       (or root if new_parent is null).
        // ReorderBefore /After = move `target` next to sibling `new_parent`
        //                       under that sibling's parent. Reuses the
        //                       new_parent slot as "the reference sibling"
        //                       since the destination chain is implied.
        enum class Kind {
            CreateRoot, CreateChild, Delete, Reparent,
            ReorderBefore, ReorderAfter, Rename,
            Duplicate, Paste
        } kind;
        ecs::Entity target;
        ecs::Entity new_parent;
        std::string new_name;
    };
    std::vector<PendingHierarchyOp> pending_hierarchy_ops;

    // Entity copy/paste clipboard. Holds a save_entity_subtree_json snapshot
    // of the most recently Copy'd (or pre-Duplicate'd) entity hierarchy.
    // Survives selection changes and panel close; cleared on world reload.
    // The Paste op spawns a fresh subtree and parents it where the
    // currently-selected entity sits (or as a root when nothing is
    // selected). Empty string == clipboard empty.
    std::string entity_clipboard;

    // Inline rename state (persists across frames during an active rename).
    ecs::Entity rename_target        = {};
    char        rename_buf[64]       = {};
    bool        rename_focus_pending = false;

    // ---- World persistence --------------------------------------------------
    // `current_world_path` is empty while running on the unsaved demo world.
    // `world_dirty` is set on structural mutations (add/remove component,
    // hierarchy ops); cleared on save or load. Fine-grained field-edit tracking
    // is out of scope for v1.
    std::string current_world_path;
    bool        world_dirty = false;

    // Flipped to true when the editor refuses to autoload a world (e.g.
    // project DLL ABI mismatch). Save paths check this and bail out so a
    // stray Ctrl+S can't overwrite the on-disk world with our gutted in-
    // memory state. Cleared on a successful project DLL load (lync watcher
    // rebuild) so saving Just Works again as soon as the DLL is good.
    bool        world_save_locked = false;

    // Two-phase selection commit for the Hierarchy: armed on mouse-down on
    // a row, committed on mouse-release IF no drag past threshold occurred.
    // Without this, clicking a Hierarchy row to start a drag-drop would
    // swap the Inspector to the dragged entity before the drop landed,
    // making EntityRef slots disappear mid-drop.
    Engine::ecs::Entity click_pending_select{};

    // Deferred file-op requests — set from the File menu, consumed at the top
    // of the next frame so the frame boundary stays clean.
    bool        want_save_world      = false;  // save to current_world_path
    std::string want_save_world_path;           // Save As — also updates current path
    std::string want_load_world_path;           // Open World

    // Up to 5 most-recently used world paths. Persisted in recent_worlds.json
    // next to the editor executable.
    std::vector<std::string> recent_worlds;
    std::filesystem::path    exe_dir;            // set once in main()

    // Game viewport state. Separate RT so Scene + Game can both render in
    // the same frame. `is_playing` is the master flag — frame loop syncs
    // World::tick_mode = is_playing ? Play : Edit. `want_focus_game` is a
    // one-shot flag set when transitioning to Play so the dock auto-tabs
    // to the Game view. `is_paused` only meaningful while playing — freezes
    // the per-phase tick loop while keeping Render alive (so panels stay
    // responsive). `play_snapshot` captures world state at the moment of
    // hitting Play; restored on Stop so playtest mutations don't stick.
    bool             is_playing      = false;
    bool             is_paused       = false;
    bool             want_focus_game = false;
    u32              game_rt         = 0;
    int              game_rt_w       = 0;
    int              game_rt_h       = 0;
    std::vector<u8>  play_snapshot;

    // Transform-gizmo drag state. Persists across frames while a handle
    // is being dragged. None means no drag in progress.
    enum class GizmoMode : u8 { None, MoveX, MoveY, MoveBoth, Rotate };
    struct GizmoDrag {
        GizmoMode          mode             = GizmoMode::None;
        Engine::math::vec2 grab_world       = {0, 0};
        Engine::math::vec2 entity_start_pos = {0, 0};
        float              entity_start_rot = 0.0f;
        float              grab_angle       = 0.0f;     // for rotation
    };
    GizmoDrag transform_drag{};

    // Collider edit-in-scene drag state. Active when the selected entity's
    // collider has `edit_in_scene == 1` and the user grabs one of the
    // handles drawn at the box's mid-edges (or the circle's right edge).
    // Same lifecycle as transform_drag: None means no drag in progress.
    enum class ColliderHandle : u8 {
        None, BoxLeft, BoxRight, BoxTop, BoxBottom, CircleRadius
    };
    struct ColliderDrag {
        ColliderHandle     handle    = ColliderHandle::None;
        // The entity whose collider is being dragged. Cached because the
        // user can change selection mid-drag (we lock to the entity that
        // grabbed the handle, not the current selection).
        Engine::ecs::Entity entity   = {};
        bool                is_circle = false;   // false = box
        Engine::math::vec2  grab_world         = {0, 0};
        Engine::math::vec2  start_half_extents = {0, 0};   // box only
        Engine::math::vec2  start_offset       = {0, 0};
        float               start_radius       = 0.0f;     // circle only
        bool                alt_at_grab        = false;    // remember if ALT was held at click
    };
    ColliderDrag collider_drag{};

    // ---- Undo / Redo --------------------------------------------------------
    // Snapshot-based: every undoable action captures the world's JSON
    // state BEFORE the mutation. Ctrl+Z restores the last snapshot,
    // Ctrl+Y / Ctrl+Shift+Z replays forward. The size cost is ~few KB
    // per snapshot; the cap keeps memory bounded for long sessions.
    //
    // Lifecycle:
    //   undo_begin(s)         -> stash current world_json into pending
    //   undo_commit(s, name)  -> push pending to undo_stack, clear redo
    //   perform_undo(s)       -> pop undo, push current to redo, restore
    //   perform_redo(s)       -> pop redo, push current to undo, restore
    //
    // begin/commit pair around any user action (drag, inspector edit,
    // add/remove component, etc). Cancelled actions just leave the
    // pending snapshot - it'll be overwritten by the next begin.
    struct UndoEntry {
        std::string name;        // e.g. "Transform drag", "BoxCollider edit"
        std::string world_json;  // world.save_json() snapshot
    };
    std::vector<UndoEntry> undo_stack;
    std::vector<UndoEntry> redo_stack;
    std::string            undo_pending_snapshot;
    bool                   undo_action_in_progress = false;
    static constexpr int   undo_max_history        = 100;

    // ---- Lync source watcher + auto-compile -----------------------------------
    // Active when a project has build.lync_main configured. Polls all .lync
    // files under project_dir every ~0.5s; on mtime change, runs the compiler
    // synchronously and toasts the result. cooldown throttles re-triggers to
    // at most once per second so rapid saves don't stack builds.
    struct LyncSourceWatch {
        std::filesystem::path            project_dir;
        std::string                      build_cmd;   // pre-built shell command
        std::filesystem::file_time_type  last_mtime{};
        float                            poll_accum = 0.0f;
        float                            cooldown   = 0.0f;
        // Save-driven trigger. The editor's save_doc sets this true; the
        // watcher loop runs the compile on the next frame and clears it.
        // We keep the mtime poll as a safety net for external edits but
        // it runs at a much slower cadence (5s) to avoid the "compiling
        // every half second" feel.
        bool                             force_fire = false;

        bool active() const { return !project_dir.empty() && !build_cmd.empty(); }
    };
    LyncSourceWatch lync_watch{};

    // Compiler config snapshot captured by main() at project-load time. The
    // live syntax checker uses these to build per-file compile commands
    // (against a temp output) without needing a Project handle.
    std::string lync_compiler_abs;
    std::string lync_plugin_abs;
    std::string lync_include_abs;
    std::string lync_prelude_abs;
    // Lync's lexer concatenates the prelude with the user source and reports
    // ALL errors using the user file's name with the COMBINED line numbers.
    // To put markers on the right line in the editor we subtract this
    // offset (= prelude line count) from any reported line >= it. Cached
    // once at project load by reading lync_prelude_abs.
    int         lync_prelude_lines = 0;

    // Project-wide identifier index for autocomplete. Built lazily by the
    // Lync editor's autocomplete on demand and refreshed when a file's
    // mtime changes. Key = file abs path, value = list of identifier names
    // discovered in that file (struct names, def names, plus the auto-
    // emitted AddX / EachX / HasX / GetX / RemoveX for any [Component]).
    // Rich symbol record for one function param. Populated from the lync
    // compiler's `--emit-symbols` JSON output. Used by the Lync editor for
    // type-aware autocomplete + UFCS-correct param hints.
    struct LyncParamSymbol {
        std::string name;
        std::string type;       // e.g. "int", "float", "Transform2D"
        bool        nullable = false;
        std::string ownership;  // "none" / "own" / "ref"
    };
    struct LyncFuncSymbol {
        std::string                  name;
        std::string                  ret_type;
        bool                         ret_nullable = false;
        bool                         is_extern = false;
        std::vector<LyncParamSymbol> params;
        std::vector<std::string>     attrs;
        // Source location of the definition. `file` is the absolute,
        // forward-slash path written by lync's --emit-symbols (see the
        // compiler's emit_symbols_json). Empty `file` => extern / no body.
        std::string                  file;
        int                          line = 0;
        int                          col  = 0;
    };
    struct LyncStructSymbol {
        std::string name;
        // pair<field_name, field_type>
        std::vector<std::pair<std::string, std::string>> fields;
        std::vector<std::string> attrs;
        std::string file;
        int         line = 0;
        int         col  = 0;
    };

    // One identifier discovered by the regex scanner, with the line it
    // was defined on. Used by goto-def / outline so unbuilt files still
    // navigate. `synthetic` is true for AddX / EachX / GetX etc. that
    // were synthesised from a [Component] decl rather than literally
    // present as a `def` line — those should jump to the parent struct.
    struct LyncFileSym {
        std::string name;
        int         line = 0;       // 0-based
        bool        is_struct = false;
        bool        synthetic = false;
        std::string parent;          // for synthetic helpers: parent struct name
    };
    struct ProjectSymbols {
        std::unordered_map<std::string, std::vector<std::string>> by_file;
        // Same key set as by_file. Each entry holds the located symbols
        // (struct + def names) discovered in that file. by_file is kept
        // in lock-step for the autocomplete fast-path which only wants
        // names; locations are read by goto-def / find-references.
        std::unordered_map<std::string, std::vector<LyncFileSym>>  by_file_locs;
        std::unordered_map<std::string, std::filesystem::file_time_type> mtime;
        // Bumped whenever by_file changes. Per-doc Lync editors compare
        // their last-applied generation against this to decide whether
        // their LanguageDefinition needs refreshing (so user-declared
        // struct types get the KnownIdentifier color).
        int generation = 0;

        // Rich, type-aware symbols. Populated each time a successful build
        // produces a fresh `<project>/.zues/symbols.json`. When `rich_loaded`
        // is false, the editor falls back to the cheaper regex-based pool.
        bool                          rich_loaded = false;
        std::vector<LyncFuncSymbol>   funcs;
        std::vector<LyncStructSymbol> structs;
    };
    ProjectSymbols project_symbols;

    // ---- Assets panel state ---------------------------------------------------
    // Lazy thumbnail cache: PNG path -> texture handle. Loaded on first display
    // (so opening a folder with 100 images doesn't blow GPU mem). Cleared on
    // shutdown via icons_free-style sweep.
    std::string assets_root_relative = "assets";
    // Asset browser view: 0 = tree/list view (current). >0 = grid with this
    // thumbnail size in pixels (Unity-style). Slider in the panel toolbar.
    float       assets_view_size     = 0.0f;
    // For grid mode only: relative path under assets_root we're showing.
    // Empty = the assets root. Drill-in / parent-up updates this string.
    std::string assets_current_subdir;

    // Currently-selected asset (single-click on a tile). When non-null
    // AND no entity is selected, the Inspector panel switches to
    // showing per-asset settings (sprite filtering, PPU, pivot, etc.)
    // rather than entity components. Cleared on entity selection or
    // by clicking blank space in the asset browser.
    Engine::Guid selected_asset_guid{};
    std::string  selected_asset_path;    // canonical (assets-root-relative)

    // Asset-browser inline rename state. When `assets_rename_target` is
    // non-empty, that file/folder's row renders an InputText instead of
    // its label until the user presses Enter (commit) or Escape (cancel).
    // F2 in the asset browser starts a rename on the selected asset.
    std::string  assets_rename_target;            // absolute path being renamed
    char         assets_rename_buf[128] = {};
    bool         assets_rename_focus_pending = false;

    // When true, the next Hierarchy panel draw scrolls the selected row
    // into view (centered). Set whenever the selection changes from
    // outside the Hierarchy itself (Scene picking, project boot, undo,
    // etc.). The Hierarchy clears it after the scroll fires so the user
    // can scroll away without being snapped back every frame.
    bool         hierarchy_scroll_to_selected = false;

    // ---- Sprite cutter modal -------------------------------------------------
    // Set when the user double-clicks a texture asset in the Asset Browser.
    // The modal opens on the next frame, edits the asset's slice list, and
    // writes back to the .meta sidecar on save.
    bool         sprite_cutter_open  = false;
    Engine::Guid sprite_cutter_guid{};
    std::string  sprite_cutter_path;            // absolute, for thumb cache
    // Working copy of the slices while the modal is open. On Save the
    // contents replace the asset's slice list. Discarded on Cancel.
    std::vector<Engine::SpriteSlice> sprite_cutter_slices;
    // Working copy of the rest of the asset settings (PPU, filter, wrap,
    // pivot). The cutter modal lets users edit them inline, and on Save
    // they're written back to the .meta along with the slice list.
    Engine::SpriteAssetSettings sprite_cutter_settings{};
    // Auto-grid inputs. Persist between opens so users don't retype.
    // Auto-cut mode:
    //   0 = By cell size  (cell W + H + padding; grid until image runs out)
    //   1 = By cell count (cols + rows; cell size derived from texture / N)
    //   2 = By alpha      (find each opaque connected region's bbox)
    int          sprite_cutter_auto_mode = 0;
    // Mode 0 inputs.
    int          sprite_cutter_grid_w    = 32;
    int          sprite_cutter_grid_h    = 32;
    int          sprite_cutter_padding   = 0;
    // Mode 1 inputs.
    int          sprite_cutter_cols      = 4;
    int          sprite_cutter_rows      = 1;
    // Mode 2 inputs.
    int          sprite_cutter_alpha_threshold = 8;   // 0..255; pixels above are "opaque"
    int          sprite_cutter_min_size        = 4;   // discard regions smaller than this on either axis
    int          sprite_cutter_selected = -1;     // index into the working slices
    // Interactive drag state. 0 = idle, 1 = creating new rect, 2 =
    // moving an existing rect, 3..6 = resizing via corner handle
    // (TL/TR/BR/BL).
    int          sprite_cutter_drag_mode = 0;
    int          sprite_cutter_drag_target = -1;   // slice index for move/resize
    int          sprite_cutter_drag_ax = 0;        // anchor in tex pixels
    int          sprite_cutter_drag_ay = 0;
    int          sprite_cutter_drag_ox = 0;        // original rect (for move)
    int          sprite_cutter_drag_oy = 0;
    int          sprite_cutter_drag_ow = 0;
    int          sprite_cutter_drag_oh = 0;

    // ---- Animation editor modal --------------------------------------------
    // Set when the user double-clicks a .zanim asset. The editor loads
    // the file's frame list into a working copy, lets the user rearrange
    // it with a live preview, and writes back on Save.
    bool                                anim_editor_open    = false;
    std::string                         anim_editor_path;          // absolute
    Engine::AnimationAsset              anim_editor_working{};     // edited copy
    int                                 anim_editor_selected = -1; // frame index
    bool                                anim_editor_preview_play = true;
    float                               anim_editor_preview_t    = 0.0f;
    float                               anim_editor_preview_speed = 1.0f;

    // Pending operations from the panel (consumed at the end of frame to
    // avoid mutating the directory mid-iteration).
    struct AssetMove { std::string src_abs; std::string dst_dir_abs; };
    std::vector<AssetMove> assets_pending_moves;
    // Inline new-folder state at the top of the panel.
    bool        assets_new_folder_open = false;
    char        assets_new_folder_buf[64] = {};

    // ---- Sprite asset registry (textures != sprites) ------------------------
    // A "sprite" is a named, UV-rectangle slice of a texture. For now every
    // loaded image auto-creates one full-extent sprite named after the file
    // (so the inspector dropdown isn't empty). Once we add a sprite editor
    // the user will be able to slice a single texture into many sprites.
    struct SpriteAsset {
        std::string display_name;   // shown in the dropdown
        std::string abs_path;       // source image path on disk
        u32         texture_handle = 0;  // renderer's texture id
        // UV rect in normalized 0..1 coords. Currently always full-extent;
        // sprite editor will populate sub-rects.
        float       u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    };
    std::vector<SpriteAsset> sprite_registry;

    // ---- World name dialog (replaces native Save As / Open dialogs) ---------
    // World files always live under project_dir/<worlds_dir>. The user picks a
    // path exactly once per project (at creation); from then on it's just a
    // name. `world_dialog_kind` drives the popup's submit action.
    enum class WorldDialogKind : u8 { None, SaveAs, Open };
    WorldDialogKind world_dialog_kind = WorldDialogKind::None;
    char            world_dialog_buf[128] = {};
    bool            world_dialog_just_opened = false;   // grab focus once

    // ---- Source panel: pre-gen templates ------------------------------------
    enum class SourceTemplate : u8 { None, Component, System, Plain };
    SourceTemplate template_kind = SourceTemplate::None;
    char           template_name_buf[64] = {};
    char           template_lang_buf[8]  = {};   // "lync" | "cpp"
    bool           template_just_opened  = false;

    // Mirror of project's default_language so panels can read it without
    // hauling the whole Project struct around. Set in main() after load_project.
    std::string project_default_language = "lync";
    std::string project_worlds_dir       = "worlds";
    std::string project_source_dir       = "src";

    // Source panel — IDE-style file tree rooted at project_dir/src. Lets the
    // user create/delete .lync files + folders without leaving the editor.
    std::string source_root_relative = "src";
    bool        show_source = true;
    // Inline new-file/new-folder state. When `pending_new_kind` != None,
    // a row in the source tree is replaced by an InputText for typing the
    // name. Parent path captured at click time.
    enum class SourceNewKind : u8 { None, File, Folder };
    SourceNewKind pending_new_kind   = SourceNewKind::None;
    std::string   pending_new_parent;          // absolute path
    char          pending_new_buf[128] = {};
    // Inline rename state for source tree.
    std::string source_rename_target;          // absolute path being renamed
    char        source_rename_buf[128] = {};
    std::unordered_map<std::string, u32> asset_thumb_cache;
    bool        assets_refresh_needed = false;

    // ---- Lync editor (integrated text editor for .lync files) ----------------
    // Plain InputTextMultiline + std::string for v1; syntax color comes later
    // via ImGuiColorTextEdit. Each open file is a tab. `dirty` tracks unsaved
    // edits — we won't auto-save: the user presses Ctrl+S (panel-focused) or
    // the Save button. On save we write to disk; the existing lync_watch then
    // notices the mtime change and recompiles + hot-reloads.
    struct LyncDoc {
        std::string path;            // absolute
        std::string display;         // filename only - tab label
        // The actual editable text + cursor + undo state lives inside the
        // TextEditor instance (vendored ImGuiColorTextEdit, global ns). We
        // store it by std::shared_ptr so the LyncDoc itself is trivially
        // movable (TextEditor's internal vectors are heavy + the type isn't
        // move-friendly enough to live directly inside std::vector<LyncDoc>).
        std::shared_ptr<::TextEditor> editor;
        bool        dirty = false;
        bool        focus_next_frame = false;
        // Find / Replace bar state (per doc).
        bool        find_open    = false;
        bool        replace_open = false;
        char        find_buf[128]    = {};
        char        replace_buf[128] = {};
        // Per-doc Find toggle (the bar's [Aa] checkbox). Persists across
        // bar open/close so the user's preference is sticky within a
        // session. Default off (case-insensitive) since most lync code
        // is identifier-grep and that's the friendlier default.
        bool        find_case_sensitive = false;
        // True the frame draw_find_replace_bar should grab focus on the
        // search input. Set whenever Ctrl+F / Ctrl+H opens the bar.
        bool        find_focus_pending  = false;
        // Go-to-line popup trigger (per doc).
        bool        goto_line_open = false;
        char        goto_line_buf[16] = {};
        // F2 rename modal (per doc).
        bool        rename_open = false;
        std::string rename_old;
        char        rename_buf[64] = {};
        // Attribute-snippet expansion: caret line recorded at the END of the
        // previous frame. When the caret moves to a new (empty) line we check
        // whether the line above is a recognised hook attribute and, if so,
        // insert the matching function skeleton. -1 = not yet initialised.
        int         snippet_prev_caret_line = -1;

        // Autocomplete dropdown: persistent selected index across frames so
        // Up/Down can step through suggestions. Reset to 0 whenever the
        // ac.prefix or trigger context changes (handled in the per-frame
        // autocomplete pass).
        int         ac_selected_idx       = 0;
        std::string ac_last_prefix;        // for change-detection
        bool        ac_last_template_mode = false;

        // Last project_symbols.generation applied to this doc's TextEditor
        // language def. -1 = not yet applied. Diffed against the live
        // generation each frame to decide whether to refresh identifiers.
        int         applied_symbol_generation = -1;

        // Cached error markers (1-based line -> message) so the editor's
        // hover-tooltip path can show diagnostics without round-tripping
        // through TextEditor's private mErrorMarkers. Mirrored every time
        // SetErrorMarkers is called inside apply_lync_diagnostics_*.
        std::map<int, std::string> error_markers;

        // Set by Esc while the autocomplete dropdown is open. Suppresses
        // the dropdown for the rest of THIS trigger context - cleared
        // automatically when the user types more / moves the caret to a
        // new prefix.
        bool        ac_dismissed_for_prefix = false;

        // Generic keyword-snippet expansion (Tab trigger).
        // Before Render we detect keyword+Tab and set this flag. After Render
        // we undo the spurious tab TextEditor inserted and inject the snippet.
        bool        snippet_tab_pending    = false;
        std::string snippet_kw;            // matched keyword ("for", "if", etc.)
        int         snippet_kw_line        = -1;
        int         snippet_kw_col_start   = -1;  // start column of the keyword
        std::string snippet_kw_indent;             // captured leading whitespace

        // Surround-with-bracket: capture selection BEFORE Render so we can
        // wrap it when the user types one of () [] {} " '.
        bool        surround_pending       = false;
        char        surround_open_char     = 0;
        std::string surround_text;         // captured selection text
        int         surround_sel_line_start = -1;
        int         surround_sel_col_start  = -1;
        int         surround_sel_line_end   = -1;
        int         surround_sel_col_end    = -1;

        // Postfix `.match` template: caret right after `<expr>.match` and
        // Tab pressed -> after Render we replace [expr_start, caret) with a
        // full match-block scaffold and place caret in the some-arm body.
        // Expression text + start column are captured BEFORE Render.
        bool        postfix_match_pending = false;
        std::string postfix_match_expr;        // captured `<expr>` text
        int         postfix_match_line       = -1;
        int         postfix_match_expr_start = -1;  // col where <expr> begins
        int         postfix_match_caret_end  = -1;  // col right after `match`

        // Match-Enter expansion: detected when user presses Enter on a line
        // ending with `match <expr> {`. After Render we insert the
        // some/null skeleton inside the now-open block.
        bool        match_enter_pending     = false;
        int         match_enter_brace_line  = -1;   // line that ends in `{`
        std::string match_enter_indent;             // indent of that line

        // Multi-cursor ghost carets (Ctrl+Alt+Down). Each entry is a
        // (line, col) at which the editor replays whatever the user types
        // at the primary caret. Cleared by Esc / arrow keys / mouse click.
        // Tracked alongside, not inside, TextEditor (which is single-caret).
        std::vector<std::pair<int,int>> ghost_carets;
    };
    std::vector<LyncDoc> lync_docs;
    int                  lync_active_doc = -1;   // index into lync_docs, -1 = none
    bool                 show_lync_editor = true;

    // ---- Goto-def navigation history --------------------------------------
    // Each entry is a (abs path, 0-based line, 0-based col) the caret was
    // sitting at right BEFORE a goto-def jump. Ctrl+- pops the last entry
    // and restores the caret. Capped to 32 entries (drops oldest).
    struct LyncJumpFrame {
        std::string file;
        int         line = 0;
        int         col  = 0;
    };
    std::vector<LyncJumpFrame> lync_jump_back;

    // ---- Find references popup --------------------------------------------
    // Triggered by Shift+F12 with a word under the caret. Opens a modal
    // listing every word-boundary hit across all .lync files in the
    // project source dir. Click a row to jump (and push a back-stack
    // entry). Esc / outside-click closes.
    struct LyncRefsPopupHit {
        std::string file;       // abs forward-slash
        int         line  = 0;  // 0-based
        int         col   = 0;
        std::string preview;
    };
    struct LyncRefsPopup {
        bool                          open = false;
        std::string                   query;
        std::vector<LyncRefsPopupHit> hits_;
    };
    LyncRefsPopup lync_refs;

    // ---- Symbol outline (active doc) --------------------------------------
    // The outline lives inside the existing left Source pane (under a
    // CollapsingHeader). Type-to-filter narrows the list as you type.
    char  lync_outline_filter[64] = {};
    // Breadcrumbs: at the top of the editor body we show the enclosing
    // struct / def name for the current caret line. Click to jump back
    // to that decl. Toggled from the editor View menu.
    bool  lync_show_breadcrumbs = true;

    // ---- Cross-file rename state ------------------------------------------
    // When the F2 rename modal's "across project" checkbox is on, rename
    // walks every .lync file under <project>/<src>/ and word-boundary
    // replaces. Persisted across modal opens so a user who wants project-
    // wide renames doesn't have to tick the box every time.
    bool lync_rename_across_project = false;

    // Recent Lync files. Most-recent first. Capped at 12 entries; older
    // ones drop off the back. Updated on open_lync_doc; persisted into
    // %APPDATA%/Zues/recent_lync_files.json so it survives restarts.
    std::vector<std::string> lync_recent_files;
    // Set true by the Lync editor body each frame the embedded TextEditor
    // owns keyboard focus. Read by the global Ctrl+S handler in
    // panel_dockspace.cpp to skip the world-save path when the user is
    // typing in code. Reset to false at the start of draw_main_menu_bar.
    bool                 lync_editor_focused = false;
    // Inline Source tree pane on the left of the Lync editor (Jetbrains-
    // style). Width is in pixels; the user can drag the splitter to resize.
    bool                 lync_show_source_pane = true;
    float                lync_source_pane_w    = 240.0f;

    // ---- Lync editor preferences (Settings popup) --------------------------
    // Persisted: not yet (next round). For now they're per-session defaults.
    enum class LyncBraceStyle : u8 { SameLine, NewLine };
    LyncBraceStyle lync_brace_style    = LyncBraceStyle::SameLine;
    bool           lync_rainbow        = true;
    bool           lync_word_wrap      = false;
    bool           lync_auto_close     = true;       // () [] {} "" ''
    bool           lync_show_settings  = false;      // popup visibility
    bool           lync_status_bar     = true;
    bool           lync_live_check     = true;       // live syntax check on idle
    int            lync_wrap_col       = 0;          // visual cut-line column (0 = off)
    bool           lync_indent_guides  = false;      // vertical indent ticks
    bool           lync_scope_marker   = false;      // 1px gutter line for caret's enclosing { }

    // Live-syntax check state. A worker thread fires the lync compiler in
    // syntax-check mode (writing the active doc's buffer to a temp file)
    // when typing has been idle for ~600ms. Result lands in `live_diag_*`
    // and the UI thread applies markers + clears the pending flag.
    struct LiveCheck {
        std::string in_flight_path;        // path of doc being checked
        bool        result_ready = false;
        std::string result_output;         // captured stderr/stdout
        bool        result_ok    = false;
        // Per-doc idle timer (path -> seconds since last edit).
        std::unordered_map<std::string, float>       idle_secs;
        // Snapshot from last frame, for "did user edit this frame" detection.
        std::unordered_map<std::string, std::string> prev_frame_text;
        // Snapshot of what we last sent to a worker, for "is buffer dirty
        // since last check" detection. Distinct from prev_frame_text -
        // conflating the two breaks the idle trigger on doc-open.
        std::unordered_map<std::string, std::string> last_text;
    };
    LiveCheck live_check;

    // Panel visibility (mirrored in the View menu).
    // ---- Generic confirm-on-delete dialog ----------------------------------
    // Any code that wants to do a destructive action calls request_confirm()
    // (declared below): it stashes a message + a callback here, and the next
    // frame draw_confirm_modal pops a centred Yes/Cancel popup. On Yes the
    // callback runs once. ESC or Cancel discards. Single in-flight request.
    std::string           confirm_message;
    std::function<void()> confirm_action;
    bool                  confirm_open = false;

    bool show_hierarchy = true;
    bool show_inspector = true;
    bool show_console   = true;
    bool show_scene     = true;
    bool show_game      = true;
    bool show_systems   = true;
    bool show_assets    = true;
    bool show_audio     = false;
    bool show_cue_editor = false;
    bool show_todos     = true;
    bool show_demo      = false;
    bool show_search    = false;

    // Set from Ctrl+Shift+F global shortcut to focus the Search panel input.
    bool search_focus_pending = false;

    // ---- AudioCue editor state ---------------------------------------------
    // Live edit buffer for the currently-open .zcue. Saved back to disk on
    // any field change (autosave). `cue_editor_target` is the cue's guid;
    // null guid = no cue loaded (panel renders an empty-state hint).
    Engine::Guid     cue_editor_target{};
    Engine::AudioCue cue_editor_buffer{};
    // Toast text + timer surfaced when the buffer is saved, mostly so
    // the user gets confirmation when edits go to disk on every drag.
    double           cue_editor_save_flash_until_s = 0.0;

    // ---- TODO tracker panel state ------------------------------------------
    // Filter + marker toggles for the TODO panel (panel_todos.cpp). Persisted
    // in EditorState so they survive panel close/reopen. Defaults: all four
    // marker kinds visible, empty text filter.
    // Asset browser: paths whose user has clicked the "expand slices"
    // arrow. Lets a texture with slices reveal a draggable list of
    // its sub-rects right under the row, like Unity's atlas expand.
    std::unordered_map<std::string, bool> assets_slice_expanded;

    char todos_filter[128] = {};
    bool todos_show_todo   = true;
    bool todos_show_fixme  = true;
    bool todos_show_xxx    = true;
    bool todos_show_hack   = true;

    // ---- Docs panel ---------------------------------------------------------
    // In-engine documentation. Topics live as markdown in Editor/src/docs/*.md
    // (see project_zues_in_engine_docs memory for the maintenance contract).
    // `docs_lang` toggles between cpp/lync code snippet variants.
    bool        show_docs        = false;
    bool        show_project_settings = false;
    // Set true by the Build → Export menu; main's frame loop calls
    // build_export(s) once on the next tick and clears it. Goes through a
    // flag (rather than calling inline from the menu) so the toast/log it
    // produces aren't issued mid-ImGui-frame from a sub-menu draw.
    bool        want_build_export = false;
    // Which config the menu picked. Read once when want_build_export fires.
    ExportKind  build_export_kind = ExportKind::Debug;
    // Project settings the panel reads + writes back. Mirrored from the
    // loaded Project on boot; main.cpp persists changes to .zuesproject.
    std::string project_default_world;        // path relative to project_dir
    bool        project_settings_dirty = false;
    // Runtime window settings. Editor ignores them (it owns its own
    // dock-space window); the standalone runtime applies them on launch
    // via IWindow_v1::set_size / set_resizable / set_fullscreen.
    int         project_window_width   = 1280;
    int         project_window_height  = 720;
    bool        project_fixed_size     = false;
    bool        project_fullscreen     = false;
    std::string docs_active      = "quickstart";   // current topic id
    enum class DocsLang : u8 { Cpp, Lync };
    DocsLang    docs_lang        = DocsLang::Lync;
    // Set by jump_to_docs() to bring the panel forward + scroll to the
    // anchor named here (matches a heading text). Cleared after one frame.
    std::string docs_pending_anchor;
};

// ---- imgui layer ------------------------------------------------------------

bool imgui_init(GLFWwindow* window);
void imgui_shutdown();
void imgui_begin_frame();
void imgui_end_frame();

// Monospace font for code views. Loaded at imgui_init from the OS's
// stock coding font (Cascadia Mono on Windows, Menlo on macOS, DejaVu
// Sans Mono on Linux). nullptr if the fallback chain failed — code
// views can detect that and render in the default UI font.
extern ImFont* g_code_font;

// ---- theme ------------------------------------------------------------------

void theme_apply_dark_minimal();

// ---- panels -----------------------------------------------------------------

void draw_main_menu_bar(EditorState& s);
void draw_dockspace();
void request_reset_layout();

void draw_hierarchy_panel(EditorState& s);
void draw_inspector_panel(EditorState& s);
void draw_console_panel  (EditorState& s);
void draw_scene_panel    (EditorState& s);
void draw_game_panel     (EditorState& s);
void draw_systems_panel  (EditorState& s);
void draw_assets_panel   (EditorState& s);
// Audio Mixer panel. Master volume + mute, voice/clip counts, an
// emergency "stop all" button. Hidden by default; toggle from View
// menu. Falls back to a "service unavailable" message if the audio
// system failed to boot (no device).
void draw_audio_panel    (EditorState& s);
// AudioCue editor panel. Opens when the user double-clicks a .zcue
// (full edit) or a .wav/.mp3/.ogg/.flac (opens the auto-generated cue
// with a locked entries list). Carries volume / pitch / random / loop
// sliders + the entries list (drag-drop to add audio, X to remove).
void draw_cue_editor_panel(EditorState& s);
// Open the cue editor for a specific cue guid. Loads the cue from
// disk into the editor's buffer and shows the panel. Pass an audio
// guid via open_cue_editor_for_audio() to resolve to its auto-cue.
void open_cue_editor_for_cue  (EditorState& s, Engine::Guid cue_guid);
void open_cue_editor_for_audio(EditorState& s, Engine::Guid audio_guid);
void draw_lync_editor_panel(EditorState& s);
void draw_world_dialog   (EditorState& s);
void draw_confirm_modal  (EditorState& s);
void draw_docs_panel     (EditorState& s);
void draw_project_settings_panel(EditorState& s);
void draw_search_panel           (EditorState& s);

// Jump to a documentation symbol. `symbol` is an identifier (Lync built-in,
// auto-injected helper, attribute name); we map it to a topic id + scroll
// to the heading inside that topic. Opens the Docs panel if closed.
// No-op when the symbol isn't documented.
void jump_to_docs(EditorState& s, const std::string& symbol);

// Stash a destructive action for a yes/cancel confirmation popup. The next
// frame draw_confirm_modal pops a centred prompt; on Yes, `action` runs.
inline void request_confirm(EditorState& s, std::string message,
                             std::function<void()> action) {
    s.confirm_message = std::move(message);
    s.confirm_action  = std::move(action);
    s.confirm_open    = true;
}
// Renders just the source tree contents (no Begin/End). Used by the Lync
// Editor to embed Source as a left-side collapsible pane.
void draw_source_tree_inline(EditorState& s);

// Open `path` in the Lync editor — focuses an existing tab if already open,
// otherwise loads from disk and adds a new tab. Toasts on failure.
void open_lync_doc(EditorState& s, const std::string& abs_path);

// Item 3: Read %APPDATA%/Zues/recent_lync_tabs.json and restore all listed
// tabs, then jump to the last-active one. Safe to call on any state; skips
// paths that no longer exist on disk. Calls open_lync_doc() internally.
void load_recent_lync_tabs(EditorState& s);

// TODO/FIXME/XXX/HACK tracker panel. Walks the project src tree and lists
// every marker comment with a click-to-jump row. State (filter, marker
// toggles, show flag) lives on EditorState.
void draw_todos_panel(EditorState& s);

// Spawn a subprocess and capture its merged stdout+stderr into `out`. On
// Windows uses CreateProcess + CREATE_NO_WINDOW so background invocations
// (live-syntax-check, build) don't flash a console window. Returns the
// child's exit code, or -1 if spawn failed.
int run_capture(const std::string& cmd, std::string& out);

// Normalise mixed Windows separators ('\\') into forward slashes. Use at
// every boundary where a path becomes a UI string or is stored on
// EditorState - paths assembled from std::filesystem::path::string() on
// Windows mix slashes (preserving whatever the user typed), which looks
// ugly when displayed and breaks string comparisons.
inline std::string normalize_path(std::string p) {
    for (auto& c : p) if (c == '\\') c = '/';
    return p;
}

// Convenience: stringify a std::filesystem::path with forward slashes only.
// Use this at every UI / log / state boundary instead of bare Engine::editor::path_str(path).
inline std::string path_str(const std::filesystem::path& p) {
    return normalize_path(p.string());
}

// Sprite cutter modal. Edits the .meta "slices" array for a texture
// asset. Opened by double-clicking a .png in the Asset Browser, which
// sets sprite_cutter_open + sprite_cutter_guid. The draw function is
// idempotent -- safe to call every frame.
void draw_sprite_cutter(EditorState& s);
void open_sprite_cutter(EditorState& s, const std::string& abs_path);

// Animation editor modal. Edits a .zanim asset's frame list (texture
// + slice + per-frame duration), loop flag, FPS. Opened by
// double-clicking a .zanim file in the Asset Browser. Save persists
// to disk; Cancel discards.
void draw_animation_editor(EditorState& s);
void open_animation_editor(EditorState& s, const std::string& abs_path);

// F1 help overlay. Renders a discoverable cheat-sheet of every
// editor-wide shortcut: F2 rename, Ctrl+S save world, Ctrl+R reload
// project, etc. No-op when `show_hotkeys` is false.
void draw_hotkeys_overlay(EditorState& s);

// Debug-draw service. Engine subsystems (particles, animator, audio
// later) publish gizmo lines / circles / rects through this service
// when their category bit is enabled. Editor implements the service
// (queues into the gizmo renderer); runtime stays unaware.
//
//   register_debug_draw_service()  -- once at startup
//   debug_draw_set_selected(e)     -- per-frame, from main loop
//   debug_draw_categories()        -- read mask (for the View menu)
//   debug_draw_set_categories(m)   -- write mask
void register_debug_draw_service();
void debug_draw_set_selected(ecs::Entity e);
Engine::u32  debug_draw_categories();
void         debug_draw_set_categories(Engine::u32 mask);

// OS-drop import. Drains the window service's dropped-path queue and
// copies any image files into the asset browser's current subfolder.
// No-op when no project is loaded; non-image drops are ignored. The
// caller is responsible for clearing the queue afterwards.
void handle_dropped_files(EditorState& s, ::IWindow_v1* window, int count);

// Build → Export. Copies zues_runtime.exe + engine DLLs + project DLL +
// assets + .zuesproject into <project>/dist/<Name>-<Config>/ . The result
// is a self-contained folder the user can zip and ship. Toasts on success
// or failure. See Editor/src/build_export.cpp for file list + skip rules.
//
// Two configs:
//   Debug   - shipped runtime is the Debug build (console window visible,
//             logs intact). For dev iteration / bug repros.
//   Release - Release runtime (no console, log calls compiled to no-ops).
//             For shipping.
bool build_export(EditorState& s, ExportKind kind);

// Parse `compiler_output` (lync + C backend stderr merged) and push any
// `file:line: message` diagnostics into the matching open Lync editor tabs
// as red wavy underlines. Pass an empty string OR `compile_ok=true` to
// clear all markers (e.g. on a successful build). Safe to call when no
// docs are open.
void apply_lync_diagnostics(EditorState& s,
                             const std::string& compiler_output,
                             bool compile_ok);

// Per-file variant: only updates markers on the doc whose path matches
// `source_path`. Other docs' markers are left untouched. Used by the live
// syntax check so a successful run on file A doesn't wipe errors on file B.
// `source_path` should be the absolute, forward-slash path of the doc.
void apply_lync_diagnostics_for_file(EditorState& s,
                                      const std::string& compiler_output,
                                      const std::string& source_path);

// Watcher variant: on success, clears markers ONLY for `primary_file`
// (typically the project's lync_main). On failure, parses the output and
// applies markers to every file the diagnostics mention. Files not in the
// compile chain (e.g. standalone .lync files in src/) keep whatever
// markers the live syntax check has put on them.
void apply_lync_diagnostics_watcher(EditorState& s,
                                     const std::string& compiler_output,
                                     bool compile_ok,
                                     const std::string& primary_file);

// Undo / Redo helpers. Snapshot-based: each action captures the world
// before the mutation; undo restores. See EditorState::UndoEntry above
// for the lifecycle. Cheap to call - the world snapshot is JSON, not a
// deep copy of the archetype tables.
void undo_begin(EditorState& s);
void undo_commit(EditorState& s, const char* name);
void undo_cancel(EditorState& s);
bool undo_perform_undo(EditorState& s);
bool undo_perform_redo(EditorState& s);

// Read the JSON symbol table written by `lync --emit-symbols` into
// EditorState::project_symbols. Silently no-ops if the file is missing or
// malformed (the autocomplete falls back to its regex-based pool).
// Bumps project_symbols.generation so per-doc TextEditors refresh their
// language-definition identifier set.
void load_lync_symbols_json(EditorState& s, const std::string& json_path);

// Register (or look up) a sprite for the given image. De-duped on abs_path.
// `texture_handle` is the renderer's texture id returned by load_texture_from_file.
// Returns the registered sprite's index in EditorState::sprite_registry.
int register_sprite_asset(EditorState& s,
                          const std::string& abs_path,
                          Engine::u32 texture_handle);

// Save the currently-selected entity (and all descendants) as a fresh
// .zprefab under <project>/assets/prefabs/<Name>.zprefab. Mints a new Guid,
// registers the asset in AssetRegistry::instance(). Returns true on success.
// Toasts on failure.
bool prefab_save_selected(EditorState& s);

// Read a .zprefab from disk and instantiate it into the live world. Returns
// the freshly-spawned root entity, or NULL_ENTITY on failure. New entities
// have new ids; intra-subtree refs are remapped, refs outside the subtree
// drop to NULL. The root's Transform2D::position is overridden with `world_pos`.
Engine::ecs::Entity prefab_instantiate_from_file(EditorState& s,
                                                  const std::string& abs_path,
                                                  Engine::math::vec2 world_pos);

// Overwrite an existing .zprefab's snapshot with the dragged entity's subtree,
// PRESERVING THE EXISTING GUID. Used by the Asset Browser drop target so a
// hierarchy entity dragged onto a prefab file replaces its content without
// breaking PrefabRef fields elsewhere in the project. Returns true on success.
bool prefab_overwrite_from_entity(EditorState& s,
                                   const std::string& abs_path,
                                   Engine::ecs::Entity src);

// Spawn a sprite entity from the given asset path at the given world position.
// Used by both the asset-browser context menu (origin) and Scene drag-drop
// (cursor world pos). Returns the new entity, or NULL_ENTITY on failure
// (e.g. texture load failed, no Sprite component registered yet).
Engine::ecs::Entity spawn_sprite_from_asset(EditorState& s,
                                             const std::string& abs_path,
                                             Engine::math::vec2 world_pos);
void draw_toast_overlay  (EditorState& s);

// Trigger a transient toast notification in the top-right. `seconds` is
// how long the toast stays visible (typical: 2-3s); `is_warning` makes it
// amber instead of neutral. Replaces any in-flight toast.
void show_toast(EditorState& s, const char* message,
                float seconds = 2.5f, bool is_warning = false);

// Install / uninstall the log sink that feeds the Console.
void install_log_sink (LogRingBuffer* dest);
void uninstall_log_sink();

}  // namespace Engine::editor
