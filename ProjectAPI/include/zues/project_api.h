#ifndef ZUES_PROJECT_API_H
#define ZUES_PROJECT_API_H

/*
 * Pure C ABI contract between the editor/engine and a user's project.dll.
 *
 * Project.dll MUST NOT link zues_core. It talks to the engine ONLY through
 * this header and the function-pointer tables defined below.
 *
 * See docs/04-dll-safety.md for the reasoning.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Bump on any structural change. Host refuses to load a project with a
 * different version.
 *   v2: components/systems/queries (Phase 4.x.c)
 *   v3: SystemDomain (Editor/Game/Both) + add_sprite_default helper
 *   v4: get/set transform helpers (Transform2D auto-attached on create)
 *   v5: input (keyboard + mouse) on host
 *   v6: load_texture + add_sprite_textured (real PNG/JPG assets)
 *   v7: register_component gains optional default_bytes (NULL = zero-init)
 *   v8: set_component_category(eng, id, "Project/UI/Buttons") for the
 *       editor's Add Component picker. Categories are pure UI metadata. */
#define ZUES_PROJECT_API_VERSION 21

/* Opaque handle for the engine instance. Treat as a void*. */
typedef struct ZuesEngine ZuesEngine;

/* Renderer texture handle (GL texture id under the hood). 0 = invalid.
 * Matches the ZuesTextureHandle in renderer_2d.h — same underlying type.
 * Guarded so both headers can be included together without a redefinition. */
#ifndef ZUES_TEXTURE_HANDLE_DEFINED
#define ZUES_TEXTURE_HANDLE_DEFINED
typedef uint32_t ZuesTextureHandle;
#endif

/* Log levels passed to the host's log fn. Matches Engine::LogLevel. */
typedef enum ZuesLogLevel {
    ZUES_LOG_TRACE = 0,
    ZUES_LOG_DEBUG = 1,
    ZUES_LOG_INFO  = 2,
    ZUES_LOG_WARN  = 3,
    ZUES_LOG_ERROR = 4,
    ZUES_LOG_FATAL = 5
} ZuesLogLevel;

/* ---- ECS types (mirror Engine::ecs::* layouts) -------------------------- */

typedef struct ZuesEntity {
    uint32_t index;
    uint32_t generation;
} ZuesEntity;

typedef uint32_t ZuesComponentId;     /* 0 = invalid */

/* Field kind for inspector rendering. Numerically identical to
 * Engine::ecs::FieldKind — host casts directly. Append-only; never reorder. */
typedef enum ZuesFieldKind {
    ZUES_FIELD_UNKNOWN     = 0,
    ZUES_FIELD_BOOL        = 1,
    ZUES_FIELD_I8          = 2,
    ZUES_FIELD_I16         = 3,
    ZUES_FIELD_I32         = 4,
    ZUES_FIELD_I64         = 5,
    ZUES_FIELD_U8          = 6,
    ZUES_FIELD_U16         = 7,
    ZUES_FIELD_U32         = 8,
    ZUES_FIELD_U64         = 9,
    ZUES_FIELD_F32         = 10,
    ZUES_FIELD_F64         = 11,
    ZUES_FIELD_VEC2        = 12,
    ZUES_FIELD_VEC3        = 13,
    ZUES_FIELD_VEC4        = 14,
    ZUES_FIELD_COLOR       = 15,
    ZUES_FIELD_ENTITY      = 16,
    ZUES_FIELD_ENTITY_REF  = 17,
    ZUES_FIELD_HANDLE      = 18,
    ZUES_FIELD_CHAR_BUFFER = 19,
    ZUES_FIELD_ENUM        = 20,
    ZUES_FIELD_PREFAB_REF  = 21,
    ZUES_FIELD_SPRITE_REF  = 22,
    ZUES_FIELD_TEXTURE_REF = 23,
    ZUES_FIELD_AUDIO_REF   = 24,
    ZUES_FIELD_FONT_REF    = 25,
    ZUES_FIELD_AUDIO_CUE_REF = 26
} ZuesFieldKind;

/* One entry per field of a project-defined component. The host deep-copies
 * the array (and each `name` string) on register, so the project may store
 * these in stack/static memory — they don't need to outlive the call. */
typedef struct ZuesFieldInfo {
    const char*   name;
    ZuesFieldKind kind;
    uint32_t      offset;
    uint32_t      size;
} ZuesFieldInfo;

/* System execution phases — mirror Engine::ecs::Phase order. */
typedef enum ZuesPhase {
    ZUES_PHASE_INPUT          = 0,
    ZUES_PHASE_PRE_UPDATE     = 1,
    ZUES_PHASE_PHYSICS        = 2,
    ZUES_PHASE_POST_UPDATE    = 3,
    ZUES_PHASE_NET_REPLICATE  = 4,
    ZUES_PHASE_UI_INPUT       = 5,
    ZUES_PHASE_UI_LAYOUT      = 6,
    ZUES_PHASE_RENDER         = 7,
    ZUES_PHASE_UI_RENDER      = 8
} ZuesPhase;

/* When does a system run? Mirrors Engine::ecs::SystemDomain.
 *   BOTH   = always (default if you call legacy add_system)
 *   EDITOR = only when the editor is in Edit mode (paused on Play)
 *   GAME   = only when a Play session is active (paused in Edit) */
