// zues_physics_box2d - Box2D 3.0 backend implementing IPhysics_v1.
//
// Lifecycle:
//   on_load:   create b2World with default gravity, register IPhysics_v1
//   on_unload: destroy world (auto-destroys all bodies)
//
// Per-frame work happens inside the IPhysics_v1 step methods, called from
// the editor's main loop in this order:
//   1. pre_step(world, dt)  - find new RigidBody entities, create b2Body;
//                              find dropped entities, destroy b2Body;
//                              sync Transform2D -> body for static/kinematic
//   2. step(dt)             - b2World_Step at fixed substeps
//   3. post_step(world, dt) - sync dynamic body -> Transform2D;
//                              drain contact events (TODO: fire hooks)
//
// World position units: same as Transform2D (1 unit = 1 cm). Box2D's internal
// units are arbitrary; we just pass cm through. Gravity default is
// (0, -981 cm/s^2) so a 1-cm Transform position drops realistically.

#include <zues/api.h>
#include <zues/log.h>
#include <zues/module.h>
#include <zues/service.h>
#include <zues/services/physics.h>
#include <zues/components/physics.h>
#include <zues/components/transform.h>
#include <zues/ecs/world.h>

#include <box2d/box2d.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Engine;

namespace {

// =============================================================================
// Module-local state. One world per process - matches the editor model where
// the live World owns one physics scene.
// =============================================================================

// Track every body we've created so we can destroy them when their owning
// entity is destroyed. The ECS doesn't know about Box2D, so without this
// registry, an entity destroy leaves a phantom b2Body in the world -- it
// keeps participating in contact events with stale userData (entity index
// of the now-destroyed slot) which corrupts the trigger callback's
// self/other dispatch and eventually wedges the contact graph after a
// handful of pipes disappear off-screen.
struct BodyEntry {
    b2BodyId    body;
    Engine::ecs::Entity owner;   // index + generation captured at create time
};

struct PhysicsState {
    b2WorldId world{};
    bool      world_valid = false;
    float     gravity_x = 0.0f;
    float     gravity_y = -9.81f;   // m/s^2 - Transform units are treated as
                                    // meters for physics. If your art assumes
                                    // pixels, scale via Transform.scale or
                                    // a per-project gravity override.

