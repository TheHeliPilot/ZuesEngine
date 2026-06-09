// Animation editor modal. Opens on double-click of a .zanim asset
// in the Asset Browser. Edits the asset's frame list (texture + slice
// + per-frame duration), loop flag, FPS, and offers a live preview
// pane. Save persists; Cancel discards.
//
// Frame model (per AnimationFrame in <zues/animation.h>):
//   texture  -- Guid of a Texture asset (.png) the frame samples from
//   slice    -- index into that texture's .meta slice array, -1 = whole
//   duration -- seconds the frame is on screen; 0 falls back to 1/fps
//
// UX shipped here:
//   * three-pane layout: header (FPS / Loop / counts) | preview | frames
//   * preview plays automatically; speed scrubber + play/pause/scrub
//   * frame strip with thumbnails, click to select / scrub to that frame
//   * per-frame editor: TextureRef drag-target + slice combo + duration
//   * drag any .png from Assets onto the frame list to add a new frame

#include "editor.h"

#include <zues/animation.h>
#include <zues/asset.h>
#include <zues/services/renderer_2d.h>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace Engine::editor {

namespace {

// Resolve a texture asset's guid -> renderer texture handle, loading
// the file if it isn't already in the editor's thumb cache. Mirrors
// the lazy-load path used by panel_assets's thumb_for(). Returns 0 on
// failure. Caches failed lookups so we don't spam the log when a
// frame's texture asset is missing on disk -- the editor would
// otherwise re-attempt the load every render frame.
u32 resolve_texture(EditorState& s, Engine::Guid g) {
    if (g.is_null()) return 0;
    auto& reg = AssetRegistry::instance();
    const char* path = reg.path_for(g);
    if (!path) return 0;
    // AssetRegistry stores paths relative to the project's ASSETS
    // ROOT (project_dir / assets_root_relative), not the project dir
    // itself. Resolve through both so we don't end up trying to load
    // ".../MyGame/sprites/foo.png" when the actual file lives at
    // ".../MyGame/assets/sprites/foo.png".
    std::string abs = s.project_dir;
    if (!abs.empty() && abs.back() != '/' && abs.back() != '\\') abs += '/';
    if (!s.assets_root_relative.empty()) {
        abs += s.assets_root_relative;
        if (abs.back() != '/' && abs.back() != '\\') abs += '/';
    }
    abs += path;

    auto it = s.asset_thumb_cache.find(abs);
    if (it != s.asset_thumb_cache.end()) return it->second;

    // Failure cache: per-process set of paths we've already tried and
    // found missing. Keeps the console quiet when an animation frame
    // points at a deleted/moved asset.
    static thread_local std::unordered_set<std::string> failed;
    if (failed.count(abs)) return 0;

    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
    if (!r || !r->load_texture_from_file) return 0;
    const u32 tex = r->load_texture_from_file(r, abs.c_str());
    if (tex) {
        s.asset_thumb_cache[abs] = tex;
    } else {
        failed.insert(abs);
    }
    return tex;
}

// Compute the currently-playing frame index based on accumulated
// preview time + per-frame durations. Wraps when looping; clamps to
// the last frame otherwise.
int frame_at_time(const Engine::AnimationAsset& a, float t) {
    if (a.frames.empty()) return -1;
    const float inv_fps = (a.fps > 0.0f) ? (1.0f / a.fps) : 0.083f;
    float total = 0.0f;
    for (const auto& f : a.frames)
        total += (f.duration > 0.0f ? f.duration : inv_fps);
    if (total <= 0.0f) return 0;
    float u = t;
    if (a.loop) {
        u = std::fmod(t, total);
        if (u < 0.0f) u += total;
    } else if (u >= total) {
        return (int)a.frames.size() - 1;
    }
    float acc = 0.0f;
    for (int i = 0; i < (int)a.frames.size(); ++i) {
        const float d = (a.frames[i].duration > 0.0f
                          ? a.frames[i].duration : inv_fps);
        if (u < acc + d) return i;
        acc += d;
    }
    return (int)a.frames.size() - 1;
}

// Draw a single frame's texture+slice into the given screen rect with
// a checkered background. Used by the live preview pane and the frame
// thumbnails so they look consistent.
void draw_frame_thumb(EditorState& s, ImDrawList* dl,
                       ImVec2 tl, ImVec2 br,
                       const Engine::AnimationFrame& f) {
    const ImU32 dark  = IM_COL32(40, 40, 40, 255);
    const ImU32 light = IM_COL32(60, 60, 60, 255);
    const float box   = 8.0f;
    dl->AddRectFilled(tl, br, dark, 4.0f);
    int rows = (int)((br.y - tl.y) / box) + 1;
    int cols = (int)((br.x - tl.x) / box) + 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if ((r + c) & 1) {
                ImVec2 a{ tl.x + c * box,       tl.y + r * box };
                ImVec2 b{ std::min(a.x + box, br.x),
                          std::min(a.y + box, br.y) };
                dl->AddRectFilled(a, b, light);
            }
        }
    }

    const u32 tex = resolve_texture(s, f.texture);
    if (!tex) return;

    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
    int tw = 0, th = 0;
    if (r && r->get_texture_size) r->get_texture_size(r, tex, &tw, &th);
    if (tw <= 0 || th <= 0) return;

    // Resolve UVs from the slice index, if any.
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    int sw_px = tw, sh_px = th;
    if (f.slice >= 0) {
        const auto sett = AssetRegistry::instance()
            .sprite_settings_for(f.texture);
        if (f.slice < (int)sett.slices.size()) {
            const auto& sl = sett.slices[f.slice];
            u0 = (float)sl.x / (float)tw;
            v0 = (float)sl.y / (float)th;
            u1 = (float)(sl.x + sl.w) / (float)tw;
            v1 = (float)(sl.y + sl.h) / (float)th;
            sw_px = sl.w; sh_px = sl.h;
        }
    }

    // Aspect-fit inside the rect.
    const float rw = br.x - tl.x;
    const float rh = br.y - tl.y;
    const float at = (float)sw_px / (float)sh_px;
    const float ar = rw / rh;
    float dw, dh;
    if (at > ar) { dw = rw;        dh = rw / at; }
    else         { dh = rh;        dw = rh * at; }
    ImVec2 ia{ tl.x + (rw - dw) * 0.5f, tl.y + (rh - dh) * 0.5f };
    ImVec2 ib{ ia.x + dw, ia.y + dh };
    dl->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                 ia, ib, ImVec2(u0, v0), ImVec2(u1, v1));
}