typedef enum ZuesSystemDomain {
    ZUES_DOMAIN_BOTH   = 0,
    ZUES_DOMAIN_EDITOR = 1,
    ZUES_DOMAIN_GAME   = 2
} ZuesSystemDomain;

/* System callback. Receives the engine handle (opaque to the project — pass
 * back into host fns), dt, and the user pointer the project registered. */
typedef void (*ZuesSystemFn)(ZuesEngine* engine, float dt, void* user);

/* Per-particle iteration callback used by `particles_for_each`. Called
 * once per live particle in the emitter, in [0, count) order. */
typedef void (*ZuesParticleEachFn)(ZuesEntity emitter, int idx, float dt,
                                    void* user);

/* Query callback. Invoked once per matching entity. `column_ptrs` has one
 * void* per requested component, in the SAME order as the `required` array
 * passed to query_each. Cast each to your component type. */
typedef void (*ZuesQueryFn)(ZuesEntity entity,
                            void** column_ptrs, uint32_t column_count,
                            void* user);

/* Host-provided API. Editor fills this in and passes to the project's
 * on_load. Projects keep a copy to call back into the engine.
 *
 * Adding fields: only APPEND, only bump ZUES_PROJECT_API_VERSION. Removing
 * or reordering fields is a breaking change. */