    // Collision callback dispatch. Set by the editor via
    // IPhysics_v1::set_collision_handlers; called from post_step's drain.
    void* collision_user = nullptr;
    // {a_idx, a_gen, b_idx, b_gen} for both contact and sensor events.
    // Generation is recovered from the world's slot table at drain time
    // so handlers see the live entity even after slot recycling.
    void (*on_collision)    (void*, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;
    void (*on_trigger_enter)(void*, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;
    void (*on_trigger_exit) (void*, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;

    // All live bodies + their owning entity. Walked at the top of pre_step
    // so bodies whose owners died last frame get b2DestroyBody'd.
    std::vector<BodyEntry> live_bodies;
};
PhysicsState g_phys;

// Resolve a shape's userData to an entity index. Box2D 3 buffers
// sensor/contact events for one step; if a shape was destroyed
// between Step() and the drain (the gameplay path that DestroyEntity's
// pipes from inside an on_trigger_enter handler does this) the
// shape id becomes invalid and b2Shape_GetUserData returns garbage.
// Gate on b2Shape_IsValid -- invalid shapes return -1, which the
// drain treats as "skip this event" instead of dispatching with a
// nonsense entity index.
int entity_idx_from_shape(b2ShapeId sh) {
    if (!b2Shape_IsValid(sh)) return -1;
    void* ud = b2Shape_GetUserData(sh);
    return static_cast<int>(reinterpret_cast<uintptr_t>(ud));
}

void api_set_collision_handlers(IPhysics_v1*, void* user,
        void (*on_collision)    (void*, uint32_t, uint32_t, uint32_t, uint32_t),
        void (*on_trigger_enter)(void*, uint32_t, uint32_t, uint32_t, uint32_t),
        void (*on_trigger_exit) (void*, uint32_t, uint32_t, uint32_t, uint32_t)) {
    g_phys.collision_user    = user;
    g_phys.on_collision      = on_collision;
    g_phys.on_trigger_enter  = on_trigger_enter;
    g_phys.on_trigger_exit   = on_trigger_exit;
}

void api_reset_world(IPhysics_v1*) {
    if (g_phys.world_valid) {
        b2DestroyWorld(g_phys.world);
        g_phys.world_valid = false;
    }
    // b2DestroyWorld implicitly frees every body inside it, so the entries
    // in our registry are now stale handles. Clear them before any new
    // bodies get created in the new world.
    g_phys.live_bodies.clear();
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = b2Vec2{g_phys.gravity_x, g_phys.gravity_y};
    g_phys.world = b2CreateWorld(&def);
    g_phys.world_valid = B2_IS_NON_NULL(g_phys.world);
    // RigidBody._body_handle on every entity is now stale; pre_step's
    // body-creation pass treats _body_handle == 0 as "needs creation".
    // The editor's world snapshot/restore zeroes the Handle field on
    // load, so by the time we get here it's already 0 - this destroy
    // just matches the runtime side.
}

// Forward decls.
b2BodyId body_id_from(unsigned long long h) {
    b2BodyId id;
    std::memcpy(&id, &h, sizeof(id) < sizeof(h) ? sizeof(id) : sizeof(h));
    return id;
}
unsigned long long body_id_to(b2BodyId id) {
    unsigned long long h = 0;
    std::memcpy(&h, &id, sizeof(id) < sizeof(h) ? sizeof(id) : sizeof(h));
    return h;
}

// Find an entity's body. Returns b2_nullBodyId if entity has no RigidBody
// component or its handle is unset OR the saved handle is stale.
//
// Stale-handle check matters: world save/load preserves `_body_handle`
// as plain bytes, but the b2World those bytes referenced may have been
// destroyed (Play->Stop reset, world reload, hot-reload). `B2_IS_NON_NULL`
// only checks `index1 != 0`; it accepts stale handles. `b2Body_IsValid`
// queries the live world. Always use the latter from any code that's
// going to call into b2 -- otherwise the call passes a dangling id and
// crashes (or worse, silently mutates the wrong body).
//
// When we detect a stale handle we ALSO zero `_body_handle` so the next
// pre_step's body-creation pass treats this entity as fresh. Otherwise
// the entity sits there forever with a non-zero handle and never gets
// a body recreated.
b2BodyId body_for(ecs::World* world, int e_idx) {
    if (!world || !g_phys.world_valid) return b2_nullBodyId;
    auto rb_id = world->find_component_id("RigidBody");
    if (!rb_id) return b2_nullBodyId;
    // Look up the slot's real generation. The lync wrappers pass only
    // an int slot index across the boundary, so we'd previously hard-
    // coded `generation = 1` here -- which made every physics call a
    // silent no-op once a world had been reloaded (slot generations
    // bump on each reuse, and saved worlds preserve them). Scan
    // iterate_alive instead and match the index.
    Engine::ecs::Entity ent{};
    world->iterate_alive([&](Engine::ecs::Entity e) {
        if (e.index == (uint32_t)e_idx) ent = e;
    });
    if (ent.is_null()) return b2_nullBodyId;
    auto* rb = static_cast<components::RigidBody*>(
        world->get_component(ent, rb_id));
    if (!rb || rb->_body_handle == 0) return b2_nullBodyId;
    b2BodyId b = body_id_from(rb->_body_handle);
    if (!b2Body_IsValid(b)) {
        // Saved bytes refer to a body that no longer exists. Zero the
        // handle so pre_step recreates it on the next frame.
        rb->_body_handle = 0;
        return b2_nullBodyId;
    }
    return b;
}

// =============================================================================
// IPhysics_v1 implementation. All entries are no-ops if the world isn't ready
// or the entity doesn't have a body — keeps the surface forgiving for newly-
// loaded scenes where physics is still spinning up.
// =============================================================================

void api_set_gravity(IPhysics_v1*, float x, float y) {
    g_phys.gravity_x = x;
    g_phys.gravity_y = y;
    if (g_phys.world_valid) {
        b2World_SetGravity(g_phys.world, b2Vec2{x, y});
    }
}
void api_get_gravity(IPhysics_v1*, float* ox, float* oy) {
    if (ox) *ox = g_phys.gravity_x;
    if (oy) *oy = g_phys.gravity_y;
}

// Per-body APIs need the live world to look up entities. We stash it on
// every pre_step / post_step call so per-body API calls between steps see
// the correct world pointer. No-ops until the first step lands.
ecs::World* g_world_for_calls = nullptr;

void api_apply_impulse(IPhysics_v1*, int e, float fx, float fy) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) return;
    b2Body_ApplyLinearImpulseToCenter(b, b2Vec2{fx, fy}, true);
}
void api_apply_force(IPhysics_v1*, int e, float fx, float fy) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) return;
    b2Body_ApplyForceToCenter(b, b2Vec2{fx, fy}, true);
}
void api_set_velocity(IPhysics_v1*, int e, float vx, float vy) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) return;
    b2Body_SetLinearVelocity(b, b2Vec2{vx, vy});
}
void api_get_velocity(IPhysics_v1*, int e, float* ovx, float* ovy) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) { if (ovx) *ovx = 0; if (ovy) *ovy = 0; return; }
    b2Vec2 v = b2Body_GetLinearVelocity(b);
    if (ovx) *ovx = v.x;
    if (ovy) *ovy = v.y;
}
void api_set_position(IPhysics_v1*, int e, float x, float y, float r) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) return;
    b2Body_SetTransform(b, b2Vec2{x, y}, b2MakeRot(r));
}
void api_wake_body(IPhysics_v1*, int e) {
    b2BodyId b = body_for(g_world_for_calls, e);
    if (!B2_IS_NON_NULL(b)) return;
    b2Body_SetAwake(b, true);
}

