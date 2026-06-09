#ifndef ZUES_SERVICE_PHYSICS_H
#define ZUES_SERVICE_PHYSICS_H

/*
 * Physics service - implemented by zues_physics_box2d.
 *
 * Operations on rigid bodies are addressed by entity index. The service
 * looks up the entity's RigidBody component, dereferences the opaque
 * b2BodyId stored in `_body_handle`, and calls the matching Box2D API.
 *
 * World gravity is set once at module init (default { 0, -9.81 }) and can
 * be changed at runtime via set_gravity. Step rate is fixed at 60 Hz with
 * 4 substeps - the engine's frame-step accumulator handles dt-to-step
 * conversion so the game runs the same on a 30 Hz monitor as a 240 Hz one.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Per-frame step methods take a world-as-void* (the editor passes its
 * ecs::World*; the module casts internally). Putting them on the vtable
 * avoids exporting raw C symbols from the module DLL — cleaner ABI. */
typedef struct IPhysics_v1 {
    uint32_t abi_version;

    /* World-level controls. */
    void (*set_gravity)(struct IPhysics_v1* self, float x, float y);
    void (*get_gravity)(struct IPhysics_v1* self, float* out_x, float* out_y);

    /* Per-body controls. `e_idx` is an entity index. No-ops if the entity
     * has no RigidBody. The module looks up the entity through the world
     * pointer last passed to pre_step / post_step. */
    void (*apply_impulse)  (struct IPhysics_v1* self, int e_idx, float fx, float fy);
    void (*apply_force)    (struct IPhysics_v1* self, int e_idx, float fx, float fy);
    void (*set_velocity)   (struct IPhysics_v1* self, int e_idx, float vx, float vy);
    void (*get_velocity)   (struct IPhysics_v1* self, int e_idx, float* out_vx, float* out_vy);
    void (*set_position)   (struct IPhysics_v1* self, int e_idx, float x, float y, float radians);
    void (*wake_body)      (struct IPhysics_v1* self, int e_idx);

    /* Step pipeline. Editor calls these once per game-tick frame in this
     * order. `world` is an Engine::ecs::World* (opaque from C). */
    void (*pre_step) (struct IPhysics_v1* self, void* world, float dt);
    void (*step)     (struct IPhysics_v1* self, float dt);
    void (*post_step)(struct IPhysics_v1* self, void* world, float dt);

    /* Raycast. Returns 1 if hit, fills out_e_idx with the entity hit and
     * out_x/out_y with the hit point in world space. */
    int  (*raycast)(struct IPhysics_v1* self,
                    float ax, float ay, float bx, float by,
                    int* out_e_idx, float* out_x, float* out_y);

    /* Collision callbacks. Set once after project load; the module invokes
     * them from inside post_step for each contact-begin / sensor-begin /
     * sensor-end event Box2D produced this frame. Pass NULL fns to clear.
     * `user` is forwarded so the editor can route to the live project DLL.
     *
     * Each entity is identified by an (index, generation) pair so handlers
     * see the correct ZuesEntity even after slots get recycled across many
     * spawn/destroy cycles -- earlier versions stamped generation = 1 and
     * triggers stopped firing once a slot was reused. */
    void (*set_collision_handlers)(struct IPhysics_v1* self,
                                    void* user,
                                    void (*on_collision)    (void* user,
                                        uint32_t a_idx, uint32_t a_gen,
                                        uint32_t b_idx, uint32_t b_gen),
                                    void (*on_trigger_enter)(void* user,
                                        uint32_t self_idx, uint32_t self_gen,
                                        uint32_t other_idx, uint32_t other_gen),
                                    void (*on_trigger_exit) (void* user,
                                        uint32_t self_idx, uint32_t self_gen,
                                        uint32_t other_idx, uint32_t other_gen));

    /* Tear down + recreate the physics world. Called by the editor on
     * Play -> Stop so b2 bodies from the play session don't leak into
     * the next Play. Does NOT touch ECS components - the editor's world
     * snapshot/restore handles that. After reset, all RigidBody handles
     * are stale; the next pre_step will recreate them on demand. */
    void (*reset_world)(struct IPhysics_v1* self);
} IPhysics_v1;

#define ZUES_SERVICE_PHYSICS         "zues.physics"
#define ZUES_SERVICE_PHYSICS_VERSION 1

#ifdef __cplusplus
}
#endif

#endif /* ZUES_SERVICE_PHYSICS_H */