// Local copy of the inspector's asset-ref drag-target widget (same
// payload + same kind-vs-extension check). Inlined here so this
// translation unit doesn't reach into panel_inspector.cpp's anon ns.
void texture_ref_widget(Engine::Guid* g, const char* hidden_id) {
    std::string label;
    if (g->is_null()) {
        label = "<none>  (Texture)";
    } else {
        const char* path = AssetRegistry::instance().path_for(*g);
        label = path ? path
                     : (std::string("<missing> ") + Engine::guid_to_hex(*g));
    }
    ImGui::Button((label + std::string(hidden_id)).c_str(),
                  ImVec2(-FLT_MIN, 0));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
            const char* abs = static_cast<const char*>(payload->Data);
            if (abs) {
                const Guid res =
                    AssetRegistry::instance().guid_for_any_path(abs);
                if (!res.is_null() &&
                    asset_kind_from_extension(abs) == AssetKind::Texture) {
                    *g = res;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Clear")) *g = Guid{};
        ImGui::EndPopup();
    }
}

// Slice combo for a frame. Lets the user pick which named slice of
// the frame's texture to sample, or "(whole texture)".
void draw_frame_slice_combo(EditorState& s, Engine::AnimationFrame& f) {
    const auto sett = AssetRegistry::instance()
        .sprite_settings_for(f.texture);
    const char* preview = (f.slice < 0)
        ? "(whole texture)"
        : (f.slice < (int)sett.slices.size()
            ? sett.slices[f.slice].name.c_str()
            : "(missing slice)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##slice", preview)) {
        if (ImGui::Selectable("(whole texture)", f.slice < 0)) {
            f.slice = -1;
        }
        ImGui::Separator();
        for (int i = 0; i < (int)sett.slices.size(); ++i) {
            const bool sel = (i == f.slice);
            if (ImGui::Selectable(sett.slices[i].name.c_str(), sel)) {
                f.slice = i;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("[%d, %d, %d x %d]",
                                   sett.slices[i].x, sett.slices[i].y,
                                   sett.slices[i].w, sett.slices[i].h);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        if (sett.slices.empty()) {
            ImGui::TextDisabled("Texture has no slices.");
            ImGui::TextDisabled("Double-click the .png in Assets to add some.");
        }
        ImGui::EndCombo();
    }
}

}  // namespace

void open_animation_editor(EditorState& s, const std::string& abs_path) {
    if (abs_path.empty()) return;
    Engine::AnimationAsset a{};
    if (Engine::load_animation(abs_path.c_str(), a) != Engine::Result::Ok) {
        show_toast(s, "Animation editor: failed to load .zanim",
                   3.0f, true);
        return;
    }
    s.anim_editor_open      = true;
    s.anim_editor_path      = abs_path;
    s.anim_editor_working   = std::move(a);
    s.anim_editor_selected  = s.anim_editor_working.frames.empty() ? -1 : 0;
    s.anim_editor_preview_t = 0.0f;
    s.anim_editor_preview_play = true;
    s.anim_editor_preview_speed = 1.0f;
}

void draw_animation_editor(EditorState& s) {
    if (!s.anim_editor_open) return;

    // Normal dockable window (NOT a modal). The previous modal popup
    // blocked input to every other panel, which made it impossible to
    // drag a .png in from the Asset Browser. As a regular Begin()
    // window the editor can dock anywhere, the asset browser stays
    // interactive, and ImGui's drag-drop layer routes payloads across
    // panel boundaries.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.55f,
                                     vp->WorkSize.y * 0.65f),
                              ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                     vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                              ImGuiCond_FirstUseEver,
                              ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Animation Editor", &s.anim_editor_open)) {
        ImGui::End();
        return;
    }

    auto& a  = s.anim_editor_working;
    const float dt = s.last_dt;

    // Advance preview clock.
    if (s.anim_editor_preview_play) {
        s.anim_editor_preview_t += dt * s.anim_editor_preview_speed;
    }

    // ---- Header row: filename + FPS / Loop / frame count -----------
    {
        std::string fname = s.anim_editor_path;
        const auto sl = fname.find_last_of("/\\");
        if (sl != std::string::npos) fname = fname.substr(sl + 1);
        ImGui::Text("%s", fname.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%d frames)", (int)a.frames.size());
    }
    ImGui::Separator();

    // ---- Top toolbar: name / fps / loop / preview controls --------
    // Labels rendered BEFORE inputs (Unity-style), tight spacing.
    auto label_before = [](const char* lbl) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(lbl);
        ImGui::SameLine(0.0f, 4.0f);
    };
    {
        label_before("Name");
        char nm[64] = {};
        std::strncpy(nm, a.name.c_str(), sizeof(nm) - 1);
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("##nm", nm, sizeof(nm))) a.name = nm;

        ImGui::SameLine(0.0f, 12.0f);
        label_before("FPS");
        ImGui::SetNextItemWidth(60);
        ImGui::DragFloat("##fps", &a.fps, 0.1f, 0.1f, 240.0f, "%.1f");

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::Checkbox("Loop", &a.loop);

        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::Button(s.anim_editor_preview_play ? "Pause" : "Play",
                            ImVec2(56, 0))) {
            s.anim_editor_preview_play = !s.anim_editor_preview_play;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("|<", ImVec2(28, 0))) { s.anim_editor_preview_t = 0.0f; }

        ImGui::SameLine(0.0f, 12.0f);
        label_before("Speed");
        ImGui::SetNextItemWidth(70);
        ImGui::DragFloat("##spd", &s.anim_editor_preview_speed,
                          0.05f, 0.0f, 8.0f, "%.2fx");
    }
    ImGui::Separator();

    // ---- Body: preview (left) + frame strip & editor (right) ------
    const float right_w   = 360.0f;
    const float content_h = ImGui::GetContentRegionAvail().y - 40.0f;

    // ----- Preview pane ----------------------------------------------
    ImGui::BeginChild("##anim_preview",
        ImVec2(ImGui::GetContentRegionAvail().x - right_w - 8.0f, content_h),
        true);
    {
        const ImVec2 tl  = ImGui::GetCursorScreenPos();
        ImVec2       avail = ImGui::GetContentRegionAvail();
        // Reserve space for the timeline scrubber underneath. Slim
        // bar -- the user only ever clicks/drags it, so the previous
        // 32px bar was wasting vertical room better given to the
        // preview itself.
        const float  bar_h = 14.0f;
        const ImVec2 frame_tl = tl;
        const ImVec2 frame_br{ tl.x + avail.x, tl.y + avail.y - bar_h - 6.0f };
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const int cur = frame_at_time(a, s.anim_editor_preview_t);
        if (cur >= 0 && cur < (int)a.frames.size()) {
            draw_frame_thumb(s, dl, frame_tl, frame_br, a.frames[cur]);
            // Frame index overlay, top-left.
            char ix[64];
            std::snprintf(ix, sizeof(ix), "Frame %d / %d",
                          cur + 1, (int)a.frames.size());
            dl->AddText(ImVec2(frame_tl.x + 8, frame_tl.y + 6),
                        IM_COL32(255, 255, 255, 220), ix);
        } else {
            // Empty state.
            dl->AddRect(frame_tl, frame_br, IM_COL32(80, 80, 80, 200));
            const char* msg = "No frames yet";
            const ImVec2 ts = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(frame_tl.x + ((frame_br.x - frame_tl.x) - ts.x) * 0.5f,
                                frame_tl.y + ((frame_br.y - frame_tl.y) - ts.y) * 0.5f),
                        IM_COL32(180, 180, 180, 220), msg);
        }

        // Position the cursor below the preview so the timeline bar
        // renders beneath it inside this child.
        ImGui::SetCursorScreenPos(ImVec2(frame_tl.x, frame_br.y + 8.0f));

        // ----- Timeline scrubber ------------------------------------
        // Horizontal bar split into per-frame segments proportional to
        // their durations. Clicking a segment scrubs to that frame.
        if (!a.frames.empty()) {
            const float inv_fps = (a.fps > 0.0f) ? (1.0f / a.fps) : 0.083f;
            float total = 0.0f;
            for (const auto& f : a.frames)
                total += (f.duration > 0.0f ? f.duration : inv_fps);

            const ImVec2 b_tl = ImGui::GetCursorScreenPos();
            const float  bw   = avail.x;
            const ImVec2 b_br{ b_tl.x + bw, b_tl.y + bar_h };
            dl->AddRectFilled(b_tl, b_br, IM_COL32(35, 35, 38, 255), 3.0f);

            float seg_x = 0.0f;
            for (int i = 0; i < (int)a.frames.size(); ++i) {
                const float d = (a.frames[i].duration > 0.0f
                                  ? a.frames[i].duration : inv_fps);
                const float frac = d / total;
                const float w    = bw * frac;
                ImVec2 sa{ b_tl.x + seg_x, b_tl.y };
                ImVec2 sb{ sa.x + w,        b_br.y };
                const bool is_cur = (i == cur);
                ImU32 fill = is_cur
                    ? IM_COL32(255, 165, 60, 200)
                    : ((i == s.anim_editor_selected)
                        ? IM_COL32(80, 130, 180, 200)
                        : IM_COL32(80, 90, 100, 180));
                dl->AddRectFilled(sa, sb, fill);
                if (i + 1 < (int)a.frames.size())
                    dl->AddLine(ImVec2(sb.x, sa.y), ImVec2(sb.x, sb.y),
                                 IM_COL32(0, 0, 0, 200));
                seg_x += w;
            }
            // Clickable invisible button covering the bar area for scrub.
            ImGui::InvisibleButton("##anim_bar", ImVec2(bw, bar_h));
            if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
                const float mx = ImGui::GetMousePos().x - b_tl.x;
                const float u  = std::clamp(mx / bw, 0.0f, 0.9999f);
                s.anim_editor_preview_t = u * total;
                s.anim_editor_preview_play = false;
                s.anim_editor_selected = frame_at_time(a, s.anim_editor_preview_t);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ----- Right column: frame strip + per-frame editor -------------
    ImGui::BeginChild("##anim_right", ImVec2(right_w, content_h), true);
    {
        ImGui::TextDisabled("Frames");
        ImGui::Separator();

        // Drop zone: drag any image asset here to append a new frame.
        const ImVec2 drop_tl = ImGui::GetCursorScreenPos();
        const float  dw      = ImGui::GetContentRegionAvail().x;
        const float  dh      = 32.0f;
        ImDrawList*  dl      = ImGui::GetWindowDrawList();
        dl->AddRect(drop_tl, ImVec2(drop_tl.x + dw, drop_tl.y + dh),
                    IM_COL32(110, 130, 160, 200), 4.0f, 0, 1.5f);
        ImGui::InvisibleButton("##frame_drop", ImVec2(dw, dh));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl =
                    ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                const char* abs = static_cast<const char*>(pl->Data);
                if (abs) {
                    const Guid g = AssetRegistry::instance().guid_for_any_path(abs);
                    if (asset_kind_from_extension(abs) == AssetKind::Texture) {
                        Engine::AnimationFrame nf;
                        nf.texture = g;
                        nf.slice = -1;
                        nf.duration = 0.0f;     // -> use 1/fps
                        a.frames.push_back(nf);
                        s.anim_editor_selected = (int)a.frames.size() - 1;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        const char* drop_hint = "Drop .png  to add a frame";
        const ImVec2 ts = ImGui::CalcTextSize(drop_hint);
        dl->AddText(ImVec2(drop_tl.x + (dw - ts.x) * 0.5f,
                            drop_tl.y + (dh - ts.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    drop_hint);
        ImGui::Spacing();

        // Frame strip: thumbnails in a horizontally-scrollable list.
        const float strip_h = 64.0f;
        ImGui::BeginChild("##frame_strip", ImVec2(0, strip_h + 12), false,
                           ImGuiWindowFlags_HorizontalScrollbar);
        {
            for (int i = 0; i < (int)a.frames.size(); ++i) {
                ImGui::PushID(i);
                if (i > 0) ImGui::SameLine();
                const ImVec2 tl = ImGui::GetCursorScreenPos();
                const ImVec2 br{ tl.x + strip_h, tl.y + strip_h };
                ImDrawList* dlx = ImGui::GetWindowDrawList();
                draw_frame_thumb(s, dlx, tl, br, a.frames[i]);
                // Selection / playback ring.
                const bool is_sel = (i == s.anim_editor_selected);
                const bool is_cur = (i == frame_at_time(a, s.anim_editor_preview_t));
                if (is_sel) dlx->AddRect(tl, br,
                    IM_COL32(80, 130, 180, 255), 4.0f, 0, 2.0f);
                if (is_cur) dlx->AddRect(
                    ImVec2(tl.x - 1, tl.y - 1), ImVec2(br.x + 1, br.y + 1),
                    IM_COL32(255, 165, 60, 255), 4.0f, 0, 2.0f);
                ImGui::InvisibleButton("##fr", ImVec2(strip_h, strip_h));
                if (ImGui::IsItemClicked()) {
                    s.anim_editor_selected = i;
                    // Scrub preview to the start of this frame.
                    const float inv_fps = (a.fps > 0.0f) ? (1.0f / a.fps) : 0.083f;
                    float t = 0.0f;
                    for (int k = 0; k < i; ++k)
                        t += (a.frames[k].duration > 0.0f
                              ? a.frames[k].duration : inv_fps);
                    s.anim_editor_preview_t = t;
                    s.anim_editor_preview_play = false;
                }
                // Index label below the thumb.
                char ix[8];
                std::snprintf(ix, sizeof(ix), "%d", i);
                dlx->AddText(ImVec2(tl.x + 4, br.y + 2),
                              IM_COL32(200, 200, 200, 230), ix);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();

        // Per-frame editor for the selected frame.
        if (s.anim_editor_selected >= 0 &&
            s.anim_editor_selected < (int)a.frames.size()) {
            ImGui::Text("Frame %d", s.anim_editor_selected);
            auto& f = a.frames[s.anim_editor_selected];

            if (ImGui::BeginTable("##fedit", 2,
                    ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("l",
                    ImGuiTableColumnFlags_WidthFixed,  70.0f);
                ImGui::TableSetupColumn("c",
                    ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Texture");
                ImGui::TableNextColumn();
                {
                    // Reuse the standard asset-ref widget so .png drag
                    // from the Asset Browser works identically here.
                    const Guid before = f.texture;
                    texture_ref_widget(&f.texture, "##tex");
                    if (!(before == f.texture)) {
                        // New texture probably has different slices --
                        // reset to whole-texture so we don't keep an
                        // out-of-range slice index.
                        f.slice = -1;
                    }
                }

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Slice");
                ImGui::TableNextColumn();
                draw_frame_slice_combo(s, f);

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Duration");
                ImGui::TableNextColumn();
                {
                    float d = f.duration;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("##dur", &d, 0.005f, 0.0f, 60.0f,
                                           "%.3fs (0 = use 1/FPS)")) {
                        f.duration = std::max(0.0f, d);
                    }
                }
                ImGui::EndTable();
            }

            // Reorder + delete buttons.
            ImGui::Spacing();
            const int cur = s.anim_editor_selected;
            ImGui::BeginDisabled(cur <= 0);
            if (ImGui::SmallButton("<- Move")) {
                std::swap(a.frames[cur], a.frames[cur - 1]);
                s.anim_editor_selected = cur - 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(cur >= (int)a.frames.size() - 1);
            if (ImGui::SmallButton("Move ->")) {
                std::swap(a.frames[cur], a.frames[cur + 1]);
                s.anim_editor_selected = cur + 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Duplicate")) {
                a.frames.insert(a.frames.begin() + cur + 1, a.frames[cur]);
                s.anim_editor_selected = cur + 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                a.frames.erase(a.frames.begin() + cur);
                if (s.anim_editor_selected >= (int)a.frames.size())
                    s.anim_editor_selected = (int)a.frames.size() - 1;
            }
        } else {
            ImGui::TextDisabled("Click a frame above (or drop a .png) to "
                                 "edit it.");
        }
    }
    ImGui::EndChild();

    // ---- Footer: Save / Revert / Close ---------------------------
    // No "Cancel" -- the window is non-modal so the user can leave it
    // open without losing changes. Save persists. Revert reloads from
    // disk to discard edits. Close X (top-right) closes the panel.
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        if (Engine::save_animation(s.anim_editor_path.c_str(),
                                    s.anim_editor_working) != Engine::Result::Ok) {
            show_toast(s, "Animation: write failed", 3.0f, true);
        } else {
            char m[128];
            std::snprintf(m, sizeof(m), "Saved %d frame%s",
                          (int)a.frames.size(),
                          a.frames.size() == 1 ? "" : "s");
            show_toast(s, m, 2.0f, false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(120, 0))) {
        Engine::AnimationAsset reloaded{};
        if (Engine::load_animation(s.anim_editor_path.c_str(),
                                    reloaded) == Engine::Result::Ok) {
            s.anim_editor_working   = std::move(reloaded);
            s.anim_editor_selected  = s.anim_editor_working.frames.empty()
                                        ? -1 : 0;
            s.anim_editor_preview_t = 0.0f;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(close the panel to dismiss)");

    ImGui::End();
}

}  // namespace Engine::editor
