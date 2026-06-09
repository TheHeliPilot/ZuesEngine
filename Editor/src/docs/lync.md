# The Lync language

Lync is the project-side language Zues defaults to. It compiles to a
plain C99 source file which a backend C compiler turns into the project
DLL the editor hot-reloads on every save. Think of it as "C with the
sharp edges sanded off and ECS support baked in" -- no GC, no runtime
overhead, no surprise allocations. If you can write C++ you can write
Lync in a day.

You can also write your project in C++ -- every example in these docs
shows both. Lync just removes the boilerplate.

## File layout

Lync source lives under `<project>/src/`. Every `.lync` file under that
tree is auto-discovered, compiled together, and linked into your
project DLL. There's no main file; nothing to register; nothing to
include. The plugin scans, sees your `[Component]` / `[System]` /
`[OnLoad]` attributes, and wires them up.

The hidden `_zues_main.lync` next to your sources is an editor-managed
manifest -- never edit it.

## A complete example

```lync
// src/health.lync
[Component]
[Category("Combat")]
Health: struct {
    hp:  int,
    max: int
}

[System("Update", "Game")]
def damage_pulse(eng: ptr, dt: float, user: ptr): void {
    if (!IsKeyPressed(KeyCode().X)) { return; }
    Each<Health>(tick);
}

def tick(e: EntityRef, h: ref Health, dt: float): void {
    h.hp -= 10;
    if (h.hp <= 0) {
        DestroyEntity(e);
    }
}
```

Hit Ctrl+S in the Lync editor. The compiler runs, the DLL rebuilds,
the editor hot-reloads -- typically within a second. Your scene's
`Health` instances persist across the swap.

## Functions

```lync
def add(x: int, y: int): int {
    return x + y;
}

def greet(name: ptr): void {
    Log<ptr>(name);
}
```

- Trailing return type after `:`.
- `void` for "no return."
- No default args, no optional params, no overloading.

## Types

| Lync             | C equivalent       | Notes                                   |
| ---------------- | ------------------ | --------------------------------------- |
| `bool`           | `bool`             |                                         |
| `int`            | `int`              | platform int                            |
| `usize`          | `size_t`           | unsigned size; matches libc `size_t`. Use in extern decls for malloc/realloc/snprintf size args. |
| `float`          | `float`            | 32-bit IEEE                             |
| `f64`            | `double`           |                                         |
| `ptr`            | `void*`            | opaque                                  |
| `char[N]`        | `char[N]`          | inline buffer, fixed size               |
| `Vec2`/`3`/`4`   | `Engine::math::vec*` | x/y/z/w fields                       |
| `Color`          | `Engine::math::color` | r/g/b/a 0..1                          |
| `EntityRef`      | `ZuesEntity`       | (index, generation) pair                |
| `PrefabRef`/etc. | `Engine::Handle`   | (index, generation), 0 = null           |

There's no `string` type yet -- use `char[N]` buffers in components and
copy with `SetText` (Text component). Variable-length strings come in
v6.

## Variables

```lync
hp:    int   = 100;             // explicit type
max         = 100;              // type inferred (=int from literal)
v:     Vec2  = Vec2{ x: 0, y: 0 };
e:     EntityRef = CreateEntity();
mgr:   GameManager? = GameManager();   // nullable
```

`var: T = expr` is the long form; just `var = expr` infers `T` from
the right-hand side.

## Ownership: `ref` and `own`

Lync has three pointer flavors:

- **`ref T`** -- non-null borrowed pointer. Doesn't own; can't outlive
  the source. Used for `Each<T>` callback args (`v: ref Velocity`).
- **`own T`** -- owning pointer. The receiver is responsible for
  freeing. Rare in gameplay code; used for arena allocations.
- **`T?` / `ref T?` / `own T?`** -- nullable variant. Unwrap with
  `match`.

```lync
def heal(h: ref Health, amount: int): void {
    h.hp = h.hp + amount;     // h is non-null; no check needed
    if (h.hp > h.max) { h.hp = h.max; }
}

def maybe_heal(e: EntityRef): void {
    match e.Get<Health>() {
        some(h): heal(h, 10);   // h: ref Health inside this arm
        null:    { /* no Health on this entity */ }
    }
}
```

## Match expressions