int api_raycast(IPhysics_v1*, float, float, float, float, int*, float*, float*) {
    // TODO: wire b2World_CastRayClosest with a small filter callback.
    return 0;
}

// Forward decls for the step methods - their implementations live below
// (they need create_body_for_entity, which is a multi-page function).
void api_pre_step (IPhysics_v1*, void* world_void, float dt);
void api_step     (IPhysics_v1*, float dt);
void api_post_step(IPhysics_v1*, void* world_void, float dt);

IPhysics_v1 g_iphys = {
    .abi_version   = 1,
    .set_gravity   = api_set_gravity,
    .get_gravity   = api_get_gravity,
    .apply_impulse = api_apply_impulse,
    .apply_force   = api_apply_force,
    .set_velocity  = api_set_velocity,
    .get_velocity  = api_get_velocity,
    .set_position  = api_set_position,
    .wake_body     = api_wake_body,
    .pre_step      = api_pre_step,
    .step          = api_step,
    .post_step     = api_post_step,
    .raycast       = api_raycast,
    .set_collision_handlers = api_set_collision_handlers,
    .reset_world            = api_reset_world,
};

// =============================================================================
// Step pipeline. The editor calls these once per game-tick frame; in Edit mode
// they're skipped so bodies don't drift while the user is dragging entities.
//
// We expose them as plain C symbols (NOT through the service vtable) because
// they need ecs::World* which is a C++ type. The editor links the module DLL's
// import lib at build time and calls these directly.
// =============================================================================