typedef struct ZuesHostApi {
    uint32_t abi_version;

    /* ---- v1 ------------------------------------------------------------ */

    void* (*get_service)(ZuesEngine* engine, const char* service_id, uint32_t version);
    void  (*log)        (ZuesEngine* engine, ZuesLogLevel level, const char* msg);

    /* ---- v2: components ------------------------------------------------- */

    /* Register a POD component type. Returns 0 on failure.
     * `fields_data` is an array of ZuesFieldInfo (cast to const void* to keep
     * the signature pure-C-stable). `fields_count` is the number of entries.
     * Pass NULL/0 for tag (zero-field) components. The host deep-copies the
     * array AND each field name; project memory may go away after the call.
     * `default_bytes` (v7): pointer to a default-initialized instance of the
     * component struct, or NULL to zero-init new instances. The host
     * deep-copies these bytes too — project memory may go away after the call. */
    ZuesComponentId (*register_component)(ZuesEngine* engine,
                                           const char* name,
                                           uint32_t size, uint32_t align,
                                           const void* fields_data, uint32_t fields_count,
                                           const void* default_bytes);
    ZuesComponentId (*find_component_id) (ZuesEngine* engine, const char* name);

    /* ---- v8: editor metadata ------------------------------------------- */
    /* Override the editor menu category for an already-registered component.
     * Slash-separated path ("Project/UI/Buttons"). Pass NULL or "" to clear.
     * The host deep-copies the string; project memory may go away after.
     * Pure UI metadata; doesn't affect runtime behaviour. */
    void (*set_component_category)(ZuesEngine* engine,
                                    ZuesComponentId id,
                                    const char* category);

    /* ---- v2: entities --------------------------------------------------- */

    ZuesEntity (*create_entity)  (ZuesEngine* engine);
    void       (*destroy_entity) (ZuesEngine* engine, ZuesEntity e);
    int        (*is_entity_alive)(ZuesEngine* engine, ZuesEntity e);

    /* ---- v2: components on entities (raw byte access) ------------------ */

    /* Adds component `id` to `e`. If `initial` non-null, copies size(id)
     * bytes from it; otherwise zero-initializes. Returns pointer to the
     * stored component bytes (writeable). NULL on error. */
    void* (*add_component)   (ZuesEngine* engine, ZuesEntity e,
                              ZuesComponentId id, const void* initial);
    void  (*remove_component)(ZuesEngine* engine, ZuesEntity e, ZuesComponentId id);
    void* (*get_component)   (ZuesEngine* engine, ZuesEntity e, ZuesComponentId id);
    int   (*has_component)   (ZuesEngine* engine, ZuesEntity e, ZuesComponentId id);

    /* ---- v2: systems + queries ----------------------------------------- */

    /* Registers a system. Runs every frame in the given phase, in
     * registration order. The engine drives the per-frame world.tick(). */
    void (*add_system)(ZuesEngine* engine, const char* name,
                       ZuesPhase phase, ZuesSystemFn fn, void* user);

    /* Visit every entity matching the filter. Required components must all
     * be present; excluded components must not be present. column_ptrs in
     * the callback are in the SAME order as the `required` array — cast to
     * your component types. */
    void (*query_each)(ZuesEngine* engine,
                       const ZuesComponentId* required, uint32_t n_required,
                       const ZuesComponentId* excluded, uint32_t n_excluded,
                       ZuesQueryFn fn, void* user);

    /* ---- v3: domain-aware systems + built-in component helpers --------- */

    /* Like add_system, but tags the system with a domain so it auto-pauses
     * in the wrong tick mode (Editor systems pause in Play; Game systems
     * pause in Edit). Use ZUES_DOMAIN_GAME for gameplay systems — they'll
     * stop ticking when the user hits Stop. */
    void (*add_system_with_domain)(ZuesEngine* engine, const char* name,
                                    ZuesPhase phase, ZuesSystemDomain domain,
                                    ZuesSystemFn fn, void* user);

    /* Convenience: add a default Sprite component to an entity. Equivalent
     * to add_component(entity, find_component_id("Sprite"), &bytes), but
     * the host fills the byte layout — projects don't need to mirror the
     * engine-side struct. Texture defaults to the renderer's 1×1 white
     * fallback; pivot is (0.5, 0.5); flip flags off. Useful for getting
     * gameplay entities visible without including engine headers. */
    void (*add_sprite_default)(ZuesEngine* engine, ZuesEntity e,
                                float w_cm, float h_cm,
                                float r, float g, float b, float a);

    /* ---- v4: Transform2D helpers --------------------------------------- */
    /*
     * Every entity is auto-attached a default Transform2D on create_entity
     * (position 0,0; rotation 0; scale 1,1). These helpers let projects
     * read/write it without mirroring the engine-side struct layout.
     *
     * No-ops if the entity is dead or Transform2D isn't registered.
     * Out pointers may be NULL — get_transform fills only the ones provided.
     */

    void (*set_transform)(ZuesEngine* engine, ZuesEntity e,
                          float x, float y, float rotation,
                          float scale_x, float scale_y);

    void (*set_transform_position)(ZuesEngine* engine, ZuesEntity e,
                                    float x, float y);

    void (*get_transform)(ZuesEngine* engine, ZuesEntity e,
                          float* out_x, float* out_y, float* out_rotation,
                          float* out_scale_x, float* out_scale_y);

    /* ---- v5: input ----------------------------------------------------- */
    /*
     * Old-Unity-style input. `key` codes mirror GLFW: ASCII for letters
     * (A=65, ..., Z=90) and digits (0=48..9=57); named constants below for
     * common non-ASCII keys. Mouse buttons: 0=left, 1=right, 2=middle.
     *
     *   _down     = held this frame
     *   _pressed  = edge: down this frame, up last
     *   _released = edge: up this frame, down last
     *
     * mouse_pos returns window pixels, top-left origin.
     * mouse_wheel returns vertical scroll delta accumulated this frame
     * (positive = scroll up).
     */
    int   (*is_key_down)      (ZuesEngine* engine, int key);
    int   (*is_key_pressed)   (ZuesEngine* engine, int key);
    int   (*is_key_released)  (ZuesEngine* engine, int key);
    void  (*mouse_pos)        (ZuesEngine* engine, float* out_x, float* out_y);
    int   (*is_mouse_down)    (ZuesEngine* engine, int button);
    int   (*is_mouse_pressed) (ZuesEngine* engine, int button);
    int   (*is_mouse_released)(ZuesEngine* engine, int button);
    float (*mouse_wheel)      (ZuesEngine* engine);

    /* ---- v6: textured sprites --------------------------------------------- */

    /* Load a PNG/JPG/etc via the renderer. Returns a ZuesTextureHandle (the
     * GL texture id). 0 = failure (file missing or unsupported format). The
     * renderer caches by path — subsequent calls with the same path return the
     * same id with no duplicate GPU upload.
     *
     * Path resolution for v1: paths are relative to the editor executable's
     * working directory. Use absolute paths for portability, or paths relative
     * to the editor .exe. Asset-browser-relative paths land with the asset
     * browser feature. */
    ZuesTextureHandle (*load_texture)(ZuesEngine* engine, const char* path);

    /* Like add_sprite_default but attaches a real texture. The tint color is
     * multiplied with the sampled pixel (rgba). pivot defaults to (0.5, 0.5);
     * flip flags off — call set_sprite_flip etc. afterwards if needed. */
    void (*add_sprite_textured)(ZuesEngine* engine, ZuesEntity e,
                                 float w_cm, float h_cm,
                                 ZuesTextureHandle tex,
                                 float r, float g, float b, float a);

    /* ---- Prefab instantiation (v10) ---------------------------------------
     * Instantiate a .zprefab into the live world. `prefab_path` is project-
     * relative with forward slashes ("assets/prefabs/Player.zprefab") and
     * resolved through the editor's AssetRegistry. The returned entity is
     * the root of the spawned subtree; descendants follow the source's
     * hierarchy. References inside the subtree are remapped; refs that
     * pointed outside the saved subtree drop to NULL.
     *
     * `world_x` / `world_y` override the root's Transform2D::position so
     * the call site can spawn at cursor / spawn-point. Returns 0 on
     * failure (file missing, parse error, no project loaded). */
    ZuesEntity (*instantiate_prefab)(ZuesEngine* engine,
                                      const char* prefab_path,
                                      float world_x, float world_y);

    /* ---- Singletons + cache invalidation (v11) ---------------------------
     * Singleton components: one designated entity carries one instance of
     * a component declared `[Singleton]` in Lync (or registered via the C++
     * builtins path with a singleton hint). The plugin's auto-generated
     * on_load calls ensure_singleton for each one; user code reads via the
     * cached getter the plugin emits per type.
     *
     *  ensure_singleton: find-or-create the designated entity. Idempotent;
     *      adopts an existing matching entity from a loaded world if one
     *      exists. Returns NULL_ENTITY (index=0, generation=0) on bad id.
     *
     *  find_singleton:   query-only. Returns NULL_ENTITY if no entity is
     *      registered or the recorded one was destroyed / lost the
     *      component.
     *
     *  world_version:    monotonic counter that bumps on every archetype
     *      mutation (component add/remove, world clear/load, hot-reload).
     *      The plugin's cached singleton getter compares this against its
     *      own snapshot to know when to refresh its pointer. Cheap u64
     *      compare on the hot path. */
    ZuesEntity (*ensure_singleton)(ZuesEngine* engine, ZuesComponentId id);
    ZuesEntity (*find_singleton)  (ZuesEngine* engine, ZuesComponentId id);
    uint64_t   (*world_version)   (ZuesEngine* engine);

    /* GUID-keyed prefab instantiation. Same semantics as instantiate_prefab
     * but takes a 16-byte GUID (low/high u64 halves) instead of a string
     * path; the host resolves through AssetRegistry. Used by the typed
     * Instantiate(PrefabRef, ...) wrapper the plugin emits — refs carry
     * their guid through the world without ever touching paths. */
    ZuesEntity (*instantiate_prefab_guid)(ZuesEngine* engine,
                                           uint64_t guid_hi, uint64_t guid_lo,
                                           float world_x, float world_y);

    /* ---- v12: timers + random ----------------------------------------------
     *
     * Timers fire `cb(user)` after `seconds` of accumulated game time. The
     * scheduler ticks once per frame between PreUpdate and Update, so a
     * fired callback sees the same world snapshot a system in PreUpdate
     * would. Returns a u32 handle; 0 means "scheduling failed" (e.g. host
     * is shutting down).
     *
     *   set_timeout:  one-shot. Auto-deletes after firing.
     *   set_interval: repeating. Re-arms with the same `seconds` after each
     *                 fire. Cancel via cancel_timer to stop.
     *   cancel_timer: idempotent; cancelling an already-cancelled / fired
     *                 / unknown handle is a no-op. Returns 1 if the handle
     *                 referred to a live timer, 0 otherwise.
     *
     * Timers DO NOT survive world reload / hot reload. The plugin re-arms
     * its OnLoad-spawned timers from user [OnLoad] code each project boot.
     * If you need persistent delayed actions, use a Timer component instead. */
    uint32_t (*set_timeout) (ZuesEngine* engine, float seconds,
                              void (*cb)(void* user), void* user);
    uint32_t (*set_interval)(ZuesEngine* engine, float seconds,
                              void (*cb)(void* user), void* user);
    int      (*cancel_timer)(ZuesEngine* engine, uint32_t handle);

    /* PRNG. Engine-side mt19937 seeded from steady_clock at startup; reseed
     * via random_seed for deterministic playthroughs. All four are safe to
     * call from any system phase; not threadsafe (we're single-threaded). */
    float    (*random_float)(ZuesEngine* engine);                  /* [0, 1)   */
    float    (*random_range)(ZuesEngine* engine, float lo, float hi); /* [lo, hi) */
    int      (*random_int)  (ZuesEngine* engine, int lo, int hi);  /* [lo, hi] */
    void     (*random_seed) (ZuesEngine* engine, uint64_t seed);

    /* ---- v13: hierarchy queries -------------------------------------------
     *
     * All return ZuesEntity by value. A return with generation == 0 means
     * "no such relation" (no parent / no children / index out of range) --
     * Engine::ecs::Entity uses generation 0 as the null sentinel internally
     * and the C-stable mirror keeps that convention.
     *
     *   get_parent(e):           e's parent, or {0,0} if e is a root.
     *   get_child_count(e):      number of direct children of e.
     *   get_child_at(e, idx):    e's idx-th child in the FirstChild->NextSibling
     *                              chain order (matches Hierarchy panel order).
     *                              {0,0} if idx out of range or e has no children.
     *   get_first_child(e):      head of the child list, or {0,0}.
     *   get_next_sibling(e):     next entry in the parent's child list, or {0,0}
     *                              if e is the last sibling. */
    ZuesEntity (*get_parent)      (ZuesEngine* engine, ZuesEntity e);
    uint32_t   (*get_child_count) (ZuesEngine* engine, ZuesEntity e);
    ZuesEntity (*get_child_at)    (ZuesEngine* engine, ZuesEntity e, uint32_t idx);
    ZuesEntity (*get_first_child) (ZuesEngine* engine, ZuesEntity e);
    ZuesEntity (*get_next_sibling)(ZuesEngine* engine, ZuesEntity e);

    /* ---- v14: HUD / Text helpers -----------------------------------------
     *
     * The `Text` component has a fixed char[256] buffer that doesn't map
     * cleanly to Lync's type system. These helpers let user code attach +
     * update text without touching the struct directly:
     *
     *   add_text_default: attach a Text component (creating the entity if
     *                     needed) with the given string + size + color.
     *                     Idempotent -- replaces existing Text content.
     *   set_text:         overwrite an existing entity's Text.utf8.
     *                     No-op if the entity has no Text component.
     *   set_text_color:   update Text.color in-place. */
    void (*add_text_default)(ZuesEngine* engine, ZuesEntity e,
                              const char* utf8, float size_px,
                              float r, float g, float b, float a);
    void (*set_text)        (ZuesEngine* engine, ZuesEntity e, const char* utf8);
    void (*set_text_color)  (ZuesEngine* engine, ZuesEntity e,
                              float r, float g, float b, float a);

    /* ---- v15: Animator playback ------------------------------------------
     *
     * Drive an Animator component's currently-playing clip. The clip
     * table itself (named entries each pointing at a .zanim) is set up
     * in the editor via the Animator inspector. Lync calls below pick
     * which entry plays + start/stop/scrub it.
     *
     * `animator_play_by_name`  -- find the clip whose name matches `name`
     *     in this entity's Animator.clips table; on hit, set Animator.
     *     current to that index, reset time to 0, set playing = 1, and
     *     mirror the resolved guid into Animator.animation. Returns 1
     *     on success, 0 if the entity has no Animator or no match.
     * `animator_set_playing`   -- toggle Animator.playing without
     *     touching `current` or `time`.
     * `animator_seek`          -- write Animator.time directly (seconds).
     *     Use 0 to restart the active clip.
     *
     * Reflected on the Lync side as PlayByName / SetPlaying / Seek
     * (see project_zues_lync_naming for the snake->Pascal rule). */
    int  (*animator_play_by_name)(ZuesEngine* engine, ZuesEntity e,
                                   const char* name);
    void (*animator_set_playing) (ZuesEngine* engine, ZuesEntity e, int playing);
    void (*animator_seek)        (ZuesEngine* engine, ZuesEntity e, float seconds);

    /* ---- v16: Particles control ------------------------------------------
     *
     * Drive an entity's Particles emitter from gameplay code. The
     * editor authors emitters in the Inspector (rate, shape, lifetime,
     * etc.); these calls toggle/burst at runtime.
     *
     * `particles_emit_burst` -- spawn `count` particles immediately
     *     (in addition to the emitter's normal rate). Useful for
     *     one-off effects like "explosion right here, right now."
     * `particles_set_playing` -- start / stop emission without losing
     *     existing live particles.
     * `particles_restart`    -- reset age and burst schedule. Combined
     *     with playing=1, replays burst-driven emitters from t=0. */
    void (*particles_emit_burst) (ZuesEngine* engine, ZuesEntity e, int count);
    void (*particles_set_playing)(ZuesEngine* engine, ZuesEntity e, int playing);
    void (*particles_restart)    (ZuesEngine* engine, ZuesEntity e);

    /* ---- v17: Particle pool inspection / extra-field access -----------
     *
     * Per-particle iteration + a user-defined "scratch float" per slot,
     * addressed by name. Names are typed in the Inspector (Particles.
     * extra_names); Lync code reads/writes them at runtime via these
     * thunks. Eight slots per particle -- enough for hp/team/damage/
     * morale style state without bloating the hot path.
     *
     * `particles_count` -- live particle count for an emitter entity.
     * `particles_get_pos` / `particles_set_pos` -- world position.
     * `particles_get_vel` / `particles_set_vel` -- velocity.
     * `particles_get_field` / `particles_set_field` -- one of the 8
     *      named scratch slots. Returns 0 / no-ops on unknown name.
     * `particles_kill` -- remove a particle by index (swap-remove). */
    int   (*particles_count)    (ZuesEngine* engine, ZuesEntity e);
    void  (*particles_get_pos)  (ZuesEngine* engine, ZuesEntity e, int idx,
                                  float* out_x, float* out_y);
    void  (*particles_set_pos)  (ZuesEngine* engine, ZuesEntity e, int idx,
                                  float x, float y);
    void  (*particles_get_vel)  (ZuesEngine* engine, ZuesEntity e, int idx,
                                  float* out_vx, float* out_vy);
    void  (*particles_set_vel)  (ZuesEngine* engine, ZuesEntity e, int idx,
                                  float vx, float vy);
    float (*particles_get_field)(ZuesEngine* engine, ZuesEntity e, int idx,
                                  const char* field);
    void  (*particles_set_field)(ZuesEngine* engine, ZuesEntity e, int idx,
                                  const char* field, float value);
    void  (*particles_kill)     (ZuesEngine* engine, ZuesEntity e, int idx);

    /* ---- v18: Spatial query + bulk movement helpers -------------------
     *
     * `particles_nearest_neighbor` -- find the closest live particle in
     *      `target_emitter` to (x, y), within max_radius. Uses the
     *      target emitter's lazy spatial grid (rebuilt on demand).
     *      Returns -1 on miss. Cell size used internally is max_radius
     *      so a single 3x3-cell scan covers the search area.
     *
     * `particles_step_toward` -- move particle `idx` of `emitter` toward
     *      (target_x, target_y) by at most max_speed * dt world units.
     *      Returns 1 when the particle is at (or past) the target this
     *      tick (within 1e-3 units), 0 otherwise. Saves 3 host calls
     *      vs the get_pos/compute/set_pos pattern.
     *
     * `particles_for_each` -- iterate live particles of `emitter`,
     *      invoking `cb(emitter, idx, dt, user)` per particle. The C++
     *      side runs the loop with a cached pool ptr so per-call cost
     *      drops -- subsequent ParticleGet/Set/Pos inside the callback
     *      hit the same cached pool. */
    int  (*particles_nearest_neighbor)(ZuesEngine* engine,
                                        ZuesEntity target_emitter,
                                        float x, float y, float max_radius);
    int  (*particles_step_toward)     (ZuesEngine* engine, ZuesEntity e, int idx,
                                        float target_x, float target_y,
                                        float max_speed, float dt);
    void (*particles_for_each)        (ZuesEngine* engine, ZuesEntity e,
                                        ZuesParticleEachFn cb, void* user);

    /* Raw SoA pointers for hot loops. Each slice_X returns a writable
     * `float*` of length ParticleCount(e), valid for THIS frame only
     * (pool resize / erase invalidates). Skip ParticleGet/Set entirely
     * inside tight inner loops by indexing these directly. */
    float* (*particles_slice_px)   (ZuesEngine* engine, ZuesEntity e);
    float* (*particles_slice_py)   (ZuesEngine* engine, ZuesEntity e);
    float* (*particles_slice_vx)   (ZuesEngine* engine, ZuesEntity e);
    float* (*particles_slice_vy)   (ZuesEngine* engine, ZuesEntity e);
    float* (*particles_slice_age)  (ZuesEngine* engine, ZuesEntity e);
    float* (*particles_slice_field)(ZuesEngine* engine, ZuesEntity e,
                                     const char* field);

    /* ---- v19: Audio playback (2D + 3D) -------------------------------
     *
     * One-shots use absolute or project-relative paths -- the host
     * resolves through miniaudio's resource manager + caches the
     * decoded data, so repeated calls with the same path don't re-read
     * the file. Returns a u32 voice handle (0 = failed to start).
     * Source-bound playback toggles `AudioSource.playing` and the
     * audio system picks up the change on its next tick.
     *
     * `audio_play_one_shot` -- 2D fire-and-forget. volume <= 0 is
     *      treated as 1.0; pitch <= 0 is treated as 1.0.
     * `audio_play_one_shot_at` -- 3D variant. min_distance / max_distance
     *      define the inverse-attenuation window in world units.
     * `audio_stop_voice` -- stop a voice handle from play_one_shot*.
     *      No-op for unknown / already-finished handles.
     * `audio_source_play / stop / pause` -- drive an AudioSource voice
     *      directly. UFCS-friendly under Lync.
     * `audio_set_master_volume` / `audio_master_volume` /
     *      `audio_set_muted` -- master bus controls. Mirror the
     *      Audio Mixer panel sliders. */
    uint32_t (*audio_play_one_shot)   (ZuesEngine* engine, const char* path,
                                        float volume, float pitch);
    uint32_t (*audio_play_one_shot_at)(ZuesEngine* engine, const char* path,
                                        float x, float y,
                                        float min_distance, float max_distance,
                                        float volume);
    void     (*audio_stop_voice)      (ZuesEngine* engine, uint32_t voice);
    void     (*audio_source_play)     (ZuesEngine* engine, ZuesEntity e);
    void     (*audio_source_stop)     (ZuesEngine* engine, ZuesEntity e);
    void     (*audio_source_pause)    (ZuesEngine* engine, ZuesEntity e,
                                        int paused);
    int      (*audio_source_is_playing)(ZuesEngine* engine, ZuesEntity e);
    void     (*audio_set_master_volume)(ZuesEngine* engine, float v);
    float    (*audio_master_volume)    (ZuesEngine* engine);
    void     (*audio_set_muted)        (ZuesEngine* engine, int muted);

    /* ---- v21: SpawnAudio / SpawnAudio3D ------------------------------
     *
     * Fire-and-forget cue playback. Spawns a fresh entity with an
     * AudioSource bound to the given cue, plays it, and destroys the
     * entity when the voice finishes naturally (auto_destroy = 1).
     * Looped cues stay alive until the caller manually stops them.
     *
     * The cue is passed as a 16-byte guid (hi/lo halves) the same way
     * instantiate_prefab_guid does it. Returns the spawned entity so
     * gameplay code can grab the handle to mute / stop early. */
    ZuesEntity (*audio_spawn)   (ZuesEngine* engine,
                                  uint64_t cue_hi, uint64_t cue_lo);
    ZuesEntity (*audio_spawn_3d)(ZuesEngine* engine,
                                  uint64_t cue_hi, uint64_t cue_lo,
                                  float x, float y, float max_distance);
} ZuesHostApi;

