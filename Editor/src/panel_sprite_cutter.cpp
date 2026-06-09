// Sprite cutter modal. Opens on double-click of a texture asset in
// the Asset Browser. Edits the asset's slice list (.meta sidecar's
// "sprite.slices" array) and writes back on Save.
//
// MVP scope:
//   - Auto-grid: type cell W/H + padding, click "Slice" to overwrite
//     the slice list with a regular grid named "<base>_<n>".
//   - Slice list: rename, delete individual slices, click to select.
//   - Live preview of all slice rects on top of the texture.
//   - Save -> persists to .meta. Cancel -> discards working copy.
//
// Future:
//   - Manual rect drag in the preview (drag from empty space to
//     create; corner handles to resize an existing slice).
//   - Per-slice pivot widget (3x3 grid mirroring the asset settings).

#include "editor.h"

#include <zues/asset.h>
#include <zues/services/renderer_2d.h>

// stb_image is also compiled into Renderer_GL but in a different
// translation unit (DLL vs editor exe), so STB_IMAGE_STATIC keeps both
// copies' symbols local and avoids any linker collision.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

namespace Engine::editor {

namespace {

// Generic helper: append a named slice with rect (x,y,w,h) to the
// working list. Centred default pivot.
void push_slice(EditorState& s, const std::string& base_name, int idx,
                 int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    SpriteSlice sl;
    char nm[64];
    std::snprintf(nm, sizeof(nm), "%s_%d", base_name.c_str(), idx);
    sl.name = nm;
    sl.x = x; sl.y = y; sl.w = w; sl.h = h;
    sl.pivot_x = 0.5f; sl.pivot_y = 0.5f;
    s.sprite_cutter_slices.push_back(std::move(sl));
}

// Mode 0: by cell size. Tile until the image runs out, leaving any
// remainder pixels uncovered (so a 100x32 sheet at cellsize 32 gets
// three slices, not three plus a 4px sliver).
void rebuild_by_cell_size(EditorState& s, int tex_w, int tex_h,
                           const std::string& base) {
    s.sprite_cutter_slices.clear();
    if (tex_w <= 0 || tex_h <= 0) return;
    const int cw  = s.sprite_cutter_grid_w;
    const int ch  = s.sprite_cutter_grid_h;
    const int pad = std::max(0, s.sprite_cutter_padding);
    if (cw <= 0 || ch <= 0) return;
    int idx = 0;
    for (int y = 0; y + ch <= tex_h; y += ch + pad) {
        for (int x = 0; x + cw <= tex_w; x += cw + pad) {
            push_slice(s, base, idx++, x, y, cw, ch);
        }
    }
}

// Mode 1: by cell count. Each cell gets exactly tex_w/cols x tex_h/rows
// pixels (rounded down). Last column / row may absorb the remainder so
// no pixels are lost on tex sizes that don't divide evenly.
void rebuild_by_cell_count(EditorState& s, int tex_w, int tex_h,
                            const std::string& base) {
    s.sprite_cutter_slices.clear();
    if (tex_w <= 0 || tex_h <= 0) return;
    const int cols = std::max(1, s.sprite_cutter_cols);
    const int rows = std::max(1, s.sprite_cutter_rows);
    const int cw_b = tex_w / cols;
    const int ch_b = tex_h / rows;
    if (cw_b <= 0 || ch_b <= 0) return;
    int idx = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x = c * cw_b;
            const int y = r * ch_b;
            const int w = (c == cols - 1) ? (tex_w - x) : cw_b;
            const int h = (r == rows - 1) ? (tex_h - y) : ch_b;
            push_slice(s, base, idx++, x, y, w, h);
        }
    }
}

// Mode 2: by alpha. Read the actual texture pixels via stb_image, then
// flood-fill connected non-transparent regions and bound each one with
// a rect. Useful for sprite atlases where artists laid out characters
// freely on a transparent canvas without a strict grid.
//
// Algorithm: 4-connected BFS over pixels whose alpha >= threshold,
// emitting one rect per region. Discards regions whose width or height
// is below `min_size` to filter dust / single-pixel speckle.
void rebuild_by_alpha(EditorState& s,
                       const std::string& abs_path,
                       const std::string& base) {
    s.sprite_cutter_slices.clear();
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load(abs_path.c_str(), &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        show_toast(s, "Alpha-cut: failed to read image", 3.0f, true);
        return;
    }

    const int total = w * h;
    std::vector<unsigned char> visited((size_t)total, 0);
    const int thr = std::max(0, std::min(255, s.sprite_cutter_alpha_threshold));
    const int min_sz = std::max(1, s.sprite_cutter_min_size);

    auto opaque_at = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        const stbi_uc a = pixels[(y * w + x) * 4 + 3];
        return a >= thr;
    };

    int idx = 0;
    std::vector<int> stack;
    stack.reserve(1024);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            if (visited[i]) continue;
            if (!opaque_at(x, y)) { visited[i] = 1; continue; }

            // Flood fill from (x,y), tracking the bounding box.
            int bx0 = x, by0 = y, bx1 = x, by1 = y;
            stack.clear();
            stack.push_back(i);
            visited[i] = 1;
            while (!stack.empty()) {
                const int p = stack.back(); stack.pop_back();
                const int px = p % w;
                const int py = p / w;
                if (px < bx0) bx0 = px;
                if (py < by0) by0 = py;
                if (px > bx1) bx1 = px;
                if (py > by1) by1 = py;
                static const int dx[4] = { 1, -1, 0, 0 };
                static const int dy[4] = { 0,  0, 1, -1 };
                for (int k = 0; k < 4; ++k) {
                    const int nx = px + dx[k];
                    const int ny = py + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const int ni = ny * w + nx;
                    if (visited[ni]) continue;
                    if (!opaque_at(nx, ny)) { visited[ni] = 1; continue; }
                    visited[ni] = 1;
                    stack.push_back(ni);
                }
            }

            const int rw = bx1 - bx0 + 1;
            const int rh = by1 - by0 + 1;
            if (rw >= min_sz && rh >= min_sz) {
                push_slice(s, base, idx++, bx0, by0, rw, rh);
            }
        }
    }

    stbi_image_free(pixels);
}

// Map a texture-space pixel rect into the modal's preview-pane screen
// coords. The preview keeps the texture's aspect, so we re-derive the
// fit for every slice draw -- cheap.
struct PreviewFit {
    ImVec2 origin;       // top-left of the drawn texture image
    float  scale = 1.0f; // tex pixels -> screen pixels
};