void create_body_for_entity(ecs::World* world, ecs::Entity e,
                             components::RigidBody* rb,
                             components::Transform2D* xform) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = (rb->body_type == components::BodyType_Static)    ? b2_staticBody
            : (rb->body_type == components::BodyType_Kinematic) ? b2_kinematicBody
                                                                 : b2_dynamicBody;
    // Spawn the body at the entity's COMPOSED world transform, so a body
    // attached to a child entity starts where the parent puts it instead
    // of at the child's local-space origin.
    const auto W = world ? world->world_transform_2d(e)
                          : ecs::World::WorldTransform2D{xform->position.x,
                                                          xform->position.y,
                                                          xform->rotation,
                                                          xform->scale.x,
                                                          xform->scale.y};
    bd.position        = b2Vec2{W.pos_x, W.pos_y};
    bd.rotation        = b2MakeRot(W.rot);
    bd.linearDamping   = rb->linear_damping;
    bd.angularDamping  = rb->angular_damping;
    bd.gravityScale    = rb->gravity_scale;
    bd.fixedRotation   = rb->fixed_rotation != 0;
    bd.isBullet        = rb->is_bullet != 0;
    bd.userData        = reinterpret_cast<void*>(static_cast<uintptr_t>(e.index));

    b2BodyId body = b2CreateBody(g_phys.world, &bd);
    rb->_body_handle = body_id_to(body);
    // Remember the body so the entity-died sweep can find + destroy it.
    g_phys.live_bodies.push_back({body, e});

    // Attach colliders if present. A single body may carry both a box and a
    // circle; both shapes get fixtures attached.
    auto box_id    = world->find_component_id("BoxCollider");
    auto circle_id = world->find_component_id("CircleCollider");

    // Material props now live on RigidBody (one set per body, applied to
    // every shape). Caller already passed `rb` so we read them here.
    const float mat_density     = rb->density;
    const float mat_friction    = rb->friction;
    const float mat_restitution = rb->restitution;

    // Collider shape sizes scale with the entity's COMPOSED world scale, so
    // a child of a parent with scale X (or a child whose own Transform2D has
    // a scale) gets a shape that matches its sprite. Box2D shapes are sized
    // at creation time and don't auto-rescale; if the user later edits scale
    // they'll need to "Rebuild Body" (clear _body_handle) for the shape to
    // pick it up. Same convention Unity / Godot use: scale baked at body
    // creation, change-of-scale needs explicit rebuild.
    const float sx = std::fabs(W.scale_x) > 1e-6f ? std::fabs(W.scale_x) : 1.0f;
    const float sy = std::fabs(W.scale_y) > 1e-6f ? std::fabs(W.scale_y) : 1.0f;
    const float s_uniform = std::max(sx, sy);   // for radius-bearing circles

    if (box_id) {
        auto* box = static_cast<components::BoxCollider*>(world->get_component(e, box_id));
        if (box) {
            b2ShapeDef sd = b2DefaultShapeDef();
            sd.density     = mat_density;
            sd.friction    = mat_friction;
            sd.restitution = mat_restitution;
            sd.isSensor    = box->is_sensor != 0;
            // Stamp the entity index into shape userData. The contact-event
            // drain reads this back to map shape -> entity without a side
            // table.
            sd.userData    = reinterpret_cast<void*>(static_cast<uintptr_t>(e.index));
            // Box2D 3 requires BOTH shapes in a pair to opt in for an
            // event to fire. Gating sensor events to "sensor-only shapes"
            // means the non-sensor visitor (e.g. a player) silently
            // suppresses every trigger -- which was the symptom: the
            // pipe trigger fired sporadically (only when something
            // *else* with is_sensor brushed it). Enable both kinds on
            // every shape; the actual sensor flag still routes which
            // event queue (contact vs sensor) the pair lands in.
            sd.enableContactEvents = true;
            sd.enableSensorEvents  = true;
            b2Polygon poly = b2MakeOffsetBox(box->half_extents.x * sx,
                                              box->half_extents.y * sy,
                                              b2Vec2{box->offset.x * sx,
                                                     box->offset.y * sy},
                                              0.0f);
            b2ShapeId sh = b2CreatePolygonShape(body, &sd, &poly);
            unsigned long long h = 0;
            std::memcpy(&h, &sh, sizeof(sh) < sizeof(h) ? sizeof(sh) : sizeof(h));
            box->_shape_handle = h;
        }
    }
    if (circle_id) {
        auto* c = static_cast<components::CircleCollider*>(world->get_component(e, circle_id));
        if (c) {
            b2ShapeDef sd = b2DefaultShapeDef();
            sd.density     = mat_density;
            sd.friction    = mat_friction;
            sd.restitution = mat_restitution;
            sd.isSensor    = c->is_sensor != 0;
            sd.userData    = reinterpret_cast<void*>(static_cast<uintptr_t>(e.index));
            // See BoxCollider above -- both shapes in a sensor pair need
            // both event flags set or Box2D 3 silently drops the event.
            sd.enableContactEvents = true;
            sd.enableSensorEvents  = true;
            b2Circle circle{};
            // Circles don't have an x/y axis so we apply the dominant scale.
            // Non-uniform scaling on a circle collider is ambiguous anyway --
            // users should switch to a Box for that case.
            circle.center = b2Vec2{c->offset.x * sx, c->offset.y * sy};
            circle.radius = c->radius * s_uniform;
            b2ShapeId sh = b2CreateCircleShape(body, &sd, &circle);
            unsigned long long h = 0;
            std::memcpy(&h, &sh, sizeof(sh) < sizeof(h) ? sizeof(sh) : sizeof(h));
            c->_shape_handle = h;
        }
    }
}

