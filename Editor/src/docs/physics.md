# Physics

Box2D 3.0 powers physics. Bodies are addressed by entity index. Add a
`RigidBody` plus one or more collider components and the engine takes
care of stepping, syncing positions, and dispatching collision events.

## Components

| Component        | Purpose                                          | Category         |
| ---------------- | ------------------------------------------------ | ---------------- |
| `RigidBody`      | dynamics + material (mass, damping, density, friction, restitution) | Engine/Physics   |
| `BoxCollider`    | rectangle shape on the body                      | Engine/Physics   |
| `CircleCollider` | circle shape on the body                         | Engine/Physics   |

### `RigidBody`

| Field             | Kind  | Notes                                             |
| ----------------- | ----- | ------------------------------------------------- |
| `body_type`       | enum  | `Static` / `Kinematic` / `Dynamic` (default)      |
| `mass`            | f32   | kg. Ignored for static bodies.                    |
| `linear_damping`  | f32   | velocity decay                                    |
| `angular_damping` | f32   | angular velocity decay                            |
| `gravity_scale`   | f32   | `0` ignores world gravity. Default `1.0`.         |
| `density`         | f32   | material -- applied to every collider on this body |
| `friction`        | f32   | material                                          |
| `restitution`     | f32   | bounce: 0 = none, 1 = perfectly elastic           |
| `fixed_rotation`  | bool  | freeze rotation (good for player capsules)        |
| `is_bullet`       | bool  | enable continuous collision (fast small bodies)   |

Density / friction / restitution **live on the body**, not on each
collider. One material per body keeps the inspector clean and matches
how Unity's Rigidbody2D behaves. A future `PhysicsMaterial` asset will
let you share materials across bodies.

### `BoxCollider`

| Field           | Kind | Notes                                            |
| --------------- | ---- | ------------------------------------------------ |
| `half_extents`  | Vec2 | half-width, half-height (1 unit = 1 cm)          |
| `offset`        | Vec2 | shape offset from body center                    |
| `is_sensor`     | bool | `true` makes it a trigger (no contact response)  |
| `edit_in_scene` | bool | show resize handles in the Scene viewport        |

### `CircleCollider`

| Field           | Kind | Notes                          |
| --------------- | ---- | ------------------------------ |
| `radius`        | f32  | (1 unit = 1 cm)                |
| `offset`        | Vec2 | shape offset from body center  |
| `is_sensor`     | bool | trigger volume                 |
| `edit_in_scene` | bool | show radius handle in Scene    |

Position + rotation come from `Transform2D` (already on every entity).
The physics module syncs:

- **Pre-step**: `Transform2D` -> `b2Body` for static + kinematic bodies
  (so editor moves register).
- **Post-step**: `b2Body` -> `Transform2D` for dynamic bodies (so the
  renderer sees the new positions).

## Setup

```lync
e: EntityRef = CreateEntity();
e.SetTransform(0.0, 5.0, 0.0, 1.0, 1.0);
e.Add<RigidBody>(RigidBody{
    body_type:       2,        // Dynamic
    mass:            1.0,
    linear_damping:  0.0,
    angular_damping: 0.0,
    gravity_scale:   1.0,
    density:         1.0,
    friction:        0.3,
    restitution:     0.0,
    fixed_rotation:  true,
    is_bullet:       false
});
e.Add<BoxCollider>(BoxCollider{
    half_extents:  Vec2{ x: 0.5, y: 0.5 },
    offset:        Vec2{ x: 0.0, y: 0.0 },
    is_sensor:     false,
    edit_in_scene: false
});
```

```cpp
auto e = host->create_entity(eng);
host->set_transform(eng, e, 0.0f, 5.0f, 0.0f, 1.0f, 1.0f);
zues::Add<Engine::components::RigidBody>(e, {
    .body_type      = Engine::components::BodyType::Dynamic,
    .mass           = 1.0f,
    .density        = 1.0f,
    .friction       = 0.3f,
    .gravity_scale  = 1.0f,
    .fixed_rotation = true,
});
zues::Add<Engine::components::BoxCollider>(e, {
    .half_extents = {0.5f, 0.5f},
    .offset       = {0.0f, 0.0f},
});
```

