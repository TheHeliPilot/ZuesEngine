#pragma once

// Debug-draw service. Engine subsystems (particle_system,
// animator_system, future audio_system) publish per-frame visual
// debug primitives through this service; the editor implements it
// (queues into the gizmo renderer) and the runtime registers a no-op
// implementation (or none -- callers gracefully skip when service is
// null).
//
// Drawing is CATEGORIZED. Each category is a bit in a u32 mask the
// editor toggles via the View menu. Subsystems check the bit before
// emitting, so disabled categories cost approximately one bit-test
// per call and zero rendering work.
//
// Coordinates are WORLD units (the same coords gameplay code uses);
// the service translates to screen via the active camera before
// rendering. Subsystems don't think in pixels.

#include <zues/api.h>
#include <zues/types.h>

#define ZUES_SERVICE_DEBUG_DRAW          "zues.debug_draw"
#define ZUES_SERVICE_DEBUG_DRAW_VERSION  1

// Standard category bits. Project DLLs may also use bits >= 16 for
// their own gameplay debug overlays without colliding with engine.
#define ZUES_DBG_PARTICLES  (1u << 0)
#define ZUES_DBG_ANIMATOR   (1u << 1)
#define ZUES_DBG_AUDIO      (1u << 2)
#define ZUES_DBG_PHYSICS    (1u << 3)
#define ZUES_DBG_AI         (1u << 4)
#define ZUES_DBG_ALL        0xffffffffu

#ifdef __cplusplus
extern "C" {
#endif

// Opaque entity identifier (matches ZuesEntity layout). Used by
// `selected_entity` so subsystems can opt into "draw only when this
// entity is selected" without depending on editor headers.
typedef struct ZuesDbgEntity {
    Engine::u32 index;
    Engine::u32 generation;
} ZuesDbgEntity;

typedef struct IDebugDraw_v1 {
    Engine::u32 abi_version;

    // ---- Category mask --------------------------------------------
    // Get / set the bitmask of enabled categories. set() is called
    // from the editor's View menu; get() from publishers' is_enabled.
    Engine::u32 (*get_categories)(struct IDebugDraw_v1* self);
    void        (*set_categories)(struct IDebugDraw_v1* self,
                                   Engine::u32 mask);
    // Inline helper for publishers: cheap bitmask check.
    int         (*is_enabled)    (struct IDebugDraw_v1* self,
                                   Engine::u32 cat);

    // ---- Selection awareness --------------------------------------
    // Editor reports the currently-selected entity here each frame.
    // Subsystems use it to decide "draw only when selected" (e.g.
    // show an emitter's spawn shape only for the focused emitter).
    // {0,0} when nothing selected.
    void          (*set_selected_entity)(struct IDebugDraw_v1* self,
                                          ZuesDbgEntity e);
    ZuesDbgEntity (*selected_entity)    (struct IDebugDraw_v1* self);

    // ---- Drawing primitives ---------------------------------------
    // Coords + sizes in WORLD units. Color in linear 0..1 RGBA.
    // Each call is no-op when its category bit is clear.
    void (*line)  (struct IDebugDraw_v1* self, Engine::u32 cat,
                   float x0, float y0, float x1, float y1,
                   float r, float g, float b, float a);
    void (*circle)(struct IDebugDraw_v1* self, Engine::u32 cat,
                   float cx, float cy, float radius,
                   float r, float g, float b, float a);
    void (*rect)  (struct IDebugDraw_v1* self, Engine::u32 cat,
                   float cx, float cy, float w, float h, float rotation,
                   float r, float g, float b, float a);
    void (*arrow) (struct IDebugDraw_v1* self, Engine::u32 cat,
                   float x0, float y0, float x1, float y1,
                   float r, float g, float b, float a);
} IDebugDraw_v1;

#ifdef __cplusplus
}
#endif