`match` is the only way to inspect a nullable. Both arms must be
present; the compiler errors on a non-exhaustive match.

```lync
match e.Get<Health>() {
    some(h): { h.hp -= 1; }
    null:    { LogWarn("entity has no Health"); }
}
```

A single-line arm uses `:`; a block arm uses `{ ... }`.

## Control flow

```lync
if (cond)        { ... }
if (cond)        { ... } else if (cond2) { ... } else { ... }

while (count > 0) { count -= 1; }

// for is range-based:
for (i: int = 0; i < 10; i += 1) {
    Log<int>(i);
}
```

`while (count)` is fine -- non-zero ints are truthy.

## Operators

```lync
// arithmetic + assignment
hp += 10;     hp -= 1;     hp *= 2;     hp /= 2;     hp %= 4;
hp++;         hp--;
v.x += dx;    arr[i] += 1;

// comparison
if (a == b) ...
if (a != b) ...
if (a < b)  ...

// boolean
if (alive && hp > 0)            ...
if (paused || at_menu)          ...
if (!alive)                     ...
```

`==` works on ints, floats, EntityRefs, and pointers. There's no `===`
distinction.

### Pipelines

`|>` reads top-to-bottom. `x |> f` is `f(x)`; `x |> f(a, b)` is
`f(x, a, b)`. The first operand becomes the first argument of the
call on the right. Chains naturally:

```lync
result: int = x
    |> damage
    |> scale(1.5f)
    |> clamp(0, 99);
```

### `defer`

Stash a statement to run at function exit (LIFO with other defers).
Every `return` path -- including early-exits in branches -- flows
through the deferred bodies. Compiles to a `goto __zues_cleanup`
label, no runtime cost.

```lync
def load(path: string): int {
    f: own ptr = open(path);
    defer close(f);                   // single statement OR a block
    defer { /* multiple statements */ };

    if (!read_into(f)) { return -1; } // close(f) still runs
    return 0;                          // close(f) still runs
}
```

Function-scope (defers in a `for` body run at function exit, not
loop-iteration exit — same as Go).

### `unsafe` blocks

Declarative marker. Compiles to a regular block; the keyword exists
so reviewers can `grep -n unsafe` to find the code that opted out of
the engine's usual safety guidance.

```lync
unsafe {
    p: ptr = base;
    n: int = *(p + 4);
}
```

### Labelled loops

Loops can carry a label so `break` / `continue` can jump out of
arbitrary nesting:

```lync
for(i: 0 to grid_w) as outer {
    for(j: 0 to grid_h) {
        if (grid[i][j] == target) { break outer; }   // exits BOTH loops
    }
}
```

Plain `break;` / `continue;` still target the innermost loop.

## Structs

```lync
Point: struct {
    x: float,
    y: float
}

p: Point = Point{ x: 1.5, y: 2.0 };
p.x = 10.0;
Log<float>(p.x);
```

Struct field access is `.` for both value and pointer types -- the
compiler picks `.` vs `->` for you.

### Methods + operator overloads + static

Structs can carry methods, operator overloads, and static constructors.
Everything compiles to flat C functions named `<Type><Method>` — no
vtables, no dispatch tables, zero runtime tax. `this` is the implicit
receiver (`self` also works).

```lync
Vec2: struct {
    x: float, y: float,

    // Static constructors -- callable without an instance.
    def static zero(): Vec2 { return Vec2{ x: 0.0f, y: 0.0f }; },
    def static of(x: float, y: float): Vec2 {
        return Vec2{ x: x, y: y };
    },

    // Operator overloads. Supported: + - * / % == != < > <= >= !
    // Unary `-` (no params) and binary `-` (one param) both legal.
    def +(o: Vec2): Vec2 { return Vec2{ x: this.x + o.x, y: this.y + o.y }; },
    def -(): Vec2        { return Vec2{ x: -this.x, y: -this.y }; },

    // Instance methods.
    def length_sq(): float { return this.x * this.x + this.y * this.y; },

    // Visibility.
    private secret: int,
    private def helper(): int { return 1; }
}

def main(): int {
    a: Vec2 = Vec2.of(3.0f, 4.0f);     // static call -- no instance
    b: Vec2 = Vec2.zero();
    c: Vec2 = a + b;                    // op overload
    n: Vec2 = -c;                       // unary op
    s: float = c.length_sq();           // UFCS dispatch
    return 0;
}
```