PreviewFit fit_preview(ImVec2 area_tl, ImVec2 area_br, int tex_w, int tex_h) {
    PreviewFit out;
    if (tex_w <= 0 || tex_h <= 0) {
        out.origin = area_tl;
        return out;
    }
    const float aw = area_br.x - area_tl.x;
    const float ah = area_br.y - area_tl.y;
    const float at = (float)tex_w / (float)tex_h;
    const float ar = aw / ah;
    float draw_w, draw_h;
    if (at > ar) { draw_w = aw; draw_h = aw / at; }
    else         { draw_h = ah; draw_w = ah * at; }
    out.origin = ImVec2(area_tl.x + (aw - draw_w) * 0.5f,
                         area_tl.y + (ah - draw_h) * 0.5f);
    out.scale  = draw_w / (float)tex_w;
    return out;
}

}  // namespace

void open_sprite_cutter(EditorState& s, const std::string& abs_path) {
    if (abs_path.empty()) return;
    const auto& reg = AssetRegistry::instance();
    const Engine::Guid g = reg.guid_for_any_path(abs_path.c_str());
    if (g.is_null()) return;

    s.sprite_cutter_open = true;
    s.sprite_cutter_guid = g;
    s.sprite_cutter_path = abs_path;

    // Seed the working slice list from the existing meta. User can
    // either keep editing or replace via the auto-grid button.
    s.sprite_cutter_settings = reg.sprite_settings_for(g);
    s.sprite_cutter_slices = s.sprite_cutter_settings.slices;
    s.sprite_cutter_selected = -1;
}

// Push the working slice list to every live Sprite component whose
// slice rect matches one of these slices. Called every frame the
// cutter is open so dragging the 9-slice handles updates the scene
// live (no Save click required for preview). The asset's .meta is
// only persisted on Save -- this is purely an in-memory mirror of the
// editor's working copy into the live ECS.
//
// Cheap: one iterate_query over Sprite components, plus a tiny linear
// scan against the slice list per matching sprite.
static void broadcast_slices_to_sprites(EditorState& s,
                                         const std::vector<SpriteSlice>& slices,
                                         float texture_ppu) {
    if (!s.world) return;
    const auto sprite_id = s.world->find_component_id("Sprite");
    if (!sprite_id) return;
    const auto* desc = s.world->get_component_type(sprite_id);
    if (!desc) return;

    const ecs::FieldInfo* fx=nullptr;  const ecs::FieldInfo* fy=nullptr;
    const ecs::FieldInfo* fw=nullptr;  const ecs::FieldInfo* fh=nullptr;
    const ecs::FieldInfo* fbl=nullptr; const ecs::FieldInfo* fbr=nullptr;
    const ecs::FieldInfo* fbt=nullptr; const ecs::FieldInfo* fbb=nullptr;
    const ecs::FieldInfo* fsm=nullptr; const ecs::FieldInfo* fcm=nullptr;
    const ecs::FieldInfo* fpu=nullptr;
    for (u32 i = 0; i < desc->field_count; ++i) {
        const auto& f = desc->fields[i];
        if (!f.name) continue;
        if      (std::strcmp(f.name, "slice_x")     == 0) fx  = &f;
        else if (std::strcmp(f.name, "slice_y")     == 0) fy  = &f;
        else if (std::strcmp(f.name, "slice_w")     == 0) fw  = &f;
        else if (std::strcmp(f.name, "slice_h")     == 0) fh  = &f;
        else if (std::strcmp(f.name, "border_l")    == 0) fbl = &f;
        else if (std::strcmp(f.name, "border_r")    == 0) fbr = &f;
        else if (std::strcmp(f.name, "border_t")    == 0) fbt = &f;
        else if (std::strcmp(f.name, "border_b")    == 0) fbb = &f;
        else if (std::strcmp(f.name, "scale_mode")  == 0) fsm = &f;
        else if (std::strcmp(f.name, "center_mode") == 0) fcm = &f;
        else if (std::strcmp(f.name, "texture_ppu") == 0) fpu = &f;
    }
    if (!fx || !fy || !fw || !fh || !fbl || !fbr || !fbt || !fbb || !fsm) {
        // One-shot warning when the new fields aren't visible to
        // reflection -- means this binary still has the pre-9-slice
        // Sprite layout. Useful for catching ABI drift between Core
        // and the project DLL.
        static bool warned = false;
        if (!warned) {
            warned = true;
            ZUES_LOG_WARN("9slice broadcast: Sprite missing border_*/scale_mode "
                          "fields in reflection -- rebuild the project DLL");
        }
        return;
    }

    const ecs::ComponentId req[1] = { sprite_id };
    struct Closure {
        const std::vector<SpriteSlice>* slices;
        const ecs::FieldInfo* fx;  const ecs::FieldInfo* fy;
        const ecs::FieldInfo* fw;  const ecs::FieldInfo* fh;
        const ecs::FieldInfo* fbl; const ecs::FieldInfo* fbr;
        const ecs::FieldInfo* fbt; const ecs::FieldInfo* fbb;
        const ecs::FieldInfo* fsm; const ecs::FieldInfo* fcm;
        const ecs::FieldInfo* fpu;
        float ppu;
        int matched;
    } cl{ &slices, fx, fy, fw, fh, fbl, fbr, fbt, fbb, fsm, fcm, fpu,
          (texture_ppu > 0.0f ? texture_ppu : 100.0f), 0 };

    s.world->iterate_query(req, 1, nullptr, 0,
        +[](void* u, ecs::Entity, void** cols, u32) {
            auto* C = static_cast<Closure*>(u);
            u8* d = static_cast<u8*>(cols[0]);
            const i32 sx = *reinterpret_cast<i32*>(d + C->fx->offset);
            const i32 sy = *reinterpret_cast<i32*>(d + C->fy->offset);
            const i32 sw = *reinterpret_cast<i32*>(d + C->fw->offset);
            const i32 sh = *reinterpret_cast<i32*>(d + C->fh->offset);
            if (sw <= 0 || sh <= 0) return;
            for (const auto& sl : *C->slices) {
                if (sl.x==sx && sl.y==sy &&
                    sl.w==sw && sl.h==sh) {
                    *reinterpret_cast<i32*>(d + C->fbl->offset) = sl.border_l;
                    *reinterpret_cast<i32*>(d + C->fbr->offset) = sl.border_r;
                    *reinterpret_cast<i32*>(d + C->fbt->offset) = sl.border_t;
                    *reinterpret_cast<i32*>(d + C->fbb->offset) = sl.border_b;
                    *reinterpret_cast<i32*>(d + C->fsm->offset) = (i32)sl.scale_mode;
                    if (C->fcm)
                        *reinterpret_cast<i32*>(d + C->fcm->offset) = (i32)sl.center_mode;
                    if (C->fpu)
                        *reinterpret_cast<float*>(d + C->fpu->offset) = C->ppu;
                    C->matched++;
                    break;
                }
            }
        }, &cl);

    // Diagnostic: log a one-liner whenever the working border values
    // for any slice change. Lets us see, in the console, exactly what
    // values the cutter is mirroring into the live ECS as the user
    // drags the green handles. One line per real edit (per slice).
    static thread_local std::vector<std::pair<int, std::array<i32,5>>> last_seen;
    for (int i = 0; i < (int)slices.size(); ++i) {
        const auto& sl = slices[i];
        std::array<i32,5> v{ sl.border_l, sl.border_r,
                              sl.border_t, sl.border_b,
                              (i32)sl.scale_mode };
        bool changed = true;
        for (auto& e : last_seen) {
            if (e.first == i) {
                if (e.second == v) changed = false;
                else               e.second = v;
                goto checked;
            }
        }
        last_seen.push_back({i, v});
    checked:
        if (changed && (sl.border_l|sl.border_r|sl.border_t|sl.border_b)) {
            char m[160];
            std::snprintf(m, sizeof(m),
                          "9slice broadcast: slice[%d] '%s' borders="
                          "[L%d R%d T%d B%d] mode=%d (matched=%d)",
                          i, sl.name.c_str(),
                          sl.border_l, sl.border_r,
                          sl.border_t, sl.border_b,
                          (int)sl.scale_mode, cl.matched);
            ZUES_LOG_INFO(m);
        }
    }
}

