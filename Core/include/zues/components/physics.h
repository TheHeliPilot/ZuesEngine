#pragma once

// Physics components - read by the Physics_Box2D module. Live in Core so
// every host (editor, runtime, tests) sees the same memory layout.
//
// Three-component split mirrors most ECS physics designs:
//   RigidBody      - dynamics (mass, damping, gravity scale, body type)
//   BoxCollider    - rectangular shape attached to the rigid body
//   CircleCollider - circular shape attached to the rigid body
//
// Position + rotation come from Transform2D (already on every entity
// that has a body). The physics module syncs:
//   PrePhysics  : Transform2D -> b2Body for kinematic + static bodies
//   PostPhysics : b2Body      -> Transform2D for dynamic bodies
//
// The opaque `_body_handle` slot is owned by the physics module and must
// not be touched by user code. World save/load skips it (FieldKind::Handle
// already serializes as zero so reload reattaches cleanly).

#include <zues/api.h>
#include <zues/ecs/reflection.h>
#include <zues/math/vec2.h>

namespace Engine::components {

// Body type values mirror Box2D's b2BodyType so the module can pass them
// through directly. Kept as plain int for simple reflection.
enum BodyType : int {
    BodyType_Static    = 0,
    BodyType_Kinematic = 1,
    BodyType_Dynamic   = 2,
};

// RigidBody owns the body-wide physics material now: density, friction,
// and restitution were per-collider in v1 (Box2D's natural layout) but
// almost every game wants them shared across all of an entity's shapes.
// Per-shape control is recoverable later via an [advanced] override
// struct if needed; for now the simpler model wins.
struct RigidBody {
    int   body_type      = BodyType_Dynamic;  // see BodyType_*
    float mass           = 1.0f;              // ignored for static; kg
    float linear_damping = 0.0f;
    float angular_damping= 0.0f;
    float gravity_scale  = 1.0f;              // 0 = ignore world gravity
    int   fixed_rotation = 0;                 // 1 = freeze rotation (good for player capsule)
    int   is_bullet      = 0;                 // 1 = continuous CCD (fast-moving small bodies)

    // Material properties (apply to every collider on this entity).
    float density        = 1.0f;
    float friction       = 0.3f;
    float restitution    = 0.0f;

    // Engine-owned. Don't touch from user code. b2BodyId stored as 64-bit
    // opaque so we can swap physics backends without changing the layout.
    unsigned long long _body_handle = 0;
};

struct BoxCollider {
    Engine::math::vec2 half_extents{0.5f, 0.5f};   // visible as Vec2 in inspector
    Engine::math::vec2 offset{0.0f, 0.0f};         // local offset from entity transform
    int   is_sensor  = 0;        // 1 = trigger (no contact response, OnTriggerEnter/Exit fires)
    int   edit_in_scene = 0;     // 1 = show drag handles in scene viewport

    unsigned long long _shape_handle = 0;  // b2ShapeId
};

struct CircleCollider {
    float radius     = 0.5f;
    Engine::math::vec2 offset{0.0f, 0.0f};
    int   is_sensor  = 0;
    int   edit_in_scene = 0;

    unsigned long long _shape_handle = 0;
};

}  // namespace Engine::components

ZUES_COMPONENT_FIELDS(Engine::components::RigidBody,
    body_type, mass, linear_damping, angular_damping,
    gravity_scale, fixed_rotation, is_bullet,
    density, friction, restitution,
    _body_handle);

ZUES_COMPONENT_FIELDS(Engine::components::BoxCollider,
    half_extents, offset, is_sensor, edit_in_scene, _shape_handle);

ZUES_COMPONENT_FIELDS(Engine::components::CircleCollider,
    radius, offset, is_sensor, edit_in_scene, _shape_handle);
