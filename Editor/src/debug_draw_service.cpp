// Editor-side implementation of the IDebugDraw_v1 service. Engine
// subsystems publish debug visuals through this service; we forward
// each call to the editor's gizmo queue (already drawn on top of
// the Scene viewport after the entity render pass).
//
// State is global because the service vtable is plain C and has no
// instance pointer beyond `self`. One service per editor.
//
// Editor wiring:
//   * register_debug_draw_service()  -> called once at startup,
//     installs the vtable into Engine::services().
//   * update_selected_entity_each_frame()  -> the editor's main loop
//     pushes its current selection here so subsystems can do
//     "draw only when selected" filtering.
//   * The View menu reads / writes the category mask.

#include "editor.h"
#include "gizmos.h"

#include <zues/engine.h>
#include <zues/service.h>
#include <zues/services/debug_draw.h>

namespace Engine::editor {

namespace {

struct DbgState {
    u32           categories     = ZUES_DBG_ALL;     // default: all on
    ZuesDbgEntity selected{0, 0};
} g_dbg;

// ---- Service vtable functions ----------------------------------------
// All take `self` for ABI symmetry but ignore it -- the state is
// process-global. Lync extern callers see the full prototype either way.

u32 fn_get_categories(IDebugDraw_v1*) {
    return g_dbg.categories;
}
void fn_set_categories(IDebugDraw_v1*, u32 mask) {
    g_dbg.categories = mask;
}
int fn_is_enabled(IDebugDraw_v1*, u32 cat) {
    return (g_dbg.categories & cat) != 0u;
}

void fn_set_selected(IDebugDraw_v1*, ZuesDbgEntity e) { g_dbg.selected = e; }
ZuesDbgEntity fn_selected(IDebugDraw_v1*) { return g_dbg.selected; }

// Each draw call short-circuits when its category is disabled.
inline bool gate(u32 cat) { return (g_dbg.categories & cat) != 0u; }

void fn_line(IDebugDraw_v1*, u32 cat,
             float x0, float y0, float x1, float y1,
             float r, float g, float b, float a) {
    if (!gate(cat)) return;
    gizmo_line({x0, y0}, {x1, y1}, Engine::math::color{r, g, b, a});
}
void fn_circle(IDebugDraw_v1*, u32 cat,
               float cx, float cy, float radius,
               float r, float g, float b, float a) {
    if (!gate(cat)) return;
    gizmo_circle({cx, cy}, radius, Engine::math::color{r, g, b, a});
}
void fn_rect(IDebugDraw_v1*, u32 cat,
             float cx, float cy, float w, float h, float rotation,
             float r, float g, float b, float a) {
    if (!gate(cat)) return;
    gizmo_rect_outline({cx, cy}, {w, h},
                       Engine::math::color{r, g, b, a},
                       1.5f, rotation);
}
void fn_arrow(IDebugDraw_v1*, u32 cat,
              float x0, float y0, float x1, float y1,
              float r, float g, float b, float a) {
    if (!gate(cat)) return;
    gizmo_arrow({x0, y0}, {x1, y1}, Engine::math::color{r, g, b, a});
}

IDebugDraw_v1 g_vtable{
    /* abi_version */          ZUES_SERVICE_DEBUG_DRAW_VERSION,
    /* get_categories */       fn_get_categories,
    /* set_categories */       fn_set_categories,
    /* is_enabled */           fn_is_enabled,
    /* set_selected_entity */  fn_set_selected,
    /* selected_entity */      fn_selected,
    /* line */                 fn_line,
    /* circle */               fn_circle,
    /* rect */                 fn_rect,
    /* arrow */                fn_arrow,
};

}  // namespace

// ---- Public lifecycle hooks (called from main.cpp) ------------------

void register_debug_draw_service() {
    auto* sr = Engine::services();
    if (!sr) return;
    sr->register_service(ZUES_SERVICE_DEBUG_DRAW,
                          ZUES_SERVICE_DEBUG_DRAW_VERSION,
                          &g_vtable);
}

void debug_draw_set_selected(ecs::Entity e) {
    g_dbg.selected = ZuesDbgEntity{e.index, e.generation};
}

u32  debug_draw_categories()              { return g_dbg.categories; }
void debug_draw_set_categories(u32 mask)  { g_dbg.categories = mask; }

}  // namespace Engine::editor
