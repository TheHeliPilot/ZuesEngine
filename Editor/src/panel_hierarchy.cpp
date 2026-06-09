#include "editor.h"

#include <zues/components.h>
#include <zues/ecs/world.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace Engine::editor {

namespace {

// Returns true if `potential_ancestor` is `e` itself or one of its ancestors.
// Walks the parent chain — O(depth). Used for drag-drop cycle prevention.
bool is_ancestor_or_self(ecs::World* world, ecs::Entity potential_ancestor, ecs::Entity e) {
    ecs::Entity cur = e;
    while (!cur.is_null()) {
        if (cur == potential_ancestor) return true;
        cur = world->parent_of(cur);
    }
    return false;
}

const char* get_entity_name(ecs::World* world, ecs::Entity e) {
    const auto id = world->find_component_id("Name");
    if (id == ecs::INVALID_COMPONENT_ID) return nullptr;
    const auto* n = static_cast<const components::Name*>(world->get_component(e, id));
    return (n && n->value[0] != '\0') ? n->value : nullptr;
}

// "Main Camera" is editor-owned; refuse deletes to avoid nuking the demo world.
// Singletons (entities marked as the designated host of a [Singleton]
// component) are also protected — destroying them would just trigger an
// auto-respawn next frame, so block the Delete UI to make that explicit.
bool is_singleton_entity(ecs::World* world, ecs::Entity e) {
    bool match = false;
    world->iterate_component_types(
        [&](ecs::ComponentId id, const ecs::ComponentType&) {
            if (match) return;
            if (world->find_singleton(id) == e) match = true;
        });
    return match;
}

bool is_protected(ecs::World* world, ecs::Entity e) {
    const char* name = get_entity_name(world, e);
    if (name && std::strcmp(name, "Main Camera") == 0) return true;
    return is_singleton_entity(world, e);
}

void start_rename(EditorState& s, ecs::Entity e) {
    s.rename_target        = e;
    s.rename_focus_pending = true;
    const char* cur = get_entity_name(s.world, e);
    if (cur)
        std::strncpy(s.rename_buf, cur, sizeof(s.rename_buf) - 1);
    else
        s.rename_buf[0] = '\0';
    s.rename_buf[sizeof(s.rename_buf) - 1] = '\0';
}

void draw_entity_node(EditorState& s, ecs::Entity e) {
    if (!s.world->is_alive(e)) return;

    const u32   child_count = s.world->child_count(e);
    const char* name        = get_entity_name(s.world, e);

    // ---- Inline rename mode: replace tree row with an InputText ----------
    if (s.rename_target == e) {
        if (s.rename_focus_pending) {
            ImGui::SetKeyboardFocusHere();
            s.rename_focus_pending = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        const bool committed = ImGui::InputText(
            "##rename", s.rename_buf, sizeof(s.rename_buf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (committed) {
            if (s.rename_buf[0] != '\0') {
                s.pending_hierarchy_ops.push_back({
                    EditorState::PendingHierarchyOp::Kind::Rename,
                    e, {}, std::string(s.rename_buf)
                });
            }
            s.rename_target = {};
        } else if (ImGui::IsItemDeactivated()) {
            s.rename_target = {};   // Escape / click-away = cancel
        }
        return;
    }

    // ---- Build label & flags ---------------------------------------------
    char label[96];
    if (name)
        std::snprintf(label, sizeof(label), "%s##e%u_%u", name, e.index, e.generation);
    else
        std::snprintf(label, sizeof(label), "Entity %u##e%u_%u", e.index, e.index, e.generation);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;
    if (child_count == 0)    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool is_selected = (s.selected_entity == e);
    if (is_selected) flags |= ImGuiTreeNodeFlags_Selected;

    // Auto-scroll the selected row into view. Fires once per selection
    // change (tracked by selection_scroll_seen) so subsequent frames
    // don't keep re-snapping the scrollbar when the user is reading.
    if (is_selected && s.hierarchy_scroll_to_selected) {
        ImGui::SetScrollHereY(0.5f);
        s.hierarchy_scroll_to_selected = false;
    }

    // Pre-record the row's screen rect so we can paint a stronger
    // highlight than ImGui's default tint after the TreeNode draws.
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const bool open = ImGui::TreeNodeEx(label, flags);
    const ImVec2 row_max = ImGui::GetItemRectMax();
    if (is_selected) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Left-edge accent bar: bright amber so it reads against the
        // dark theme even when the row isn't hovered.
        dl->AddRectFilled(
            ImVec2(row_min.x, row_min.y),
            ImVec2(row_min.x + 3.0f, row_max.y),
            IM_COL32(255, 165, 60, 255));
    }

    // Component-family badges. A small coloured dot per family present
    // on the entity (audio = green, physics = orange, render = cyan,
    // ...) drawn flush to the right of the row. Lets the user scan the
    // Hierarchy and see "this entity has audio + physics" at a glance
    // without expanding every row's components in the inspector.
    {
        struct FamilyHit {
            ImU32 col;
            const char* tip;
            bool seen;
        };
        FamilyHit fams[] = {
            { IM_COL32(140,200,240,255), "Render",    false }, // Sprite/Camera2D/Text/UIAnchor
            { IM_COL32(220,130,220,255), "Animator",  false },
            { IM_COL32(165,140,245,255), "Particles", false },
            { IM_COL32(245,160, 76,255), "Physics",   false }, // RigidBody/Box/Circle
            { IM_COL32(140,220,140,255), "Audio",     false }, // Source/Listener
        };
        auto mark = [&](int fam_idx) { fams[fam_idx].seen = true; };
        auto check = [&](const char* name, int fam_idx) {
            const auto cid = s.world->find_component_id(name);
            if (cid && s.world->has_component(e, cid)) mark(fam_idx);
        };
        check("Sprite", 0);    check("Camera2D", 0);
        check("Text", 0);      check("UIAnchor", 0);
        check("Animator", 1);
        check("Particles", 2);
        check("RigidBody", 3); check("BoxCollider", 3); check("CircleCollider", 3);
        check("AudioSource", 4); check("AudioListener", 4);

        int n_visible = 0;
        for (auto& f : fams) if (f.seen) ++n_visible;
        if (n_visible > 0) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r       = 3.5f;
            const float gap     = 9.0f;
            const float right_x = row_max.x - 6.0f;
            const float cy      = row_min.y + (row_max.y - row_min.y) * 0.5f;
            float x = right_x;
            for (int i = (int)IM_ARRAYSIZE(fams) - 1; i >= 0; --i) {
                if (!fams[i].seen) continue;
                dl->AddCircleFilled(ImVec2(x, cy), r, fams[i].col);
                x -= gap;
            }
        }
    }

    // Selection commit gating. ImGui's TreeNode `IsItemClicked` fires on
    // mouse-DOWN, which is too early for our purposes -- it commits the
    // selection BEFORE the user's drag-drop has a chance to start, so
    // the Inspector swaps to the dragged entity and the EntityRef slot
    // they were aiming at vanishes. Defer to release-without-drag.
    //
    // We use IsItemActivated (mouse-down on this row) to record an
    // intent-to-select, then commit only when the drag threshold is NOT
    // crossed before release. This works even when ImGui's drag-drop
    // tear-down has already fired by the release frame, because the
    // drag-past-threshold check measures from the click's anchor.
    if (ImGui::IsItemActivated() && !ImGui::IsItemToggledOpen()) {
        // Mouse just went down on this row -- remember that we'd like
        // to select on release.
        s.click_pending_select = e;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        // Drag started -- never select via this click.
        s.click_pending_select = ecs::Entity{};
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !s.click_pending_select.is_null() &&
        s.click_pending_select == e) {
        s.selected_entity = e;
        s.click_pending_select = ecs::Entity{};
    }

    // ---- Context menu (right-click on this row) --------------------------
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Set as Selected"))
            s.selected_entity = e;
        ImGui::Separator();
        if (ImGui::MenuItem("Create Empty Child"))
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::CreateChild, e, {}, {} });
        if (ImGui::MenuItem("Rename"))
            start_rename(s, e);
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::Duplicate, e, {}, {} });
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            // Snapshot the right-clicked subtree into the editor's
            // entity-clipboard. Subsequent Paste spawns from this string;
            // the string survives selection changes and frame boundaries.
            if (s.world && s.world->is_alive(e)) {
                s.entity_clipboard = s.world->save_entity_subtree_json(e);
            }
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !s.entity_clipboard.empty())) {
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::Paste, e, {}, {} });
        }
        // Save as Prefab acts on the right-clicked row regardless of the
        // currently-selected entity. We temporarily make it the selection
        // because prefab_save_selected reads s.selected_entity — minor
        // coupling but it keeps the saver focused on a single concept.
        if (ImGui::MenuItem("Save as Prefab")) {
            const auto prev = s.selected_entity;
            s.selected_entity = e;
            prefab_save_selected(s);
            s.selected_entity = prev;
        }
        ImGui::Separator();
        if (is_protected(s.world, e)) {
            ImGui::BeginDisabled();
            ImGui::MenuItem("Delete");
            ImGui::EndDisabled();
        } else if (ImGui::MenuItem("Delete")) {
            const Engine::ecs::Entity ent = e;
            request_confirm(s, "Delete this entity (and its children)?",
                [&s, ent]() {
                    s.pending_hierarchy_ops.push_back(
                        { EditorState::PendingHierarchyOp::Kind::Delete, ent, {}, {} });
                });
        }
        ImGui::EndPopup();
    }

    // ---- Drag source (left-drag to reparent) -----------------------------
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &e, sizeof(e));
        if (name) ImGui::TextUnformatted(name);
        else      ImGui::Text("Entity %u", e.index);
        ImGui::EndDragDropSource();
    }

    // ---- Drop target (edge-aware: top edge = before, middle = reparent,
    //                   bottom edge = after) ------------------------------
    //
    // Split the row's vertical extent into thirds. The middle third
    // reparents (drop INTO this entity); the top + bottom thirds reorder
    // (drop BEFORE / AFTER this entity, preserving the parent). Visual
    // feedback: a thin coloured line at the relevant edge while a payload
    // hovers, drawn manually because ImGui's default highlight doesn't
    // distinguish position.
    const ImVec2 dnd_row_min = ImGui::GetItemRectMin();
    const ImVec2 dnd_row_max = ImGui::GetItemRectMax();
    const float  row_h   = dnd_row_max.y - dnd_row_min.y;
    const float  edge_h  = row_h * 0.25f;   // top / bottom band thickness

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* peek = ImGui::GetDragDropPayload();
        const bool is_hier = peek && peek->IsDataType("HIERARCHY_ENTITY");
        const float my = ImGui::GetIO().MousePos.y;
        const bool above   = is_hier && my <  dnd_row_min.y + edge_h;
        const bool below   = is_hier && my >  dnd_row_max.y - edge_h;
        const bool middle  = is_hier && !above && !below;

        // Draw our custom indicator: a thin coloured edge for reorder, a
        // soft tinted fill for reparent. Foreground draw list so it lands
        // on top of the tree node's selection highlight. The default
        // ImGui drop rect (yellow) is suppressed via
        // ImGuiDragDropFlags_AcceptNoDrawDefaultRect on the Accept call
        // below.
        if (is_hier) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImU32 line_col = IM_COL32(120, 180, 255, 230);
            const ImU32 fill_col = IM_COL32(120, 180, 255,  60);
            if (above) {
                dl->AddLine(ImVec2(dnd_row_min.x, dnd_row_min.y),
                            ImVec2(dnd_row_max.x, dnd_row_min.y), line_col, 2.0f);
            } else if (below) {
                dl->AddLine(ImVec2(dnd_row_min.x, dnd_row_max.y),
                            ImVec2(dnd_row_max.x, dnd_row_max.y), line_col, 2.0f);
            } else if (middle) {
                dl->AddRectFilled(dnd_row_min, dnd_row_max, fill_col);
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "HIERARCHY_ENTITY",
                ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            const ecs::Entity dragged = *static_cast<const ecs::Entity*>(payload->Data);
            // Reject: dropping onto self.
            if (dragged != e) {
                using Kind = EditorState::PendingHierarchyOp::Kind;
                if (above) {
                    // Reorder before — needs same parent, so the cycle
                    // check is "dragged isn't an ancestor of e".
                    if (!is_ancestor_or_self(s.world, dragged, e)) {
                        s.pending_hierarchy_ops.push_back({
                            Kind::ReorderBefore, dragged, e, {}
                        });
                    }
                } else if (below) {
                    if (!is_ancestor_or_self(s.world, dragged, e)) {
                        s.pending_hierarchy_ops.push_back({
                            Kind::ReorderAfter, dragged, e, {}
                        });
                    }
                } else {
                    // Middle: reparent — disallow dropping onto own
                    // descendant (would create a cycle).
                    if (!is_ancestor_or_self(s.world, dragged, e)) {
                        s.pending_hierarchy_ops.push_back({
                            Kind::Reparent, dragged, e, {}
                        });
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ---- Recurse into children -------------------------------------------
    if (open && child_count > 0) {
        s.world->iterate_children(e, [&](ecs::Entity child) {
            draw_entity_node(s, child);
        });
        ImGui::TreePop();
    }
}

} // namespace

void draw_hierarchy_panel(EditorState& s) {
    if (!s.show_hierarchy) return;
    if (!ImGui::Begin("Hierarchy", &s.show_hierarchy)) { ImGui::End(); return; }

    if (!s.world) {
        ImGui::TextDisabled("No world loaded.");
        ImGui::End();
        return;
    }

    // ---- World name header (right-click for Save / Save As) -------------
    {
        std::string label;
        if (s.current_world_path.empty()) {
            label = "(unsaved world)";
        } else {
            label = std::filesystem::path(s.current_world_path).stem().string();
            if (s.world_dirty) label += " *";
        }
        ImGui::PushStyleColor(ImGuiCol_Text,
            s.world_dirty ? ImVec4(1.0f, 0.85f, 0.45f, 1.0f)
                           : ImVec4(0.85f, 0.85f, 0.90f, 1.0f));
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("##world_ctx")) {
            const bool can_save = !s.is_playing && !s.current_world_path.empty();
            if (ImGui::MenuItem("Save", nullptr, false, can_save))
                s.want_save_world = true;
            if (ImGui::MenuItem("Save As...", nullptr, false, !s.is_playing)) {
                s.world_dialog_kind = EditorState::WorldDialogKind::SaveAs;
                s.world_dialog_buf[0] = 0;
                s.world_dialog_just_opened = true;
            }
            if (ImGui::MenuItem("Open...", nullptr, false, !s.is_playing)) {
                s.world_dialog_kind = EditorState::WorldDialogKind::Open;
                s.world_dialog_buf[0] = 0;
                s.world_dialog_just_opened = true;
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
    }

    // ---- Toolbar ---------------------------------------------------------
    if (ImGui::Button("+ New Entity"))
        s.pending_hierarchy_ops.push_back(
            { EditorState::PendingHierarchyOp::Kind::CreateRoot, {}, {}, {} });

    ImGui::SameLine();
    {
        const bool can_dup = s.world->is_alive(s.selected_entity);
        ImGui::BeginDisabled(!can_dup);
        if (ImGui::Button("Duplicate")) {
            s.pending_hierarchy_ops.push_back({
                EditorState::PendingHierarchyOp::Kind::Duplicate,
                s.selected_entity, {}, {}
            });
        }
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    {
        const bool can_del = s.world->is_alive(s.selected_entity)
                          && !is_protected(s.world, s.selected_entity);
        ImGui::BeginDisabled(!can_del);
        if (ImGui::Button("Delete Selected")) {
            const Engine::ecs::Entity ent = s.selected_entity;
            request_confirm(s, "Delete the selected entity (and its children)?",
                [&s, ent]() {
                    s.pending_hierarchy_ops.push_back(
                        { EditorState::PendingHierarchyOp::Kind::Delete, ent, {}, {} });
                });
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::Text("Entities: %u   Archetypes: %u",
                s.world->entity_count(), s.world->archetype_count());
    ImGui::Separator();

    // ---- Scrollable tree -------------------------------------------------
    ImGui::BeginChild("##hier_tree", ImVec2(0, 0), false);

    // Keyboard shortcuts — active when the tree pane is focused
    if (ImGui::IsWindowFocused()) {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)
                && s.world->is_alive(s.selected_entity)
                && !is_protected(s.world, s.selected_entity)) {
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::Delete, s.selected_entity, {}, {} });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && s.world->is_alive(s.selected_entity))
            start_rename(s, s.selected_entity);
        if (ImGui::IsKeyPressed(ImGuiKey_N) && io.KeyCtrl)
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::CreateRoot, {}, {}, {} });
        // Ctrl+D / Ctrl+C / Ctrl+V on the selected entity. Same shape
        // as the context-menu items above; duplicated here so users
        // never have to right-click to clone.
        if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
                ImGui::IsKeyPressed(ImGuiKey_D, false) &&
                s.world->is_alive(s.selected_entity)) {
            s.pending_hierarchy_ops.push_back({
                EditorState::PendingHierarchyOp::Kind::Duplicate,
                s.selected_entity, {}, {} });
        }
        if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
                ImGui::IsKeyPressed(ImGuiKey_C, false) &&
                s.world->is_alive(s.selected_entity)) {
            s.entity_clipboard = s.world->save_entity_subtree_json(
                s.selected_entity);
        }
        if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
                ImGui::IsKeyPressed(ImGuiKey_V, false) &&
                !s.entity_clipboard.empty()) {
            s.pending_hierarchy_ops.push_back({
                EditorState::PendingHierarchyOp::Kind::Paste,
                s.selected_entity, {}, {} });
        }
    }

    // ---- Globals (singleton entities) ------------------------------------
    // Singletons live as real entities but they're project-wide config /
    // managers, not scene content. Render them in a pinned collapsing
    // header at the top of the tree so the rest of the hierarchy stays
    // readable. Discovered by walking every component type and asking the
    // world for its designated singleton entity — an O(types) sweep, fine
    // for editor cadence.
    std::vector<ecs::Entity> singletons;
    {
        std::unordered_set<u64> seen;   // dedupe when one entity hosts multiple singleton components
        s.world->iterate_component_types(
            [&](ecs::ComponentId id, const ecs::ComponentType&) {
                const ecs::Entity e = s.world->find_singleton(id);
                if (e.is_null()) return;
                const u64 key = (u64(e.generation) << 32) | u64(e.index);
                if (seen.insert(key).second) singletons.push_back(e);
            });
    }
    if (!singletons.empty()) {
        // Don't restyle ImGuiCol_Header here -- CollapsingHeader uses
        // that exact color for its always-on filled bar, so a saturated
        // blue makes the header look permanently "selected." Use the
        // default theme so it reads as a section heading.
        const bool open = ImGui::CollapsingHeader("Globals",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            ImGui::Indent(8.0f);
            for (const auto e : singletons) draw_entity_node(s, e);
            ImGui::Unindent(8.0f);
        }
        ImGui::Separator();
    }

    // Draw root entities (entities without a Parent component) in the
    // user-controlled order tracked by World::iterate_roots. Skip
    // singleton entities since they're already in the Globals header.
    std::unordered_set<u64> singleton_keys;
    for (const auto e : singletons)
        singleton_keys.insert((u64(e.generation) << 32) | u64(e.index));
    s.world->iterate_roots([&](ecs::Entity e) {
        const u64 key = (u64(e.generation) << 32) | u64(e.index);
        if (singleton_keys.count(key)) return;
        draw_entity_node(s, e);
    });

    // "(scene root)" sentinel — drop target to unparent a dragged entity
    ImGui::Separator();
    ImGui::TextDisabled("(scene root)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
            const ecs::Entity dragged = *static_cast<const ecs::Entity*>(payload->Data);
            if (!s.world->parent_of(dragged).is_null()) {
                s.pending_hierarchy_ops.push_back({
                    EditorState::PendingHierarchyOp::Kind::Reparent,
                    dragged, ecs::NULL_ENTITY, {}
                });
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click on empty space → create root entity
    if (ImGui::BeginPopupContextWindow("##hier_bg",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity"))
            s.pending_hierarchy_ops.push_back(
                { EditorState::PendingHierarchyOp::Kind::CreateRoot, {}, {}, {} });
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    // ---- Drain deferred ops (after iterate_alive walk is complete) -------
    for (const auto& op : s.pending_hierarchy_ops) {
        using Kind = EditorState::PendingHierarchyOp::Kind;
        switch (op.kind) {
            case Kind::CreateRoot: {
                const ecs::Entity ne = s.world->create_entity();
                s.selected_entity = ne;
                break;
            }
            case Kind::CreateChild: {
                if (s.world->is_alive(op.target)) {
                    const ecs::Entity ne = s.world->create_entity();
                    s.world->set_parent(ne, op.target);
                    s.selected_entity = ne;
                }
                break;
            }
            case Kind::Delete: {
                if (s.world->is_alive(op.target)) {
                    // NOTE: hot-reload re-spawns project entities; user deletes
                    // will be undone on next reload (v1 known limitation).
                    s.world->destroy_entity(op.target);
                    if (s.selected_entity == op.target) s.selected_entity = {};
                    if (s.rename_target   == op.target) s.rename_target   = {};
                }
                break;
            }
            case Kind::Reparent: {
                if (s.world->is_alive(op.target)) {
                    if (op.new_parent.is_null())
                        s.world->unparent(op.target);
                    else if (s.world->is_alive(op.new_parent))
                        s.world->set_parent(op.target, op.new_parent);
                }
                break;
            }
            case Kind::ReorderBefore: {
                if (s.world->is_alive(op.target) && s.world->is_alive(op.new_parent))
                    s.world->move_before(op.target, op.new_parent);
                break;
            }
            case Kind::ReorderAfter: {
                if (s.world->is_alive(op.target) && s.world->is_alive(op.new_parent))
                    s.world->move_after(op.target, op.new_parent);
                break;
            }
            case Kind::Rename: {
                if (s.world->is_alive(op.target) && !op.new_name.empty()) {
                    const auto name_id = s.world->find_component_id("Name");
                    if (name_id != ecs::INVALID_COMPONENT_ID) {
                        auto* n = static_cast<components::Name*>(
                            s.world->get_component(op.target, name_id));
                        if (n) {
                            std::strncpy(n->value, op.new_name.c_str(), sizeof(n->value) - 1);
                            n->value[sizeof(n->value) - 1] = '\0';
                        }
                    }
                }
                break;
            }
            case Kind::Duplicate: {
                // Snapshot the source subtree, instantiate fresh entities,
                // and parent the clone to the source's parent (so it lands
                // as a sibling). Position stays identical -- the user can
                // drag the clone afterward. New ids; intra-subtree
                // EntityRefs are remapped automatically.
                if (s.world->is_alive(op.target)) {
                    const std::string snap =
                        s.world->save_entity_subtree_json(op.target);
                    if (!snap.empty() && snap != "{}") {
                        const ecs::Entity clone =
                            s.world->instantiate_entity_subtree_json(
                                snap.data(), snap.size());
                        if (!clone.is_null()) {
                            const ecs::Entity src_parent =
                                s.world->parent_of(op.target);
                            if (!src_parent.is_null())
                                s.world->set_parent(clone, src_parent);
                            s.selected_entity = clone;
                        }
                    }
                }
                break;
            }
            case Kind::Paste: {
                // Spawn from the clipboard. If `target` is a live entity,
                // parent the new root under it; otherwise it lands as a
                // root. Repeated paste keeps spawning new copies -- the
                // clipboard isn't consumed.
                if (s.entity_clipboard.empty()) break;
                const ecs::Entity clone =
                    s.world->instantiate_entity_subtree_json(
                        s.entity_clipboard.data(),
                        s.entity_clipboard.size());
                if (!clone.is_null()) {
                    if (s.world->is_alive(op.target)) {
                        s.world->set_parent(clone, op.target);
                    }
                    s.selected_entity = clone;
                }
                break;
            }
        }
    }
    if (!s.pending_hierarchy_ops.empty()) s.world_dirty = true;
    s.pending_hierarchy_ops.clear();

    ImGui::End();
}

}  // namespace Engine::editor