## Engine API

The physics module registers `IPhysics_v1`. Lync wrappers in the prelude:

| Function                                  | What it does                            |
| ----------------------------------------- | --------------------------------------- |
| `ApplyImpulse(e, fx, fy)`                 | instant velocity change (kg*cm/s)       |
| `ApplyForce(e, fx, fy)`                   | continuous force (kg*cm/s^2)             |
| `SetVelocity(e, vx, vy)`                  | hard-set linear velocity                |
| `SetBodyPosition(e, x, y, rot)`           | teleport (use sparingly on dynamics)    |
| `WakeBody(e)`                             | re-enable simulation if Box2D slept it  |

C++ code calls `IPhysics_v1` directly (`get_service` for `"zues.physics"`
v1).

World gravity defaults to `(0, -981)` cm/s^2 -- matches `Transform2D`'s
1 unit = 1 cm convention so a 1-unit-tall body falls realistically.

## Collision + trigger hooks

Three Lync attributes register callbacks:

```lync
[OnCollision]
def on_hit(eng: ptr, a: EntityRef, b: EntityRef): void {
    // a + b are the two entities that touched (both have non-trigger
    // colliders). Use a.Get<T>() / b.Get<T>() to inspect components.
}

[OnTriggerEnter]
def on_enter(eng: ptr, self: EntityRef, other: EntityRef): void {
    // self has a sensor collider; other entered it
}

[OnTriggerExit]
def on_exit(eng: ptr, self: EntityRef, other: EntityRef): void {
    // other left self
}
```

Type one of these on its own line and press Enter in the Lync editor --
the function skeleton autocompletes with the cursor inside the body.

C++ exposes the same hooks through `ZuesProjectApi::on_collision` /
`on_trigger_enter` / `on_trigger_exit` (function pointer slots).

## In-scene collider editing

Toggle `edit_in_scene` on the collider (`true` in the inspector). The
Scene viewport then draws drag handles:

- **Box** -- four mid-edge handles. Drag to grow `half_extents` on that
  axis. **ALT** held during drag mirrors the change (keeps the box
  centered around `offset`); default drag offsets the opposite edge.
- **Circle** -- single handle on the +X edge. Drag to set `radius`.

The drag wraps in undo (`undo_begin` / `undo_commit`), so **Ctrl+Z**
restores the previous shape.

## Sensor visualisation

Sensors render **blue** (`(0.30, 0.70, 1.00)`); solid colliders render
**green**. The colour is shown in the Scene viewport's gizmo overlay so
you can spot trigger volumes at a glance.

## Performance notes

- Step rate: fixed 60 Hz with 4 substeps -- `dt`-independent.
- Position units = `Transform2D` units (1 unit = 1 cm). Internal Box2D
  scale is 1.0; positions pass through unchanged.
- Body / shape handles are cached on the component itself
  (`_body_handle`, `_shape_handle`) and survive world save/load. After
  deserialisation the handles read as 0 and the physics module
  re-creates them on the next pre-step.
- Static colliders never sync from `Transform2D` after creation unless
  the editor mutates them -- cheap.

## Play / Stop reset

Hitting Play snapshots the world; Stop restores it. Box2D state is
rebuilt on Stop via `IPhysics_v1::reset_world` so contact pairs and
sleeping flags don't bleed into the editor's edit-mode session.

## Limits + footguns

- `body_type` changes after creation re-create the underlying b2Body
  but don't re-issue contact-end events for already-touching bodies.
- Raycast lives in the service vtable but isn't yet exposed to Lync;
  C++ projects can call it directly.
- The `_body_handle` / `_shape_handle` slots are engine-owned. Don't
  read them from gameplay code.
