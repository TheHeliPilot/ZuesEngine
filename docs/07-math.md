# Math

User-facing types in `Engine::math::`. Glm backs the SIMD-heavy paths; users never include glm.

## Types (all in `Engine::math`)

- `vec2`, `vec3`, `vec4` — float vectors, `{x, y, z, w}` fields, `operator==` default
- `ivec2`, `ivec3` — integer vectors
- `mat3`, `mat4` — column-major
- `quat` — unit quaternion
- `rect` — `{x, y, w, h}` 2D AABB
- `aabb` — 3D AABB `{min, max}`
- `transform` — position + rotation + scale, decomposed (matrix on demand)
- `color` — `{r, g, b, a}` float 0..1

Common types are aliased into `Engine::` as well. `Engine::vec2` works; so does `Engine::math::vec2`.

## Example

```cpp
using namespace Engine::math;
vec2 pos{10, 20};
vec2 vel{1, 0};
pos += vel * dt;
float d = length(pos - target);
```

## Backing

- Each type has a plain-data struct layout (`float x, y;` etc.) in the public header.
- Functions that do SIMD-friendly work (`normalize`, `mul(mat4, vec4)`, quaternion slerp) are declared in headers, defined in `zues_core.dll`'s `.cpp` files where they use glm internally.
- **Glm is a private dependency of `zues_core.dll`.** User project.dll never sees glm headers. Only `Engine::math::` crosses the user boundary.

## Why not expose glm directly

- Users shouldn't need to know a third-party library.
- We can add engine-specific helpers (`clamp_to_rect`, `from_angle`, `look_at_2d`) without touching glm.
- If we replace glm later (e.g., our own SIMD), no user code changes.

## Not fixed-point

Engine uses `float` freely. We're doing client-server + prediction, not deterministic rollback. Rollback could ship later as an optional netcode module with its own math types.

## Interop helpers (`zues_core.dll` internals only)

Private header `core/math/glm_interop.h` has `to_glm` / `from_glm` conversions. Module internals that want raw glm go through this. User code never does.