Methods on a standalone struct work the same way:

```lync
def Vec2.dot(o: Vec2): float {
    return this.x * o.x + this.y * o.y;
}
def Vec2.static unit_x(): Vec2 {
    return Vec2{ x: 1.0f, y: 0.0f };
}
```

Private fields and methods are only reachable from inside the
struct's own methods. The compiler enforces it at analyser time.

## Attributes

Attributes mark a `def` or `struct` for the plugin to wire up. Most
common ones:

| Attribute          | Goes on   | What it does                                    |
| ------------------ | --------- | ----------------------------------------------- |
| `[Component]`      | `struct`  | Register as an ECS component.                   |
| `[Singleton]`      | `struct`  | (Use with `[Component]`.) Auto-create one.      |
| `[Category("...")]` | `struct` | Inspector "Add Component" path.                |
| `[System(phase, domain)]` | `def` | Register as a system in that phase + domain. |
| `[OnLoad]`         | `def`     | Run at project DLL load.                        |
| `[OnUpdate]`       | `def`     | Run every frame.                                |
| `[OnUnload]`       | `def`     | Run at project DLL unload.                      |
| `[OnCollision]`    | `def`     | Box2D contact-begin.                            |
| `[OnTriggerEnter]` | `def`     | Sensor-collider entered.                        |
| `[OnTriggerExit]`  | `def`     | Sensor-collider left.                           |

Multiple `[OnLoad]` / `[OnUpdate]` etc. across files are all called.

Attributes can also be written with the optional Rust-style `#` prefix:
`#[OnLoad]` and `[OnLoad]` are interchangeable.

Codegen-mapped attributes (translate to C compiler directives):

| Attribute       | Effect                                                         |
| --------------- | -------------------------------------------------------------- |
| `[inline]`      | emits `static inline` -- the C compiler will inline call sites |
| `[noinline]`    | emits `__attribute__((noinline))`                              |
| `[pure]`        | emits `__attribute__((pure))` -- enables CSE                   |
| `[const_attr]`  | emits `__attribute__((const))` -- even purer (no global reads) |

```lync
#[inline] def Vec2.length_sq(): float { return this.x*this.x + this.y*this.y; }
#[pure]   def clamp(v: int, lo: int, hi: int): int { ... }
```

## ECS helpers (auto-injected)

Once a `[Component] T` is registered, these are available everywhere:

```lync
e.Add<T>(T{ ...fields })       // attach a copy
e.Remove<T>()                  // detach
e.Has<T>()                     // bool presence check
e.Get<T>()                     // ref T?  (nullable -- use match)
Each<T>(callback)              // iterate every T
```

`Each<T>`'s callback signature is `(EntityRef, ref T, float) -> void`.
The `ref` means the pointer is non-null inside the body -- mutate
freely, no match needed.

`Singleton<T>()` returns `T?` for any `[Singleton]` component, with a
cached pointer that costs one int compare in steady state.

## Templates

`def name<T>(...)` and `struct Name<T> { ... }` give you C++-style
monomorphisation. Same syntax as the helpers above use.

```lync
def double_it<T>(x: T): T {
    return x + x;
}

a: int   = double_it<int>(5);    // 10
b: float = double_it<float>(1.5); // 3.0
```

See the `templates` topic for the longer write-up.

## Prelude built-ins

The plugin auto-injects a small standard library. Highlights:

```lync
// Logging
LogDebug("trace");  LogInfo("hi");  LogWarn("oops");  LogError("bad");
Log<int>(score);     Log<float>(velocity.x);  Log<bool>(active);

// Input
if (IsKeyDown(KeyCode().W)) { ... }
if (IsKeyPressed(KeyCode().Space)) { ... }   // edge: pressed this frame
if (IsKeyReleased(KeyCode().LeftShift)) { ... }

// Random
RandomFloat()                 // [0, 1)
RandomRange(0.0, 10.0)        // [lo, hi)
RandomInt(1, 6)               // [lo, hi]  (both ends inclusive)
RandomSeed(42)

// Timers
SetTimeout(1.5, on_done)
SetInterval(2.0, spawn_pipe)
CancelTimer(handle)

// Entities
e: EntityRef = CreateEntity();
e.SetTransform(x, y, rotation, sx, sy);
DestroyEntity(e);

// Prefabs
b: EntityRef = Instantiate("assets/prefabs/Bullet.zprefab", x, y);
b: EntityRef = InstantiatePrefab(my_prefab_ref, x, y);    // GUID-keyed
```