// query_each thunks. Defined as static C++ functions; the world's
// iterate_query takes a plain function pointer so we can't use captureful
// lambdas here.
//
// pre_step needs the timestep to convert kinematic Transform deltas into
// linear velocities. We stash dt on the state right before iterate_query.
float g_pre_step_dt = 1.0f / 60.0f;

void pre_step_thunk(void* /*user*/, ecs::Entity e, void** cols, uint32_t /*n*/) {
    auto* rb    = static_cast<components::RigidBody*>(cols[0]);
    auto* xform = static_cast<components::Transform2D*>(cols[1]);
    if (!rb || !xform) return;

    // The handle may be:
    //   1. Zero -- newly added RigidBody, hasn't been built yet.
    //   2. Non-zero AND points at a live body -- normal steady state.
    //   3. Non-zero AND stale -- saved JSON brought us back here after a
    //      world reload / Play-Stop reset / hot-reload destroyed the
    //      original b2World. b2Body_IsValid catches this (B2_IS_NON_NULL
    //      doesn't -- it only checks index1 != 0).
    // In cases 1 + 3, recreate. Case 3 also resets shape handles on
    // child colliders so they get rebuilt against the new body.
    if (rb->_body_handle == 0 ||
        !b2Body_IsValid(body_id_from(rb->_body_handle))) {
        if (rb->_body_handle != 0) {
            // Stale -- zero shape handles too so create_body_for_entity's
            // collider attach pass writes fresh ones.
            if (auto box_id = g_world_for_calls->find_component_id("BoxCollider"))
                if (auto* bx = static_cast<components::BoxCollider*>(
                        g_world_for_calls->get_component(e, box_id)))
                    bx->_shape_handle = 0;
            if (auto cir_id = g_world_for_calls->find_component_id("CircleCollider"))
                if (auto* cr = static_cast<components::CircleCollider*>(
                        g_world_for_calls->get_component(e, cir_id)))
                    cr->_shape_handle = 0;
            rb->_body_handle = 0;
        }
        create_body_for_entity(g_world_for_calls, e, rb, xform);
        return;
    }

    b2BodyId b = body_id_from(rb->_body_handle);

    // Box2D works in WORLD space. If this entity is parented, the local
    // Transform2D.position is relative to its parent -- using it directly
    // would freeze the body at parent-origin instead of riding along when
    // the parent moves. Compose the world transform up the hierarchy and
    // hand THAT to Box2D. Roots compose to themselves at zero cost.
    const auto W = g_world_for_calls
        ? g_world_for_calls->world_transform_2d(e)
        : ecs::World::WorldTransform2D{xform->position.x, xform->position.y,
                                        xform->rotation, xform->scale.x, xform->scale.y};

    if (rb->body_type == components::BodyType_Static) {
        // Static bodies: teleport. They're not expected to move under play, so
        // the editor-side "drag the entity around" workflow trumps swept
        // motion.
        b2Body_SetTransform(b, b2Vec2{W.pos_x, W.pos_y},
                                b2MakeRot(W.rot));
    } else if (rb->body_type == components::BodyType_Kinematic) {
        // Kinematic bodies: convert WORLD-Transform delta into a velocity so
        // Box2D sweeps the body across the step and generates contact /
        // sensor events against dynamic shapes. SetTransform here would
        // teleport, skipping the contact graph entirely.
        //
        // The "target" is the entity's composed world position THIS frame --
        // which automatically reflects any movement the user did to a
        // parent in [System("PreUpdate")]. The body chases that target at
        // the velocity needed to arrive in one step.
        const float dt = (g_pre_step_dt > 1e-6f) ? g_pre_step_dt : (1.0f / 60.0f);
        b2Vec2 cur = b2Body_GetPosition(b);
        b2Vec2 v {
            (W.pos_x - cur.x) / dt,
            (W.pos_y - cur.y) / dt,
        };
        b2Body_SetLinearVelocity(b, v);
        // Rotation is rare for kinematic gameplay bodies; teleport it to
        // avoid having to track angular delta. If swept-rotation contact
        // matters for a use case, switch to b2Body_SetAngularVelocity
        // mirroring the linear case.
        b2Rot cur_rot = b2Body_GetRotation(b);
        b2Rot target_rot = b2MakeRot(W.rot);
        if (cur_rot.c != target_rot.c || cur_rot.s != target_rot.s) {
            b2Body_SetTransform(b, cur, target_rot);
        }
        b2Body_SetAwake(b, true);
    }
    // Dynamic: leave alone — Box2D integrates from the body's own state and
    // post_step writes the result back to Transform.
}