/* ---- Key code constants (subset; mirror GLFW values) -------------------- */
/* Letter/digit keys are their ASCII upper-case codes (W=87, A=65, etc).
 * These named constants cover the non-ASCII ones you'll commonly want. */
#define ZUES_KEY_SPACE       32
#define ZUES_KEY_APOSTROPHE  39
#define ZUES_KEY_COMMA       44
#define ZUES_KEY_MINUS       45
#define ZUES_KEY_PERIOD      46
#define ZUES_KEY_SLASH       47
#define ZUES_KEY_0           48
#define ZUES_KEY_1           49
#define ZUES_KEY_2           50
#define ZUES_KEY_3           51
#define ZUES_KEY_4           52
#define ZUES_KEY_5           53
#define ZUES_KEY_6           54
#define ZUES_KEY_7           55
#define ZUES_KEY_8           56
#define ZUES_KEY_9           57
#define ZUES_KEY_A           65
#define ZUES_KEY_B           66
#define ZUES_KEY_C           67
#define ZUES_KEY_D           68
#define ZUES_KEY_E           69
#define ZUES_KEY_F           70
#define ZUES_KEY_G           71
#define ZUES_KEY_H           72
#define ZUES_KEY_I           73
#define ZUES_KEY_J           74
#define ZUES_KEY_K           75
#define ZUES_KEY_L           76
#define ZUES_KEY_M           77
#define ZUES_KEY_N           78
#define ZUES_KEY_O           79
#define ZUES_KEY_P           80
#define ZUES_KEY_Q           81
#define ZUES_KEY_R           82
#define ZUES_KEY_S           83
#define ZUES_KEY_T           84
#define ZUES_KEY_U           85
#define ZUES_KEY_V           86
#define ZUES_KEY_W           87
#define ZUES_KEY_X           88
#define ZUES_KEY_Y           89
#define ZUES_KEY_Z           90
#define ZUES_KEY_ESCAPE      256
#define ZUES_KEY_ENTER       257
#define ZUES_KEY_TAB         258
#define ZUES_KEY_BACKSPACE   259
#define ZUES_KEY_RIGHT       262
#define ZUES_KEY_LEFT        263
#define ZUES_KEY_DOWN        264
#define ZUES_KEY_UP          265
#define ZUES_KEY_LEFT_SHIFT  340
#define ZUES_KEY_LEFT_CTRL   341

