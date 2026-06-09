#include "editor.h"

#include <zues/host/audio_system.h>

#include <zues/animation.h>
#include <zues/components/render.h>
#include <zues/components/transform.h>
#include <zues/engine.h>
#include <zues/service.h>
#include <zues/services/renderer_2d.h>
#include <zues/services/window.h>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX            // windows.h defines max/min macros that
                                //  break std::max / std::min usage below
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace Engine::editor {

namespace {
    namespace fs = std::filesystem;

    // Lower-case ASCII compare on extension. fs::path::extension() includes
    // the leading dot.
    bool ext_eq(const fs::path& p, const char* e) {
        std::string s = p.extension().string();
        for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
        return s == e;
    }
    bool is_image(const fs::path& p) {
        return ext_eq(p, ".png") || ext_eq(p, ".jpg") || ext_eq(p, ".jpeg");
    }
    bool is_lync(const fs::path& p)    { return ext_eq(p, ".lync"); }
    bool is_zworld(const fs::path& p)  { return ext_eq(p, ".zworld"); }
    bool is_zprefab(const fs::path& p) { return ext_eq(p, ".zprefab"); }
    bool is_audio(const fs::path& p)   {
        return ext_eq(p, ".wav") || ext_eq(p, ".mp3") ||
               ext_eq(p, ".ogg") || ext_eq(p, ".flac");
    }
    bool is_zcue (const fs::path& p)   { return ext_eq(p, ".zcue"); }

    // Auto-generated cue filename pattern: `<basename>.<audio-ext>.zcue`
    // (e.g. `coin.wav.zcue`). The asset registry mints these alongside
    // every audio file; the asset browser hides them so users see one
    // entry per sound, not two. User-authored cues are typed names
    // ending in just `.zcue` -- these stay visible.
    bool is_auto_cue(const fs::path& p) {
        if (!is_zcue(p)) return false;
        const fs::path stem = p.stem();           // strip `.zcue`
        return is_audio(stem);
    }

    // Resolve the assets root: project_dir / assets_root_relative if a project
    // is loaded. When no project is loaded we return an empty path — the
    // panel renders a "(no project loaded)" placeholder instead of dumping
    // the editor's cwd (which would surface the engine source tree).
    fs::path resolve_assets_root(EditorState& s) {
        if (s.project_loaded && !s.project_dir.empty()) {
            return fs::path(s.project_dir) / s.assets_root_relative;
        }
        return {};
    }

#if defined(_WIN32)
    void shell_open(const fs::path& p) {
        std::string s = p.string();
        ShellExecuteA(nullptr, "open", s.c_str(), nullptr, nullptr, SW_SHOW);
    }
    void shell_reveal(const fs::path& p) {
        // ShellExecute on the parent directory opens it in Explorer.
        const fs::path parent = p.parent_path();
        std::string s = parent.string();
        ShellExecuteA(nullptr, "open", s.c_str(), nullptr, nullptr, SW_SHOW);
    }
#else
    void shell_open(const fs::path&)   {}
    void shell_reveal(const fs::path&) {}
#endif

    // Lazy-load + cache a thumbnail for `path`. Returns 0 on failure
    // (file unreadable / unsupported format / no renderer).
    u32 thumb_for(EditorState& s, const fs::path& abs_path) {
        const auto key = Engine::editor::path_str(abs_path);
        if (auto it = s.asset_thumb_cache.find(key); it != s.asset_thumb_cache.end()) {
            return it->second;
        }
        auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
        if (!r || !r->load_texture_from_file) return 0;

        // If the registry already has a runtime handle for this asset
        // (e.g. the world's lazy resolver loaded the texture before the
        // user opened the asset browser), reuse THAT GL handle instead
        // of creating a duplicate. Without this guard, calling
        // load_from_file again creates a fresh GL texture and the
        // subsequent bind_runtime_handle replaces the registry's
        // mapping -- the world's sprites then point at the old handle
        // while the inspector + browser show the new one, which made
        // every world-loaded sprite render as "(unregistered texture)"
        // and lost any in-progress filter/wrap edits.
        const Guid g =
            AssetRegistry::instance().guid_for_any_path(key.c_str());
        if (!g.is_null()) {
            const u32 existing = AssetRegistry::instance()
                .runtime_handle_for_guid(AssetKind::Texture, g);
            if (existing != 0) {
                s.asset_thumb_cache[key] = existing;
                register_sprite_asset(s, key, existing);
                return existing;
            }
        }

        const u32 tex = r->load_texture_from_file(r, key.c_str());
        s.asset_thumb_cache[key] = tex;     // cache 0 too, to avoid re-attempts
        if (tex != 0) {
            register_sprite_asset(s, key, tex);
            if (!g.is_null()) {
                AssetRegistry::instance()
                    .bind_runtime_handle(AssetKind::Texture, tex, g);
                // Apply the .meta filter + wrap on first load so the
                // first thumb / first sprite render already shows the
                // user's authored Nearest/Repeat/etc. settings.
                const auto sett =
                    AssetRegistry::instance().sprite_settings_for(g);
                if (r->set_texture_filter)
                    r->set_texture_filter(r, tex,
                        sett.filter == SpriteFilter::Nearest ? 1 : 0);
                if (r->set_texture_wrap) {
                    int w = 0;
                    if (sett.wrap == SpriteWrap::Repeat) w = 1;
                    else if (sett.wrap == SpriteWrap::Mirror) w = 2;
                    r->set_texture_wrap(r, tex, w);
                }
            }
        }
        return tex;
    }

    // ---- Grid view (Unity-style tile flow) ---------------------------------
    // Shows the contents of `root / s.assets_current_subdir` only - drill in
    // by clicking a folder tile, drill out via the ".." tile. Tile size in
    // pixels comes from the slider.
    void draw_grid(EditorState& s, const fs::path& root, float tile);

    // ---- Drag-drop helpers --------------------------------------------------
    // Asset browser drag payload: the absolute path string of the source
    // file/folder. Folders accept this as a drop target and re-parent the
    // dragged item via std::filesystem::rename. We queue moves on the
    // EditorState so they happen at end-of-frame (mid-iteration rename
    // would invalidate directory_iterator).
    // Asset-browser drag uses the same payload type the Scene panel already
    // recognises ("ZUES_ASSET_PATH" = absolute path string). ImGui only
    // supports one payload type per drag, so we share. Folders interpret
    // the payload as a move source; the Scene viewport interprets it as a
    // sprite spawn. Both safe because they read different drop targets.
    constexpr const char* kAssetDragId = "ZUES_ASSET_PATH";

    void make_drag_source(EditorState& /*s*/, const fs::path& abs) {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) return;
        const std::string p = Engine::editor::path_str(abs);
        ImGui::SetDragDropPayload(kAssetDragId, p.c_str(), p.size() + 1);
        ImGui::Text("Move: %s", abs.filename().string().c_str());
        ImGui::EndDragDropSource();
    }
    void accept_drop_into(EditorState& s, const fs::path& dst_dir) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kAssetDragId)) {
            const std::string src(static_cast<const char*>(pl->Data));
            // Skip pathological no-op drops (item onto its own parent).
            const fs::path src_p(src);
            std::error_code ec;
            if (src_p.parent_path() != dst_dir &&
                    !fs::equivalent(src_p, dst_dir, ec)) {
                s.assets_pending_moves.push_back({src,
                    Engine::editor::path_str(dst_dir)});
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Confirmation-aware delete used by both views.
    void delete_with_confirm(EditorState& s, const fs::path& target,
                              bool is_dir) {
        const std::string disp = target.filename().string();
        const fs::path captured = target;
        if (is_dir) {
            request_confirm(s,
                "Delete folder '" + disp + "' and ALL its contents? "
                "This cannot be undone.",
                [captured]() {
                    std::error_code ec;
                    fs::remove_all(captured, ec);
                });
        } else {
            request_confirm(s,
                "Delete '" + disp + "'?",
                [captured]() {
                    std::error_code ec;
                    fs::remove(captured, ec);
                });
        }
    }

    // Inline rename widget. Returns true while the rename is active --
    // caller should suppress its normal label/Selectable. Commits on
    // Enter (renames file or folder on disk), cancels on Escape or
    // click-away. Updates selection to the new path on commit.
    bool draw_inline_rename(EditorState& s, const fs::path& target,
                             float width = 220.0f) {
        const std::string tgt_str = Engine::editor::path_str(target);
        if (s.assets_rename_target != tgt_str) return false;

        if (s.assets_rename_focus_pending) {
            ImGui::SetKeyboardFocusHere();
            s.assets_rename_focus_pending = false;
        }
        ImGui::SetNextItemWidth(width);
        const bool enter = ImGui::InputText("##assets_rename",
            s.assets_rename_buf, sizeof(s.assets_rename_buf),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
        const bool deactivated = ImGui::IsItemDeactivated();
        if (enter) {
            const fs::path parent = target.parent_path();
            const fs::path dst    = parent / s.assets_rename_buf;
            std::error_code ec;
            if (s.assets_rename_buf[0] && dst != target) {
                if (!fs::exists(dst, ec)) {
                    fs::rename(target, dst, ec);
                    // Move the .meta sidecar alongside the asset so
                    // the GUID stays paired correctly.
                    fs::path src_meta = target; src_meta += ".meta";
                    fs::path dst_meta = dst;    dst_meta += ".meta";
                    if (fs::exists(src_meta, ec)) {
                        fs::rename(src_meta, dst_meta, ec);
                    }
                    if (s.selected_asset_path == tgt_str) {
                        s.selected_asset_path = Engine::editor::path_str(dst);
                    }
                } else {
                    show_toast(s, "Rename: target already exists",
                               3.0f, true);
                }
            }
            s.assets_rename_target.clear();
        } else if (deactivated || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.assets_rename_target.clear();
        }
        return true;
    }

    // Pick a non-colliding "<base> N.<ext>" filename inside `dir`.
    // Returns the full path. Caller writes the file.
    fs::path next_unique(const fs::path& dir, const char* base,
                          const char* ext) {
        for (int i = 0; i < 1000; ++i) {
            fs::path p = (i == 0)
                ? dir / (std::string(base) + ext)
                : dir / (std::string(base) + " " + std::to_string(i) + ext);
            std::error_code ec;
            if (!fs::exists(p, ec)) return p;
        }
        return dir / (std::string(base) + ext);   // fallback
    }

    // Shared "Create" submenu drawn inside the asset browser's right-
    // click menus (folder rows + the empty-area menu). Centralises the
    // list of factory-able asset types so both the tree view and the
    // grid view stay in sync. `dir` is the directory the new asset
    // should land in.
    void draw_create_menu(EditorState& s, const fs::path& dir) {
        if (!ImGui::BeginMenu("Create")) return;

        if (ImGui::MenuItem("Folder")) {
            std::error_code ec;
            fs::create_directory(next_unique(dir, "New Folder", ""), ec);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Animation (.zanim)")) {
            const fs::path p = next_unique(dir, "NewAnimation", ".zanim");
            Engine::AnimationAsset a{};
            a.name = p.stem().string();
            a.loop = true;
            a.fps  = 12.0f;
            // No frames yet -- user fills via animator panel later.
            const std::string ps = Engine::editor::path_str(p);
            if (Engine::save_animation(ps.c_str(), a) != Engine::Result::Ok) {
                show_toast(s, "Create animation: write failed",
                           3.0f, true);
            } else {
                const std::string msg = std::string("Created ") +
                                         p.filename().string();
                show_toast(s, msg.c_str(), 2.0f, false);
            }
        }
        // Future hooks: Prefab, World, Lync component, Scene, etc.
        ImGui::EndMenu();
    }

    // Recursive draw. `rel` is for display labels; `abs` is what we hand off
    // to filesystem ops + thumbnails.
    void draw_dir(EditorState& s, const fs::path& abs, const fs::path& assets_root) {
        std::error_code ec;
        if (!fs::exists(abs, ec) || !fs::is_directory(abs, ec)) return;

        // Collect entries first so we can sort folders-before-files.
        struct Entry { fs::path path; bool is_dir; };
        std::vector<Entry> entries;
        for (auto& it : fs::directory_iterator(abs, ec)) {
            // Hide engine-managed sidecars from the user-facing browser.
            // .meta files are addressable via their parent asset's
            // settings inspector; surfacing them as separate rows just
            // confuses people and invites accidental edits / deletes.
            if (it.path().extension() == ".meta") continue;
            // Hide auto-generated AudioCues -- the audio file itself
            // is the user-facing entry; the cue is its sidecar.
            if (is_auto_cue(it.path())) continue;
            entries.push_back({it.path(), it.is_directory(ec)});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            return Engine::editor::path_str(a.path.filename()) < Engine::editor::path_str(b.path.filename());
        });

        for (const auto& e : entries) {
            const std::string filename = Engine::editor::path_str(e.path.filename());
            ImGui::PushID(filename.c_str());

            if (e.is_dir) {
                // Folders open by default at the root; nested ones collapsed.
                const ImGuiTreeNodeFlags flags =
                    (e.path.parent_path() == assets_root)
                        ? ImGuiTreeNodeFlags_DefaultOpen
                        : 0;
                bool open = false;
                if (draw_inline_rename(s, e.path)) {
                    // Suppressed the TreeNode while renaming; treat as
                    // "closed" so children don't render this frame.
                } else {
                    open = ImGui::TreeNodeEx(filename.c_str(), flags);
                }
                // Folder is both a drop target (move into it) and a drag
                // source (move the folder itself elsewhere).
                accept_drop_into(s, e.path);
                make_drag_source(s, e.path);
                // Folder context menu: Create > submenu + housekeeping.
                if (ImGui::BeginPopupContextItem()) {
                    draw_create_menu(s, e.path);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename", "F2")) {
                        s.assets_rename_target = Engine::editor::path_str(e.path);
                        s.assets_rename_focus_pending = true;
                        const std::string nm = e.path.filename().string();
                        std::strncpy(s.assets_rename_buf, nm.c_str(),
                                     sizeof(s.assets_rename_buf) - 1);
                        s.assets_rename_buf[sizeof(s.assets_rename_buf) - 1] = 0;
                    }
                    if (ImGui::MenuItem("Delete folder"))
                        delete_with_confirm(s, e.path, true);
                    if (ImGui::MenuItem("Reveal in Explorer"))
                        shell_reveal(e.path);
                    ImGui::EndPopup();
                }
                if (open) {
                    draw_dir(s, e.path, assets_root);
                    ImGui::TreePop();
                }
            } else {
                // File leaf. Thumbnail (if image) + filename. Selectable so
                // it highlights on hover and can be a drag-drop source.
                const bool image = is_image(e.path);
                // For textures that have slices in their .meta, render
                // a small expand arrow before the thumbnail so the user
                // can reveal a draggable list of slice rects right
                // under the row -- same UX as Unity's atlas expand.
                const Engine::AssetEntry* img_entry = nullptr;
                if (image) {
                    const std::string abs_str = Engine::editor::path_str(e.path);
                    const Engine::Guid g =
                        AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                    if (!g.is_null()) img_entry = AssetRegistry::instance().find(g);
                }
                const bool has_slices =
                    img_entry && !img_entry->sprite.slices.empty();

                bool* expand_flag = nullptr;
                if (has_slices) {
                    const std::string key = Engine::editor::path_str(e.path);
                    expand_flag = &s.assets_slice_expanded[key];
                    const char* arrow = (*expand_flag) ? "v" : ">";
                    if (ImGui::SmallButton(arrow))
                        *expand_flag = !*expand_flag;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Toggle slice list (drag a slice onto a Sprite "
                            "to bake texture + slice rect in one drop).");
                    ImGui::SameLine();
                }
                if (image) {
                    const u32 tex = thumb_for(s, e.path);
                    if (tex != 0) {
                        ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                                     ImVec2(20, 20));
                        ImGui::SameLine();
                    } else {
                        ImGui::TextDisabled("[img] ");
                        ImGui::SameLine();
                    }
                } else if (is_lync(e.path)) {
                    ImGui::TextDisabled("[L] ");
                    ImGui::SameLine();
                } else if (is_zworld(e.path)) {
                    ImGui::TextDisabled("[w] ");
                    ImGui::SameLine();
                } else if (is_audio(e.path)) {
                    // Real play-icon button before the selectable so the
                    // preview affordance reads as a button rather than a
                    // glyph. The icon textures are loaded once at editor
                    // startup (assets.cpp::icons_load); a missing icon
                    // falls back to a small text label so the row still
                    // works on systems where the icon dir is missing.
                    if (s.icons.play != 0) {
                        if (ImGui::ImageButton("##aprev",
                                static_cast<ImTextureID>(static_cast<std::uintptr_t>(s.icons.play)),
                                ImVec2(14, 14))) {
                            Engine::host::audio_api::preview_path(
                                Engine::editor::path_str(e.path).c_str());
                        }
                    } else {
                        if (ImGui::SmallButton("play##aprev")) {
                            Engine::host::audio_api::preview_path(
                                Engine::editor::path_str(e.path).c_str());
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Preview (one-shot)");
                    ImGui::SameLine();
                } else if (is_zcue(e.path)) {
                    ImGui::TextDisabled("[cue] ");
                    ImGui::SameLine();
                } else {
                    ImGui::TextDisabled("[ ] ");
                    ImGui::SameLine();
                }
                bool row_clicked = false;
                if (!draw_inline_rename(s, e.path)) {
                    row_clicked = ImGui::Selectable(filename.c_str());
                }

                // Switch the inspector to "asset settings" only on
                // button-RELEASE without an intervening drag, AND only
                // for asset kinds that have something useful to show
                // there (textures + cues). On button-press the drag
                // source has the chance to claim the input, so a quick
                // press-and-drag works as expected. Audio / anim / lync
                // files don't get a settings pane today; clicking them
                // shouldn't blow away the inspector's previous content.
                const bool selectable_kind =
                    is_image(e.path) || is_zcue(e.path);
                if (selectable_kind && row_clicked &&
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
                    const std::string abs_sel = Engine::editor::path_str(e.path);
                    s.selected_asset_path = abs_sel;
                    s.selected_asset_guid =
                        AssetRegistry::instance().guid_for_any_path(abs_sel.c_str());
                    s.selected_entity = ecs::Entity{};   // mutually exclusive
                }

                // Double-click handlers per file type:
                //   .lync           -> open in Lync editor
                //   .zworld         -> load as world
                //   image           -> open sprite cutter
                //   .wav/.mp3/etc.  -> open the audio's auto-generated cue
                //                     in the AudioCue editor (entries
                //                     list locked, settings editable)
                //   .zcue           -> open in the AudioCue editor (full edit)
                if (ImGui::IsItemHovered() &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    const std::string abs_str = Engine::editor::path_str(e.path);
                    if      (is_lync(e.path))   open_lync_doc(s, abs_str);
                    else if (is_zworld(e.path)) s.want_load_world_path = abs_str;
                    else if (is_image(e.path))  open_sprite_cutter(s, abs_str);
                    else if (e.path.extension() == ".zanim")
                        open_animation_editor(s, abs_str);
                    else if (is_audio(e.path)) {
                        // Files dropped via Explorer don't pass through
                        // the editor's import flow, so the registry may
                        // not yet know about this audio file. Rescan
                        // once on miss before giving up -- minting the
                        // .meta sidecar AND the auto-generated cue.
                        Guid g = AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        if (g.is_null() && !s.project_dir.empty()) {
                            const fs::path root = fs::path(s.project_dir)
                                                / s.assets_root_relative;
                            AssetRegistry::instance().rescan(root.string().c_str());
                            g = AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        }
                        if (!g.is_null()) open_cue_editor_for_audio(s, g);
                    } else if (is_zcue(e.path)) {
                        Guid g = AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        if (g.is_null() && !s.project_dir.empty()) {
                            const fs::path root = fs::path(s.project_dir)
                                                / s.assets_root_relative;
                            AssetRegistry::instance().rescan(root.string().c_str());
                            g = AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        }
                        if (!g.is_null()) open_cue_editor_for_cue(s, g);
                    }
                }

                // (Audio preview button is rendered before the
                // selectable above so it stays clickable when the
                // selectable claims the rest of the row.)

                // Drag source - one payload (kAssetDragId == "ZUES_ASSET_PATH").
                // Scene viewport treats this as a sprite spawn; asset folders
                // treat it as a move. Same data, different drop targets.
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    const std::string abs_str = Engine::editor::path_str(e.path);
                    ImGui::SetDragDropPayload(kAssetDragId,
                        abs_str.c_str(), abs_str.size() + 1);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drop target on .zprefab rows: accept a HIERARCHY_ENTITY drag
                // and overwrite the prefab's snapshot with the dragged entity
                // subtree, KEEPING the existing guid. PrefabRef fields scattered
                // around the project (and saved worlds) reference the prefab by
                // guid, so minting a new one would break every reference. With
                // guid preserved, every existing reference picks up the new
                // contents on next instantiate.
                if (is_zprefab(e.path) && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl =
                            ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                        if (pl->DataSize == (int)sizeof(Engine::ecs::Entity)) {
                            Engine::ecs::Entity dragged{};
                            std::memcpy(&dragged, pl->Data, sizeof(dragged));
                            (void)Engine::editor::prefab_overwrite_from_entity(
                                s, Engine::editor::path_str(e.path), dragged);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Per-file context menu.
                if (ImGui::BeginPopupContextItem()) {
                    if (image) {
                        if (ImGui::MenuItem("Spawn as Sprite Entity")) {
                            spawn_sprite_from_asset(s, Engine::editor::path_str(e.path), {0.0f, 0.0f});
                        }
                    } else if (is_lync(e.path)) {
                        if (ImGui::MenuItem("Open in Lync Editor")) {
                            open_lync_doc(s, Engine::editor::path_str(e.path));
                        }
                        if (ImGui::MenuItem("Open in External Editor")) {
                            shell_open(e.path);
                        }
                    } else if (is_zworld(e.path)) {
                        if (ImGui::MenuItem("Load as World")) {
                            s.want_load_world_path = Engine::editor::path_str(e.path);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename", "F2")) {
                        s.assets_rename_target = Engine::editor::path_str(e.path);
                        s.assets_rename_focus_pending = true;
                        const std::string nm = e.path.filename().string();
                        std::strncpy(s.assets_rename_buf, nm.c_str(),
                                     sizeof(s.assets_rename_buf) - 1);
                        s.assets_rename_buf[sizeof(s.assets_rename_buf) - 1] = 0;
                    }
                    if (ImGui::MenuItem("Delete"))
                        delete_with_confirm(s, e.path, false);
                    if (ImGui::MenuItem("Reveal in Explorer")) {
                        shell_reveal(e.path);
                    }
                    ImGui::EndPopup();
                }

                // Nested slice list -- shown when the user clicked the
                // expand arrow next to a texture-with-slices. Each row
                // is its own drag source emitting ZUES_SPRITE_SLICE so
                // dropping on a Sprite component bakes both the texture
                // handle AND the slice rect in one motion.
                if (has_slices && expand_flag && *expand_flag && img_entry) {
                    const u32 tex = thumb_for(s, e.path);
                    int tex_w = 0, tex_h = 0;
                    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
                    if (tex && r && r->get_texture_size)
                        r->get_texture_size(r, tex, &tex_w, &tex_h);

                    ImGui::Indent(20.0f);
                    for (size_t si = 0; si < img_entry->sprite.slices.size(); ++si) {
                        const auto& sl = img_entry->sprite.slices[si];
                        ImGui::PushID(static_cast<int>(si));
                        const float u0 = (tex_w > 0) ? (float)sl.x        / (float)tex_w : 0.0f;
                        const float v0 = (tex_h > 0) ? (float)sl.y        / (float)tex_h : 0.0f;
                        const float u1 = (tex_w > 0) ? (float)(sl.x+sl.w) / (float)tex_w : 1.0f;
                        const float v1 = (tex_h > 0) ? (float)(sl.y+sl.h) / (float)tex_h : 1.0f;

                        if (tex) {
                            ImGui::Image(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                                         ImVec2(18, 18),
                                         ImVec2(u0, v0), ImVec2(u1, v1));
                        } else {
                            ImGui::TextDisabled("[s]");
                        }
                        ImGui::SameLine();
                        const std::string label =
                            sl.name.empty() ? std::string("(slice ") +
                                              std::to_string(si) + ")"
                                            : sl.name;
                        ImGui::Selectable(label.c_str());

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            // Same payload layout the texture asset
                            // settings panel uses; the Sprite component
                            // accepts both. Inlined here so the asset
                            // browser doesn't depend on inspector
                            // internals.
                            struct SlicePayload {
                                Engine::Guid texture_guid;
                                int   slice_index;
                                int   slice_x, slice_y, slice_w, slice_h;
                                int   border_l, border_r, border_t, border_b;
                                int   scale_mode, center_mode;
                                float texture_ppu;
                                float pivot_x, pivot_y;
                            };
                            SlicePayload pl{};
                            pl.texture_guid = img_entry->guid;
                            pl.slice_index  = (int)si;
                            pl.slice_x = sl.x; pl.slice_y = sl.y;
                            pl.slice_w = sl.w; pl.slice_h = sl.h;
                            pl.border_l = sl.border_l; pl.border_r = sl.border_r;
                            pl.border_t = sl.border_t; pl.border_b = sl.border_b;
                            pl.scale_mode  = static_cast<int>(sl.scale_mode);
                            pl.center_mode = static_cast<int>(sl.center_mode);
                            pl.texture_ppu = img_entry->sprite.pixels_per_unit;
                            pl.pivot_x = sl.pivot_x;
                            pl.pivot_y = sl.pivot_y;
                            ImGui::SetDragDropPayload("ZUES_SPRITE_SLICE",
                                &pl, sizeof(pl));
                            if (tex) {
                                ImGui::Image(
                                    static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                                    ImVec2(48, 48),
                                    ImVec2(u0, v0), ImVec2(u1, v1));
                                ImGui::SameLine();
                            }
                            ImGui::Text("%s", label.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s\n%dx%d px",
                                label.c_str(), sl.w, sl.h);
                        ImGui::PopID();
                    }
                    ImGui::Unindent(20.0f);
                }
            }

            ImGui::PopID();
        }
    }

    void draw_grid(EditorState& s, const fs::path& root, float tile) {
        // Resolve current dir relative to root + clamp it back inside.
        fs::path cur = root;
        if (!s.assets_current_subdir.empty()) cur = root / s.assets_current_subdir;
        std::error_code ec;
        if (!fs::exists(cur, ec) || !fs::is_directory(cur, ec)) {
            // Drill state went stale (folder removed). Snap back to root.
            s.assets_current_subdir.clear();
            cur = root;
        }

        // Collect entries sorted (folders first, then files alphabetical).
        struct Entry { fs::path path; bool is_dir; };
        std::vector<Entry> entries;
        for (auto& it : fs::directory_iterator(cur, ec)) {
            // Hide .meta sidecars; see the list-view loop above for rationale.
            if (it.path().extension() == ".meta") continue;
            // Hide auto-generated AudioCues -- the audio file itself
            // is the user-facing entry; the cue is its sidecar.
            if (is_auto_cue(it.path())) continue;
            entries.push_back({it.path(), it.is_directory(ec)});
        }
        std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) {
                if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
                return a.path.filename().string() < b.path.filename().string();
            });

        ImGui::BeginChild("##assets_grid", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Compute tiles per row from current width.
        const float pad     = 8.0f;
        const float label_h = ImGui::GetTextLineHeight() * 1.5f;
        const float cell_w  = tile + pad;
        const float cell_h  = tile + label_h + pad;
        const float avail_w = ImGui::GetContentRegionAvail().x;
        int cols            = std::max(1, (int)(avail_w / cell_w));

        auto begin_tile = [&](int idx) {
            const int col = idx % cols;
            if (col != 0) ImGui::SameLine();
        };

        int idx = 0;

        // Parent up tile (only when not at root).
        if (!s.assets_current_subdir.empty()) {
            begin_tile(idx++);
            ImGui::PushID("##up");
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##up_sel", false, 0,
                                                    ImVec2(tile, cell_h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, ImVec2(pos.x + tile, pos.y + tile),
                              IM_COL32(80, 80, 90, 255), 4.0f);
            dl->AddText(ImVec2(pos.x + tile * 0.4f, pos.y + tile * 0.4f),
                        IM_COL32_WHITE, "..");
            dl->AddText(ImVec2(pos.x + 4, pos.y + tile + 2),
                        IM_COL32(180,180,180,255), "(parent)");
            if (clicked) {
                fs::path p(s.assets_current_subdir);
                if (p.has_parent_path()) {
                    s.assets_current_subdir =
                        Engine::editor::path_str(p.parent_path());
                } else {
                    s.assets_current_subdir.clear();
                }
            }
            ImGui::PopID();
        }

        for (const auto& e : entries) {
            begin_tile(idx++);
            const std::string filename = e.path.filename().string();
            ImGui::PushID(filename.c_str());

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##tile_sel", false,
                                                    ImGuiSelectableFlags_AllowDoubleClick,
                                                    ImVec2(tile, cell_h));
            const bool double_clicked = clicked &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 thumb_max{pos.x + tile, pos.y + tile};

            if (e.is_dir) {
                dl->AddRectFilled(pos, thumb_max,
                                  IM_COL32(70, 90, 120, 255), 4.0f);
                dl->AddText(ImVec2(pos.x + tile * 0.4f, pos.y + tile * 0.35f),
                            IM_COL32_WHITE, "[D]");
                accept_drop_into(s, e.path);
                make_drag_source(s, e.path);
                if (clicked && !double_clicked) {
                    // single click = no-op for now (could select)
                }
                if (double_clicked) {
                    fs::path rel = e.path.lexically_relative(root);
                    s.assets_current_subdir = Engine::editor::path_str(rel);
                }
            } else {
                const bool is_img = is_image(e.path);
                u32 tex = 0;
                if (is_img) tex = thumb_for(s, e.path);
                if (tex != 0) {
                    dl->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                                 pos, thumb_max);
                } else {
                    // Coloured square keyed by extension.
                    const std::string ext = e.path.extension().string();
                    ImU32 c = IM_COL32(90, 90, 95, 255);
                    if      (ext == ".lync")    c = IM_COL32(220, 150, 60, 255);
                    else if (ext == ".cpp" || ext == ".h") c = IM_COL32(60, 130, 200, 255);
                    else if (ext == ".zworld" || ext == ".json") c = IM_COL32(110, 180, 90, 255);
                    dl->AddRectFilled(pos, thumb_max, c, 4.0f);
                    if (!ext.empty()) {
                        dl->AddText(ImVec2(pos.x + 4, pos.y + 4),
                                    IM_COL32_WHITE, ext.c_str());
                    }
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    const std::string p = Engine::editor::path_str(e.path);
                    ImGui::SetDragDropPayload(kAssetDragId,
                        p.c_str(), p.size() + 1);
                    ImGui::Text("%s", filename.c_str());
                    ImGui::EndDragDropSource();
                }
                if (clicked && !double_clicked) {
                    // Single-click selects the asset. Inspector picks
                    // it up and shows per-asset settings (sprite
                    // filtering, PPU, pivot, ...) when no entity is
                    // selected. Selecting an entity clears the asset
                    // selection elsewhere.
                    const std::string p = Engine::editor::path_str(e.path);
                    s.selected_asset_path = p;
                    s.selected_asset_guid =
                        AssetRegistry::instance().guid_for_any_path(p.c_str());
                    s.selected_entity = ecs::Entity{};   // mutually exclusive
                }
                if (double_clicked) {
                    const std::string abs_str = Engine::editor::path_str(e.path);
                    if      (is_lync(e.path))   open_lync_doc(s, abs_str);
                    else if (is_zworld(e.path)) s.want_load_world_path = abs_str;
                    else if (is_image(e.path))  open_sprite_cutter(s, abs_str);
                    else if (e.path.extension() == ".zanim")
                        open_animation_editor(s, abs_str);
                    else if (is_audio(e.path)) {
                        const Guid g =
                            AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        if (!g.is_null()) open_cue_editor_for_audio(s, g);
                    } else if (is_zcue(e.path)) {
                        const Guid g =
                            AssetRegistry::instance().guid_for_any_path(abs_str.c_str());
                        if (!g.is_null()) open_cue_editor_for_cue(s, g);
                    }
                }
            }

            // File / folder label below the thumb. While renaming this
            // tile, render an InputText overlaid in the label area so
            // users can type without losing context.
            const std::string tgt_str = Engine::editor::path_str(e.path);
            const bool renaming = (s.assets_rename_target == tgt_str);
            if (renaming) {
                ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + tile));
                draw_inline_rename(s, e.path, tile);
            } else {
                std::string display = filename;
                if (display.size() > (size_t)(tile / 6))
                    display = display.substr(0, (size_t)(tile / 6) - 1) + "..";
                dl->AddText(ImVec2(pos.x + 2, pos.y + tile + 2),
                            IM_COL32(220,220,220,255), display.c_str());
            }

            // Right-click context menu on the tile.
            if (ImGui::BeginPopupContextItem()) {
                if (e.is_dir) {
                    draw_create_menu(s, e.path);
                }
                if (ImGui::MenuItem("Rename", "F2")) {
                    s.assets_rename_target = tgt_str;
                    s.assets_rename_focus_pending = true;
                    std::strncpy(s.assets_rename_buf, filename.c_str(),
                                 sizeof(s.assets_rename_buf) - 1);
                    s.assets_rename_buf[sizeof(s.assets_rename_buf) - 1] = 0;
                }
                if (e.is_dir) {
                    if (ImGui::MenuItem("Delete folder"))
                        delete_with_confirm(s, e.path, true);
                } else {
                    if (ImGui::MenuItem("Delete"))
                        delete_with_confirm(s, e.path, false);
                }
                if (ImGui::MenuItem("Reveal in Explorer")) shell_reveal(e.path);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        // Whole grid background also accepts drops (move to current dir).
        ImGui::Dummy(ImVec2(0, 8));
        accept_drop_into(s, cur);

        // Empty-area right-click: Create > submenu so users can spawn
        // assets without first having to right-click an existing item.
        if (ImGui::BeginPopupContextWindow("##grid_bg_ctx", ImGuiPopupFlags_MouseButtonRight |
                                                              ImGuiPopupFlags_NoOpenOverItems)) {
            draw_create_menu(s, cur);
            ImGui::EndPopup();
        }

        ImGui::EndChild();
    }

}  // namespace

int register_sprite_asset(EditorState& s,
                          const std::string& abs_path,
                          Engine::u32 texture_handle) {
    // Bind the renderer-assigned slot id back to the asset's stable
    // GUID so the world serializer can persist a guid hex tail
    // alongside the (idx, gen) pair on Sprite::texture. Without this
    // a saved Sprite re-loads with whatever index was current at save
    // time -- different across runs, so the texture goes blank.
    if (texture_handle != 0) {
        const Engine::Guid g =
            Engine::AssetRegistry::instance().guid_for_any_path(abs_path.c_str());
        if (!g.is_null()) {
            Engine::AssetRegistry::instance()
                .bind_runtime_handle(Engine::AssetKind::Texture,
                                      texture_handle, g);
        }
    }

    // De-dup by absolute path. If the same image is reloaded (e.g. cache
    // cleared) we update the texture handle in place so existing entities
    // referencing the sprite stay valid after the picker rewrites them.
    for (int i = 0; i < static_cast<int>(s.sprite_registry.size()); ++i) {
        if (s.sprite_registry[i].abs_path == abs_path) {
            s.sprite_registry[i].texture_handle = texture_handle;
            return i;
        }
    }
    EditorState::SpriteAsset sa;
    sa.abs_path        = abs_path;
    sa.display_name    = std::filesystem::path(abs_path).stem().string();
    sa.texture_handle  = texture_handle;
    s.sprite_registry.push_back(std::move(sa));
    return static_cast<int>(s.sprite_registry.size()) - 1;
}

// Spawn helper - also called from panel_scene.cpp on drag-drop. Keeps the
// "load + create entity + add Sprite" recipe in one place.
ecs::Entity spawn_sprite_from_asset(EditorState& s,
                                     const std::string& abs_path,
                                     Engine::math::vec2 world_pos) {
    if (!s.world) return {};
    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
    if (!r || !r->load_texture_from_file) {
        show_toast(s, "No renderer", 2.0f, true);
        return {};
    }
    const u32 tex = r->load_texture_from_file(r, abs_path.c_str());
    if (tex == 0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Texture load failed: %s", abs_path.c_str());
        show_toast(s, buf, 3.0f, true);
        return {};
    }
    // Cache for later thumbnail use too.
    s.asset_thumb_cache[abs_path] = tex;
    register_sprite_asset(s, abs_path, tex);

    const auto sprite_id = s.world->find_component_id("Sprite");
    if (!sprite_id) {
        show_toast(s, "Sprite component not registered yet", 2.0f, true);
        return {};
    }
    const auto e = s.world->create_entity();      // auto-Transform2D + auto-Name
    // Set position via Transform2D.
    const auto xform_id = s.world->find_component_id("Transform2D");
    if (xform_id) {
        if (auto* tr = static_cast<components::Transform2D*>(
                s.world->get_component(e, xform_id))) {
            tr->position = world_pos;
        }
    }
    components::Sprite sp{};
    sp.size = {1.0f, 1.0f};
    sp.tint = math::color::white();
    sp.texture.index      = tex;
    sp.texture.generation = 1;
    s.world->add_component(e, sprite_id, &sp);

    s.selected_entity = e;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Spawned sprite from %s",
                  Engine::editor::path_str(std::filesystem::path(abs_path).filename()).c_str());
    show_toast(s, buf, 2.0f, false);
    return e;
}

