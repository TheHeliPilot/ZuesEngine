// Undo / redo system. Snapshot-based - every undoable action captures
// the world's JSON state before the mutation, undo restores it, redo
// replays forward. Simpler than per-command Do/Undo functions and works
// uniformly across transform drag, inspector edits, add/remove entity,
// component changes, hierarchy reparent.
//
// The cost is one save_json per action (~few KB for a small scene).
// Bounded to `undo_max_history` entries; the oldest gets dropped when
// the cap is hit.

#include "editor.h"

#include <zues/ecs/world.h>
#include <zues/log.h>

namespace Engine::editor {

void undo_begin(EditorState& s) {
    if (!s.world)                   return;
    if (s.undo_action_in_progress)  return;   // already capturing
    s.undo_pending_snapshot = s.world->save_json();
    s.undo_action_in_progress = true;
}

void undo_commit(EditorState& s, const char* name) {
    if (!s.undo_action_in_progress) return;
    s.undo_action_in_progress = false;

    // The "after" state is the live world right now; we don't need to
    // serialize it yet - perform_undo will snapshot at the moment the
    // user undoes (which is also the "current" state at that point).
    EditorState::UndoEntry e;
    e.name       = name ? name : "";
    e.world_json = std::move(s.undo_pending_snapshot);
    s.undo_pending_snapshot.clear();

    // No-op if the snapshot didn't actually change (avoids polluting
    // the history with empty actions, e.g. a click that didn't drag).
    if (!s.undo_stack.empty() &&
        s.undo_stack.back().world_json == e.world_json) {
        return;
    }
    if (s.world) {
        const std::string current = s.world->save_json();
        if (current == e.world_json) return;   // mutation actually was no-op
    }

    s.undo_stack.push_back(std::move(e));
    if ((int)s.undo_stack.size() > EditorState::undo_max_history) {
        s.undo_stack.erase(s.undo_stack.begin());
    }
    // Any new action invalidates the redo branch.
    s.redo_stack.clear();
}

void undo_cancel(EditorState& s) {
    s.undo_action_in_progress = false;
    s.undo_pending_snapshot.clear();
}

static bool restore_from_json(EditorState& s, const std::string& json) {
    if (!s.world || json.empty()) return false;
    return s.world->load_json(json.data(), json.size()) == Result::Ok;
}

bool undo_perform_undo(EditorState& s) {
    if (s.undo_stack.empty() || !s.world) return false;
    // Snapshot the CURRENT state into the redo stack so a subsequent
    // redo can bring us back to where we started.
    EditorState::UndoEntry redo;
    redo.name       = s.undo_stack.back().name;
    redo.world_json = s.world->save_json();

    auto& target = s.undo_stack.back();
    if (!restore_from_json(s, target.world_json)) {
        log_write(LogLevel::Warn, "editor.undo", "undo failed - load_json error");
        return false;
    }
    s.redo_stack.push_back(std::move(redo));
    s.undo_stack.pop_back();
    // Selection may now point at a freed slot; clear defensively.
    s.selected_entity = ecs::Entity{};
    return true;
}

bool undo_perform_redo(EditorState& s) {
    if (s.redo_stack.empty() || !s.world) return false;
    EditorState::UndoEntry undo;
    undo.name       = s.redo_stack.back().name;
    undo.world_json = s.world->save_json();

    auto& target = s.redo_stack.back();
    if (!restore_from_json(s, target.world_json)) {
        log_write(LogLevel::Warn, "editor.undo", "redo failed - load_json error");
        return false;
    }
    s.undo_stack.push_back(std::move(undo));
    s.redo_stack.pop_back();
    s.selected_entity = ecs::Entity{};
    return true;
}

}  // namespace Engine::editor