#define ZUES_MOUSE_LEFT   0
#define ZUES_MOUSE_RIGHT  1
#define ZUES_MOUSE_MIDDLE 2

/* Project-provided API. project.dll fills this in and returns it from
 * zues_project_entry. All fn pointers except on_load are optional. */
typedef struct ZuesProjectApi {
    uint32_t abi_version;

    void (*on_load)  (ZuesEngine* engine, const ZuesHostApi* host);
    void (*on_update)(ZuesEngine* engine, float dt);
    void (*on_unload)(ZuesEngine* engine);

    /* Physics events. The host's physics module calls these from the
     * post-step contact/sensor drain. All optional - leave NULL to opt
     * out. Entity arguments are the .index field of ZuesEntity (the
     * per-frame array slot). The LyncPlugin emits dispatch stubs here
     * when the project declares any [OnCollision] / [OnTriggerEnter] /
     * [OnTriggerExit] functions. */
    /* v20: callbacks now receive ZuesEntity (index + generation) for both
     * sides instead of bare int indices. Earlier versions stamped a fake
     * generation = 1 in the lync dispatch, so handlers calling Has<Tag>
     * or Get<Tag> failed to resolve once the slot got recycled (the
     * notorious "trigger stops firing after a few spawn/destroy cycles"
     * symptom). Now the physics module recovers the live generation
     * from the world's slot table at drain time and passes it through. */
    void (*on_collision)    (ZuesEngine* engine, ZuesEntity a, ZuesEntity b);
    void (*on_trigger_enter)(ZuesEngine* engine, ZuesEntity self, ZuesEntity other);
    void (*on_trigger_exit) (ZuesEngine* engine, ZuesEntity self, ZuesEntity other);
} ZuesProjectApi;