void draw_assets_panel(EditorState& s) {
    if (!s.show_assets) return;
    if (!ImGui::Begin("Assets", &s.show_assets)) { ImGui::End(); return; }

    const fs::path root = resolve_assets_root(s);

    // No project loaded → friendly placeholder, no file tree.
    if (root.empty()) {
        ImGui::TextDisabled("(no project loaded)");
        ImGui::TextDisabled("Open a project from the Launcher, or pass");
        ImGui::TextDisabled("--project=<path/to/project.zproj> on the command line.");
        ImGui::End();
        return;
    }

    // ---- Toolbar -----------------------------------------------------
    if (ImGui::Button("Refresh")) s.asset_thumb_cache.clear();
    ImGui::SameLine();
    if (ImGui::Button("+ Folder")) {
        s.assets_new_folder_open = true;
        s.assets_new_folder_buf[0] = 0;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("##view_size", &s.assets_view_size, 0.0f, 128.0f,
                        s.assets_view_size <= 1.0f ? "list" : "grid %.0fpx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag right for grid view (Unity style); leftmost = list view.");
    ImGui::SameLine();
    {
        std::string label = Engine::editor::path_str(root);
        if (!s.assets_current_subdir.empty() && s.assets_view_size > 1.0f)
            label += "/" + s.assets_current_subdir;
        if (label.size() > 60) label = "..." + label.substr(label.size() - 57);
        ImGui::TextDisabled("%s", label.c_str());
    }

    // Inline new-folder input row (in whichever dir is "current").
    if (s.assets_new_folder_open) {
        const fs::path target_dir =
            s.assets_view_size > 1.0f
                ? root / s.assets_current_subdir
                : root;
        ImGui::SetNextItemWidth(220);
        ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##new_folder",
            s.assets_new_folder_buf, sizeof(s.assets_new_folder_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool create_clicked = ImGui::SmallButton("Create");
        ImGui::SameLine();
        const bool cancel_clicked = ImGui::SmallButton("Cancel");
        if (enter || create_clicked) {
            if (s.assets_new_folder_buf[0]) {
                std::error_code mec;
                fs::create_directory(target_dir / s.assets_new_folder_buf, mec);
            }
            s.assets_new_folder_open = false;
        } else if (cancel_clicked || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.assets_new_folder_open = false;
        }
    }
    // F2 starts a rename on the currently-selected asset. Mirrors the
    // hierarchy panel's rename flow. Only fires when the assets panel
    // has keyboard focus so it doesn't steal F2 from other panels.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_F2) &&
        !s.selected_asset_path.empty() &&
        s.assets_rename_target.empty()) {
        s.assets_rename_target       = s.selected_asset_path;
        s.assets_rename_focus_pending = true;
        const fs::path p = s.selected_asset_path;
        const std::string nm = p.filename().string();
        std::strncpy(s.assets_rename_buf, nm.c_str(),
                     sizeof(s.assets_rename_buf) - 1);
        s.assets_rename_buf[sizeof(s.assets_rename_buf) - 1] = 0;
    }

    ImGui::Separator();

    std::error_code ec;
    if (!fs::exists(root, ec)) {
        ImGui::TextDisabled("(assets directory not found)");
        ImGui::TextDisabled("Create:  %s", Engine::editor::path_str(root).c_str());
        ImGui::End();
        return;
    }

    // ---- Body: list view at slider~0, grid view above ---------------
    if (s.assets_view_size <= 1.0f) {
        if (ImGui::BeginChild("##assets_tree", ImVec2(0, 0), false,
                               ImGuiWindowFlags_HorizontalScrollbar)) {
            draw_dir(s, root, root);
            // Root itself accepts drops (move-to-root).
            ImGui::Dummy(ImVec2(0, 4));   // small drop zone padding
            accept_drop_into(s, root);
        }
        ImGui::EndChild();
    } else {
        draw_grid(s, root, s.assets_view_size);
    }

    // ---- Apply queued moves at end of frame -------------------------
    // Mid-iteration rename would invalidate the directory_iterator; do
    // them now after every panel pass is done. Toast on conflict.
    for (const auto& m : s.assets_pending_moves) {
        const fs::path src(m.src_abs);
        const fs::path dst = fs::path(m.dst_dir_abs) / src.filename();
        std::error_code mec;
        if (fs::exists(dst, mec)) {
            show_toast(s, "Move skipped: target already exists", 2.5f, true);
            continue;
        }
        fs::rename(src, dst, mec);
        if (mec) show_toast(s, "Move failed", 2.5f, true);
    }
    s.assets_pending_moves.clear();

    ImGui::End();
}

// ----------------------------------------------------------------------------
// OS-drop import: copies dropped files into the currently-visible asset
// folder. Image files (.png / .jpg / .bmp / .tga / .jpeg) become assets
// the registry can address; everything else is ignored with a toast so
// the user knows what we accept.
// ----------------------------------------------------------------------------

void handle_dropped_files(EditorState& s, ::IWindow_v1* window, int count) {
    if (!window || !window->dropped_path_at) return;
    if (s.project_dir.empty()) return;

    // Destination = asset browser's current subdirectory (the user
    // dropped onto whatever they were looking at).
    const fs::path assets_root = fs::path(s.project_dir) /
                                  s.assets_root_relative;
    const fs::path dest_dir = s.assets_current_subdir.empty()
        ? assets_root
        : (assets_root / s.assets_current_subdir);

    std::error_code ec;
    fs::create_directories(dest_dir, ec);

    int imported = 0;
    int skipped  = 0;
    for (int i = 0; i < count; ++i) {
        char buf[1024];
        const int n = window->dropped_path_at(window, i, buf, sizeof(buf));
        if (n <= 0) continue;
        const fs::path src(buf);
        if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec)) {
            ++skipped; continue;
        }

        // Recognise the same image extensions the asset registry already
        // treats as Texture kind, plus .jpeg.
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool is_image =
            ext == ".png" || ext == ".jpg"  || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga";
        if (!is_image) { ++skipped; continue; }

        fs::path dst = dest_dir / src.filename();
        // Dedupe by appending _1, _2, ... so an accidental drop of an
        // already-imported file doesn't overwrite the existing asset
        // (and lose its .meta + sliced data).
        if (fs::exists(dst, ec)) {
            const fs::path stem = dst.stem();
            int n2 = 1;
            while (true) {
                fs::path cand = dest_dir /
                    (stem.string() + "_" + std::to_string(n2) + ext);
                if (!fs::exists(cand, ec)) { dst = cand; break; }
                ++n2;
                if (n2 > 256) break;       // give up after 256 dedupes
            }
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { ++skipped; continue; }
        ++imported;
    }

    // Re-scan the registry so the new files become addressable
    // immediately (otherwise the user has to reload the project).
    AssetRegistry::instance().rescan(assets_root.string().c_str());

    char msg[160];
    if (imported > 0 && skipped == 0)
        std::snprintf(msg, sizeof(msg),
            "Imported %d file%s", imported, imported == 1 ? "" : "s");
    else if (imported > 0)
        std::snprintf(msg, sizeof(msg),
            "Imported %d, skipped %d (only image files supported)",
            imported, skipped);
    else
        std::snprintf(msg, sizeof(msg),
            "No images found in drop (%d skipped)", skipped);
    show_toast(s, msg, 3.0f, imported == 0);
}

}  // namespace Engine::editor