void post_step_thunk(void* /*user*/, ecs::Entity e, void** cols, uint32_t /*n*/) {
    auto* rb    = static_cast<components::RigidBody*>(cols[0]);
    auto* xform = static_cast<components::Transform2D*>(cols[1]);
    if (!rb || !xform || rb->_body_handle == 0) return;
    // Mirror Box2D -> Transform for both Dynamic and Kinematic. Kinematic
    // needs this so next frame's delta-to-velocity calculation reads from
    // the actual integrated position rather than wherever the user's
    // PreUpdate write last left it (the two converge after one step, but
    // collisions can nudge the kinematic body by tiny amounts).
    if (rb->body_type == components::BodyType_Static) return;
    b2BodyId b = body_id_from(rb->_body_handle);
    // Stale-handle guard: same reasoning as pre_step. Don't write a
    // Transform from a bogus body; pre_step will rebuild it.
    if (!b2Body_IsValid(b)) { rb->_body_handle = 0; return; }
    const b2Vec2 wp = b2Body_GetPosition(b);
    const float  wr = b2Rot_GetAngle(b2Body_GetRotation(b));

    // Project the world-space body pose back into the parent's local frame
    // before storing. For root entities (parent identity) this is the
    // existing pass-through; for parented bodies it strips the parent's
    // contribution so renderer + physics agree on the entity's visible
    // position next frame.
    ecs::Entity parent = g_world_for_calls
        ? g_world_for_calls->parent_of(e)
        : ecs::Entity{};
    if (parent.is_null() || !g_world_for_calls) {
        xform->position.x = wp.x;
        xform->position.y = wp.y;
        xform->rotation   = wr;
        return;
    }
    const auto pw = g_world_for_calls->world_transform_2d(parent);
    const float dx = wp.x - pw.pos_x;
    const float dy = wp.y - pw.pos_y;
    const float c  = std::cos(-pw.rot);
    const float s  = std::sin(-pw.rot);
    const float lx = c * dx - s * dy;
    const float ly = s * dx + c * dy;
    const float sx = (std::fabs(pw.scale_x) > 1e-6f) ? pw.scale_x : 1.0f;
    const float sy = (std::fabs(pw.scale_y) > 1e-6f) ? pw.scale_y : 1.0f;
    xform->position.x = lx / sx;
    xform->position.y = ly / sy;
    xform->rotation   = wr - pw.rot;
}