/* Each project.dll exports this C function. Host resolves it via
 * GetProcAddress / dlsym and calls it once after LoadLibrary. */
/* const ZuesProjectApi* zues_project_entry(void); */

#ifdef __cplusplus
}
#endif

/* Visibility helper. Project authors mark `zues_project_entry` with this:
 *
 *   ZUES_PROJECT_EXPORT const ZuesProjectApi* zues_project_entry(void) {
 *       return &my_api;
 *   }
 *
 * The extern "C" + dllexport / visibility("default") keep the symbol findable
 * by the editor's loader (LoadLibrary + GetProcAddress / dlopen + dlsym).
 */
// In C++, force C linkage on the entry point so the editor's GetProcAddress
// finds it without name-mangling. In pure C, the `extern "C"` is invalid
// syntax — but C functions already have C linkage, so we just emit the
// dllexport / visibility attribute.
#if defined(_WIN32)
    #ifdef __cplusplus
        #define ZUES_PROJECT_EXPORT extern "C" __declspec(dllexport)
    #else
        #define ZUES_PROJECT_EXPORT __declspec(dllexport)
    #endif
#else
    #ifdef __cplusplus
        #define ZUES_PROJECT_EXPORT extern "C" __attribute__((visibility("default")))
    #else
        #define ZUES_PROJECT_EXPORT __attribute__((visibility("default")))
    #endif
