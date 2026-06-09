# Timers & Random

Two utility services exposed by the host. Both run engine-side -- no
language changes, no new keywords. Just call from any system or `[OnLoad]`.

## Timers

```lync
def spawn_pipe(): void {
    p: EntityRef = CreateEntity();
    p.SetTransformPosition(8.0, RandomRange(-2.0, 2.0));
    /* ... attach colliders ... */
}

[OnLoad]
def boot(eng: ptr, host: ptr): void {
    SetInterval(2.0, spawn_pipe);   // pipe every 2 seconds
}
```

| Call                            | Returns | Notes                                                        |
|---------------------------------|---------|--------------------------------------------------------------|
| `SetTimeout(seconds, cb)`       | `int`   | one-shot, auto-cancels after firing                          |
| `SetInterval(seconds, cb)`      | `int`   | repeating; re-arms with the same period after each fire      |
| `CancelTimer(handle)`           | `int`   | `1` if a live timer was cancelled, `0` if handle was stale   |

`cb` is a parameterless `def f(): void`. The function-pointer value
coerces to `ptr` at the call site, the same way Each<T> callbacks do.

**When timers tick.** Once per frame, between PreUpdate and Update.
Callbacks see the world state a PreUpdate system would see this frame.
A timer scheduled *during* the current frame's PreUpdate / Update first
fires next frame.

**Timers don't survive world reload or hot reload.** Re-arm them in
`[OnLoad]` if they should run on every project boot. For delayed actions
that must persist across save/load, use a custom `Timer` component
instead -- ECS data is restored, host-side timer lists aren't.

**Cancellation is idempotent.** Calling `CancelTimer` on a handle whose
timer already fired (one-shots) or was already cancelled is a no-op
returning `0`. Safe to call without tracking timer state yourself.

**Drift.** Repeating timers re-arm by *adding* the period to the
remaining time, not by resetting -- so over many fires the cumulative
drift is bounded by a single `dt`, not by the full period times the
frame-rate variance.

## Random

```lync
[System("PreUpdate", "Game")]
def jitter(eng: ptr, dt: float, user: ptr): void {
    if (IsKeyPressed(KeyCode().R)) {
        RandomSeed(42);                 // reproducible
        x: float = RandomRange(0.0, 10.0);
        n: int   = RandomInt(1, 6);     // dice roll, both ends inclusive
        Log<float>(x);
        Log<int>(n);
    }
}
```

| Call                          | Returns | Range                              |
|-------------------------------|---------|------------------------------------|
| `RandomFloat()`               | `float` | `[0, 1)`                           |
| `RandomRange(lo, hi)`         | `float` | `[lo, hi)`                         |
| `RandomInt(lo, hi)`           | `int`   | `[lo, hi]` -- both ends inclusive  |
| `RandomSeed(seed)`            | `void`  | reseed the engine PRNG             |

The engine uses `mt19937_64` seeded from `steady_clock` at first use.
Reseed via `RandomSeed` for deterministic playthroughs (replays, tests,
golden recordings). The PRNG is single-threaded and shared across all
gameplay calls -- if you need an independent stream for procgen or
shuffling, draw from this one and seed your own local generator from
the result.

## What this is *not* (yet)

- **Async / await.** Timers cover the "do X in N seconds" use case. If
  you want linear `wait(1.0); do_a(); wait(0.5); do_b()` flow, that
  needs language-level coroutines, which we haven't shipped.
- **Per-callback state.** `cb` takes no args. To pass data, keep it on
  a [Singleton] component the callback reads, or close over a
  module-level variable. A `(cb, user_ptr)` overload could be added if
  enough projects need it.
