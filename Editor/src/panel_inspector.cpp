#include "editor.h"

#include <zues/asset.h>
#include <zues/components/name.h>
#include <zues/components/audio.h>
#include <zues/components/physics.h>
#include <zues/components/render.h>
#include <zues/host/audio_system.h>
#include <zues/host/task_runner.h>
#include <zues/ecs/component_type.h>
#include <zues/ecs/reflection.h>
#include <zues/ecs/world.h>
#include <zues/services/renderer_2d.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Engine::editor {

namespace {
    // Forward decl — defined below at namespace scope. Needed by draw_component
    // for the Sprite-texture special case.
    void draw_sprite_picker(EditorState& s, void* component_data,
                            const ecs::FieldInfo& f);

    // Defined below; lists the texture's .meta slices in a Combo and
    // writes the selection's pixel rect into the Sprite's slice_x/y/w/h
    // fields. Needs the whole ComponentType so it can resolve those four
    // field offsets by name.
    void draw_slice_picker(EditorState& s, void* component_data,
                           const ecs::ComponentType& desc);

    // Defined below; named-clip table for the Animator component.
    // Parses the component's `clips` CharBuffer, renders one row per
    // entry (name + AnimationRef drag-target + Play/Delete buttons),
    // re-encodes on edit. Also offers a Combo to pick which clip is
    // "current" -- writes the resolved guid into Animator.animation.
    void draw_animator_clips(EditorState& s, void* component_data,
                             const ecs::ComponentType& desc);

    // Defined below; sectioned editor for the Particles component.
    // Replaces the flat reflection-driven 42-field inspector with a
    // grouped UI organised by purpose (Identity / Emission / Shape /
    // Lifetime / Forces / Visual / Modules / Steering / Diagnostics).
    void draw_particles_editor(EditorState& s, ecs::Entity e,
                                void* component_data);

    // Defined below; sectioned editor for AudioSource. Top: AudioCueRef
    // slot with preview + jump-to-cue-editor. Transport row (Play/Stop
    // buttons + live status). Mix sliders (volume / pitch / pan).
    // Spatial group (3D toggle / min-max distance / blend / bus).
    void draw_audio_source_editor(EditorState& s, ecs::Entity e,
                                    void* component_data);

    // Defined below; sectioned editors mirroring the AudioSource
    // pattern. Each one groups fields under a colored accent header
    // (Body / Material / Damping / Misc, Shape / Behaviour, etc.) so
    // the inspector reads as the same product across every built-in.
    void draw_rigidbody_editor(EditorState& s, ecs::Entity e,
                                void* component_data);
    void draw_box_collider_editor(EditorState& s, ecs::Entity e,
                                   void* component_data);
    void draw_circle_collider_editor(EditorState& s, ecs::Entity e,
                                      void* component_data);
    void draw_camera2d_editor(EditorState& s, ecs::Entity e,
                               void* component_data);

    // Sprite "Set Native Size" support, defined below. The check
    // returns true when the entity's current Transform2D.scale * size
    // already matches the slice's source pixel size at the asset's
    // PPU (within a small epsilon). Used to hide the button when
    // pressing it would be a no-op.
    bool sprite_is_at_native_size(EditorState& s, ecs::Entity e,
                                   void* component_data,
                                   const ecs::ComponentType& desc);
    void apply_set_native_size(EditorState& s, ecs::Entity e,
                                void* component_data,
                                const ecs::ComponentType& desc);

    // ---- Inspector row helpers ----
    // Two-column table layout: label column (left, fixed-ish width) +
    // control column (right, fills remaining). Each row gets a tight
    // "[label] [control]" pair, consistent across components, no ImGui
    // built-in labels ("##" hides them).
    //
    // Caller must have started a 2-column ImGui::Table via
    // begin_inspector_table / end via end_inspector_table.
    bool begin_inspector_table(const char* id) {
        // Static label width: wide enough for "angular_damping" without
        // clipping. Fixed so every component aligns identically.
        constexpr ImGuiTableFlags F = ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_NoSavedSettings   |
                                      ImGuiTableFlags_NoBordersInBody;
        if (!ImGui::BeginTable(id, 2, F)) return false;
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }
    void end_inspector_table() { ImGui::EndTable(); }

    // Shared accent palette used by the high-fidelity component drawers.
    // Centralised so every built-in editor reads as the same product.
    inline ImVec4 col_accent()    { return {0.42f, 0.72f, 0.95f, 1.0f}; }
    inline ImVec4 col_accent_g()  { return {0.55f, 0.85f, 0.55f, 1.0f}; }
    inline ImVec4 col_accent_w()  { return {0.85f, 0.65f, 0.20f, 1.0f}; }
    inline ImVec4 col_accent_r()  { return {0.85f, 0.45f, 0.40f, 1.0f}; }

    // Per-family palette. Each engine subsystem gets a distinct accent
    // so the inspector reads as a colour-coded map of the entity:
    //   Core       (Transform2D / Name)            -- soft blue
    //   Hierarchy  (Parent / FirstChild / etc.)    -- muted grey
    //   Render     (Sprite / Camera2D / Text / UI) -- cyan
    //   Animator                                   -- magenta
    //   Particles                                  -- purple
    //   Physics    (RigidBody / Colliders)         -- orange
    //   Audio      (AudioSource / AudioListener)   -- green
    //   Project (anything user-defined)            -- amber
    // Returns one of the four `col_accent_*` shades for status badges
    // (the slot the user-defined component-family colour folds into).
    inline ImVec4 col_for_component(const char* name) {
        if (!name) return col_accent();
        // Core
        if (std::strcmp(name, "Transform2D") == 0 ||
            std::strcmp(name, "Name")        == 0)
            return ImVec4(0.55f, 0.78f, 0.95f, 1.0f);
        // Hierarchy
        if (std::strcmp(name, "Parent")      == 0 ||
            std::strcmp(name, "FirstChild")  == 0 ||
            std::strcmp(name, "NextSibling") == 0)
            return ImVec4(0.65f, 0.65f, 0.70f, 1.0f);
        // Render family
        if (std::strcmp(name, "Sprite")    == 0 ||
            std::strcmp(name, "Camera2D")  == 0 ||
            std::strcmp(name, "Text")      == 0 ||
            std::strcmp(name, "UIAnchor")  == 0)
            return ImVec4(0.40f, 0.80f, 0.90f, 1.0f);
        if (std::strcmp(name, "Animator")  == 0)
            return ImVec4(0.85f, 0.50f, 0.85f, 1.0f);
        if (std::strcmp(name, "Particles") == 0)
            return ImVec4(0.65f, 0.55f, 0.95f, 1.0f);
        // Physics family
        if (std::strcmp(name, "RigidBody")      == 0 ||
            std::strcmp(name, "BoxCollider")    == 0 ||
            std::strcmp(name, "CircleCollider") == 0)
            return ImVec4(0.95f, 0.62f, 0.30f, 1.0f);
        // Audio family
        if (std::strcmp(name, "AudioSource")   == 0 ||
            std::strcmp(name, "AudioListener") == 0)
            return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
        // Project / unknown
        return ImVec4(0.95f, 0.78f, 0.40f, 1.0f);
    }

    // Section header used inside custom component drawers. Renders as a
    // tinted small-caps-style label followed by a thin divider so the
    // sections read as discrete "panels within a panel." Pass a color
    // override to match the parent component's family accent.
    void inspector_section(const char* label, ImVec4 color) {
        ImGui::Spacing();
        ImGui::TextColored(color, "%s", label);
        ImGui::Separator();
    }
    void inspector_section(const char* label) {
        inspector_section(label, col_accent());
    }