Full list in autocomplete -- press Ctrl+Space in the Lync editor.

## Low-level primitives

For container code, allocator wrappers, and anything that has to talk
to libc, three primitives are available:

```lync
// sizeof(T): yields a usize at compile time.
sz: usize = sizeof(int);
sz_struct: usize = sizeof(MyStruct);

// addr_of(x): yields a ptr to the local x. Use to memcpy a value into
// a raw heap buffer.
n: int = 42;
p: ptr = addr_of(n);

// Pointer arithmetic: ptr + int / ptr - int / ptr - ptr.
slot: ptr = buffer + index * sizeof(int);
```

These three together let you write generic allocators, stride-walked
buffers, and "memcpy a T into slot[i]" patterns in pure Lync. Reach
for them when stdlib `List<T>` doesn't fit.

## Standard library

Lync ships with a small stdlib in `lync/stdlib/` next to the compiler.
Import a module to pull its functions into scope:

```lync
include std.math.*;       // Sqrt, Sin, Cos, Pi(), Min<T>, Max<T>, Clamp<T>, Lerp<T>, Abs<T>
include std.list.*;       // ListNew<T>, ListPush<T>, ListGet<T>, ListPop<T>, ListSet<T>, ListRemove<T>, ListClear<T>, ListFree<T>
include std.string.*;     // StrLen, StrEq, StrStartsWith, StrEndsWith, StrContains
include std.hash.*;       // HashInt, HashStr (full HashMap lands with real Strings)
include std.mem.*;        // Alloc, Realloc, MemCopy, MemSet, Swap<T>

// Single-import form for a focused pull:
include std.math.Sqrt;
include std.math.Clamp;
```

Highlights:

- **`std.math`** -- `Sqrt`, `Floor`, `Ceil`, `Pow`, `Sin`, `Cos`,
  `Pi()`, `E()`, plus generic `Min<T>`, `Max<T>`, `Clamp<T>`,
  `Abs<T>`, `Lerp<T>`. The generics monomorphise per-type at compile
  time, so using them on `int` and `float` produces specialised code
  with no branch overhead. Float overloads (`SqrtF`/`SinF`/`CosF`)
  exist for hot 32-bit paths.
- **`std.list`** -- a growable `List<T>` with `ListPush`, `ListPop`,
  `ListGet`, `ListSet`, `ListRemove`, `ListClear`, `ListFree`. Backed
  by the heap; `ListFree` releases the buffer.

  **Method-style call.** Every `<StructName><Method>` function can be
  invoked as a method on the receiver; the compiler infers the type
  args from the receiver and rewrites to the mangled form:
  ```lync
  xs: List<int> = ListNew<int>(4);
  xs.Push(7);          // = ListPush<int>(xs, 7)
  xs.Set(0, 99);       // = ListSet<int>(xs, 0, 99)
  g: ptr? = xs.Get(1); // = ListGet<int>(xs, 1)
  xs.Free();           // = ListFree<int>(xs)
  ```
  Works for any user-defined struct, not just stdlib types -- write
  a function `MyThingDoSomething(self: ref MyThing, ...)` and you
  can call `obj.DoSomething(...)`.
- **`std.string`** -- `StrLen`, `StrEq`, `StrStartsWith`,
  `StrEndsWith`, `StrContains`. Construction (concat / format /
  int-to-str) ships with the real `String` type in the next pass.
- **`std.hash`** -- `HashInt` (Knuth multiplicative), `HashStr`
  (djb2). The full `HashMap<K, V>` lands once generic key equality +
  real Strings are available.
- **`std.mem`** -- `Alloc`, `Realloc`, `MemCopy`, `MemSet`,
  `Swap<T>`. Thin wrappers over libc that work with the new
  `sizeof(T)` + `addr_of(x)` + `ptr+int` primitives.

## Engine math helpers

The Zues prelude (auto-injected, no import needed) adds Vec2 + Color
helpers tuned for engine types:

```lync
// Vec2 -- everything you need for HUD layout, camera follow,
// damage falloff, projectile aiming.
v: Vec2 = Vec2Of(3.0, 4.0);
len:  float = Vec2Length(v);              // 5.0
dist: float = Vec2Distance(player, enemy);
dir:  Vec2  = Vec2Normalize(Vec2Sub(target, origin));
mid:  Vec2  = Vec2Lerp(start, end, 0.5);

// Color -- presets + RGB[A] builders + Lerp.
tx.color = ColorRed();
tx.color = ColorRGB(0.2, 0.5, 0.8);
faded   := ColorLerp(ColorWhite(), ColorClear(), 0.7);

// Easing curves -- pair with Lerp to tween.
t01: float = Clamp01(elapsed / duration);
e:   float = EaseOutCubic(t01);
pos: Vec2  = Vec2Lerp(from, to, e);

// Float helpers (without needing `using std.math.*;`).
hp = Approach(hp, target_hp, 50.0 * dt);   // chase target at 50/sec
v  = Clamp(v, 0.0, 100.0);
```

Full list (see `LyncPlugin/zues_api.lync`):

| Vec2                 | Color           | Easing            | Scalar     |
| -------------------- | --------------- | ----------------- | ---------- |
| `Vec2Zero/One/Of`    | `ColorWhite`    | `EaseInQuad`      | `Lerp`     |
| `Vec2Add/Sub/Scale`  | `ColorBlack`    | `EaseOutQuad`     | `Clamp`    |
| `Vec2Dot`            | `ColorRed/Green/Blue` | `EaseInOutQuad` | `Clamp01` |
| `Vec2Length(Sq)`     | `ColorYellow`   | `EaseInCubic`     | `Approach` |
| `Vec2Distance(Sq)`   | `ColorClear`    | `EaseOutCubic`    |            |
| `Vec2Normalize`      | `ColorRGB[A]`   |                   |            |
| `Vec2Lerp`           | `ColorLerp`     |                   |            |

### Transform2D look-at

UFCS helpers for aiming a sprite at a world-space point. Convention: at
`rotation = 0`, the right vector is +X and the up vector is +Y.

```lync
match e.Get<Transform2D>() {
    some(t): {
        t.LookAtRight(target_pos);   // sprite "front" = +X local
        // or
        t.LookAt(target_pos);        // sprite "front" = +Y local
    }
    null: {}
}
```

Both are no-ops when `target` coincides with `t.position` (no NaN). Pick
`LookAtRight` for sprites drawn facing right at zero rotation, `LookAt`
for sprites drawn facing up.

## Common patterns

### "I want to do X every N seconds"

```lync
[OnLoad]
def boot(eng: ptr, host: ptr): int {
    SetInterval(2.0, spawn_pipe);
    return 0;
}

def spawn_pipe(): void {
    e: EntityRef = Instantiate("assets/prefabs/Pipe.zprefab",
                                8.0, RandomRange(-2.0, 2.0));
}
```

### "When the player hits anything ..."

```lync
[OnCollision]
def on_hit(eng: ptr, a: EntityRef, b: EntityRef): void {
    // a and b are the two contacting entities. Inspect components to
    // figure out which is the player.
    if (a.Has<PlayerData>()) { take_damage(a, b); return; }
    if (b.Has<PlayerData>()) { take_damage(b, a); return; }
}
```

### "Project-wide config"

Use a `[Singleton]` component. Drag refs into it from the Inspector.
Read it from any system with `Singleton<T>()`. See the `components`
topic.

## Limits + footguns (v5)

- Strings are fixed-size `char[N]` buffers; no dynamic strings yet.
- No exceptions, no panics. Use sentinel values (`generation == 0`,
  `null`) for failure.
- No closures over local state in callbacks -- if you need to pass
  data into a timer or `Each<T>` callback, put it on a singleton or
  a component.
- The C backend is invoked behind your back; Lync compile errors
  surface in the Console panel and as red wavy underlines on the
  offending line.

## Where to next

- **Components** -- declare data shapes, asset references, singletons.
- **Systems** -- per-frame behaviour, phases, domains.
- **Lifecycle** -- `[OnLoad]` / `[OnUpdate]` / `[OnUnload]`.
- **Templates** -- the longer write-up on monomorphisation.
- **Timers & Random** -- the engine-side utility services.