#endif

/* =============================================================================
 * C++-only convenience layer for project authors.
 *
 * Mirrors the engine-side field_kind_of trait + ZUES_COMPONENT_FIELDS macro
 * but emits ZuesFieldInfo[] arrays the project can hand to register_component.
 * Keeps the C ABI clean while letting C++ project code declare components
 * with the same one-liner ergonomics as engine-internal components.
 *
 * Usage:
 *   struct Position { float x, y; };
 *   ZUES_PROJECT_FIELDS(Position, x, y);
 *
 *   // in on_load:
 *   g_pos_id = host->register_component(eng, "MyGame.Position",
 *       sizeof(Position), alignof(Position),
 *       zues_fields_of_Position, zues_fields_count_Position);
 *
 * For non-primitive members the trait returns ZUES_FIELD_UNKNOWN; declare a
 * mapping with ZUES_PROJECT_FIELD_KIND(MyType, ZUES_FIELD_VEC2) before the
 * ZUES_PROJECT_FIELDS line.
 * ===========================================================================*/

#ifdef __cplusplus

#include <cstddef>      /* offsetof, size_t */
#include <type_traits>  /* is_enum, enable_if */

namespace zues_project {

template <class T, class = void>
struct FieldKindOf { static constexpr ZuesFieldKind value = ZUES_FIELD_UNKNOWN; };

template <class T>
struct FieldKindOf<T, typename std::enable_if<std::is_enum<T>::value>::type> {
    static constexpr ZuesFieldKind value = ZUES_FIELD_ENUM;
};

template <std::size_t N>
struct FieldKindOf<char[N]> { static constexpr ZuesFieldKind value = ZUES_FIELD_CHAR_BUFFER; };

#define ZUES__P_SPEC(T, K) \
    template<> struct FieldKindOf<T> { static constexpr ZuesFieldKind value = K; }

ZUES__P_SPEC(bool,     ZUES_FIELD_BOOL);
ZUES__P_SPEC(int8_t,   ZUES_FIELD_I8);
ZUES__P_SPEC(int16_t,  ZUES_FIELD_I16);
ZUES__P_SPEC(int32_t,  ZUES_FIELD_I32);
ZUES__P_SPEC(int64_t,  ZUES_FIELD_I64);
ZUES__P_SPEC(uint8_t,  ZUES_FIELD_U8);
ZUES__P_SPEC(uint16_t, ZUES_FIELD_U16);
ZUES__P_SPEC(uint32_t, ZUES_FIELD_U32);
ZUES__P_SPEC(uint64_t, ZUES_FIELD_U64);
ZUES__P_SPEC(float,    ZUES_FIELD_F32);
ZUES__P_SPEC(double,   ZUES_FIELD_F64);

ZUES__P_SPEC(ZuesEntity, ZUES_FIELD_ENTITY);

#undef ZUES__P_SPEC

}  /* namespace zues_project */