    // Convert a snake_case / camelCase identifier to "Title Case" for
    // display in the inspector. Doesn't allocate per call - keeps a small
    // static buffer because field names are short and the inspector loop
    // is per-frame. `_` becomes space, every word's first char becomes
    // uppercase. Numbers + "2D"/"3D" suffixes are preserved as-is so
    // "Transform2D" stays "Transform2D" not "Transform2 D".
    const char* humanize_label(const char* in) {
        static thread_local char buf[64];
        if (!in) { buf[0] = 0; return buf; }
        size_t o = 0;
        bool start_of_word = true;
        for (size_t i = 0; in[i] && o + 1 < sizeof(buf); ++i) {
            char c = in[i];
            if (c == '_') {
                if (o > 0 && buf[o - 1] != ' ') buf[o++] = ' ';
                start_of_word = true;
                continue;
            }
            if (start_of_word && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            buf[o++] = c;
            start_of_word = false;
        }
        buf[o] = 0;
        return buf;
    }

    // Start a row: lays out the label cell + advances to the value cell
    // with the next item ready to fill the remaining width. Caller draws
    // the actual widget on a hidden-label "##xxx" id.
    void inspector_row_label(const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", humanize_label(label));
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    // Build a hidden ImGui label from a field name. We always want the
    // visual label to come from inspector_row_label, never the widget.
    inline std::string hidden_id(const char* name) {
        std::string s = "##";
        s += name;
        return s;
    }

    // Inspector pass-scoped world pointer. Set at the top of
    // draw_inspector_panel and cleared at the bottom; lets the leaf widgets
    // (entity-ref name lookup, asset-ref kind filtering) reach the live
    // world + asset registry without threading state through every call.
    // Nullable — widgets must check.
    thread_local EditorState* tls_inspector_state = nullptr;

    // EntityRef / Entity widget. Renders the target entity's Name (or
    // "<none>" / "{idx,gen} (dead)" fallback) and accepts a HIERARCHY_ENTITY
    // drag payload. Right-click clears the slot.
    void draw_entity_ref_widget(u8* p, const char* hidden_id) {
        u32* ints = reinterpret_cast<u32*>(p);
        ecs::Entity e{ints[0], ints[1]};
        std::string label;
        if (e.is_null()) {
            label = "<none>";
        } else if (tls_inspector_state && tls_inspector_state->world) {
            auto* w = tls_inspector_state->world;
            if (!w->is_alive(e)) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "[dead %u/%u]", e.index, e.generation);
                label = buf;
            } else {
                const ecs::ComponentId nid = w->find_component_id("Name");
                const auto* n = nid != ecs::INVALID_COMPONENT_ID
                    ? static_cast<const ::Engine::components::Name*>(w->get_component(e, nid))
                    : nullptr;
                label = (n && n->value[0]) ? n->value : "Entity";
            }
        } else {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%u/%u", e.index, e.generation);
            label = buf;
        }
        // Selectable so the whole cell is the drop target. The "##" id keeps
        // the visible text controlled by us (label above), not the widget.
        ImGui::Button((label + std::string(hidden_id)).c_str(), ImVec2(-FLT_MIN, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                const ecs::Entity dragged = *static_cast<const ecs::Entity*>(payload->Data);
                ints[0] = dragged.index;
                ints[1] = dragged.generation;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Clear")) { ints[0] = 0; ints[1] = 0; }
            ImGui::EndPopup();
        }
    }

    // PrefabRef / SpriteRef / TextureRef / AudioRef / FontRef widget. The
    // bytes are a Guid; we resolve through AssetRegistry to a project-
    // relative path for display. Drag-drop accepts the standard asset path
    // payload but only commits if the dropped file's extension matches the
    // expected AssetKind for this slot. Right-click clears.
    AssetKind kind_for_field(ecs::FieldKind fk) {
        switch (fk) {
            case ecs::FieldKind::PrefabRef:    return AssetKind::Prefab;
            case ecs::FieldKind::SpriteRef:    return AssetKind::Sprite;
            case ecs::FieldKind::TextureRef:   return AssetKind::Texture;
            case ecs::FieldKind::AudioRef:     return AssetKind::Audio;
            case ecs::FieldKind::FontRef:      return AssetKind::Font;
            case ecs::FieldKind::AnimationRef: return AssetKind::Animation;
            case ecs::FieldKind::AudioCueRef:  return AssetKind::AudioCue;
            default:                         return AssetKind::Unknown;
        }
    }

    void draw_asset_ref_widget(u8* p, ecs::FieldKind fk, const char* hidden_id) {
        Guid g{};
        std::memcpy(&g, p, sizeof(Guid));
        const AssetKind expected = kind_for_field(fk);

        std::string label;
        if (g.is_null()) {
            label = std::string("<none>  (") + asset_kind_name(expected) + ")";
        } else {
            const char* path = AssetRegistry::instance().path_for(g);
            label = path ? path : (std::string("<missing> ") + guid_to_hex(g));
        }

        // For audio + cue refs, leave room on the right for a small
        // preview-play button so the user can audition the slot
        // without leaving the inspector. Other ref kinds use full width.
        const bool show_preview = !g.is_null() &&
            (fk == ecs::FieldKind::AudioRef ||
             fk == ecs::FieldKind::AudioCueRef);
        const float preview_w   = show_preview ? 28.0f : 0.0f;
        const float pad         = show_preview ? ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
        ImGui::Button((label + std::string(hidden_id)).c_str(),
                      ImVec2(show_preview ? -(preview_w + pad) : -FLT_MIN, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                const char* abs = static_cast<const char*>(payload->Data);
                if (abs && tls_inspector_state) {
                    // Hand the absolute path straight to the registry's
                    // forgiving lookup. It tries exact canonical match
                    // first, then suffix match against every stored entry
                    // (handles abs / project-rel / assets-rel forms
                    // uniformly). What we WRITE to the slot is the GUID
                    // bytes — the path is just a key for resolution.
                    const Guid resolved =
                        AssetRegistry::instance().guid_for_any_path(abs);
                    const AssetKind k    = asset_kind_from_extension(abs);
                    if (!resolved.is_null() && k == expected) {
                        std::memcpy(p, &resolved, sizeof(Guid));
                    } else if (expected == AssetKind::AudioCue &&
                               k == AssetKind::Audio &&
                               !resolved.is_null()) {
                        // Convenience: dropping a raw audio file onto a
                        // Cue slot resolves to the audio's auto-cue. Keeps
                        // the workflow identical to v1 ("drag the wav,
                        // it just plays") while routing through the cue
                        // pipeline so volume/pitch/random work uniformly.
                        const auto* cue =
                            AssetRegistry::instance().find_auto_cue_for(resolved);
                        if (cue) {
                            const Guid cg = cue->guid;
                            std::memcpy(p, &cg, sizeof(Guid));
                        }
                    }
                    // Else: silently reject — wrong type or unindexed path.
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Clear")) {
                Guid z{};
                std::memcpy(p, &z, sizeof(Guid));
            }
            ImGui::EndPopup();
        }

        if (show_preview) {
            ImGui::SameLine();
            const std::string btn_id = std::string("\xE2\x96\xB6")  // ▶ U+25B6
                                       + std::string("##aprev") + hidden_id;
            if (ImGui::Button(btn_id.c_str(), ImVec2(preview_w, 0))) {
                if (fk == ecs::FieldKind::AudioCueRef) {
                    // Cue refs go through the cue preview path so
                    // volume/pitch/random + the random pick all match
                    // what the source-bound voice would produce at
                    // runtime.
                    Engine::host::audio_api::preview_cue(g);
                } else {
                    const char* rel = AssetRegistry::instance().path_for(g);
                    if (rel && tls_inspector_state &&
                        !tls_inspector_state->project_dir.empty()) {
                        std::string abs = tls_inspector_state->project_dir + "/"
                                        + tls_inspector_state->assets_root_relative
                                        + "/" + rel;
                        Engine::host::audio_api::preview_path(abs.c_str());
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Preview (one-shot)");
        }
    }

    // Field renderer. Rebuilt to use the table-row pattern: label column
    // then a full-width value widget. Boolean ints, dropdowns, checkboxes
    // and Vec2/Vec3 all line up consistently because they share the same
    // SetNextItemWidth(-FLT_MIN) anchor.
    void draw_field(const ecs::FieldInfo& f, void* base, const void* def_base) {
        u8* p = static_cast<u8*>(base) + f.offset;
        const bool at_default = def_base &&
            std::memcmp(p, static_cast<const u8*>(def_base) + f.offset, f.size) == 0;
        if (at_default)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushID(f.name);

        const std::string id = hidden_id(f.name);

        switch (f.kind) {
            case ecs::FieldKind::Bool: {
                inspector_row_label(f.name);
                bool b = *p != 0;
                if (ImGui::Checkbox(id.c_str(), &b)) *p = b ? 1 : 0;
                break;
            }
            case ecs::FieldKind::F32: {
                inspector_row_label(f.name);
                auto* fp = reinterpret_cast<float*>(p);
                const bool is_angle = std::strstr(f.name, "rotation") != nullptr
                                   || std::strstr(f.name, "angle")    != nullptr;
                if (is_angle) {
                    constexpr float TO_DEG = 57.29577951f;
                    constexpr float TO_RAD = 0.01745329f;
                    float deg = *fp * TO_DEG;
                    if (ImGui::DragFloat(id.c_str(), &deg, 0.5f, 0.0f, 0.0f, "%.2f deg")) {
                        *fp = deg * TO_RAD;
                    }
                } else {
                    ImGui::DragFloat(id.c_str(), fp, 0.05f);
                }
                break;
            }
            case ecs::FieldKind::F64: {
                inspector_row_label(f.name);
                float fv = static_cast<float>(*reinterpret_cast<double*>(p));
                if (ImGui::DragFloat(id.c_str(), &fv, 0.05f))
                    *reinterpret_cast<double*>(p) = static_cast<double>(fv);
                break;
            }
            case ecs::FieldKind::I8:
            case ecs::FieldKind::I16:
            case ecs::FieldKind::I32: {
                inspector_row_label(f.name);
                int v = 0;
                std::memcpy(&v, p, f.size);
                if (ImGui::DragInt(id.c_str(), &v))
                    std::memcpy(p, &v, f.size);
                break;
            }
            case ecs::FieldKind::U8:
            case ecs::FieldKind::U16:
            case ecs::FieldKind::U32: {
                inspector_row_label(f.name);
                int v = 0;
                std::memcpy(&v, p, f.size);
                if (ImGui::DragInt(id.c_str(), &v, 1.0f, 0, 0))
                    std::memcpy(p, &v, f.size);
                break;
            }
            case ecs::FieldKind::I64:
            case ecs::FieldKind::U64: {
                inspector_row_label(f.name);
                ImGui::Text("%lld (64-bit)",
                            static_cast<long long>(*reinterpret_cast<i64*>(p)));
                break;
            }
            case ecs::FieldKind::Vec2:
                inspector_row_label(f.name);
                ImGui::DragFloat2(id.c_str(), reinterpret_cast<float*>(p), 0.05f);
                break;
            case ecs::FieldKind::Vec3:
                inspector_row_label(f.name);
                ImGui::DragFloat3(id.c_str(), reinterpret_cast<float*>(p), 0.05f);
                break;
            case ecs::FieldKind::Vec4:
                inspector_row_label(f.name);
                ImGui::DragFloat4(id.c_str(), reinterpret_cast<float*>(p), 0.05f);
                break;
            case ecs::FieldKind::Color:
                inspector_row_label(f.name);
                ImGui::ColorEdit4(id.c_str(), reinterpret_cast<float*>(p),
                                   ImGuiColorEditFlags_NoInputs |
                                   ImGuiColorEditFlags_AlphaBar);
                break;
            case ecs::FieldKind::Entity:
            case ecs::FieldKind::EntityRef: {
                inspector_row_label(f.name);
                draw_entity_ref_widget(p, id.c_str());
                break;
            }
            case ecs::FieldKind::PrefabRef:
            case ecs::FieldKind::SpriteRef:
            case ecs::FieldKind::TextureRef:
            case ecs::FieldKind::AudioRef:
            case ecs::FieldKind::FontRef:
            case ecs::FieldKind::AnimationRef:
            case ecs::FieldKind::AudioCueRef: {
                inspector_row_label(f.name);
                draw_asset_ref_widget(p, f.kind, id.c_str());
                break;
            }
            case ecs::FieldKind::Handle: {
                inspector_row_label(f.name);
                const auto* ints = reinterpret_cast<const u32*>(p);
                ImGui::Text("[handle idx=%u, gen=%u]", ints[0], ints[1]);
                break;
            }
            case ecs::FieldKind::CharBuffer:
                inspector_row_label(f.name);
                ImGui::InputText(id.c_str(), reinterpret_cast<char*>(p), f.size);
                break;
            case ecs::FieldKind::Enum: {
                inspector_row_label(f.name);
                int v = 0;
                std::memcpy(&v, p, f.size);
                if (f.enum_options && f.enum_option_count > 0) {
                    int sel_idx = 0;
                    for (u32 i = 0; i < f.enum_option_count; ++i) {
                        if (f.enum_options[i].value == v) { sel_idx = (int)i; break; }
                    }
                    const char* preview = f.enum_options[sel_idx].name;
                    if (ImGui::BeginCombo(id.c_str(), preview)) {
                        for (u32 i = 0; i < f.enum_option_count; ++i) {
                            const bool selected = ((int)i == sel_idx);
                            if (ImGui::Selectable(f.enum_options[i].name, selected)) {
                                int new_v = f.enum_options[i].value;
                                std::memcpy(p, &new_v, f.size);
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    if (ImGui::DragInt(id.c_str(), &v))
                        std::memcpy(p, &v, f.size);
                }
                break;
            }
            case ecs::FieldKind::Unknown:
            default:
                inspector_row_label(f.name);
                ImGui::TextDisabled("(unsupported, size=%u)", f.size);
                break;
        }

        ImGui::PopID();
        if (at_default) ImGui::PopStyleColor();
    }

    // Wrap a draw_field call with undo bookkeeping. ImGui's
    // IsItemActivated fires when the user grabs focus on the widget;
    // IsItemDeactivatedAfterEdit fires once when focus is released and
    // the value actually changed. The pair brackets the user's atomic
    // action - one undo entry per field commit.
    void draw_field_undoable(EditorState& s, const ecs::FieldInfo& f,
                              void* base, const void* def_base) {
        draw_field(f, base, def_base);
        if (ImGui::IsItemActivated()) undo_begin(s);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Edit %s", f.name);
            undo_commit(s, buf);
            // Inspector commits write directly to component bytes (no
            // pending_op drain hooks them), so we mark the world dirty
            // here. The * indicator + Ctrl+S menu enable depend on it.
            s.world_dirty = true;
        }
        // Per-keystroke edits (drag float without releasing, char buffer
        // typing) also need to mark dirty so the user sees the * during
        // the edit rather than only after release. IsItemEdited fires
        // any time the value changed since the previous frame.
        if (ImGui::IsItemEdited()) {
            s.world_dirty = true;
        }
    }

    // Components the user can't add or remove via the Inspector. Transform2D
    // is auto-attached to every entity (engine invariant). Hierarchy
    // primitives are managed by the World::set_parent / unparent helpers —
    // editing them by hand would corrupt the linked-list structure.
    bool is_protected_component(const char* name) {
        if (!name) return false;
        return std::strcmp(name, "Transform2D") == 0
            || std::strcmp(name, "Parent")      == 0
            || std::strcmp(name, "FirstChild")  == 0
            || std::strcmp(name, "NextSibling") == 0;
    }

    void draw_component(EditorState& s, ecs::Entity e, ecs::ComponentId id) {
        const auto* desc = s.world->get_component_type(id);
        if (!desc) return;

        void* data = s.world->get_component(e, id);
        if (!data) return;

        char header[128];
        std::snprintf(header, sizeof(header), "%s##c%u", desc->name, id);
        // Tint the component name with its FAMILY accent so the
        // inspector reads as a colour-coded map of the entity (audio
        // green, physics orange, render cyan, ...). The accent only
        // colours the label; the chevron + filled bar keep their theme
        // colours so the header still reads as ImGui-native and the
        // open-state gradient remains visible.
        const ImVec4 family_color = col_for_component(desc->name);
        ImGui::PushStyleColor(ImGuiCol_Text, family_color);
        const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();

        // Right-click on the header → context menu. Transform2D + hierarchy
        // primitives can't be removed (auto-managed). Removal is queued and
        // applied after the iteration finishes (mid-walk mutation breaks
        // the type registry walker).
        if (ImGui::BeginPopupContextItem(header)) {
            if (is_protected_component(desc->name)) {
                ImGui::TextDisabled("%s is required", desc->name);
            } else if (ImGui::MenuItem("Remove Component")) {
                const auto entity = e;
                const auto cid    = id;
                const std::string nm = desc->name;
                request_confirm(s,
                    std::string("Remove component '") + nm + "' from this entity?",
                    [&s, entity, cid]() {
                        s.pending_component_removals.push_back({entity, cid});
                    });
            }
            ImGui::EndPopup();
        }

        if (open) {
            ImGui::PushID(static_cast<int>(id));
            if (desc->field_count == 0) {
                ImGui::TextDisabled("(tag - no fields)");
            } else {
                const bool is_sprite_comp = std::strcmp(desc->name, "Sprite") == 0;
                const bool is_rigidbody   = std::strcmp(desc->name, "RigidBody") == 0;
                const bool is_box_coll    = std::strcmp(desc->name, "BoxCollider") == 0;
                const bool is_circle_coll = std::strcmp(desc->name, "CircleCollider") == 0;
                const bool is_animator    = std::strcmp(desc->name, "Animator") == 0;
                const bool is_particles   = std::strcmp(desc->name, "Particles") == 0;
                const bool is_audio_source = std::strcmp(desc->name, "AudioSource") == 0;
                const bool is_camera2d     = std::strcmp(desc->name, "Camera2D")    == 0;
                const bool full_custom_rb  = is_rigidbody;
                const bool full_custom_box = is_box_coll;
                const bool full_custom_cir = is_circle_coll;
                const bool full_custom_cam = is_camera2d;

                if (begin_inspector_table("##inspector_fields")) {
                    for (u32 i = 0; i < desc->field_count; ++i) {
                        const auto& f = desc->fields[i];

                        // Hide engine-internal handle fields (start with `_`).
                        if (f.name && f.name[0] == '_') continue;

                        // Sprite.texture: dropdown instead of raw Handle.
                        if (is_sprite_comp && std::strcmp(f.name, "texture") == 0) {
                            inspector_row_label(f.name);
                            draw_sprite_picker(s, data, f);

                            // Right under the texture row, draw a slice
                            // dropdown listing the slices defined in the
                            // texture's .meta. Picking one writes
                            // slice_x/y/w/h on this same Sprite. This is
                            // the only way to assign a sub-rect of an
                            // atlas without hand-editing four ints.
                            draw_slice_picker(s, data, *desc);

                            // Set Native Size lives RIGHT under the
                            // slice row so the visual chain is "pick
                            // texture -> pick slice -> snap to native."
                            // Hidden when the entity is already at its
                            // native scale (button would be a no-op).
                            if (!sprite_is_at_native_size(s, e, data, *desc)) {
                                inspector_row_label("");
                                if (ImGui::Button("Set Native Size",
                                                    ImVec2(-FLT_MIN, 0))) {
                                    apply_set_native_size(s, e, data, *desc);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip(
                                        "Resize entity so 1 texture pixel\n"
                                        "= 1 world pixel at the asset's PPU.\n"
                                        "Hidden when already at native size.");
                                }
                            }
                            continue;
                        }

                        // Sprite slice fields are written by the slice
                        // picker above; hide the raw int rows so users
                        // don't get four redundant scrubbers underneath.
                        // Animator: hide the raw clip-table buffer +
                        // the 'animation' guid (driven by the clip
                        // picker). Custom UI rendered below the table.
                        if (is_animator &&
                            (std::strcmp(f.name, "clips")     == 0 ||
                             std::strcmp(f.name, "animation") == 0)) {
                            continue;
                        }

                        // Particles: skip ALL raw rows -- a fully
                        // custom sectioned editor renders below the
                        // table. The flat 42-field list is unusable.
                        if (is_particles) {
                            continue;
                        }

                        // AudioSource: same deal -- the raw flat list
                        // hides the "what does this entity actually
                        // sound like" mental model behind 11 unrelated
                        // sliders. A sectioned editor (cue / transport /
                        // mix / spatial) renders below the table.
                        if (is_audio_source) {
                            continue;
                        }

                        // Physics components + Camera2D: skip the flat
                        // field rows; a sectioned drawer renders below.
                        if (full_custom_rb  ||
                            full_custom_box ||
                            full_custom_cir ||
                            full_custom_cam) {
                            continue;
                        }

                        if (is_sprite_comp &&
                            (std::strcmp(f.name, "slice_x")    == 0 ||
                             std::strcmp(f.name, "slice_y")    == 0 ||
                             std::strcmp(f.name, "slice_w")    == 0 ||
                             std::strcmp(f.name, "slice_h")    == 0 ||
                             std::strcmp(f.name, "border_l")    == 0 ||
                             std::strcmp(f.name, "border_r")    == 0 ||
                             std::strcmp(f.name, "border_t")    == 0 ||
                             std::strcmp(f.name, "border_b")    == 0 ||
                             std::strcmp(f.name, "scale_mode")  == 0 ||
                             std::strcmp(f.name, "center_mode") == 0 ||
                             std::strcmp(f.name, "texture_ppu") == 0)) {
                            continue;
                        }

                        // RigidBody.body_type dropdown.
                        if (is_rigidbody && std::strcmp(f.name, "body_type") == 0) {
                            inspector_row_label(f.name);
                            int* v = reinterpret_cast<int*>(static_cast<char*>(data) + f.offset);
                            static const char* const labels[] = {"Static", "Kinematic", "Dynamic"};
                            int idx = (*v >= 0 && *v <= 2) ? *v : 2;
                            const int prev = idx;
                            if (ImGui::Combo("##body_type", &idx, labels, IM_ARRAYSIZE(labels))) {
                                undo_begin(s);
                                *v = idx;
                                undo_commit(s, "Change body_type");
                            }
                            (void)prev;
                            continue;
                        }

                        // Bool-like int flags rendered as checkboxes. The
                        // reflection layer types these as i32 to keep the
                        // structs POD; the inspector translates.
                        const bool is_bool_like_flag =
                            f.kind == ecs::FieldKind::I32 &&
                            ((is_rigidbody && (
                                std::strcmp(f.name, "fixed_rotation") == 0 ||
                                std::strcmp(f.name, "is_bullet") == 0)) ||
                             ((is_box_coll || is_circle_coll) && (
                                std::strcmp(f.name, "is_sensor") == 0 ||
                                std::strcmp(f.name, "edit_in_scene") == 0)));
                        if (is_bool_like_flag) {
                            inspector_row_label(f.name);
                            int* v = reinterpret_cast<int*>(static_cast<char*>(data) + f.offset);
                            bool b = (*v != 0);
                            const std::string id2 = std::string("##") + f.name;
                            if (ImGui::Checkbox(id2.c_str(), &b)) {
                                undo_begin(s);
                                *v = b ? 1 : 0;
                                char buf[64];
                                std::snprintf(buf, sizeof(buf), "Toggle %s", f.name);
                                undo_commit(s, buf);
                            }
                            continue;
                        }

                        draw_field_undoable(s, f, data, desc->default_data);
                    }
                    end_inspector_table();
                }

                // Animator: clip table editor. Renders the inline
                // named-clip list with add/remove rows + drag-target
                // AnimationRef per row + a "current" picker.
                if (is_animator) {
                    draw_animator_clips(s, data, *desc);
                }

                // Particles: fully custom sectioned editor.
                if (is_particles) {
                    draw_particles_editor(s, e, data);
                }

                // AudioSource: cue slot + transport row + mix sliders +
                // spatial section. See draw_audio_source_editor below.
                if (is_audio_source) {
                    draw_audio_source_editor(s, e, data);
                }

                if (full_custom_rb)  draw_rigidbody_editor      (s, e, data);
                if (full_custom_box) draw_box_collider_editor   (s, e, data);
                if (full_custom_cir) draw_circle_collider_editor(s, e, data);
                if (full_custom_cam) draw_camera2d_editor       (s, e, data);
            }
            ImGui::PopID();
        }
    }

    // "Add Component" picker. Two modes inside the popup:
    //   - Search active (any text in the box): flat best-match list, no
    //     category nesting. Up/Down arrows navigate, Enter commits.
    //   - Search empty: categorized tree. "Engine/<sub>" first (built-ins),
    //     then "Project/<sub>" (user-defined). Components with empty category
    //     fall under "Project". Categories themselves are slash-separated.
    // Case-insensitive substring match.
    void draw_add_component(EditorState& s, ecs::Entity e) {
        ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
            ImGui::OpenPopup("##add_component_popup");
        }

        if (!ImGui::BeginPopup("##add_component_popup")) return;

        // ---- Search field (auto-focus on open, persists while open) ------
        static char search[64] = {0};
        static int  selected   = 0;     // index into the filtered list
        if (ImGui::IsWindowAppearing()) {
            search[0] = 0;
            selected  = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(280);
        const bool enter_pressed = ImGui::InputText("##search", search, sizeof(search),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);

        // ---- Collect candidates (skip already-attached + protected) ------
        struct Cand {
            ecs::ComponentId id;
            const char*      name;
            const char*      category;   // never null; "" means default group
        };
        std::vector<Cand> cands;
        s.world->iterate_component_types(
            [&](ecs::ComponentId id, const ecs::ComponentType& t) {
                if (s.world->has_component(e, id))  return;
                if (is_protected_component(t.name)) return;
                cands.push_back({id, t.name, t.category ? t.category : ""});
            });

        // Case-insensitive substring helper.
        auto ci_contains = [](const char* hay, const char* needle) -> bool {
            if (!needle || !*needle) return true;
            for (; *hay; ++hay) {
                const char* h = hay; const char* n = needle;
                while (*h && *n &&
                       std::tolower(static_cast<unsigned char>(*h))
                       == std::tolower(static_cast<unsigned char>(*n))) { ++h; ++n; }
                if (!*n) return true;
            }
            return false;
        };

        ImGui::Separator();

        const bool searching = (search[0] != 0);

        if (searching) {
            // ---- Flat filtered list, alphabetical, arrow-navigable -------
            std::vector<int> filtered;
            for (int i = 0; i < (int)cands.size(); ++i)
                if (ci_contains(cands[i].name, search)) filtered.push_back(i);
            std::sort(filtered.begin(), filtered.end(),
                [&](int a, int b) { return std::strcmp(cands[a].name, cands[b].name) < 0; });

            // Clamp + advance selection on arrow keys (works even though the
            // InputText has focus — arrow events bubble to the popup).
            if (filtered.empty()) selected = 0;
            else {
                if (selected >= (int)filtered.size()) selected = (int)filtered.size() - 1;
                if (selected < 0) selected = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    selected = (selected + 1) % (int)filtered.size();
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    selected = (selected - 1 + (int)filtered.size()) % (int)filtered.size();
            }

            ImGui::BeginChild("##add_results", ImVec2(280, 220), true);
            for (int row = 0; row < (int)filtered.size(); ++row) {
                const Cand& c = cands[filtered[row]];
                const bool sel = (row == selected);
                if (ImGui::Selectable(c.name, sel)) {
                    s.world->add_component(e, c.id, nullptr);
                    s.world_dirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (sel) ImGui::SetScrollHereY(0.5f);
            }
            if (filtered.empty())
                ImGui::TextDisabled("(no matches)");
            ImGui::EndChild();

            // Enter commits the highlighted match.
            if (enter_pressed && !filtered.empty()) {
                const Cand& c = cands[filtered[selected]];
                s.world->add_component(e, c.id, nullptr);
                s.world_dirty = true;
                ImGui::CloseCurrentPopup();
            }
        } else {
            // ---- Categorized tree (search empty) -------------------------
            // Group by category. Empty category becomes "Project". Sort
            // categories alphabetically; within each, sort component names.
            struct Group {
                std::string                       category;
                std::vector<int>                  cand_idx;
            };
            std::vector<Group> groups;
            auto group_for = [&](const std::string& cat) -> Group& {
                for (auto& g : groups) if (g.category == cat) return g;
                groups.push_back({cat, {}});
                return groups.back();
            };
            for (int i = 0; i < (int)cands.size(); ++i) {
                std::string cat = cands[i].category[0] ? cands[i].category : "Project";
                group_for(cat).cand_idx.push_back(i);
            }
            std::sort(groups.begin(), groups.end(),
                [](const Group& a, const Group& b) { return a.category < b.category; });
            for (auto& g : groups) {
                std::sort(g.cand_idx.begin(), g.cand_idx.end(),
                    [&](int a, int b) { return std::strcmp(cands[a].name, cands[b].name) < 0; });
            }

            ImGui::BeginChild("##add_tree", ImVec2(280, 220), true);
            // Map a category path ("Engine/Audio", "Engine/Physics", ...)
            // to the family color so the picker reads the same colour
            // language as the inspector. Match by the trailing segment
            // ("Audio", "Physics", "Render", ...) so deep paths still
            // colour correctly.
            auto category_color = [](const std::string& cat) -> ImVec4 {
                auto ends_with = [&](const char* needle) {
                    const size_t n = std::strlen(needle);
                    return cat.size() >= n &&
                           cat.compare(cat.size() - n, n, needle) == 0;
                };
                if (ends_with("Audio"))     return ImVec4(0.55f,0.85f,0.55f,1);
                if (ends_with("Physics"))   return ImVec4(0.95f,0.62f,0.30f,1);
                if (ends_with("Render"))    return ImVec4(0.40f,0.80f,0.90f,1);
                if (ends_with("UI"))        return ImVec4(0.40f,0.80f,0.90f,1);
                if (ends_with("Hierarchy")) return ImVec4(0.65f,0.65f,0.70f,1);
                if (ends_with("Core"))      return ImVec4(0.55f,0.78f,0.95f,1);
                if (cat == "Project")       return ImVec4(0.95f,0.78f,0.40f,1);
                return ImVec4(0.85f,0.85f,0.90f,1);
            };
            for (auto& g : groups) {
                // Path -> nested TreeNodes. Replace each "/" with a level.
                // For the v1 impl we render the full path as one TreeNode label;
                // a future polish pass can do real nesting.
                ImGui::PushStyleColor(ImGuiCol_Text, category_color(g.category));
                const bool open = ImGui::TreeNodeEx(g.category.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (open) {
                    for (int i : g.cand_idx) {
                        if (ImGui::Selectable(cands[i].name)) {
                            s.world->add_component(e, cands[i].id, nullptr);
                            s.world_dirty = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::TreePop();
                }
            }
            if (groups.empty())
                ImGui::TextDisabled("(no addable components)");
            ImGui::EndChild();
        }

        ImGui::EndPopup();
    }

    // Sprite picker — replaces the raw Handle widget for Sprite::texture.
    // Shows a Combo of registered sprite names. Selecting one writes the
    // texture's renderer-id into the Handle's `index` field (generation 1).
    // None / empty registry → falls back to a disabled "(no sprites loaded)"
    // placeholder, since with no asset thumbnails opened the registry is bare.
    void draw_sprite_picker(EditorState& s, void* component_data,
                            const ecs::FieldInfo& f) {
        u8* p = static_cast<u8*>(component_data) + f.offset;
        u32* handle = reinterpret_cast<u32*>(p);   // [index, generation]

        // Find current selection by texture handle.
        int current_idx = -1;
        for (int i = 0; i < static_cast<int>(s.sprite_registry.size()); ++i) {
            if (s.sprite_registry[i].texture_handle == handle[0]) {
                current_idx = i;
                break;
            }
        }
        const char* preview = (current_idx >= 0)
            ? s.sprite_registry[current_idx].display_name.c_str()
            : (handle[0] == 0 ? "(none)" : "(unregistered texture)");

        const bool combo_open = ImGui::BeginCombo("sprite", preview);

        // Drag-drop target on the combo button itself: drop an image
        // asset from the Asset Browser straight onto the slot to set
        // (and load + register if needed) the texture without opening
        // the dropdown.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                const char* abs = static_cast<const char*>(payload->Data);
                if (abs) {
                    const AssetKind k = asset_kind_from_extension(abs);
                    if (k == AssetKind::Texture) {
                        // Reuse cached texture handle when the editor
                        // has already loaded a thumb for this path; load
                        // through the renderer otherwise. Try the registry's
                        // runtime handle first so we share one GL texture
                        // with whatever the world's lazy resolver may have
                        // already loaded -- otherwise duplicate textures
                        // for the same asset lead to filter/wrap edits not
                        // applying to the rendered sprite.
                        u32 tex = 0;
                        auto it = s.asset_thumb_cache.find(abs);
                        if (it != s.asset_thumb_cache.end()) {
                            tex = it->second;
                        } else {
                            const Guid pre_g =
                                AssetRegistry::instance().guid_for_any_path(abs);
                            if (!pre_g.is_null()) {
                                tex = AssetRegistry::instance()
                                    .runtime_handle_for_guid(AssetKind::Texture, pre_g);
                            }
                            if (tex == 0) {
                                auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
                                if (r && r->load_texture_from_file) {
                                    tex = r->load_texture_from_file(r, abs);
                                }
                            }
                            if (tex) s.asset_thumb_cache[abs] = tex;
                        }
                        if (tex) {
                            register_sprite_asset(s, abs, tex);
                            handle[0] = tex;
                            handle[1] = 1;
                            // Bind + apply .meta filter on first load so
                            // the sprite renders with the user's authored
                            // settings instead of the GL default LINEAR.
                            // Skipping this is what made dropped sprites
                            // render through a freshly-default-filtered
                            // GL texture even when the .meta said Nearest.
                            const Guid g =
                                AssetRegistry::instance().guid_for_any_path(abs);
                            if (!g.is_null()) {
                                AssetRegistry::instance()
                                    .bind_runtime_handle(AssetKind::Texture, tex, g);
                                auto* r2 = static_cast<IRenderer_2D_v1*>(s.renderer);
                                if (r2) {
                                    const auto sett = AssetRegistry::instance()
                                        .sprite_settings_for(g);
                                    if (r2->set_texture_filter)
                                        r2->set_texture_filter(r2, tex,
                                            sett.filter == SpriteFilter::Nearest ? 1 : 0);
                                    if (r2->set_texture_wrap) {
                                        int w = 0;
                                        if (sett.wrap == SpriteWrap::Repeat) w = 1;
                                        else if (sett.wrap == SpriteWrap::Mirror) w = 2;
                                        r2->set_texture_wrap(r2, tex, w);
                                    }
                                }
                            }
                        } else {
                            show_toast(s, "Drop sprite: texture load failed",
                                       3.0f, true);
                        }
                    } else {
                        show_toast(s, "Drop sprite: not an image asset",
                                   2.0f, true);
                    }
                }
            }
            // Slice payload from the texture asset settings panel: bakes
            // texture + slice rect + 9-slice borders + scale modes + PPU
            // into the Sprite in one drop. This is the "I want this
            // specific sub-rect of an atlas" flow the user asked for --
            // before, they'd drop the texture, then have to click the
            // slice combo, then maybe set borders, and there was no
            // visual link between the slice they wanted and the dropdown
            // entry that picked it.
            if (const ImGuiPayload* sp =
                    ImGui::AcceptDragDropPayload("ZUES_SPRITE_SLICE")) {
                struct SlicePayload {
                    Guid  texture_guid;
                    int   slice_index;
                    int   slice_x, slice_y, slice_w, slice_h;
                    int   border_l, border_r, border_t, border_b;
                    int   scale_mode, center_mode;
                    float texture_ppu;
                    float pivot_x, pivot_y;
                };
                if (sp->DataSize == (int)sizeof(SlicePayload)) {
                    SlicePayload pl{};
                    std::memcpy(&pl, sp->Data, sizeof(pl));
                    // Resolve the texture guid -> live GL handle. Goes
                    // through the same lazy-load resolver the world
                    // loader uses, so the .meta filter / wrap are
                    // applied on first load.
                    u32 tex = AssetRegistry::instance()
                        .runtime_handle_for_guid(AssetKind::Texture, pl.texture_guid);
                    if (tex == 0) {
                        const char* path =
                            AssetRegistry::instance().path_for(pl.texture_guid);
                        if (path) {
                            const std::string abs2 = s.project_dir + "/"
                                + s.assets_root_relative + "/" + path;
                            auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
                            if (r && r->load_texture_from_file) {
                                tex = r->load_texture_from_file(r, abs2.c_str());
                                if (tex) {
                                    AssetRegistry::instance().bind_runtime_handle(
                                        AssetKind::Texture, tex, pl.texture_guid);
                                    register_sprite_asset(s, abs2, tex);
                                    s.asset_thumb_cache[abs2] = tex;
                                }
                            }
                        }
                    }
                    if (tex) {
                        // Write Sprite.texture (Handle) AND the slice
                        // fields by name through the component descriptor.
                        // Looking up the descriptor here keeps the patch
                        // local instead of hardcoding offsets.
                        handle[0] = tex;
                        handle[1] = 1;
                        const auto* desc =
                            s.world->get_component_type(
                                s.world->find_component_id("Sprite"));
                        if (desc) {
                            auto write = [&](const char* name, const void* src, size_t n) {
                                for (u32 fi = 0; fi < desc->field_count; ++fi) {
                                    if (std::strcmp(desc->fields[fi].name, name) == 0 &&
                                        desc->fields[fi].size == n) {
                                        std::memcpy(static_cast<u8*>(component_data)
                                                      + desc->fields[fi].offset,
                                                    src, n);
                                        return;
                                    }
                                }
                            };
                            write("slice_x",     &pl.slice_x,    sizeof(int));
                            write("slice_y",     &pl.slice_y,    sizeof(int));
                            write("slice_w",     &pl.slice_w,    sizeof(int));
                            write("slice_h",     &pl.slice_h,    sizeof(int));
                            write("border_l",    &pl.border_l,   sizeof(int));
                            write("border_r",    &pl.border_r,   sizeof(int));
                            write("border_t",    &pl.border_t,   sizeof(int));
                            write("border_b",    &pl.border_b,   sizeof(int));
                            write("scale_mode",  &pl.scale_mode, sizeof(int));
                            write("center_mode", &pl.center_mode, sizeof(int));
                            write("texture_ppu", &pl.texture_ppu, sizeof(float));
                            // Sprite.pivot is a vec2 (8 bytes); the
                            // payload carries the slice's authored
                            // pivot so the dropped sprite anchors
                            // itself correctly (e.g. feet for a hero,
                            // center for a coin) without the user
                            // hand-editing the pivot row afterwards.
                            const float pv[2] = { pl.pivot_x, pl.pivot_y };
                            write("pivot", pv, sizeof(pv));
                        }
                    } else {
                        show_toast(s, "Drop slice: texture load failed",
                                   3.0f, true);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (combo_open) {
            // "(none)" entry — clears the texture.
            const bool none_selected = (handle[0] == 0);
            if (ImGui::Selectable("(none)", none_selected)) {
                handle[0] = 0;
                handle[1] = 0;
            }
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(s.sprite_registry.size()); ++i) {
                const auto& sa = s.sprite_registry[i];
                const bool sel = (i == current_idx);
                if (ImGui::Selectable(sa.display_name.c_str(), sel)) {
                    handle[0] = sa.texture_handle;
                    handle[1] = 1;   // editor-side generation marker
                }
                // Tooltip with the source path so the user can disambiguate
                // sprites with the same display name.
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", sa.abs_path.c_str());
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            if (s.sprite_registry.empty()) {
                ImGui::TextDisabled("Open the Assets panel and click an image to register it.");
            }
            ImGui::EndCombo();
        }
    }

    // Slice picker -- shown right under the Sprite's texture combo.
    // Sources its slice list from the .meta sidecar of the currently
    // assigned texture (resolved via the editor's sprite_registry, which
    // maps texture_handle -> abs_path -> guid). "(none)" zeroes the
    // four slice_* fields so the renderer falls back to whole-texture.
    void draw_slice_picker(EditorState& s, void* component_data,
                           const ecs::ComponentType& desc) {
        // Resolve field offsets once. If reflection didn't pick the
        // fields up (older Sprite struct, etc.) we just bail silently.
        const ecs::FieldInfo* fx = nullptr;
        const ecs::FieldInfo* fy = nullptr;
        const ecs::FieldInfo* fw = nullptr;
        const ecs::FieldInfo* fh = nullptr;
        const ecs::FieldInfo* ft = nullptr;     // texture handle
        const ecs::FieldInfo* fbl = nullptr;
        const ecs::FieldInfo* fbr = nullptr;
        const ecs::FieldInfo* fbt = nullptr;
        const ecs::FieldInfo* fbb = nullptr;
        const ecs::FieldInfo* fsm = nullptr;
        const ecs::FieldInfo* fcm = nullptr;
        const ecs::FieldInfo* fpu = nullptr;
        for (u32 i = 0; i < desc.field_count; ++i) {
            const auto& f = desc.fields[i];
            if (!f.name) continue;
            if      (std::strcmp(f.name, "slice_x")     == 0) fx  = &f;
            else if (std::strcmp(f.name, "slice_y")     == 0) fy  = &f;
            else if (std::strcmp(f.name, "slice_w")     == 0) fw  = &f;
            else if (std::strcmp(f.name, "slice_h")     == 0) fh  = &f;
            else if (std::strcmp(f.name, "texture")     == 0) ft  = &f;
            else if (std::strcmp(f.name, "border_l")    == 0) fbl = &f;
            else if (std::strcmp(f.name, "border_r")    == 0) fbr = &f;
            else if (std::strcmp(f.name, "border_t")    == 0) fbt = &f;
            else if (std::strcmp(f.name, "border_b")    == 0) fbb = &f;
            else if (std::strcmp(f.name, "scale_mode")  == 0) fsm = &f;
            else if (std::strcmp(f.name, "center_mode") == 0) fcm = &f;
            else if (std::strcmp(f.name, "texture_ppu") == 0) fpu = &f;
        }
        if (!fx || !fy || !fw || !fh || !ft) return;

        u8* base = static_cast<u8*>(component_data);
        i32* px = reinterpret_cast<i32*>(base + fx->offset);
        i32* py = reinterpret_cast<i32*>(base + fy->offset);
        i32* pw = reinterpret_cast<i32*>(base + fw->offset);
        i32* ph = reinterpret_cast<i32*>(base + fh->offset);
        const u32* htex = reinterpret_cast<const u32*>(base + ft->offset);

        // Helper to write the optional 9-slice mirror fields. Safe even
        // if reflection didn't pick them up on an older Sprite struct.
        auto write_borders = [&](i32 l, i32 r, i32 t, i32 b,
                                 i32 edge_mode, i32 center_mode,
                                 float ppu) {
            if (fbl) *reinterpret_cast<i32*>(base + fbl->offset) = l;
            if (fbr) *reinterpret_cast<i32*>(base + fbr->offset) = r;
            if (fbt) *reinterpret_cast<i32*>(base + fbt->offset) = t;
            if (fbb) *reinterpret_cast<i32*>(base + fbb->offset) = b;
            if (fsm) *reinterpret_cast<i32*>(base + fsm->offset) = edge_mode;
            if (fcm) *reinterpret_cast<i32*>(base + fcm->offset) = center_mode;
            if (fpu) *reinterpret_cast<float*>(base + fpu->offset) =
                (ppu > 0.0f ? ppu : 100.0f);
        };

        // Map the assigned texture handle back to a guid via the editor's
        // registry (texture_handle -> abs_path -> guid).
        const std::string* abs_path = nullptr;
        for (const auto& sa : s.sprite_registry) {
            if (sa.texture_handle == htex[0] && htex[0] != 0) {
                abs_path = &sa.abs_path; break;
            }
        }

        inspector_row_label("slice");
        if (htex[0] == 0 || !abs_path) {
            ImGui::TextDisabled("(no texture assigned)");
            return;
        }

        const auto& reg = AssetRegistry::instance();
        const Engine::Guid g = reg.guid_for_any_path(abs_path->c_str());
        const auto settings = g.is_null()
            ? SpriteAssetSettings{}
            : reg.sprite_settings_for(g);

        // Find current selection by matching rect (slice rects are unique
        // by position/size in any reasonable atlas layout).
        int current_idx = -1;
        for (int i = 0; i < (int)settings.slices.size(); ++i) {
            const auto& sl = settings.slices[i];
            if (sl.x == *px && sl.y == *py &&
                sl.w == *pw && sl.h == *ph &&
                (sl.w != 0 || sl.h != 0)) {
                current_idx = i; break;
            }
        }

        // Auto-sync 9-slice mirror fields from the current .meta. Users
        // edit borders in the sprite cutter after the slice was already
        // assigned to a Sprite; without this re-sync the Sprite's
        // borders stay at 0 and the renderer takes the single-quad fast
        // path. Cheap (one assignment when something differs); skipped
        // entirely when the slice can't be found in the asset.
        if (current_idx >= 0 && fbl && fbr && fbt && fbb && fsm) {
            const auto& sl = settings.slices[current_idx];
            i32* bl = reinterpret_cast<i32*>(base + fbl->offset);
            i32* br = reinterpret_cast<i32*>(base + fbr->offset);
            i32* bt = reinterpret_cast<i32*>(base + fbt->offset);
            i32* bb = reinterpret_cast<i32*>(base + fbb->offset);
            i32* sm = reinterpret_cast<i32*>(base + fsm->offset);
            if (*bl != sl.border_l) *bl = sl.border_l;
            if (*br != sl.border_r) *br = sl.border_r;
            if (*bt != sl.border_t) *bt = sl.border_t;
            if (*bb != sl.border_b) *bb = sl.border_b;
            if (*sm != (i32)sl.scale_mode) *sm = (i32)sl.scale_mode;
            if (fcm) {
                i32* cm = reinterpret_cast<i32*>(base + fcm->offset);
                if (*cm != (i32)sl.center_mode) *cm = (i32)sl.center_mode;
            }
            if (fpu) {
                float* pu = reinterpret_cast<float*>(base + fpu->offset);
                const float src = settings.pixels_per_unit > 0.0f
                                    ? settings.pixels_per_unit : 100.0f;
                if (*pu != src) *pu = src;
            }
        }
        const bool none_selected = (*pw == 0 && *ph == 0);
        const char* preview = none_selected
            ? "(whole texture)"
            : (current_idx >= 0
                 ? settings.slices[current_idx].name.c_str()
                 : "(custom rect)");

        if (ImGui::BeginCombo("##slice", preview)) {
            if (ImGui::Selectable("(whole texture)", none_selected)) {
                undo_begin(s);
                *px = 0; *py = 0; *pw = 0; *ph = 0;
                write_borders(0, 0, 0, 0, 0, 0, settings.pixels_per_unit);
                undo_commit(s, "Clear slice");
            }
            ImGui::Separator();
            for (int i = 0; i < (int)settings.slices.size(); ++i) {
                const auto& sl = settings.slices[i];
                const bool sel = (i == current_idx);
                if (ImGui::Selectable(sl.name.c_str(), sel)) {
                    undo_begin(s);
                    *px = sl.x; *py = sl.y;
                    *pw = sl.w; *ph = sl.h;
                    write_borders(sl.border_l, sl.border_r,
                                   sl.border_t, sl.border_b,
                                   (i32)sl.scale_mode,
                                   (i32)sl.center_mode,
                                   settings.pixels_per_unit);
                    undo_commit(s, "Pick slice");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("[%d, %d, %d x %d]",
                                       sl.x, sl.y, sl.w, sl.h);
                if (sel) ImGui::SetItemDefaultFocus();
            }
            if (settings.slices.empty()) {
                ImGui::TextDisabled("No slices in this texture.");
                ImGui::TextDisabled("Double-click the image in Assets to slice it.");
            }
            ImGui::EndCombo();
        }
    }

    // ---- Animator clip table editor ---------------------------------
    // Stores entries as a single CharBuffer of "<name>\t<guid_hex>\n"
    // rows. Cheap to parse / re-encode; survives world save/load via
    // standard CharBuffer reflection. Maximum row count derives from
    // the buffer size (one "name<TAB>32hex<NL>" row is ~64 bytes).
    void draw_animator_clips(EditorState& s, void* component_data,
                             const ecs::ComponentType& desc) {
        // Resolve required field offsets via reflection. If anything is
        // missing (older Animator struct), bail with a hint.
        const ecs::FieldInfo* fclips = nullptr;
        const ecs::FieldInfo* fanim  = nullptr;
        const ecs::FieldInfo* fcur   = nullptr;
        for (u32 i = 0; i < desc.field_count; ++i) {
            const auto& f = desc.fields[i];
            if (!f.name) continue;
            if      (std::strcmp(f.name, "clips")     == 0) fclips = &f;
            else if (std::strcmp(f.name, "animation") == 0) fanim  = &f;
            else if (std::strcmp(f.name, "current")   == 0) fcur   = &f;
        }
        if (!fclips) {
            ImGui::TextDisabled("(Animator missing 'clips' field -- "
                                 "rebuild the project)");
            return;
        }

        u8* base   = static_cast<u8*>(component_data);
        char* clips_buf = reinterpret_cast<char*>(base + fclips->offset);
        const u32 buf_cap = fclips->size;

        // Parse the buffer into a row table for editing. Tolerates blank
        // lines and missing tabs (treats whole line as the name).
        struct Row { std::string name; Guid guid; };
        std::vector<Row> rows;
        {
            const char* p = clips_buf;
            const char* end = clips_buf + ::strnlen(clips_buf, buf_cap);
            while (p < end) {
                const char* line_end = p;
                while (line_end < end && *line_end != '\n') ++line_end;
                const char* tab = p;
                while (tab < line_end && *tab != '\t') ++tab;
                Row r;
                r.name.assign(p, tab);
                if (tab < line_end) {
                    const char* hex = tab + 1;
                    if (line_end - hex == 32) {
                        r.guid = guid_from_hex(hex, 32);
                    }
                }
                if (!r.name.empty() || !r.guid.is_null())
                    rows.push_back(std::move(r));
                p = (line_end < end) ? line_end + 1 : line_end;
            }
        }

        auto encode_rows = [&]() {
            std::string out;
            out.reserve(rows.size() * 48);
            for (const auto& r : rows) {
                out += r.name;
                out += '\t';
                if (!r.guid.is_null()) out += guid_to_hex(r.guid);
                out += '\n';
            }
            const std::size_t n = std::min<std::size_t>(out.size(), buf_cap - 1);
            std::memcpy(clips_buf, out.data(), n);
            clips_buf[n] = 0;
        };

        // ---- Header: "Current" picker (the prominent control) ------
        // The clip a user wants to *play* is the headline action -- the
        // table below is the editing surface for managing entries.
        // Writes the resolved guid into Animator.animation so the
        // runtime can find it without re-parsing the buffer.
        ImGui::Spacing();
        if (ImGui::BeginTable("##animhdr", 2,
                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed,  90.0f);
            ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Current");
            ImGui::TableNextColumn();
            if (fcur && fanim) {
                i32* cur = reinterpret_cast<i32*>(base + fcur->offset);
                const bool have_rows = !rows.empty();
                if (have_rows) {
                    if (*cur < 0 || *cur >= (int)rows.size()) *cur = 0;
                    const char* preview = rows[*cur].name.empty()
                        ? "(unnamed)" : rows[*cur].name.c_str();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##current_clip", preview)) {
                        for (int i = 0; i < (int)rows.size(); ++i) {
                            const bool sel = (i == *cur);
                            const char* nm = rows[i].name.empty()
                                ? "(unnamed)" : rows[i].name.c_str();
                            if (ImGui::Selectable(nm, sel)) {
                                *cur = i;
                                std::memcpy(base + fanim->offset,
                                            &rows[i].guid, sizeof(Guid));
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    // Keep Animator.animation mirrored every frame --
                    // catches the case where the user edits the row's
                    // ref after the combo was already set.
                    if (*cur >= 0 && *cur < (int)rows.size()) {
                        Guid prev{};
                        std::memcpy(&prev, base + fanim->offset, sizeof(Guid));
                        if (!(prev == rows[*cur].guid)) {
                            std::memcpy(base + fanim->offset,
                                        &rows[*cur].guid, sizeof(Guid));
                        }
                    }
                } else {
                    ImGui::BeginDisabled();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char* dummy = "(none)";
                    int z = 0;
                    ImGui::Combo("##current_clip", &z, &dummy, 1);
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndTable();
        }

        // ---- Clip table -------------------------------------------
        ImGui::Spacing();
        ImGui::TextDisabled("Clips  (%d)", (int)rows.size());
        ImGui::Separator();

        bool changed = false;
        int  to_delete = -1;

        if (rows.empty()) {
            // Empty-state card: clear next-action prompt + drop hint.
            // Drag-drop target the size of an inspector row so users
            // can drop a .zanim straight onto it without first clicking
            // Add.
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float  w   = ImGui::GetContentRegionAvail().x;
            const float  h   = 56.0f;
            ImDrawList*  dl  = ImGui::GetWindowDrawList();
            const ImU32  bg  = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32  brd = ImGui::GetColorU32(ImGuiCol_Border);
            dl->AddRectFilled(cur, ImVec2(cur.x + w, cur.y + h), bg, 4.0f);
            dl->AddRect      (cur, ImVec2(cur.x + w, cur.y + h), brd, 4.0f);
            ImGui::InvisibleButton("##empty_drop", ImVec2(w, h));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                    const char* abs = static_cast<const char*>(payload->Data);
                    if (abs) {
                        const Guid g = AssetRegistry::instance().guid_for_any_path(abs);
                        const AssetKind k = asset_kind_from_extension(abs);
                        if (!g.is_null() && k == AssetKind::Animation) {
                            // Default name from filename stem.
                            std::string nm = abs;
                            auto sl = nm.find_last_of("/\\");
                            if (sl != std::string::npos) nm = nm.substr(sl + 1);
                            auto dt = nm.find_last_of('.');
                            if (dt != std::string::npos) nm = nm.substr(0, dt);
                            rows.push_back(Row{nm, g});
                            changed = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            const char* hint = "Drop a .zanim here  or  click + Add clip";
            const ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(cur.x + (w - ts.x) * 0.5f,
                                cur.y + (h - ts.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        hint);
        } else {
            // Per-row card: number badge | name input | ref widget | ×
            // Highlight the "current" row with a faint accent on the
            // index badge so the runtime selection is visually
            // discoverable inside the table too.
            i32 cur_idx = -1;
            if (fcur) cur_idx = *reinterpret_cast<i32*>(base + fcur->offset);

            for (int i = 0; i < (int)rows.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::BeginTable("##animrow", 4,
                        ImGuiTableFlags_SizingStretchProp |
                        ImGuiTableFlags_PadOuterX)) {
                    ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed,  22.0f);
                    ImGui::TableSetupColumn("nm", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                    ImGui::TableSetupColumn("r",  ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("x",  ImGuiTableColumnFlags_WidthFixed, 24.0f);
                    ImGui::TableNextRow();

                    // Index badge (acts as the selection click-target).
                    ImGui::TableNextColumn();
                    {
                        char ix[8];
                        std::snprintf(ix, sizeof(ix), "%d", i);
                        const bool is_cur = (i == cur_idx);
                        if (is_cur) ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(1.0f, 0.65f, 0.25f, 1.0f));
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(ix);
                        if (is_cur) ImGui::PopStyleColor();
                        if (ImGui::IsItemClicked() && fcur && fanim) {
                            *reinterpret_cast<i32*>(base + fcur->offset) = i;
                            std::memcpy(base + fanim->offset,
                                        &rows[i].guid, sizeof(Guid));
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Click to set as current");
                    }

                    ImGui::TableNextColumn();
                    {
                        char nm[32] = {};
                        std::strncpy(nm, rows[i].name.c_str(), sizeof(nm) - 1);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##nm", nm, sizeof(nm))) {
                            rows[i].name = nm;
                            changed = true;
                        }
                    }
                    ImGui::TableNextColumn();
                    {
                        Guid before = rows[i].guid;
                        draw_asset_ref_widget(reinterpret_cast<u8*>(&rows[i].guid),
                                               ecs::FieldKind::AnimationRef,
                                               "##aref");
                        if (!(before == rows[i].guid)) changed = true;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("X", ImVec2(-FLT_MIN, 0))) {
                        to_delete = i;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove clip");
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
        }

        if (to_delete >= 0) {
            rows.erase(rows.begin() + to_delete);
            changed = true;
        }

        // ---- Add row (full-width, after the list) ------------------
        ImGui::Spacing();
        if (ImGui::Button("+  Add clip", ImVec2(-FLT_MIN, 0))) {
            char nm[32];
            std::snprintf(nm, sizeof(nm), "Clip%d", (int)rows.size());
            rows.push_back(Row{nm, Guid{}});
            changed = true;
        }

        if (changed) encode_rows();
    }

    // ---- Particles editor ------------------------------------------
    // Sectioned UI for the Particles component. Sections render with
    // `TreeNodeEx` + small accent strip rather than `CollapsingHeader`
    // so they READ as sub-sections of one component rather than four
    // child components stacked underneath. Visual hierarchy:
    //
    //   [component header -- drawn by draw_component above]
    //   [tier disclaimer banner if Heavy selected]
    //   ▸ Identity & Playback     <- TreeNode (small caret, no big bar)
    //       Name  [...]
    //       Tier  [...]
    //   ▸ Emission
    //   ▸ Spawn Shape
    //   ...
    //
    void draw_particles_editor(EditorState& s, ecs::Entity /*e*/,
                                void* data) {
        auto& p = *static_cast<components::Particles*>(data);

        // Helper: 2-column row (label fixed, control stretchy).
        auto begin_tbl = [](const char* id) -> bool {
            if (!ImGui::BeginTable(id, 2,
                    ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_PadOuterX)) return false;
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch);
            return true;
        };
        auto row = [](const char* lbl) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(lbl);
            ImGui::TableNextColumn();
        };

        // Section header that LOOKS like a sub-heading rather than a
        // separate component card. Uses TreeNodeEx so we keep the
        // collapse arrow and per-id state, but renders without the
        // CollapsingHeader's filled bar. A thin orange tick-bar on the
        // left of the contents says "this is part of the row above".
        auto begin_section = [](const char* id, const char* label,
                                 bool default_open = false) -> bool {
            const ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_SpanAvailWidth |
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            // Slightly bigger, brighter section labels.
            ImGui::PushStyleColor(ImGuiCol_Text,
                IM_COL32(230, 230, 240, 255));
            const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
            ImGui::PopStyleColor();
            if (open) {
                // Indent + accent strip so contents read as nested.
                ImGui::Indent(10.0f);
            }
            return open;
        };
        auto end_section = [](bool was_open) {
            if (was_open) ImGui::Unindent(10.0f);
        };

        ImGui::Spacing();

        // Tier note. Light + Medium are real today; Heavy still runs
        // the Medium CPU-jobs path until the GPU compute slice lands.
        if (p.tier == 1) {
            const int w = Engine::host::TaskRunner::instance().worker_count();
            ImGui::PushStyleColor(ImGuiCol_Text,
                IM_COL32(160, 200, 160, 255));
            ImGui::TextWrapped("Medium tier active -- integrating across "
                               "%d worker thread%s.",
                               w + 1, (w + 1) == 1 ? "" : "s");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        } else if (p.tier == 2) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                IM_COL32(255, 198, 109, 255));
            ImGui::TextWrapped(
                "Heavy (GPU) tier is data-only today. Sim falls back "
                "to the Medium CPU-jobs path until GPU compute lands.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        // ---- Identity & lifecycle (always open) -----------------------
        bool sec_id = begin_section("##psec_id", "Identity & Playback", true);
        if (sec_id) {
            if (begin_tbl("##pid")) {
                row("Name");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##pname", p.name, sizeof(p.name));

                row("Tier");
                static const char* tier_names[] = {
                    "Light (CPU)", "Medium (CPU jobs)", "Heavy (GPU)" };
                int tier = p.tier;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##ptier", &tier, tier_names, 3)) p.tier = tier;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Light  -- single-thread CPU, up to ~10k particles.\n"
                        "Medium -- CPU jobs, up to ~100k (deterministic).\n"
                        "Heavy  -- GPU compute, up to ~1M (no determinism).");

                row("Profile");
                static const char* profile_names[] = { "VFX", "Swarm", "Custom" };
                int prof = p.profile;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##pprof", &prof, profile_names, 3)) {
                    p.profile = prof;
                    if (prof == 0) {
                        // VFX defaults: no swarm features.
                        p.use_spatial_grid = 0; p.use_steering  = 0;
                        p.use_events       = 0; p.use_collision = 0;
                    } else if (prof == 1) {
                        // Swarm defaults: spatial + steering + events on.
                        p.use_spatial_grid = 1; p.use_steering  = 1;
                        p.use_events       = 1; p.use_collision = 1;
                    }
                }

                row("Playing");
                {
                    bool b = p.playing != 0;
                    if (ImGui::Checkbox("##pplay", &b)) p.playing = b ? 1 : 0;
                }
                row("Loop");
                {
                    bool b = p.loop != 0;
                    if (ImGui::Checkbox("##ploop", &b)) p.loop = b ? 1 : 0;
                }
                row("Duration");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pdur", &p.duration, 0.1f, -1.0f, 600.0f,
                                  p.duration < 0.0f ? "infinite" : "%.2f s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("-1 = forever. >=0 stops emitting after that many seconds.");

                row("Age");
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%.2f s", p.age);
                ImGui::EndTable();
            }
            // Quick-action buttons.
            if (ImGui::Button("Restart", ImVec2(90, 0))) {
                p.age = 0.0f;
                p.playing = 1;
            }
            ImGui::SameLine();
            if (ImGui::Button(p.playing ? "Pause" : "Play",
                                ImVec2(90, 0))) {
                p.playing = p.playing ? 0 : 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Burst x100", ImVec2(110, 0))) {
                p.burst_count  = 100;
                p.burst_time   = p.age;
                p.burst_period = 0.0f;
                p.playing      = 1;
            }
        }
        end_section(sec_id);

        // ---- Emission ---------------------------------------------------
        bool sec_em = begin_section("##psec_em", "Emission", true);
        if (sec_em) {
            if (begin_tbl("##pem")) {
                row("Max particles");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragInt("##pmax", &p.max_particles, 1, 1, 1000000);

                row("Rate (/sec)");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##prate", &p.rate, 0.5f, 0.0f, 10000.0f);

                row("Burst count");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragInt("##pbcnt", &p.burst_count, 1, 0, 1000000);

                row("Burst time");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pbt", &p.burst_time, 0.05f, 0.0f, 600.0f, "%.2f s");

                row("Burst period");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pbp", &p.burst_period, 0.05f, 0.0f, 600.0f,
                    p.burst_period <= 0.0f ? "one-shot" : "%.2f s");
                ImGui::EndTable();
            }
        }
        end_section(sec_em);

        // ---- Spawn shape -----------------------------------------------
        bool sec_sh = begin_section("##psec_sh", "Spawn Shape", true);
        if (sec_sh) {
            if (begin_tbl("##psh")) {
                row("Shape");
                static const char* shape_names[] = {
                    "Point", "Circle", "Rect", "Edge", "Ring" };
                int sh = p.shape;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##pshape", &sh, shape_names, 5)) p.shape = sh;

                if (p.shape != 0) {
                    row("Distribution");
                    static const char* dist_names[] = {
                        "Volume", "Edge", "Random" };
                    int di = p.shape_distribution;
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##pdist", &di, dist_names, 3))
                        p.shape_distribution = di;

                    row("Width");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##psw", &p.shape_w, 0.05f, 0.0f, 100.0f);
                    row("Height");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##psh2", &p.shape_h, 0.05f, 0.0f, 100.0f);
                }

                row("Initial vel");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat2("##piv",
                    reinterpret_cast<float*>(&p.initial_velocity), 0.1f);

                row("Vel random");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat2("##pvr",
                    reinterpret_cast<float*>(&p.velocity_random), 0.1f, 0.0f, 100.0f);
                ImGui::EndTable();
            }
        }
        end_section(sec_sh);

        // ---- Lifetime + appearance -------------------------------------
        bool sec_lf = begin_section("##psec_lf", "Particle Lifetime", true);
        if (sec_lf) {
            if (begin_tbl("##plf")) {
                row("Lifetime");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##plife", &p.lifetime, 0.05f, 0.001f, 600.0f, "%.2f s");

                row("Lifetime ±");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##plifer", &p.lifetime_random, 0.05f, 0.0f, 600.0f);

                row("Start size");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pss", &p.start_size, 0.02f, 0.0f, 100.0f);

                row("End size");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pes", &p.end_size, 0.02f, 0.0f, 100.0f,
                    p.end_size <= 0.0f ? "= start" : "%.3f");

                row("Start color");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::ColorEdit4("##psc",
                    reinterpret_cast<float*>(&p.start_color),
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

                row("End color");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::ColorEdit4("##pec",
                    reinterpret_cast<float*>(&p.end_color),
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::EndTable();
            }
        }
        end_section(sec_lf);

        // ---- Forces ----------------------------------------------------
        bool sec_fo = begin_section("##psec_fo", "Forces", false);
        if (sec_fo) {
            if (begin_tbl("##pf")) {
                row("Gravity");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat2("##pg",
                    reinterpret_cast<float*>(&p.gravity), 0.1f);

                row("Drag");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##pd", &p.drag, 0.01f, 0.0f, 10.0f);
                ImGui::EndTable();
            }
        }
        end_section(sec_fo);

        // ---- Visual ----------------------------------------------------
        bool sec_vi = begin_section("##psec_vi", "Visual", true);
        if (sec_vi) {
            if (begin_tbl("##pv")) {
                row("Texture");
                {
                    // Mini sprite picker. Drop a .png from the Asset
                    // Browser onto the button, or click to use the
                    // existing sprite-registry combo.
                    u32* h = reinterpret_cast<u32*>(&p.texture);
                    int current_idx = -1;
                    for (int i = 0; i < (int)s.sprite_registry.size(); ++i) {
                        if (s.sprite_registry[i].texture_handle == h[0]) {
                            current_idx = i; break;
                        }
                    }
                    const char* preview = (current_idx >= 0)
                        ? s.sprite_registry[current_idx].display_name.c_str()
                        : (h[0] == 0 ? "(none)" : "(unregistered)");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const bool open = ImGui::BeginCombo("##ptex", preview);
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* pl =
                                ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                            const char* abs = static_cast<const char*>(pl->Data);
                            if (abs && asset_kind_from_extension(abs) ==
                                AssetKind::Texture) {
                                u32 tex = 0;
                                auto it = s.asset_thumb_cache.find(abs);
                                if (it != s.asset_thumb_cache.end()) tex = it->second;
                                else {
                                    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
                                    if (r && r->load_texture_from_file) {
                                        tex = r->load_texture_from_file(r, abs);
                                        if (tex) s.asset_thumb_cache[abs] = tex;
                                    }
                                }
                                if (tex) {
                                    register_sprite_asset(s, abs, tex);
                                    h[0] = tex; h[1] = 1;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (open) {
                        if (ImGui::Selectable("(none)", h[0] == 0)) {
                            h[0] = 0; h[1] = 0;
                        }
                        ImGui::Separator();
                        for (int i = 0; i < (int)s.sprite_registry.size(); ++i) {
                            const auto& sa = s.sprite_registry[i];
                            const bool sel = (i == current_idx);
                            if (ImGui::Selectable(sa.display_name.c_str(), sel)) {
                                h[0] = sa.texture_handle; h[1] = 1;
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                row("Layer");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragInt("##player", &p.layer, 1);
                row("Order");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragInt("##porder", &p.order, 1);

                row("Blend");
                static const char* blend_names[] = {
                    "Alpha", "Additive", "Multiply" };
                int bm = p.blend_mode;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##pblend", &bm, blend_names, 3))
                    p.blend_mode = bm;
                ImGui::EndTable();
            }
        }
        end_section(sec_vi);

        // ---- Modules (swarm features) ----------------------------------
        bool sec_mo = begin_section("##psec_mo", "Modules", false);
        if (sec_mo) {
            ImGui::TextDisabled("Toggles below are honored by the runtime\n"
                                "as upcoming slices land. Today they only\n"
                                "persist; the sim ignores them.");
            ImGui::Spacing();
            auto bool_row = [&](const char* label, Engine::i32* v) {
                if (begin_tbl("##pmrow")) {
                    row(label);
                    bool b = (*v != 0);
                    std::string id = std::string("##m_") + label;
                    if (ImGui::Checkbox(id.c_str(), &b)) *v = b ? 1 : 0;
                    ImGui::EndTable();
                }
            };
            bool_row("Spatial grid", &p.use_spatial_grid);
            bool_row("Steering",     &p.use_steering);
            bool_row("Events",       &p.use_events);
            bool_row("Collision",    &p.use_collision);
        }
        end_section(sec_mo);

        // ---- Steering --------------------------------------------------
        if (p.use_steering) {
            bool sec_st = begin_section("##psec_st", "Steering", false);
            if (sec_st) {
                if (begin_tbl("##pst")) {
                    row("Seek target");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    float xy[2] = { p.steer_seek_target_x, p.steer_seek_target_y };
                    if (ImGui::DragFloat2("##psk", xy, 0.1f)) {
                        p.steer_seek_target_x = xy[0];
                        p.steer_seek_target_y = xy[1];
                    }
                    row("Seek weight");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##psw2", &p.steer_seek_weight, 0.05f, 0.0f, 10.0f);
                    row("Avoid radius");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##par", &p.steer_avoid_radius, 0.05f, 0.0f, 10.0f);
                    row("Avoid weight");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##paw", &p.steer_avoid_weight, 0.05f, 0.0f, 10.0f);
                    row("Align weight");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##palw", &p.steer_align_weight, 0.05f, 0.0f, 10.0f);
                    row("Cohesion w.");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##pcw", &p.steer_cohesion_weight, 0.05f, 0.0f, 10.0f);
                    ImGui::EndTable();
                }
            }
            end_section(sec_st);
        }

        // ---- Custom per-particle fields --------------------------------
        // Eight named float slots whose semantics the user defines:
        // type "hp", "team", "morale", whatever. Lync code reads them
        // via ParticleGet(emitter, idx, "name"). Unused rows are blank.
        bool sec_xf = begin_section("##psec_xf", "Custom Fields", false);
        if (sec_xf) {
            ImGui::TextDisabled(
                "Eight named float slots per particle. Set names here,\n"
                "read/write from Lync via ParticleGet/ParticleSet.");
            ImGui::Spacing();

            // Parse `extra_names` (a single newline-separated buffer)
            // into 8 fixed-size string rows for editing, then re-encode.
            constexpr int N = 8;
            char rows[N][32] = {};
            const char* src = p.extra_names;
            const char* end = src + ::strnlen(src, sizeof(p.extra_names));
            int row_i = 0;
            while (src < end && row_i < N) {
                const char* le = src;
                while (le < end && *le != '\n') ++le;
                const std::size_t n =
                    std::min<std::size_t>((std::size_t)(le - src), 31);
                std::memcpy(rows[row_i], src, n);
                rows[row_i][n] = 0;
                src = (le < end) ? le + 1 : le;
                ++row_i;
            }

            bool changed = false;
            if (ImGui::BeginTable("##xf_rows", 2,
                    ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("i",
                    ImGuiTableColumnFlags_WidthFixed, 24.0f);
                ImGui::TableSetupColumn("n",
                    ImGuiTableColumnFlags_WidthStretch);
                for (int i = 0; i < N; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%d", i);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    char id[16]; std::snprintf(id, sizeof(id), "##xf%d", i);
                    if (ImGui::InputText(id, rows[i], 32)) changed = true;
                }
                ImGui::EndTable();
            }
            if (changed) {
                // Re-encode back into the buffer. Trailing empty rows
                // collapse to a single trailing newline.
                std::string out;
                int last_used = -1;
                for (int i = 0; i < N; ++i)
                    if (rows[i][0]) last_used = i;
                for (int i = 0; i <= last_used; ++i) {
                    out += rows[i];
                    out += '\n';
                }
                const std::size_t n =
                    std::min<std::size_t>(out.size(), sizeof(p.extra_names) - 1);
                std::memcpy(p.extra_names, out.data(), n);
                p.extra_names[n] = 0;
            }
        }
        end_section(sec_xf);

        // ---- Diagnostics -----------------------------------------------
        bool sec_dg = begin_section("##psec_dg", "Diagnostics", true);
        if (sec_dg) {
            // Pool fill bar.
            const float frac = (p.max_particles > 0)
                ? (float)p.live_count / (float)p.max_particles : 0.0f;
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay),
                "%d / %d live", p.live_count, p.max_particles);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
            ImGui::TextDisabled("Pool fill -- 100%% means new spawns drop.");
        }
        end_section(sec_dg);
    }

    // ---- Sprite "Set Native Size" helpers --------------------------
    // Pull the slice rect + texture handle off the Sprite, look up the
    // asset's PPU through sprite_registry -> AssetRegistry, and return
    // the target Transform2D.scale that would render 1:1 at that PPU.
    // Returns {0,0} when we can't resolve any of those.
    struct NativeSizeRes {
        bool  valid;
        float scale_x;
        float scale_y;
    };
    NativeSizeRes compute_native_scale(EditorState& s, void* data,
                                        const ecs::ComponentType& desc) {
        NativeSizeRes out{ false, 0.0f, 0.0f };
        const ecs::FieldInfo* ftex = nullptr;
        const ecs::FieldInfo* fsw  = nullptr;
        const ecs::FieldInfo* fsh  = nullptr;
        for (u32 i = 0; i < desc.field_count; ++i) {
            const auto& f = desc.fields[i];
            if (!f.name) continue;
            if      (std::strcmp(f.name, "texture") == 0) ftex = &f;
            else if (std::strcmp(f.name, "slice_w") == 0) fsw  = &f;
            else if (std::strcmp(f.name, "slice_h") == 0) fsh  = &f;
        }
        if (!ftex || !fsw || !fsh) return out;
        const u32 tex_idx = *reinterpret_cast<u32*>(static_cast<u8*>(data) + ftex->offset);
        const i32 sw      = *reinterpret_cast<i32*>(static_cast<u8*>(data) + fsw->offset);
        const i32 sh      = *reinterpret_cast<i32*>(static_cast<u8*>(data) + fsh->offset);
        if (tex_idx == 0) return out;

        const std::string* abs_path = nullptr;
        for (const auto& sa : s.sprite_registry) {
            if (sa.texture_handle == tex_idx) { abs_path = &sa.abs_path; break; }
        }
        if (!abs_path) return out;
        const auto& reg = AssetRegistry::instance();
        const Engine::Guid g = reg.guid_for_any_path(abs_path->c_str());
        const auto sett = g.is_null() ? SpriteAssetSettings{}
                                       : reg.sprite_settings_for(g);
        const float ppu = sett.pixels_per_unit > 0.0f
                          ? sett.pixels_per_unit : 100.0f;
        int px_w = sw, px_h = sh;
        if (px_w <= 0 || px_h <= 0) {
            auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
            if (r && r->get_texture_size) {
                int tw = 0, th = 0;
                r->get_texture_size(r, tex_idx, &tw, &th);
                px_w = tw; px_h = th;
            }
        }
        if (px_w <= 0 || px_h <= 0) return out;
        out.valid   = true;
        out.scale_x = (float)px_w / ppu;
        out.scale_y = (float)px_h / ppu;
        return out;
    }

    // True when Transform2D.scale * Sprite.size already matches the
    // native scale (within a small epsilon). Lets the inspector hide
    // the Set Native Size button when pressing it would do nothing.
    bool sprite_is_at_native_size(EditorState& s, ecs::Entity e,
                                   void* component_data,
                                   const ecs::ComponentType& desc) {
        const NativeSizeRes ns = compute_native_scale(s, component_data, desc);
        if (!ns.valid) return true;     // can't resolve -> hide the button
        if (!s.world) return true;
        const auto xform_id = s.world->find_component_id("Transform2D");
        if (!xform_id) return true;
        void* xd = s.world->get_component(e, xform_id);
        if (!xd) return true;
        const auto* xdesc = s.world->get_component_type(xform_id);
        if (!xdesc) return true;
        float sx = 1.0f, sy = 1.0f;
        for (u32 i = 0; i < xdesc->field_count; ++i) {
            const auto& f = xdesc->fields[i];
            if (f.name && std::strcmp(f.name, "scale") == 0) {
                const float* sc = reinterpret_cast<const float*>(
                    static_cast<u8*>(xd) + f.offset);
                sx = sc[0]; sy = sc[1];
                break;
            }
        }
        // Also factor in Sprite.size.
        float spsx = 1.0f, spsy = 1.0f;
        for (u32 i = 0; i < desc.field_count; ++i) {
            const auto& f = desc.fields[i];
            if (f.name && std::strcmp(f.name, "size") == 0) {
                const float* sz = reinterpret_cast<const float*>(
                    static_cast<u8*>(component_data) + f.offset);
                spsx = sz[0]; spsy = sz[1];
                break;
            }
        }
        const float effx = sx * spsx;
        const float effy = sy * spsy;
        const float eps  = 1e-3f;
        return std::fabs(effx - ns.scale_x) < eps &&
               std::fabs(effy - ns.scale_y) < eps;
    }

    // Apply: write Sprite.size = (1,1) and Transform2D.scale to the
    // computed native scale. Single undo entry. Toast on success/miss.
    void apply_set_native_size(EditorState& s, ecs::Entity e,
                                void* data,
                                const ecs::ComponentType& desc) {
        const NativeSizeRes ns = compute_native_scale(s, data, desc);
        if (!ns.valid) {
            show_toast(s, "Set Native Size: assign a texture first",
                       3.0f, true);
            return;
        }
        const ecs::FieldInfo* fsz = nullptr;
        for (u32 i = 0; i < desc.field_count; ++i) {
            const auto& f = desc.fields[i];
            if (f.name && std::strcmp(f.name, "size") == 0) { fsz = &f; break; }
        }
        undo_begin(s);
        if (fsz) {
            float* sz = reinterpret_cast<float*>(static_cast<u8*>(data) + fsz->offset);
            sz[0] = 1.0f; sz[1] = 1.0f;
        }
        if (s.world) {
            const auto xform_id = s.world->find_component_id("Transform2D");
            if (xform_id) {
                void* xd = s.world->get_component(e, xform_id);
                const auto* xdesc = s.world->get_component_type(xform_id);
                if (xd && xdesc) {
                    for (u32 i = 0; i < xdesc->field_count; ++i) {
                        const auto& f = xdesc->fields[i];
                        if (f.name && std::strcmp(f.name, "scale") == 0) {
                            float* sc = reinterpret_cast<float*>(
                                static_cast<u8*>(xd) + f.offset);
                            sc[0] = ns.scale_x;
                            sc[1] = ns.scale_y;
                            break;
                        }
                    }
                }
            }
        }
        undo_commit(s, "Set native size");
    }
}

// Forward decl; defined below the entity-mode draw.
static void draw_asset_settings(EditorState& s);

void draw_inspector_panel(EditorState& s) {
    if (!s.show_inspector) return;

    if (!ImGui::Begin("Inspector", &s.show_inspector)) { ImGui::End(); return; }

    if (!s.world || !s.world->is_alive(s.selected_entity)) {
        // No entity selected. If the user picked an asset in the
        // Asset Browser, fall back to the asset-settings view --
        // sprite filter / PPU / pivot / etc. Otherwise show the
        // empty-selection hint.
        if (!s.selected_asset_guid.is_null()) {
            draw_asset_settings(s);
        } else {
            ImGui::TextDisabled("No entity selected.");
        }
        ImGui::End();
        return;
    }
    // Entity is selected -- clear any active asset selection so the
    // two views are mutually exclusive.
    if (!s.selected_asset_guid.is_null()) {
        s.selected_asset_guid = Engine::Guid{};
        s.selected_asset_path.clear();
    }
    // Stash for the leaf widgets (entity-ref name lookup, asset-ref guid
    // resolve). Cleared on the early-out branches above; cleared at the
    // bottom of this function on the normal path.
    tls_inspector_state = &s;

    const auto e = s.selected_entity;
    ImGui::Text("Entity { idx = %u, gen = %u }", e.index, e.generation);
    ImGui::Separator();

    // Transform2D pinned to the top — it's the spatial anchor for every
    // entity, so users always look for it in the same place. Drawn first
    // (if present), then the iteration below skips it to avoid drawing it
    // twice.
    const auto transform_id = s.world->find_component_id("Transform2D");
    if (transform_id && s.world->has_component(e, transform_id)) {
        draw_component(s, e, transform_id);
    }

    // Everything else, in registration (id) order, except Transform2D
    // (already drawn) and the hierarchy primitives (managed via API; the
    // Hierarchy panel handles them).
    s.world->iterate_component_types(
        [&](ecs::ComponentId id, const ecs::ComponentType& t) {
            if (id == transform_id) return;
            if (std::strcmp(t.name, "Parent")      == 0) return;
            if (std::strcmp(t.name, "FirstChild")  == 0) return;
            if (std::strcmp(t.name, "NextSibling") == 0) return;
            if (s.world->has_component(e, id)) {
                draw_component(s, e, id);
            }
        });

    // Unknown components: types referenced in the saved world that
    // aren't registered yet (project DLL not loaded, type renamed,
    // commented out). Their JSON data is preserved on the entity so
    // a save round-trip doesn't silently drop the user's work. Show
    // a disabled header per blob with a Remove button so the user
    // can drop them deliberately when they're sure the type is gone.
    {
        struct Pending {
            ecs::Entity e;
            std::string type_name;
        };
        static thread_local std::vector<Pending> tls_unknown_remove;
        tls_unknown_remove.clear();

        s.world->iterate_unknown_components(e,
            +[](ecs::Entity ent, const char* type_name,
                const char* /*data_json*/, void* /*user*/) {
                ImGui::PushID(type_name);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.40f, 1.0f),
                    "?  %s", type_name ? type_name : "<unnamed>");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "This component type isn't registered in the current "
                        "build. Its data is preserved on disk and will reload "
                        "automatically once the type comes back. Use Remove "
                        "if you've intentionally deleted the type.");
                ImGui::SameLine();
                const float btn_w = 90.0f;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - btn_w);
                if (ImGui::Button("Remove", ImVec2(btn_w, 0))) {
                    tls_unknown_remove.push_back(
                        Pending{ ent, type_name ? type_name : "" });
                }
                ImGui::TextDisabled("(data hidden -- recompile to edit)");
                ImGui::PopID();
            }, nullptr);

        for (const auto& p : tls_unknown_remove) {
            s.world->remove_unknown_component(p.e, p.type_name.c_str());
            s.world_dirty = true;
        }
    }

    draw_add_component(s, e);

    // Apply queued removals after the iteration so we don't disturb the
    // type-registry walker. Cleared every frame.
    if (!s.pending_component_removals.empty()) {
        for (const auto& r : s.pending_component_removals) {
            if (s.world->is_alive(r.e))
                s.world->remove_component(r.e, r.id);
        }
        s.pending_component_removals.clear();
        s.world_dirty = true;
    }

    tls_inspector_state = nullptr;
    ImGui::End();
}

// ----------------------------------------------------------------------------
// Asset settings view -- shown in the Inspector when the user has an asset
// selected (no entity). Only Texture assets currently expose editable
// settings (filter, wrap, PPU, pivot). Other kinds show summary info.
//
// Layout:
//   header (filename / path)
//   preview (checker background + scaled image, with size readout)
//   settings rows (label | control | help icon, in a 3-col table)
//   pivot picker (3x3 preset buttons + DragFloat2 fine control)
//   clear-selection button
// ----------------------------------------------------------------------------

namespace {

// Hover-help marker: the dim "(?)" text users can mouse to read a
// one-line caption without filling the panel with explanation prose.
void help_marker(const char* tip) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Render a checkerboard background in the rect (top-left, bot-right),
// then draw `tex` on top fitted aspect-correctly. Used for the sprite
// preview so transparent regions read as transparency, not solid bg.
void draw_preview_with_checker(ImDrawList* dl,
                                ImVec2 tl, ImVec2 br,
                                u32 tex, int tex_w, int tex_h) {
    const ImU32 dark  = IM_COL32(40, 40, 40, 255);
    const ImU32 light = IM_COL32(60, 60, 60, 255);
    const float box   = 8.0f;
    dl->AddRectFilled(tl, br, dark);
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
    if (tex == 0 || tex_w <= 0 || tex_h <= 0) return;

    // Fit the texture into the rect, preserving aspect ratio. Centre it.
    const float rw = br.x - tl.x;
    const float rh = br.y - tl.y;
    const float aspect_t = (float)tex_w / (float)tex_h;
    const float aspect_r = rw / rh;
    float draw_w, draw_h;
    if (aspect_t > aspect_r) { draw_w = rw; draw_h = rw / aspect_t; }
    else                     { draw_h = rh; draw_w = rh * aspect_t; }
    ImVec2 ia{ tl.x + (rw - draw_w) * 0.5f,
               tl.y + (rh - draw_h) * 0.5f };
    ImVec2 ib{ ia.x + draw_w, ia.y + draw_h };
    dl->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                 ia, ib);
    // Subtle outline so the image edges read against the checker.
    dl->AddRect(ia, ib, IM_COL32(0, 0, 0, 90));
}

// Pivot 3x3 preset grid. Returns true if the user clicked a preset.
// Sets `out_x` / `out_y` to the chosen anchor (0/0.5/1 each axis).
bool draw_pivot_grid(float current_x, float current_y,
                      float& out_x, float& out_y) {
    bool clicked = false;
    const ImVec2 cell{ 28.0f, 22.0f };
    const float xs[3] = { 0.0f, 0.5f, 1.0f };
    const float ys[3] = { 0.0f, 0.5f, 1.0f };
    const char* labels[3][3] = {
        { "TL", " T", "TR" },
        { " L", " C", " R" },
        { "BL", " B", "BR" }
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (col > 0) ImGui::SameLine();
            const float px = xs[col];
            const float py = ys[row];
            const bool active = (std::fabs(current_x - px) < 0.001f &&
                                  std::fabs(current_y - py) < 0.001f);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    IM_COL32(255, 165, 60, 200));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    IM_COL32(255, 180, 80, 220));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    IM_COL32(255, 150, 40, 255));
            }
            char id[16];
            std::snprintf(id, sizeof(id), "%s##pv%d%d", labels[row][col], row, col);
            if (ImGui::Button(id, cell)) {
                out_x = px;
                out_y = py;
                clicked = true;
            }
            if (active) ImGui::PopStyleColor(3);
        }
    }
    ImGui::PopStyleVar();
    return clicked;
}

// ============================================================================
// AudioSource sectioned editor.
//
// Layout (top -> bottom):
//   Cue slot row     -- AudioCueRef widget + preview + "Open Cue Editor"
//   Transport row    -- Play / Stop buttons + live status badge
//   Mix              -- Volume / Pitch / Pan (Pan only when 2D)
//   Spatial          -- 3D toggle, Min/Max distance, Spatial blend
//   Bus              -- SFX / Music / UI / Voice combo
//   Auto / Loop      -- compact checkboxes
//
// Edits write straight to component memory (the audio system reads it
// next tick). Volume / pitch / distances run through DragFloat with
// hard clamps so the user can't slide into invalid territory.
// ============================================================================
void draw_audio_source_editor(EditorState& s, ecs::Entity e,
                                void* component_data) {
    using namespace Engine::components;
    auto* src = static_cast<AudioSource*>(component_data);
    if (!src) return;

    const float row_w = ImGui::GetContentRegionAvail().x;
    const ImVec4 fc = col_for_component("AudioSource");

    // ---- Cue slot ----------------------------------------------------------
    inspector_section("Cue", fc);
    {
        const Guid cue_g = src->cue.guid;
        const char* path = cue_g.is_null()
            ? nullptr : AssetRegistry::instance().path_for(cue_g);
        // Compact slot button + ▶ preview + "Edit" jump button.
        const float right_w = 28.0f + 56.0f + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
        const float slot_w  = (row_w * 0.72f) - right_w - 50.0f;
        std::string label = cue_g.is_null()
            ? std::string("<no cue>")
            : (path ? std::string(path) : std::string("<missing>"));
        ImGui::Button((label + "##cue_slot").c_str(),
                      ImVec2(slot_w > 80 ? slot_w : 80, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl =
                    ImGui::AcceptDragDropPayload("ZUES_ASSET_PATH")) {
                const char* abs = static_cast<const char*>(pl->Data);
                if (abs) {
                    const Guid ag = AssetRegistry::instance().guid_for_any_path(abs);
                    const AssetKind k = asset_kind_from_extension(abs);
                    if (!ag.is_null() && k == AssetKind::AudioCue) {
                        src->cue.guid = ag;
                    } else if (!ag.is_null() && k == AssetKind::Audio) {
                        // Auto-resolve raw audio drops -> matching auto-cue.
                        const auto* cue =
                            AssetRegistry::instance().find_auto_cue_for(ag);
                        if (cue) src->cue.guid = cue->guid;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Clear")) { src->cue.guid = Guid{}; }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        const bool has_cue = !cue_g.is_null();
        ImGui::BeginDisabled(!has_cue);
        if (ImGui::Button("Play##cue_prev", ImVec2(44, 0))) {
            Engine::host::audio_api::preview_cue(cue_g);
        }
        if (ImGui::IsItemHovered() && has_cue)
            ImGui::SetTooltip("Preview the cue (one-shot)");
        ImGui::SameLine();
        if (ImGui::Button("Edit##cue_edit", ImVec2(56, 0))) {
            open_cue_editor_for_cue(s, cue_g);
        }
        if (ImGui::IsItemHovered() && has_cue)
            ImGui::SetTooltip("Open the cue in the AudioCue editor");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Transport ---------------------------------------------------------
    ImGui::Spacing();
    inspector_section("Transport", fc);
    {
        const bool live_playing =
            Engine::host::audio_api::source_is_playing(e) != 0;
        if (live_playing)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.30f, 0.55f, 0.30f, 1.0f));
        if (ImGui::Button(live_playing ? "Restart##audio_play" : "Play##audio_play",
                          ImVec2(96, 0))) {
            Engine::host::audio_api::source_play(e);
            src->playing = 1;
        }
        if (live_playing) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(!live_playing);
        if (ImGui::Button("Stop##audio_stop", ImVec2(96, 0))) {
            Engine::host::audio_api::source_stop(e);
            src->playing = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(live_playing ? col_accent_g() : col_accent_r(),
            "%s", live_playing ? "PLAYING" : "stopped");
    }

    // ---- Auto / Loop -------------------------------------------------------
    {
        bool autoplay = src->autoplay != 0;
        if (ImGui::Checkbox("Auto-play on load", &autoplay))
            src->autoplay = autoplay ? 1 : 0;
        ImGui::SameLine();
        ImGui::TextDisabled("(loop comes from the cue)");
    }

    inspector_section("Mix", fc);
    if (begin_inspector_table("##audio_mix")) {
        inspector_row_label("Volume");
        ImGui::SliderFloat("##vol", &src->volume, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Per-source multiplier on top of cue.volume.");

        inspector_row_label("Pitch");
        ImGui::SliderFloat("##pit", &src->pitch, 0.1f, 4.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Per-source multiplier on top of cue.pitch.");

        if (!src->is_3d) {
            inspector_row_label("Pan");
            ImGui::SliderFloat("##pan", &src->pan, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stereo pan (2D only). -1 = left, +1 = right.");
        }
        end_inspector_table();
    }

    inspector_section("Spatial", fc);
    {
        bool is3d = src->is_3d != 0;
        if (ImGui::Checkbox("3D (distance attenuated)", &is3d))
            src->is_3d = is3d ? 1 : 0;
        if (is3d) {
            ImGui::TextDisabled("Inverse falloff between min and max distance.");
            if (begin_inspector_table("##audio_spatial")) {
                inspector_row_label("Min distance");
                ImGui::DragFloat("##mind", &src->min_distance,
                                  0.05f, 0.01f, 1000.0f, "%.2f");
                inspector_row_label("Max distance");
                ImGui::DragFloat("##maxd", &src->max_distance,
                                  0.1f, src->min_distance + 0.01f,
                                  10000.0f, "%.2f");
                if (src->max_distance < src->min_distance)
                    src->max_distance = src->min_distance + 0.01f;
                inspector_row_label("Blend (2D < > 3D)");
                ImGui::SliderFloat("##blend", &src->spatial_blend,
                                    0.0f, 1.0f, "%.2f");
                end_inspector_table();
            }
        }
    }

    inspector_section("Bus", fc);
    {
        const char* buses[] = { "SFX", "Music", "UI", "Voice" };
        int idx = src->bus;
        if (idx < 0 || idx > 3) idx = 0;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("##bus", &idx, buses, IM_ARRAYSIZE(buses)))
            src->bus = idx;
        ImGui::SameLine();
        ImGui::TextDisabled("(routes to the matching mixer slider)");
    }
}

// ============================================================================
// RigidBody sectioned editor.
// Body / Material / Damping / Misc -- a saved Camera2D character can read
// at a glance "this is a Dynamic body that bounces" instead of squinting
// at twelve unrelated DragFloat rows.
// ============================================================================
void draw_rigidbody_editor(EditorState& /*s*/, ecs::Entity /*e*/,
                            void* component_data) {
    using namespace Engine::components;
    auto* rb = static_cast<RigidBody*>(component_data);
    if (!rb) return;

    const ImVec4 fc = col_for_component("RigidBody");
    inspector_section("Body", fc);
    if (begin_inspector_table("##rb_body")) {
        inspector_row_label("Type");
        static const char* labels[] = {"Static", "Kinematic", "Dynamic"};
        int idx = (rb->body_type >= 0 && rb->body_type <= 2) ? rb->body_type : 2;
        if (ImGui::Combo("##rb_type", &idx, labels, IM_ARRAYSIZE(labels)))
            rb->body_type = idx;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Static    -- never moves; world geometry.\n"
                "Kinematic -- moved by code; pushes dynamics.\n"
                "Dynamic   -- driven by forces / gravity.");

        inspector_row_label("Mass");
        ImGui::DragFloat("##rb_mass", &rb->mass, 0.01f, 0.0f, 10000.0f, "%.3f kg");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ignored on Static; auto-computed for Kinematic.");
        end_inspector_table();
    }

    inspector_section("Material", fc);
    if (begin_inspector_table("##rb_mat")) {
        inspector_row_label("Density");
        ImGui::DragFloat("##rb_dens", &rb->density, 0.01f, 0.0f, 1000.0f, "%.3f");
        inspector_row_label("Friction");
        ImGui::SliderFloat("##rb_fric", &rb->friction, 0.0f, 1.0f, "%.2f");
        inspector_row_label("Restitution");
        ImGui::SliderFloat("##rb_rest", &rb->restitution, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bounciness. 0 = absorb, 1 = perfect bounce.");
        end_inspector_table();
    }

    inspector_section("Damping", fc);
    if (begin_inspector_table("##rb_damp")) {
        inspector_row_label("Linear");
        ImGui::DragFloat("##rb_ld", &rb->linear_damping,  0.01f, 0.0f, 10.0f, "%.3f");
        inspector_row_label("Angular");
        ImGui::DragFloat("##rb_ad", &rb->angular_damping, 0.01f, 0.0f, 10.0f, "%.3f");
        end_inspector_table();
    }

    inspector_section("Misc", fc);
    if (begin_inspector_table("##rb_misc")) {
        inspector_row_label("Gravity scale");
        ImGui::DragFloat("##rb_gs", &rb->gravity_scale, 0.01f, -10.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = ignore world gravity. Negative = inverted.");

        inspector_row_label("Fixed rotation");
        bool fr = rb->fixed_rotation != 0;
        if (ImGui::Checkbox("##rb_fr", &fr)) rb->fixed_rotation = fr ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Freeze rotation. Useful for the player capsule.");

        inspector_row_label("Bullet");
        bool bu = rb->is_bullet != 0;
        if (ImGui::Checkbox("##rb_bu", &bu)) rb->is_bullet = bu ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Continuous-collision check.\n"
                              "Use for fast-moving small bodies.");
        end_inspector_table();
    }
}

void draw_box_collider_editor(EditorState& /*s*/, ecs::Entity /*e*/,
                                void* component_data) {
    using namespace Engine::components;
    auto* c = static_cast<BoxCollider*>(component_data);
    if (!c) return;
    const ImVec4 fc = col_for_component("BoxCollider");
    inspector_section("Shape", fc);
    if (begin_inspector_table("##bc_shape")) {
        inspector_row_label("Half extents");
        ImGui::DragFloat2("##bc_he", &c->half_extents.x, 0.01f, 0.001f, 1000.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Half-width / half-height. Total size = 2x.");
        inspector_row_label("Offset");
        ImGui::DragFloat2("##bc_off", &c->offset.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
        end_inspector_table();
    }
    inspector_section("Behaviour", fc);
    if (begin_inspector_table("##bc_beh")) {
        inspector_row_label("Trigger");
        bool t = c->is_sensor != 0;
        if (ImGui::Checkbox("##bc_sn", &t)) c->is_sensor = t ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Sensor (no contact response).\n"
                "Fires OnTriggerEnter / OnTriggerExit instead.");
        inspector_row_label("Edit in scene");
        bool ed = c->edit_in_scene != 0;
        if (ImGui::Checkbox("##bc_ed", &ed)) c->edit_in_scene = ed ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show drag handles in the scene viewport.");
        end_inspector_table();
    }
    if (c->is_sensor) {
        ImGui::TextColored(col_accent_w(),
            "TRIGGER -- contacts fire OnTriggerEnter/Exit.");
    }
}

void draw_circle_collider_editor(EditorState& /*s*/, ecs::Entity /*e*/,
                                   void* component_data) {
    using namespace Engine::components;
    auto* c = static_cast<CircleCollider*>(component_data);
    if (!c) return;
    const ImVec4 fc = col_for_component("CircleCollider");
    inspector_section("Shape", fc);
    if (begin_inspector_table("##cc_shape")) {
        inspector_row_label("Radius");
        ImGui::DragFloat("##cc_r", &c->radius, 0.01f, 0.001f, 1000.0f, "%.3f");
        inspector_row_label("Offset");
        ImGui::DragFloat2("##cc_off", &c->offset.x, 0.01f, -1000.0f, 1000.0f, "%.3f");
        end_inspector_table();
    }
    inspector_section("Behaviour", fc);
    if (begin_inspector_table("##cc_beh")) {
        inspector_row_label("Trigger");
        bool t = c->is_sensor != 0;
        if (ImGui::Checkbox("##cc_sn", &t)) c->is_sensor = t ? 1 : 0;
        inspector_row_label("Edit in scene");
        bool ed = c->edit_in_scene != 0;
        if (ImGui::Checkbox("##cc_ed", &ed)) c->edit_in_scene = ed ? 1 : 0;
        end_inspector_table();
    }
    if (c->is_sensor) {
        ImGui::TextColored(col_accent_w(),
            "TRIGGER -- contacts fire OnTriggerEnter/Exit.");
    }
}

void draw_camera2d_editor(EditorState& /*s*/, ecs::Entity /*e*/,
                           void* component_data) {
    using namespace Engine::components;
    auto* cam = static_cast<Camera2D*>(component_data);
    if (!cam) return;
    const ImVec4 fc = col_for_component("Camera2D");
    inspector_section("Camera", fc);
    if (begin_inspector_table("##cam_main")) {
        inspector_row_label("Ortho size");
        ImGui::DragFloat("##cam_sz", &cam->ortho_size, 0.05f, 0.1f, 10000.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vertical world units visible at zoom 1.");
        inspector_row_label("Active");
        bool act = cam->is_active;
        if (ImGui::Checkbox("##cam_act", &act)) cam->is_active = act;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Only one Camera2D's view is rendered.\n"
                "Multiple actives -> first wins.");
        end_inspector_table();
    }
    inspector_section("Sort", fc);
    if (begin_inspector_table("##cam_sort")) {
        inspector_row_label("Mode");
        static const char* sort_names[] = {
            "OrderOnly", "YDescending", "YAscending"
        };
        int idx = static_cast<int>(cam->sort_mode);
        if (ImGui::Combo("##cam_sm", &idx, sort_names, IM_ARRAYSIZE(sort_names)))
            cam->sort_mode = static_cast<SortMode>(idx);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "OrderOnly   -- pure layer/order (UI, side-scrollers).\n"
                "YDescending -- top-down 2.5D, lower y draws first.\n"
                "YAscending  -- isometric, lower y is the front.");
        end_inspector_table();
    }
}

}  // namespace

static void draw_asset_settings(EditorState& s) {
    auto& reg = AssetRegistry::instance();
    const AssetEntry* entry = reg.find(s.selected_asset_guid);
    if (!entry) {
        ImGui::TextDisabled("(asset no longer in registry)");
        if (ImGui::SmallButton("Clear selection")) {
            s.selected_asset_guid = Guid{};
            s.selected_asset_path.clear();
        }
        return;
    }

    // ---- Header: filename big, full path muted ----------------------------
    {
        const std::string& p = entry->path;
        std::string filename = p;
        const auto slash = p.find_last_of('/');
        if (slash != std::string::npos) filename = p.substr(slash + 1);
        ImGui::PushFont(ImGui::GetFont());
        ImGui::Text("%s", filename.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", p.c_str());
    }

    // ---- Preview pane (only for textures) ---------------------------------
    auto* r = static_cast<IRenderer_2D_v1*>(s.renderer);
    u32 tex = 0;
    int tex_w = 0, tex_h = 0;
    if (entry->kind == AssetKind::Texture && r && !s.project_dir.empty()) {
        const std::string abs =
            s.project_dir + "/" + s.assets_root_relative + "/" + entry->path;
        auto it = s.asset_thumb_cache.find(abs);
        if (it != s.asset_thumb_cache.end()) tex = it->second;
        if (tex && r->get_texture_size) r->get_texture_size(r, tex, &tex_w, &tex_h);

        // Preview area: ~192px tall, full panel width.
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float prev_h  = 192.0f;
        const ImVec2 tl = ImGui::GetCursorScreenPos();
        const ImVec2 br{ tl.x + avail_w, tl.y + prev_h };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw_preview_with_checker(dl, tl, br, tex, tex_w, tex_h);

        // Pivot indicator: small dot on the preview at the pivot point,
        // mapped through the same fit math draw_preview uses.
        if (tex_w > 0 && tex_h > 0) {
            const float aspect_t = (float)tex_w / (float)tex_h;
            const float aspect_r = avail_w / prev_h;
            float draw_w, draw_h;
            if (aspect_t > aspect_r) { draw_w = avail_w; draw_h = avail_w / aspect_t; }
            else                     { draw_h = prev_h;  draw_w = prev_h * aspect_t; }
            const float ix = tl.x + (avail_w - draw_w) * 0.5f;
            const float iy = tl.y + (prev_h  - draw_h) * 0.5f;
            const float px = ix + entry->sprite.pivot_x * draw_w;
            const float py = iy + entry->sprite.pivot_y * draw_h;
            dl->AddCircleFilled(ImVec2(px, py), 4.0f,
                                IM_COL32(255, 165, 60, 255));
            dl->AddCircle(ImVec2(px, py), 4.0f,
                          IM_COL32(0, 0, 0, 200), 0, 1.5f);
        }
        ImGui::Dummy(ImVec2(avail_w, prev_h));

        // One-line readout under the preview: WxH, kind.
        if (tex_w > 0 && tex_h > 0)
            ImGui::TextDisabled("%d x %d px   %s",
                tex_w, tex_h, asset_kind_name(entry->kind));
        else
            ImGui::TextDisabled("%s", asset_kind_name(entry->kind));
    } else {
        ImGui::TextDisabled("kind: %s", asset_kind_name(entry->kind));
        ImGui::TextDisabled("guid: %s", guid_to_hex(entry->guid).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (entry->kind == AssetKind::Audio) {
        // Quick audition: play the clip through the running audio
        // backend as a 2D one-shot. Resolves the entry's project-
        // relative path against the current project + assets root.
        ImGui::TextDisabled("Audio preview");
        ImGui::Separator();
        const std::string abs =
            s.project_dir + "/" + s.assets_root_relative + "/" + entry->path;
        if (ImGui::Button("\xE2\x96\xB6 Play", ImVec2(80, 0))) {
            Engine::host::audio_api::preview_path(abs.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", entry->path.c_str());
        ImGui::Spacing();
        if (ImGui::SmallButton("Clear selection")) {
            s.selected_asset_guid = Guid{};
            s.selected_asset_path.clear();
        }
        return;
    }

    if (entry->kind != AssetKind::Texture) {
        ImGui::TextDisabled("No editable settings for this asset kind.");
        if (ImGui::SmallButton("Clear selection")) {
            s.selected_asset_guid = Guid{};
            s.selected_asset_path.clear();
        }
        return;
    }

    // ---- Settings table: label | control | (?) help marker ----------------
    SpriteAssetSettings cur = entry->sprite;
    bool changed = false;

    if (ImGui::BeginTable("##sprite_settings", 2,
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);

        // Pixels per unit.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Pixels per unit");
        help_marker("How many texture pixels equal one world unit. "
                    "100 means a 100-px sprite is 1 unit tall. Lower "
                    "values make sprites look bigger in the scene.");
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-FLT_MIN);
        {
            float ppu = cur.pixels_per_unit;
            if (ImGui::DragFloat("##ppu", &ppu, 1.0f, 1.0f, 4096.0f, "%.1f")) {
                cur.pixels_per_unit = ppu;
                changed = true;
            }
        }
        ImGui::PopItemWidth();

        // Filter.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Filter");
        help_marker("How the GPU samples between texture pixels.\n"
                    "  Linear  -- smooth blur (good for high-res art).\n"
                    "  Nearest -- crisp blocks (use for pixel art).");
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-FLT_MIN);
        {
            const char* names[] = { "Linear", "Nearest" };
            int idx = (cur.filter == SpriteFilter::Nearest) ? 1 : 0;
            if (ImGui::Combo("##filter", &idx, names, IM_ARRAYSIZE(names))) {
                cur.filter = (idx == 1) ? SpriteFilter::Nearest
                                        : SpriteFilter::Linear;
                changed = true;
            }
        }
        ImGui::PopItemWidth();

        // Wrap.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Wrap");
        help_marker("How the texture handles UVs outside 0..1 (only "
                    "matters when sprites tile or use sub-rects).\n"
                    "  Clamp  -- repeat the edge pixel.\n"
                    "  Repeat -- tile the texture forever.\n"
                    "  Mirror -- tile, alternating flipped copies.");
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-FLT_MIN);
        {
            const char* names[] = { "Clamp", "Repeat", "Mirror" };
            int idx = static_cast<int>(cur.wrap);
            if (idx < 0 || idx > 2) idx = 0;
            if (ImGui::Combo("##wrap", &idx, names, IM_ARRAYSIZE(names))) {
                cur.wrap = static_cast<SpriteWrap>(idx);
                changed = true;
            }
        }
        ImGui::PopItemWidth();

        // Pivot: preset grid + numeric DragFloat2 for fine control.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Pivot");
        help_marker("Where the sprite is anchored within its bounding "
                    "box. (0,0) = top-left, (1,1) = bottom-right, "
                    "(0.5,0.5) = center. Click a preset or drag the "
                    "numbers below.");
        ImGui::TableNextColumn();
        {
            if (draw_pivot_grid(cur.pivot_x, cur.pivot_y,
                                cur.pivot_x, cur.pivot_y)) {
                changed = true;
            }
            float pv[2] = { cur.pivot_x, cur.pivot_y };
            ImGui::PushItemWidth(-FLT_MIN);
            if (ImGui::DragFloat2("##pivot_xy", pv, 0.01f,
                                   0.0f, 1.0f, "%.3f")) {
                cur.pivot_x = pv[0];
                cur.pivot_y = pv[1];
                changed = true;
            }
            ImGui::PopItemWidth();
        }

        ImGui::EndTable();
    }

    if (changed) {
        // Persist + live-apply filter / wrap on the loaded texture.
        if (!reg.update_sprite_settings(s.selected_asset_guid, cur)) {
            show_toast(s, "Sprite settings: write to .meta failed",
                       3.0f, true);
        }
        if (r && tex != 0) {
            if (r->set_texture_filter)
                r->set_texture_filter(r, tex,
                    cur.filter == SpriteFilter::Nearest ? 1 : 0);
            if (r->set_texture_wrap)
                r->set_texture_wrap(r, tex,
                    static_cast<int>(cur.wrap));
        }
    }

    // ---- Slice picker -----------------------------------------------------
    // When the texture has slices defined in its .meta, list each one
    // as a thumbnail (cropped to the slice rect via UVs) the user can
    // drag onto a Sprite component's `texture` slot. Dropping bakes
    // BOTH the texture handle AND the slice rect (plus 9-slice borders
    // and PPU) into the Sprite in one motion -- the previous workflow
    // forced "drag texture, then click slice combo, then maybe set
    // borders" and lost the visual link to which slice the user
    // actually wanted.
    if (!entry->sprite.slices.empty() && tex != 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("Slices (%zu) - drag onto a Sprite",
            entry->sprite.slices.size());
        ImGui::Separator();

        const float thumb = 56.0f;
        const float pad   = 6.0f;
        const float full_w = ImGui::GetContentRegionAvail().x;
        const int   per_row = std::max(1, (int)((full_w + pad) / (thumb + pad)));
        int col = 0;
        for (size_t i = 0; i < entry->sprite.slices.size(); ++i) {
            const auto& sl = entry->sprite.slices[i];
            ImGui::PushID(static_cast<int>(i));

            const float u0 = (tex_w > 0) ? (float)sl.x        / (float)tex_w : 0.0f;
            const float v0 = (tex_h > 0) ? (float)sl.y        / (float)tex_h : 0.0f;
            const float u1 = (tex_w > 0) ? (float)(sl.x+sl.w) / (float)tex_w : 1.0f;
            const float v1 = (tex_h > 0) ? (float)(sl.y+sl.h) / (float)tex_h : 1.0f;

            ImGui::ImageButton("##slice_thumb",
                static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                ImVec2(thumb, thumb),
                ImVec2(u0, v0), ImVec2(u1, v1));

            // Hover tooltip with the slice's name and rect.
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%dx%d px",
                    sl.name.empty() ? "(unnamed)" : sl.name.c_str(),
                    sl.w, sl.h);
            }

            // Drag source: payload carries texture guid + slice index +
            // the resolved rect / 9-slice / scale-mode bytes so the
            // drop target doesn't have to look anything up.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                struct SlicePayload {
                    Guid  texture_guid;
                    int   slice_index;
                    int   slice_x, slice_y, slice_w, slice_h;
                    int   border_l, border_r, border_t, border_b;
                    int   scale_mode, center_mode;
                    float texture_ppu;
                    float pivot_x, pivot_y;
                };
                SlicePayload pl{};
                pl.texture_guid = entry->guid;
                pl.slice_index  = (int)i;
                pl.slice_x = sl.x; pl.slice_y = sl.y;
                pl.slice_w = sl.w; pl.slice_h = sl.h;
                pl.border_l = sl.border_l; pl.border_r = sl.border_r;
                pl.border_t = sl.border_t; pl.border_b = sl.border_b;
                pl.scale_mode  = static_cast<int>(sl.scale_mode);
                pl.center_mode = static_cast<int>(sl.center_mode);
                pl.texture_ppu = entry->sprite.pixels_per_unit;
                pl.pivot_x = sl.pivot_x;
                pl.pivot_y = sl.pivot_y;
                ImGui::SetDragDropPayload("ZUES_SPRITE_SLICE",
                    &pl, sizeof(pl));
                // Drag preview: the slice thumbnail + a one-line label.
                ImGui::Image(
                    static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex)),
                    ImVec2(48, 48),
                    ImVec2(u0, v0), ImVec2(u1, v1));
                ImGui::SameLine();
                ImGui::Text("%s",
                    sl.name.empty() ? "(unnamed)" : sl.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Layout: pack `per_row` thumbs per visual row.
            if (++col < per_row) ImGui::SameLine();
            else                  col = 0;
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::SmallButton("Clear selection (Esc)")) {
        s.selected_asset_guid = Guid{};
        s.selected_asset_path.clear();
    }
    // Esc anywhere clears asset selection while the inspector has focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        s.selected_asset_guid = Guid{};
        s.selected_asset_path.clear();
    }
}

}  // namespace Engine::editor