void draw_sprite_cutter(EditorState& s) {
    if (!s.sprite_cutter_open) return;

    auto& reg = AssetRegistry::instance();
    const AssetEntry* entry = reg.find(s.sprite_cutter_guid);
    if (!entry || entry->kind != AssetKind::Texture) {
        s.sprite_cutter_open = false;
        return;
    }

    // Open the popup once on transition; ImGui's BeginPopupModal handles
    // the rest until we close.
    if (!ImGui::IsPopupOpen("Sprite Cutter")) {
        ImGui::OpenPopup("Sprite Cutter");
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.8f,
                                     vp->WorkSize.y * 0.8f),
                              ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                     vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                              ImGuiCond_Appearing,
                              ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Sprite Cutter", &s.sprite_cutter_open,
                                  ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    // Live-preview broadcast: every cutter frame, push the working
    // slice list (including borders + scale_mode) to any matching
    // Sprite components in the scene. Lets the user drag a 9-slice
    // edge handle and immediately see the corresponding sprite render
    // 9-sliced, before they Save the .meta. The Save button still
    // persists to disk.
    broadcast_slices_to_sprites(s, s.sprite_cutter_slices,
                                 s.sprite_cutter_settings.pixels_per_unit);

    // ---- Resolve texture handle + size from the editor's thumb cache ----
    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
    u32 tex = 0;
    int tex_w = 0, tex_h = 0;
    if (r) {
        auto it = s.asset_thumb_cache.find(s.sprite_cutter_path);
        if (it != s.asset_thumb_cache.end()) tex = it->second;
        else if (r->load_texture_from_file) {
            tex = r->load_texture_from_file(r, s.sprite_cutter_path.c_str());
            if (tex) s.asset_thumb_cache[s.sprite_cutter_path] = tex;
        }
        if (tex && r->get_texture_size) r->get_texture_size(r, tex, &tex_w, &tex_h);
    }

    // Header: filename, dimensions.
    {
        const std::string& p = entry->path;
        std::string filename = p;
        const auto slash = p.find_last_of('/');
        if (slash != std::string::npos) filename = p.substr(slash + 1);
        ImGui::Text("%s", filename.c_str());
        ImGui::SameLine();
        if (tex_w > 0 && tex_h > 0)
            ImGui::TextDisabled("(%d x %d px)", tex_w, tex_h);
    }
    ImGui::Separator();

    // ---- Two-column layout: preview (left) + slice list (right) -------
    const float right_w = 280.0f;
    const float content_h = ImGui::GetContentRegionAvail().y - 40.0f;  // leave room for footer

    ImGui::BeginChild("##cutter_left",
        ImVec2(ImGui::GetContentRegionAvail().x - right_w - 8.0f, content_h),
        true);
    {
        // ---- Auto cut: mode picker + per-mode inputs + Slice button ----
        // Three modes:
        //   0 -- by cell size (cell W + H + padding)
        //   1 -- by cell count (cols + rows; cell size derived)
        //   2 -- by alpha (find each opaque region's bbox)
        // "Single" creates one slice covering the entire image -- the
        // Unity-style "this image is one whole sprite" mode. The other
        // three carve into multiple slices using different strategies.
        const char* mode_names[] = {
            "Single (whole image)", "By cell size", "By cell count", "By alpha"
        };
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("##cutmode", &s.sprite_cutter_auto_mode,
                     mode_names, IM_ARRAYSIZE(mode_names));
        ImGui::SameLine();
        const bool slice_clicked = ImGui::Button("Slice");
        ImGui::SameLine();
        if (ImGui::Button("Clear all")) {
            s.sprite_cutter_slices.clear();
            s.sprite_cutter_selected = -1;
        }

        // Mode-specific inputs underneath.
        switch (s.sprite_cutter_auto_mode) {
            case 0: {
                // Single -- no inputs; just an explanation.
                ImGui::TextDisabled("(one slice covering the entire image)");
                break;
            }
            case 1: {
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("W##gw", &s.sprite_cutter_grid_w, 1, 1, 8192);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("H##gh", &s.sprite_cutter_grid_h, 1, 1, 8192);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("Pad##gp", &s.sprite_cutter_padding, 1, 0, 256);
                ImGui::SameLine();
                ImGui::TextDisabled("(pixel size of each cell)");
                break;
            }
            case 2: {
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("Cols##gc", &s.sprite_cutter_cols, 1, 1, 256);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("Rows##gr", &s.sprite_cutter_rows, 1, 1, 256);
                ImGui::SameLine();
                if (tex_w > 0 && tex_h > 0) {
                    int cw = tex_w / std::max(1, s.sprite_cutter_cols);
                    int ch = tex_h / std::max(1, s.sprite_cutter_rows);
                    ImGui::TextDisabled("(=> %d x %d px per cell)", cw, ch);
                } else {
                    ImGui::TextDisabled("(grid count)");
                }
                break;
            }
            case 3: {
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("Alpha >=##gat", &s.sprite_cutter_alpha_threshold,
                                1, 0, 255);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::DragInt("Min size##gms", &s.sprite_cutter_min_size,
                                1, 1, 4096);
                ImGui::SameLine();
                ImGui::TextDisabled("(opaque-region detect)");
                break;
            }
        }

        if (slice_clicked) {
            // Derive a base name from the file stem so slices read
            // naturally ("hero_0", "hero_1", ...).
            std::string base = entry->path;
            const auto slash = base.find_last_of('/');
            if (slash != std::string::npos) base = base.substr(slash + 1);
            const auto dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            switch (s.sprite_cutter_auto_mode) {
                case 0: {
                    // Single -- one slice spanning the full texture.
                    s.sprite_cutter_slices.clear();
                    if (tex_w > 0 && tex_h > 0) {
                        push_slice(s, base, 0, 0, 0, tex_w, tex_h);
                    }
                    break;
                }
                case 1: rebuild_by_cell_size (s, tex_w, tex_h, base); break;
                case 2: rebuild_by_cell_count(s, tex_w, tex_h, base); break;
                case 3: rebuild_by_alpha     (s, s.sprite_cutter_path, base); break;
            }
            s.sprite_cutter_selected = -1;
        }
        ImGui::Separator();

        // Preview area. We use an InvisibleButton to capture mouse
        // events; the visual is drawn manually on top via the window's
        // draw list. This is the standard ImGui pattern for "click +
        // drag in a custom canvas."
        ImVec2 area_tl = ImGui::GetCursorScreenPos();
        ImVec2 avail   = ImGui::GetContentRegionAvail();
        if (avail.x < 16) avail.x = 16;
        if (avail.y < 16) avail.y = 16;
        ImVec2 area_br = ImVec2(area_tl.x + avail.x, area_tl.y + avail.y);

        ImGui::InvisibleButton("##cutter_canvas", avail,
            ImGuiButtonFlags_MouseButtonLeft);
        const bool canvas_hovered = ImGui::IsItemHovered();
        const bool canvas_active  = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Checker bg.
        const ImU32 dark  = IM_COL32(40, 40, 40, 255);
        const ImU32 light = IM_COL32(60, 60, 60, 255);
        const float box   = 12.0f;
        dl->AddRectFilled(area_tl, area_br, dark);
        for (float y = area_tl.y; y < area_br.y; y += box) {
            for (float x = area_tl.x; x < area_br.x; x += box) {
                int rr = (int)((y - area_tl.y) / box);
                int cc = (int)((x - area_tl.x) / box);
                if ((rr + cc) & 1) {
                    ImVec2 a{ x, y };
                    ImVec2 b{ std::min(x + box, area_br.x),
                              std::min(y + box, area_br.y) };
                    dl->AddRectFilled(a, b, light);
                }
            }
        }

        if (tex && tex_w > 0 && tex_h > 0) {
            const PreviewFit fit = fit_preview(area_tl, area_br, tex_w, tex_h);
            const ImVec2 img_tl = fit.origin;
            const ImVec2 img_br = ImVec2(img_tl.x + tex_w * fit.scale,
                                          img_tl.y + tex_h * fit.scale);
            dl->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                         img_tl, img_br);
            dl->AddRect(img_tl, img_br, IM_COL32(0, 0, 0, 120));

            // ---- Mouse input mapping ------------------------------------
            const ImVec2 mp = ImGui::GetMousePos();
            // Convert screen pixel to texture pixel (clamped).
            auto to_tex = [&](float sx, float sy, int& tx, int& ty) {
                tx = (int)std::round((sx - img_tl.x) / fit.scale);
                ty = (int)std::round((sy - img_tl.y) / fit.scale);
                if (tx < 0) tx = 0; if (tx > tex_w) tx = tex_w;
                if (ty < 0) ty = 0; if (ty > tex_h) ty = tex_h;
            };

            // Hit-test slices. Overlapping-slice rules:
            //   1. The currently-selected slice always wins if it (or
            //      one of its corners) is under the cursor -- so the
            //      user can keep moving / resizing the slice they were
            //      working on, even when another slice covers it.
            //   2. Otherwise pick the topmost slice (last added wins).
            //   3. Alt+click cycles through stacked slices: each
            //      Alt-down click picks the next slice underneath the
            //      currently-selected one at this cursor position.
            const float hsz = 6.0f;     // corner handle radius in screen px
            auto hit_test_slice = [&](int idx, int& out_corner) -> bool {
                if (idx < 0 || idx >= (int)s.sprite_cutter_slices.size())
                    return false;
                const auto& sl = s.sprite_cutter_slices[idx];
                ImVec2 a{ img_tl.x + sl.x * fit.scale,
                          img_tl.y + sl.y * fit.scale };
                ImVec2 b{ a.x + sl.w * fit.scale,
                          a.y + sl.h * fit.scale };
                ImVec2 corners[4] = { a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y) };
                for (int c = 0; c < 4; ++c) {
                    if (mp.x >= corners[c].x - hsz &&
                        mp.x <= corners[c].x + hsz &&
                        mp.y >= corners[c].y - hsz &&
                        mp.y <= corners[c].y + hsz) {
                        out_corner = c;
                        return true;
                    }
                }
                if (mp.x >= a.x && mp.x <= b.x &&
                    mp.y >= a.y && mp.y <= b.y) {
                    out_corner = -1;
                    return true;
                }
                return false;
            };

            int hit_slice  = -1;
            int hit_corner = -1;
            // 9-slice edge-handle hit. -1 = none, 0=L, 1=R, 2=T, 3=B.
            // Tested only for the SELECTED slice (handles are only
            // visible on selected). Takes priority over corner / move
            // tests so the inner guides remain grabbable even when they
            // sit on top of a movable rect interior.
            int hit_edge   = -1;
            const ImGuiIO& io = ImGui::GetIO();

            // Edge-handle hit test for the selected slice. The four
            // handles sit at the midpoints of the inner border guides.
            // When all borders are zero (no 9-slice yet) the four
            // handles still appear at the slice's edge midpoints so the
            // user can drag them inward to start defining 9-slice
            // borders.
            if (s.sprite_cutter_selected >= 0 &&
                s.sprite_cutter_selected < (int)s.sprite_cutter_slices.size()) {
                const auto& sl = s.sprite_cutter_slices[s.sprite_cutter_selected];
                ImVec2 a{ img_tl.x + sl.x * fit.scale,
                          img_tl.y + sl.y * fit.scale };
                ImVec2 b{ a.x + sl.w * fit.scale,
                          a.y + sl.h * fit.scale };
                // Border guide screen-x/y positions. Clamp to slice
                // edges when borders are zero so handles sit ON the
                // outer rect (drag inward to introduce borders).
                const float gl_x = a.x + sl.border_l * fit.scale;
                const float gr_x = b.x - sl.border_r * fit.scale;
                const float gt_y = a.y + sl.border_t * fit.scale;
                const float gb_y = b.y - sl.border_b * fit.scale;
                const float midy = (gt_y + gb_y) * 0.5f;
                const float midx = (gl_x + gr_x) * 0.5f;
                struct EH { float hx, hy; int id; };
                EH eh[4] = {
                    { gl_x, midy, 0 },   // L
                    { gr_x, midy, 1 },   // R
                    { midx, gt_y, 2 },   // T
                    { midx, gb_y, 3 },   // B
                };
                for (int k = 0; k < 4; ++k) {
                    if (mp.x >= eh[k].hx - hsz && mp.x <= eh[k].hx + hsz &&
                        mp.y >= eh[k].hy - hsz && mp.y <= eh[k].hy + hsz) {
                        hit_edge = eh[k].id;
                        break;
                    }
                }
            }

            // Step 1: prefer the currently-selected slice.
            if (s.sprite_cutter_selected >= 0 && !io.KeyAlt) {
                int c;
                if (hit_test_slice(s.sprite_cutter_selected, c)) {
                    hit_slice = s.sprite_cutter_selected;
                    hit_corner = c;
                }
            }
            // Step 2: if Alt is held, cycle through slices under the
            // cursor starting AFTER the current selection. Falls
            // through to the topmost-wins behaviour when no Alt and
            // no current-selection hit.
            if (hit_slice < 0) {
                const int total = (int)s.sprite_cutter_slices.size();
                if (io.KeyAlt && total > 1) {
                    // Alt+click cycles. Start one below the current
                    // selection in z-order (which goes back-to-front in
                    // index order). We scan back-to-front but skip the
                    // selected slice on the FIRST hit, taking the next.
                    bool skip_one = (s.sprite_cutter_selected >= 0);
                    for (int pass = 0; pass < 2 && hit_slice < 0; ++pass) {
                        for (int i = total - 1; i >= 0; --i) {
                            int c;
                            if (!hit_test_slice(i, c)) continue;
                            if (skip_one && i == s.sprite_cutter_selected) {
                                skip_one = false;
                                continue;
                            }
                            hit_slice = i; hit_corner = c; break;
                        }
                        skip_one = false;     // pass 2 doesn't skip
                    }
                } else {
                    for (int i = total - 1; i >= 0; --i) {
                        int c;
                        if (hit_test_slice(i, c)) {
                            hit_slice = i; hit_corner = c; break;
                        }
                    }
                }
            }

            // Cursor hint while idle.
            if (s.sprite_cutter_drag_mode == 0 && canvas_hovered) {
                if (hit_edge >= 0) {
                    // L/R = horizontal resize; T/B = vertical.
                    ImGui::SetMouseCursor(
                        (hit_edge == 0 || hit_edge == 1)
                            ? ImGuiMouseCursor_ResizeEW
                            : ImGuiMouseCursor_ResizeNS);
                } else if (hit_corner >= 0) {
                    // Diagonal resize cursors.
                    ImGui::SetMouseCursor(
                        (hit_corner == 0 || hit_corner == 2)
                            ? ImGuiMouseCursor_ResizeNWSE
                            : ImGuiMouseCursor_ResizeNESW);
                } else if (hit_slice >= 0) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                } else if (mp.x >= img_tl.x && mp.x <= img_br.x &&
                           mp.y >= img_tl.y && mp.y <= img_br.y) {
                    // Inside the image but not over any slice -- the
                    // cursor would be invisible under ImGuiMouseCursor_None;
                    // a regular Arrow reads as "you can drag to create
                    // a new rect here" without removing the cursor.
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                }
            }

            // Mouse-down on the canvas starts a drag.
            if (canvas_active && s.sprite_cutter_drag_mode == 0 &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int tx, ty; to_tex(mp.x, mp.y, tx, ty);
                if (hit_edge >= 0 && s.sprite_cutter_selected >= 0) {
                    // Edge-handle drag for the 9-slice border guides.
                    // Modes 7..10 = L,R,T,B.
                    s.sprite_cutter_drag_mode   = 7 + hit_edge;
                    s.sprite_cutter_drag_target = s.sprite_cutter_selected;
                    const auto& sl = s.sprite_cutter_slices[s.sprite_cutter_selected];
                    // Cache the slice rect so the drag math doesn't
                    // ratchet when the user wiggles the handle.
                    s.sprite_cutter_drag_ox = sl.x;
                    s.sprite_cutter_drag_oy = sl.y;
                    s.sprite_cutter_drag_ow = sl.w;
                    s.sprite_cutter_drag_oh = sl.h;
                } else if (hit_corner >= 0) {
                    s.sprite_cutter_drag_mode = 3 + hit_corner;
                    s.sprite_cutter_drag_target = hit_slice;
                    s.sprite_cutter_selected    = hit_slice;
                    const auto& sl = s.sprite_cutter_slices[hit_slice];
                    s.sprite_cutter_drag_ox = sl.x;
                    s.sprite_cutter_drag_oy = sl.y;
                    s.sprite_cutter_drag_ow = sl.w;
                    s.sprite_cutter_drag_oh = sl.h;
                } else if (hit_slice >= 0) {
                    s.sprite_cutter_drag_mode = 2;
                    s.sprite_cutter_drag_target = hit_slice;
                    s.sprite_cutter_selected    = hit_slice;
                    s.sprite_cutter_drag_ax = tx;
                    s.sprite_cutter_drag_ay = ty;
                    const auto& sl = s.sprite_cutter_slices[hit_slice];
                    s.sprite_cutter_drag_ox = sl.x;
                    s.sprite_cutter_drag_oy = sl.y;
                } else if (mp.x >= img_tl.x && mp.x <= img_br.x &&
                           mp.y >= img_tl.y && mp.y <= img_br.y) {
                    // Empty space inside the image -> create.
                    s.sprite_cutter_drag_mode = 1;
                    s.sprite_cutter_drag_target = -1;
                    s.sprite_cutter_drag_ax = tx;
                    s.sprite_cutter_drag_ay = ty;
                }
            }

            // Drag in progress.
            if (s.sprite_cutter_drag_mode != 0 && canvas_active) {
                int tx, ty; to_tex(mp.x, mp.y, tx, ty);
                if (s.sprite_cutter_drag_mode == 1) {
                    // Live preview: draw the rect being created.
                    int x0 = std::min(s.sprite_cutter_drag_ax, tx);
                    int y0 = std::min(s.sprite_cutter_drag_ay, ty);
                    int x1 = std::max(s.sprite_cutter_drag_ax, tx);
                    int y1 = std::max(s.sprite_cutter_drag_ay, ty);
                    ImVec2 a{ img_tl.x + x0 * fit.scale,
                              img_tl.y + y0 * fit.scale };
                    ImVec2 b{ img_tl.x + x1 * fit.scale,
                              img_tl.y + y1 * fit.scale };
                    dl->AddRectFilled(a, b, IM_COL32(255, 165, 60, 50));
                    dl->AddRect(a, b, IM_COL32(255, 165, 60, 255), 0, 0, 2.0f);
                } else if (s.sprite_cutter_drag_mode == 2 &&
                           s.sprite_cutter_drag_target >= 0 &&
                           s.sprite_cutter_drag_target <
                               (int)s.sprite_cutter_slices.size()) {
                    auto& sl = s.sprite_cutter_slices[s.sprite_cutter_drag_target];
                    int dx = tx - s.sprite_cutter_drag_ax;
                    int dy = ty - s.sprite_cutter_drag_ay;
                    sl.x = std::max(0, std::min(tex_w - sl.w,
                                  s.sprite_cutter_drag_ox + dx));
                    sl.y = std::max(0, std::min(tex_h - sl.h,
                                  s.sprite_cutter_drag_oy + dy));
                } else if (s.sprite_cutter_drag_mode >= 3 &&
                           s.sprite_cutter_drag_mode <= 6 &&
                           s.sprite_cutter_drag_target >= 0 &&
                           s.sprite_cutter_drag_target <
                               (int)s.sprite_cutter_slices.size()) {
                    auto& sl = s.sprite_cutter_slices[s.sprite_cutter_drag_target];
                    int x0 = s.sprite_cutter_drag_ox;
                    int y0 = s.sprite_cutter_drag_oy;
                    int x1 = x0 + s.sprite_cutter_drag_ow;
                    int y1 = y0 + s.sprite_cutter_drag_oh;
                    // Move only the dragged corner; clamp to valid order.
                    switch (s.sprite_cutter_drag_mode - 3) {
                        case 0: x0 = tx; y0 = ty; break;   // TL
                        case 1: x1 = tx; y0 = ty; break;   // TR
                        case 2: x1 = tx; y1 = ty; break;   // BR
                        case 3: x0 = tx; y1 = ty; break;   // BL
                    }
                    if (x0 > x1) std::swap(x0, x1);
                    if (y0 > y1) std::swap(y0, y1);
                    sl.x = x0; sl.y = y0;
                    sl.w = std::max(1, x1 - x0);
                    sl.h = std::max(1, y1 - y0);
                } else if (s.sprite_cutter_drag_mode >= 7 &&
                           s.sprite_cutter_drag_mode <= 10 &&
                           s.sprite_cutter_drag_target >= 0 &&
                           s.sprite_cutter_drag_target <
                               (int)s.sprite_cutter_slices.size()) {
                    // 9-slice border guide drag. Convert cursor to the
                    // border value (offset from the relevant edge of the
                    // slice rect). Clamp so the two opposite borders
                    // can't cross each other (leave at least 1px of
                    // center region).
                    auto& sl = s.sprite_cutter_slices[s.sprite_cutter_drag_target];
                    switch (s.sprite_cutter_drag_mode - 7) {
                        case 0: {  // L
                            int v = tx - sl.x;
                            if (v < 0) v = 0;
                            if (v > sl.w - sl.border_r - 1)
                                v = std::max(0, sl.w - sl.border_r - 1);
                            sl.border_l = v;
                            break;
                        }
                        case 1: {  // R
                            int v = (sl.x + sl.w) - tx;
                            if (v < 0) v = 0;
                            if (v > sl.w - sl.border_l - 1)
                                v = std::max(0, sl.w - sl.border_l - 1);
                            sl.border_r = v;
                            break;
                        }
                        case 2: {  // T
                            int v = ty - sl.y;
                            if (v < 0) v = 0;
                            if (v > sl.h - sl.border_b - 1)
                                v = std::max(0, sl.h - sl.border_b - 1);
                            sl.border_t = v;
                            break;
                        }
                        case 3: {  // B
                            int v = (sl.y + sl.h) - ty;
                            if (v < 0) v = 0;
                            if (v > sl.h - sl.border_t - 1)
                                v = std::max(0, sl.h - sl.border_t - 1);
                            sl.border_b = v;
                            break;
                        }
                    }
                }
            }

            // Drag end -> commit.
            if (s.sprite_cutter_drag_mode != 0 &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (s.sprite_cutter_drag_mode == 1) {
                    // Create: only if the rect has nonzero area.
                    int tx, ty; to_tex(mp.x, mp.y, tx, ty);
                    int x0 = std::min(s.sprite_cutter_drag_ax, tx);
                    int y0 = std::min(s.sprite_cutter_drag_ay, ty);
                    int x1 = std::max(s.sprite_cutter_drag_ax, tx);
                    int y1 = std::max(s.sprite_cutter_drag_ay, ty);
                    if (x1 - x0 >= 2 && y1 - y0 >= 2) {
                        SpriteSlice nsl;
                        char nm[64];
                        std::snprintf(nm, sizeof(nm), "slice_%d",
                                      (int)s.sprite_cutter_slices.size());
                        nsl.name = nm;
                        nsl.x = x0; nsl.y = y0;
                        nsl.w = x1 - x0;
                        nsl.h = y1 - y0;
                        nsl.pivot_x = 0.5f;
                        nsl.pivot_y = 0.5f;
                        s.sprite_cutter_slices.push_back(std::move(nsl));
                        s.sprite_cutter_selected =
                            (int)s.sprite_cutter_slices.size() - 1;
                    }
                }
                s.sprite_cutter_drag_mode = 0;
                s.sprite_cutter_drag_target = -1;
            }

            // Persisted slice rects.
            for (size_t i = 0; i < s.sprite_cutter_slices.size(); ++i) {
                const auto& sl = s.sprite_cutter_slices[i];
                ImVec2 a{ img_tl.x + sl.x * fit.scale,
                          img_tl.y + sl.y * fit.scale };
                ImVec2 b{ a.x + sl.w * fit.scale,
                          a.y + sl.h * fit.scale };
                const bool sel = ((int)i == s.sprite_cutter_selected);
                ImU32 outline = sel
                    ? IM_COL32(255, 165, 60, 255)
                    : IM_COL32(80, 200, 255, 200);
                ImU32 fill = sel
                    ? IM_COL32(255, 165, 60, 50)
                    : IM_COL32(80, 200, 255, 30);
                dl->AddRectFilled(a, b, fill);
                dl->AddRect(a, b, outline, 0.0f, 0, sel ? 2.0f : 1.0f);
                // Corner handles for the selected slice.
                if (sel) {
                    ImVec2 corners[4] = { a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y) };
                    for (int c = 0; c < 4; ++c) {
                        ImVec2 ha{ corners[c].x - hsz, corners[c].y - hsz };
                        ImVec2 hb{ corners[c].x + hsz, corners[c].y + hsz };
                        dl->AddRectFilled(ha, hb, IM_COL32(255, 165, 60, 230));
                        dl->AddRect(ha, hb, IM_COL32(0, 0, 0, 200));
                    }

                    // 9-slice inner border guides + mid-edge handles.
                    // Drawn even when borders are zero so the user can
                    // grab a handle (sitting on the outer edge) and
                    // drag inward to start defining a 9-slice. The
                    // guide lines themselves only render when their
                    // border is non-zero so the slice doesn't look
                    // permanently subdivided.
                    const float gl_x = a.x + sl.border_l * fit.scale;
                    const float gr_x = b.x - sl.border_r * fit.scale;
                    const float gt_y = a.y + sl.border_t * fit.scale;
                    const float gb_y = b.y - sl.border_b * fit.scale;
                    const ImU32 guide_col  = IM_COL32(120, 220, 120, 220);
                    const ImU32 handle_col = IM_COL32(120, 220, 120, 230);
                    if (sl.border_l > 0)
                        dl->AddLine(ImVec2(gl_x, a.y), ImVec2(gl_x, b.y),
                                     guide_col, 1.0f);
                    if (sl.border_r > 0)
                        dl->AddLine(ImVec2(gr_x, a.y), ImVec2(gr_x, b.y),
                                     guide_col, 1.0f);
                    if (sl.border_t > 0)
                        dl->AddLine(ImVec2(a.x, gt_y), ImVec2(b.x, gt_y),
                                     guide_col, 1.0f);
                    if (sl.border_b > 0)
                        dl->AddLine(ImVec2(a.x, gb_y), ImVec2(b.x, gb_y),
                                     guide_col, 1.0f);

                    // Mid-edge handles. Triangular nubs so they read
                    // distinctly from the corner squares.
                    const float midy = (gt_y + gb_y) * 0.5f;
                    const float midx = (gl_x + gr_x) * 0.5f;
                    auto draw_h = [&](float hx, float hy) {
                        ImVec2 ha{ hx - hsz, hy - hsz };
                        ImVec2 hb{ hx + hsz, hy + hsz };
                        dl->AddRectFilled(ha, hb, handle_col);
                        dl->AddRect(ha, hb, IM_COL32(0, 0, 0, 200));
                    };
                    draw_h(gl_x, midy);   // L
                    draw_h(gr_x, midy);   // R
                    draw_h(midx, gt_y);   // T
                    draw_h(midx, gb_y);   // B
                }
            }
        } else {
            dl->AddText(ImVec2(area_tl.x + 12, area_tl.y + 12),
                        IM_COL32(220, 80, 80, 255),
                        "(failed to load texture)");
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##cutter_right", ImVec2(right_w, content_h), true);
    {
        // ---- Image settings: PPU / Filter / Wrap / default Pivot ------
        // Mirrors the Inspector's asset-settings panel so users can tune
        // them while looking at the cutter (you usually pick PPU + filter
        // at the same moment you slice the atlas). All four fields live
        // in the same SpriteAssetSettings written to .meta on Save.
        if (ImGui::CollapsingHeader("Image settings",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& cs = s.sprite_cutter_settings;

            // Two-column table: fixed-width label on the left, stretchy
            // control on the right. The right panel is 280px wide; using
            // -FLT_MIN on item width with the ImGui default "label after
            // control" layout pushes the parameter name off-screen, so we
            // render the label ourselves in a dedicated column.
            if (ImGui::BeginTable("##cs_tbl", 2,
                    ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed,  60.0f);
                ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch);

                auto label = [](const char* lbl, const char* tip) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lbl);
                    if (tip && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tip);
                    ImGui::TableNextColumn();
                };

                label("PPU",
                    "Pixels per world unit. 100 means a 100-px tall\n"
                    "sprite is 1 unit. Lower values draw bigger.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##cs_ppu", &cs.pixels_per_unit,
                                  1.0f, 1.0f, 4096.0f, "%.1f");

                label("Filter",
                    "Linear  -- smooth blur (good for high-res art).\n"
                    "Nearest -- crisp blocks (use for pixel art).");
                {
                    const char* names[] = { "Linear", "Nearest" };
                    int idx = (cs.filter == Engine::SpriteFilter::Nearest) ? 1 : 0;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##cs_f", &idx, names, IM_ARRAYSIZE(names))) {
                        cs.filter = (idx == 1)
                            ? Engine::SpriteFilter::Nearest
                            : Engine::SpriteFilter::Linear;
                        if (r && tex && r->set_texture_filter)
                            r->set_texture_filter(r, tex, idx);
                    }
                }

                label("Wrap",
                    "How UVs outside 0..1 are sampled.\n"
                    "Clamp / Repeat / Mirror.");
                {
                    const char* names[] = { "Clamp", "Repeat", "Mirror" };
                    int idx = static_cast<int>(cs.wrap);
                    if (idx < 0 || idx > 2) idx = 0;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##cs_w", &idx, names, IM_ARRAYSIZE(names))) {
                        cs.wrap = static_cast<Engine::SpriteWrap>(idx);
                        if (r && tex && r->set_texture_wrap)
                            r->set_texture_wrap(r, tex, idx);
                    }
                }

                label("Pivot",
                    "Default pivot for new slices. (0,0)=TL, (1,1)=BR.");
                {
                    float pv[2] = { cs.pivot_x, cs.pivot_y };
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat2("##cs_pv", pv, 0.01f, 0.0f, 1.0f, "%.3f")) {
                        cs.pivot_x = pv[0];
                        cs.pivot_y = pv[1];
                    }
                }

                ImGui::EndTable();
            }
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Text("Slices (%d)", (int)s.sprite_cutter_slices.size());
        ImGui::Separator();
        for (int i = 0; i < (int)s.sprite_cutter_slices.size(); ++i) {
            ImGui::PushID(i);
            auto& sl = s.sprite_cutter_slices[i];

            // Selectable row showing name + rect.
            char label[256];
            std::snprintf(label, sizeof(label),
                          "%s    [%d, %d, %d x %d]##row",
                          sl.name.c_str(), sl.x, sl.y, sl.w, sl.h);
            const bool sel = (i == s.sprite_cutter_selected);
            if (ImGui::Selectable(label, sel)) {
                s.sprite_cutter_selected = i;
            }

            if (sel) {
                // Rename + rect editors for the active row.
                char nm[64] = {};
                std::strncpy(nm, sl.name.c_str(), sizeof(nm) - 1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##name", nm, sizeof(nm))) {
                    sl.name = nm;
                }
                int rect[4] = { sl.x, sl.y, sl.w, sl.h };
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt4("##rect", rect, 1, 0, 8192)) {
                    sl.x = rect[0]; sl.y = rect[1];
                    sl.w = rect[2]; sl.h = rect[3];
                }

                // ---- Per-slice pivot ------------------------------------
                // Slices that share a texture often want different pivots
                // (a hero sprite anchored at the feet, a coin anchored at
                // the center). The asset-level pivot is just the default
                // for new slices; once a slice exists it carries its own
                // pivot which the slice-drop handler bakes into the target
                // Sprite.pivot. 3x3 preset grid + a DragFloat2 for fine
                // control matches the asset settings widget.
                if (ImGui::TreeNodeEx("Pivot",
                        ImGuiTreeNodeFlags_SpanAvailWidth |
                        ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("(0,0)=top-left, (0.5,0.5)=center, (1,1)=bottom-right");
                    static const float presets[3] = { 0.0f, 0.5f, 1.0f };
                    if (ImGui::BeginTable("##slice_pivot_grid", 3,
                            ImGuiTableFlags_SizingFixedFit)) {
                        for (int row = 0; row < 3; ++row) {
                            ImGui::TableNextRow();
                            for (int col = 0; col < 3; ++col) {
                                ImGui::TableNextColumn();
                                const float px = presets[col];
                                const float py = presets[row];
                                const bool active =
                                    std::fabs(sl.pivot_x - px) < 0.001f &&
                                    std::fabs(sl.pivot_y - py) < 0.001f;
                                ImGui::PushID(row * 3 + col);
                                if (active) {
                                    ImGui::PushStyleColor(ImGuiCol_Button,
                                        ImVec4(0.30f, 0.55f, 0.85f, 1.0f));
                                }
                                if (ImGui::Button(active ? "*" : ".",
                                                    ImVec2(24, 24))) {
                                    sl.pivot_x = px;
                                    sl.pivot_y = py;
                                }
                                if (active) ImGui::PopStyleColor();
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndTable();
                    }
                    float pv[2] = { sl.pivot_x, sl.pivot_y };
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat2("##slice_pv", pv, 0.005f,
                                            0.0f, 1.0f, "%.3f")) {
                        sl.pivot_x = pv[0];
                        sl.pivot_y = pv[1];
                    }
                    ImGui::TreePop();
                }

                // ---- 9-slice section -----------------------------------
                // Borders default to zero (one-quad fast path). Drag the
                // mid-edge handles in the preview to introduce them, or
                // type values directly here. Scale mode picks how
                // edges/center fill when the entity is bigger than the
                // slice's native border-derived size.
                if (ImGui::TreeNodeEx("9-slice",
                        ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    int b[4] = { sl.border_l, sl.border_r,
                                  sl.border_t, sl.border_b };
                    ImGui::TextDisabled("L  R  T  B (px)");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragInt4("##9b", b, 1, 0, 8192)) {
                        // Clamp opposite-pair sums so the center keeps
                        // at least 1px.
                        if (b[0] + b[1] > sl.w - 1)
                            b[1] = std::max(0, sl.w - 1 - b[0]);
                        if (b[2] + b[3] > sl.h - 1)
                            b[3] = std::max(0, sl.h - 1 - b[2]);
                        sl.border_l = std::max(0, b[0]);
                        sl.border_r = std::max(0, b[1]);
                        sl.border_t = std::max(0, b[2]);
                        sl.border_b = std::max(0, b[3]);
                    }

                    // Per-region modes. Edges (T/B/L/R) share one
                    // mode; the center has its own. Lets users do
                    // common patterns like "tile edges, stretch center"
                    // without the renderer needing 5 independent modes.
                    const char* mode_names[] = {
                        "Stretch", "Tile", "TileFit"
                    };
                    {
                        ImGui::TextDisabled("Edges");
                        int sm = (int)sl.scale_mode;
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##9sm", &sm, mode_names,
                                           IM_ARRAYSIZE(mode_names))) {
                            sl.scale_mode = (Engine::SpriteScaleMode)sm;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "How the four edges (T/B/L/R) fill\n"
                                "their stretchable axis.\n"
                                "Stretch / Tile / TileFit.");
                        }
                    }
                    {
                        ImGui::TextDisabled("Center");
                        int cm = (int)sl.center_mode;
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##9cm", &cm, mode_names,
                                           IM_ARRAYSIZE(mode_names))) {
                            sl.center_mode = (Engine::SpriteScaleMode)cm;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "How the center region fills, on both\n"
                                "axes. Stretch / Tile / TileFit.");
                        }
                    }

                    // Optional "per-side override" -- splits the Edges
                    // dropdown into separate H (T/B) + V (L/R) modes.
                    // Stored as the same scale_mode on disk; the
                    // override is editor state only (the runtime never
                    // reads the per-edge values today). Surfaced now so
                    // users can REQUEST the renderer support and we can
                    // wire it through later without UX churn.
                    static bool per_side_override = false;
                    ImGui::Checkbox("Override per side##9_pso", &per_side_override);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Reveal separate H (top/bottom) and V (left/right)\n"
                            "edge mode dropdowns. The on-disk format only\n"
                            "stores ONE edge mode today -- the H value\n"
                            "wins when this is on (V is documentation\n"
                            "for a future renderer extension).");
                    }
                    if (per_side_override) {
                        int hm = (int)sl.scale_mode;
                        int vm = (int)sl.scale_mode;
                        ImGui::TextDisabled("H edges (T/B)");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##9hm", &hm, mode_names,
                                           IM_ARRAYSIZE(mode_names))) {
                            sl.scale_mode = (Engine::SpriteScaleMode)hm;
                        }
                        ImGui::TextDisabled("V edges (L/R)");
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##9vm", &vm, mode_names,
                                           IM_ARRAYSIZE(mode_names))) {
                            // No on-disk slot for V edges yet; this
                            // dropdown is read-only in storage but the
                            // user can still see / set the intended
                            // value for when the data model splits.
                            (void)vm;
                        }
                    }
                    if (ImGui::SmallButton("Clear borders")) {
                        sl.border_l = sl.border_r = 0;
                        sl.border_t = sl.border_b = 0;
                    }
                    ImGui::TreePop();
                }

                if (ImGui::SmallButton("Delete")) {
                    s.sprite_cutter_slices.erase(
                        s.sprite_cutter_slices.begin() + i);
                    if (s.sprite_cutter_selected >=
                        (int)s.sprite_cutter_slices.size())
                        s.sprite_cutter_selected = -1;
                    ImGui::PopID();
                    break;
                }
                ImGui::Separator();
            }
            ImGui::PopID();
        }
        if (s.sprite_cutter_slices.empty()) {
            ImGui::TextDisabled("No slices yet. Use Auto grid above to "
                                 "carve the texture into cells.");
        }
    }
    ImGui::EndChild();

    // ---- Footer: Save / Cancel ----
    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        // Pull working PPU / filter / wrap / pivot from the in-modal
        // settings; replace the slice list with the working copy. Then
        // hand the full SpriteAssetSettings to the registry so the .meta
        // file is rewritten in one shot.
        SpriteAssetSettings cur = s.sprite_cutter_settings;
        cur.slices = s.sprite_cutter_slices;
        if (!reg.update_sprite_settings(s.sprite_cutter_guid, cur)) {
            show_toast(s, "Sprite cutter: write to .meta failed",
                       3.0f, true);
        } else {
            // Final broadcast right after persisting -- ensures the
            // live ECS matches the saved .meta even if the in-frame
            // mirror was racing with another path.
            broadcast_slices_to_sprites(s, cur.slices,
                                         cur.pixels_per_unit);

            char m[128];
            std::snprintf(m, sizeof(m), "Saved %d slice%s",
                          (int)cur.slices.size(),
                          cur.slices.size() == 1 ? "" : "s");
            show_toast(s, m, 2.0f, false);
        }
        s.sprite_cutter_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        s.sprite_cutter_open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();

    // If the modal was closed via the X button (sprite_cutter_open
    // flipped via the &p_open arg) we still need to close the popup
    // state cleanly.
    if (!s.sprite_cutter_open && ImGui::IsPopupOpen("Sprite Cutter")) {
        ImGui::CloseCurrentPopup();
    }
}

}  // namespace Engine::editor