/* User-side hook to register custom mappings (e.g. project's own vec2). */
#define ZUES_PROJECT_FIELD_KIND(T, KIND)                                       \
    namespace zues_project {                                                   \
        template<> struct FieldKindOf<T> {                                     \
            static constexpr ZuesFieldKind value = (KIND);                     \
        };                                                                     \
    }

/* FOR_EACH up to 16 args. */
#define ZUES__P_FE1(F, T, a)        F(T, a)
#define ZUES__P_FE2(F, T, a, ...)   F(T, a), ZUES__P_FE1(F, T, __VA_ARGS__)
#define ZUES__P_FE3(F, T, a, ...)   F(T, a), ZUES__P_FE2(F, T, __VA_ARGS__)
#define ZUES__P_FE4(F, T, a, ...)   F(T, a), ZUES__P_FE3(F, T, __VA_ARGS__)
#define ZUES__P_FE5(F, T, a, ...)   F(T, a), ZUES__P_FE4(F, T, __VA_ARGS__)
#define ZUES__P_FE6(F, T, a, ...)   F(T, a), ZUES__P_FE5(F, T, __VA_ARGS__)
#define ZUES__P_FE7(F, T, a, ...)   F(T, a), ZUES__P_FE6(F, T, __VA_ARGS__)
#define ZUES__P_FE8(F, T, a, ...)   F(T, a), ZUES__P_FE7(F, T, __VA_ARGS__)
#define ZUES__P_FE9(F, T, a, ...)   F(T, a), ZUES__P_FE8(F, T, __VA_ARGS__)
#define ZUES__P_FE10(F, T, a, ...)  F(T, a), ZUES__P_FE9(F, T, __VA_ARGS__)
#define ZUES__P_FE11(F, T, a, ...)  F(T, a), ZUES__P_FE10(F, T, __VA_ARGS__)
#define ZUES__P_FE12(F, T, a, ...)  F(T, a), ZUES__P_FE11(F, T, __VA_ARGS__)
#define ZUES__P_FE13(F, T, a, ...)  F(T, a), ZUES__P_FE12(F, T, __VA_ARGS__)
#define ZUES__P_FE14(F, T, a, ...)  F(T, a), ZUES__P_FE13(F, T, __VA_ARGS__)
#define ZUES__P_FE15(F, T, a, ...)  F(T, a), ZUES__P_FE14(F, T, __VA_ARGS__)
#define ZUES__P_FE16(F, T, a, ...)  F(T, a), ZUES__P_FE15(F, T, __VA_ARGS__)

#define ZUES__P_FE_GET(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) ZUES__P_FE##N
#define ZUES__P_FOR_EACH(F, T, ...) \
    ZUES__P_FE_GET(__VA_ARGS__, 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)(F, T, __VA_ARGS__)

#define ZUES__P_FIELD_ENTRY(T, member)                                          \
    ZuesFieldInfo{                                                              \
        #member,                                                                \
        ::zues_project::FieldKindOf<                                            \
            typename std::remove_reference<                                     \
                decltype(static_cast<T*>(nullptr)->member)>::type>::value,      \
        static_cast<uint32_t>(offsetof(T, member)),                             \
        static_cast<uint32_t>(sizeof(static_cast<T*>(nullptr)->member))         \
    }

/* Declares two file-scope statics:
 *   const ZuesFieldInfo zues_fields_of_<T>[];
 *   constexpr uint32_t  zues_fields_count_<T>;
 * Pass them straight to host->register_component. */
#define ZUES_PROJECT_FIELDS(T, ...)                                             \
    static const ZuesFieldInfo zues_fields_of_##T[] = {                         \
        ZUES__P_FOR_EACH(ZUES__P_FIELD_ENTRY, T, __VA_ARGS__)                   \
    };                                                                          \
    static constexpr uint32_t zues_fields_count_##T =                           \
        sizeof(zues_fields_of_##T) / sizeof(zues_fields_of_##T[0])

#endif /* __cplusplus */

#endif /* ZUES_PROJECT_API_H */