void api_pre_step(IPhysics_v1*, void* world_void, float dt) {
    auto* world = static_cast<ecs::World*>(world_void);
    if (!world || !g_phys.world_valid) return;
    g_world_for_calls = world;
    g_pre_step_dt = dt;

    // ---- Reap bodies whose entity died -----------------------------------
    // The ECS destroyed some entities since last frame (e.g. pipes scrolled
    // off-screen and got DestroyEntity'd). Their b2 bodies still exist in
    // the world with stale shape userData -- if we leave them, they keep
    // participating in contact / sensor events for the now-dead entity
    // index, corrupting the trigger dispatch and (worst case) jamming the
    // contact graph after a handful of leaks. Walk the registry, destroy
    // the b2 bodies for any entry whose owner is no longer alive, then
    // compact the vector.
    for (size_t i = 0; i < g_phys.live_bodies.size(); ) {
        const auto& be = g_phys.live_bodies[i];
        // Drop entries whose Box2D body is no longer valid -- happens
        // when a stale handle on the RigidBody triggered a recreate
        // (see pre_step_thunk's case 3). The old body was destroyed
        // there, but the registry slot wasn't compacted, so duplicates
        // accumulated until the contact graph started misbehaving.
        const bool body_dead  = !B2_IS_NON_NULL(be.body) ||
                                !b2Body_IsValid(be.body);
        const bool owner_dead = !world->is_alive(be.owner);
        if (!body_dead && !owner_dead) { ++i; continue; }
        if (!body_dead && owner_dead) b2DestroyBody(be.body);
        // Swap-remove preserves O(1) erase -- iteration order doesn't
        // matter here, only that every dead entry is processed.
        g_phys.live_bodies[i] = g_phys.live_bodies.back();
        g_phys.live_bodies.pop_back();
    }

    auto rb_id    = world->find_component_id("RigidBody");
    auto xform_id = world->find_component_id("Transform2D");
    if (!rb_id || !xform_id) return;

    ecs::ComponentId required[2] = { rb_id, xform_id };
    world->iterate_query(required, 2, nullptr, 0, pre_step_thunk, nullptr);
}

void api_step(IPhysics_v1*, float dt) {
    if (!g_phys.world_valid) return;
    b2World_Step(g_phys.world, dt, 4);
}

void api_post_step(IPhysics_v1*, void* world_void, float /*dt*/) {
    auto* world = static_cast<ecs::World*>(world_void);
    if (!world || !g_phys.world_valid) return;
    g_world_for_calls = world;

    auto rb_id    = world->find_component_id("RigidBody");
    auto xform_id = world->find_component_id("Transform2D");
    if (!rb_id || !xform_id) return;

    ecs::ComponentId required[2] = { rb_id, xform_id };
    world->iterate_query(required, 2, nullptr, 0, post_step_thunk, nullptr);

    // ---- Collision event drain ----
    // Box2D 3 buffers contact + sensor events for one step at a time; we
    // read them after Step but before the next pre_step rebuilds bodies.
    // Each shape's userData carries the entity index (stamped at shape
    // creation time), so we can map shape -> entity in O(1).
    // Resolve a bare entity index to its live (index, generation) pair via
    // the world's slot table. Returns false when the slot isn't alive --
    // happens when a body's owner died this same frame and the event was
    // queued before the reaper compacted the registry. Skipping those
    // events prevents handlers from running on a dead/recycled entity
    // with a stale generation, which manifested as "trigger stops firing
    // after N spawn/destroy cycles" because the user's `Has<Tag>` check
    // would silently fail on a slot that had been reused for an unrelated
    // entity carrying a different component set.
    auto resolve = [&](int idx, uint32_t& out_idx, uint32_t& out_gen) -> bool {
        if (idx < 0) return false;
        const auto live = world->live_entity_for_index(static_cast<uint32_t>(idx));
        if (live.is_null()) return false;
        out_idx = live.index;
        out_gen = live.generation;
        return true;
    };

    if (g_phys.on_collision) {
        b2ContactEvents ce = b2World_GetContactEvents(g_phys.world);
        for (int i = 0; i < ce.beginCount; ++i) {
            const auto& ev = ce.beginEvents[i];
            uint32_t ai = 0, ag = 0, bi = 0, bg = 0;
            if (!resolve(entity_idx_from_shape(ev.shapeIdA), ai, ag)) continue;
            if (!resolve(entity_idx_from_shape(ev.shapeIdB), bi, bg)) continue;
            g_phys.on_collision(g_phys.collision_user, ai, ag, bi, bg);
        }
    }
    if (g_phys.on_trigger_enter || g_phys.on_trigger_exit) {
        b2SensorEvents se = b2World_GetSensorEvents(g_phys.world);
        for (int i = 0; i < se.beginCount; ++i) {
            const auto& ev = se.beginEvents[i];
            uint32_t si = 0, sg = 0, oi = 0, og = 0;
            if (!resolve(entity_idx_from_shape(ev.sensorShapeId),  si, sg)) continue;
            if (!resolve(entity_idx_from_shape(ev.visitorShapeId), oi, og)) continue;
            if (g_phys.on_trigger_enter)
                g_phys.on_trigger_enter(g_phys.collision_user, si, sg, oi, og);
        }
        for (int i = 0; i < se.endCount; ++i) {
            const auto& ev = se.endEvents[i];
            uint32_t si = 0, sg = 0, oi = 0, og = 0;
            if (!resolve(entity_idx_from_shape(ev.sensorShapeId),  si, sg)) continue;
            if (!resolve(entity_idx_from_shape(ev.visitorShapeId), oi, og)) continue;
            if (g_phys.on_trigger_exit)
                g_phys.on_trigger_exit(g_phys.collision_user, si, sg, oi, og);
        }
    }
}

// =============================================================================
// Module entry points.
// =============================================================================

void on_load(ModuleContext* ctx) {
    ZUES_LOG_INFO("physics_box2d on_load");
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = b2Vec2{g_phys.gravity_x, g_phys.gravity_y};
    g_phys.world = b2CreateWorld(&def);
    g_phys.world_valid = B2_IS_NON_NULL(g_phys.world);
    if (!g_phys.world_valid) {
        ZUES_LOG_ERROR("physics_box2d: failed to create b2World");
        return;
    }
    if (ctx && ctx->services) {
        ctx->services->register_service(ZUES_SERVICE_PHYSICS,
                                        ZUES_SERVICE_PHYSICS_VERSION,
                                        &g_iphys);
    }
}

void on_unload(ModuleContext*) {
    ZUES_LOG_INFO("physics_box2d on_unload");
    if (g_phys.world_valid) {
        b2DestroyWorld(g_phys.world);
        g_phys.world_valid = false;
    }
}

const ModuleInfo INFO = {
    .name        = "zues_physics_box2d",
    .version     = "0.1.0",
    .abi_version = ZUES_MODULE_ABI_VERSION,
    .on_load     = on_load,
    .on_ready    = nullptr,
    .on_update   = nullptr,
    .on_unload   = on_unload,
};

}  // namespace

ZUES_MODULE_EXPORT const ModuleInfo* zues_module_entry() { return &INFO; }
