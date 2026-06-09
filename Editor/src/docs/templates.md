# Templates

Templates let you write one function or struct definition that works for
any type. Each distinct instantiation is compiled separately -- pure
compile-time monomorphisation, zero runtime overhead, no boxing, no vtable.
Behaviour is duck-typed: if the substituted type supports the operations
used in the body, the instantiation compiles; if not, you get an error
pointing at the call site.

## Why

- Zero runtime cost -- the compiler produces a specialised copy per type,
  identical to hand-writing it for each type.
- Write the logic once; use it for `int`, `float`, a component struct, or
  anything else.
- Semantics match C++ templates exactly -- same monomorphisation model, same
  duck-typing at instantiation.

## Functions

`def name<T>(...)` declares a template function. The type argument is always
written explicitly at the call site.

```lync
def double_it<T>(x: T): T {
    return x + x;
}

a: int   = double_it<int>(5);     // produces 10
b: float = double_it<float>(1.5); // produces 3.0
```

```cpp
template<typename T>
T double_it(T x) {
    return x + x;
}

int   a = double_it<int>(5);
float b = double_it<float>(1.5f);
```

No inference in v1 -- you must always supply `<T>` at the call site.

## Structs

`struct Name<T> { ... }` declares a template struct. Fields may use `T`
freely.

```lync
struct List<T> {
    data:     ptr,   // pointer to T[]
    length:   int,
    capacity: int
}

def list_get<T>(list: ptr, i: int): T {
    // duck-typed: assumes list.data is indexable as T*
    return list.data[i];
}

nums: List<int>;
val:  int = list_get<int>(nums, 0);
```

```cpp
template<typename T>
struct List {
    T*  data     = nullptr;
    int length   = 0;
    int capacity = 0;
};

template<typename T>
T list_get(List<T>* list, int i) {
    return list->data[i];
}

List<int> nums{};
int val = list_get<int>(&nums, 0);
```

Each instantiation (`List<int>`, `List<float>`, ...) is a distinct type with
its own symbol. The mangled form (`List__int`) is compiler-internal; you
never write or see it.

## Multiple type parameters

Separate parameters with commas: `<K, V>`.

```lync
struct Pair<K, V> {
    key: K,
    val: V
}

def make_pair<K, V>(k: K, v: V): Pair<K, V> {
    return Pair<K, V> { key: k, val: v };
}

p: Pair<int, float> = make_pair<int, float>(1, 3.14);
```

```cpp
template<typename K, typename V>
struct Pair {
    K key;
    V val;
};

template<typename K, typename V>
Pair<K, V> make_pair(K k, V v) {
    return Pair<K, V>{ k, v };
}

Pair<int, float> p = make_pair<int, float>(1, 3.14f);
```

Mangled symbol for `make_pair<int, float>` is `make_pair__int_float`.

## How errors look

Errors are reported at the instantiation site, not inside the template body.
If the substituted type does not support an operation, you see:

```lync
struct NoAdd {
    x: int
}

def double_it<T>(x: T): T {
    return x + x;   // '+' not defined for NoAdd
}

bad: NoAdd = double_it<NoAdd>(NoAdd { x: 1 });
```

```
error: operator '+' is not defined for type 'NoAdd'
  while instantiating double_it<NoAdd> at game/src/main.lync:9
```

The message names the template, the type argument, and the file and line
where you called it. The template body line is shown in the detail trace
below the header, but the top-level pointer is always the use site.

## What is allowed in a body

The body is duck-typed: any operation that the substituted type supports is
valid. There is no explicit constraint declaration in v1.

| Operation             | Works when T has ...                      |
| --------------------- | ----------------------------------------- |
| `x + y`               | `+` operator defined                      |
| `x.field`             | a field named `field`                     |
| `x.method()`          | a method or UFCS function named `method`  |
| `x[i]`                | `[]` operator or is a raw pointer         |
| pass by value         | T is trivially copyable (always true for  |
|                       | engine component structs)                 |

If T does not support the operation the compiler errors at instantiation
(see above). There is no way to restrict T to only types with a certain
field or operator in v1 -- that requires constraints, which are coming later.

## What is NOT in v1

- **Type inference at call sites** -- always write `name<int>(...)`, never
  `name(...)` with an inferred type.
- **Constraints / traits** -- no `where T: Addable` or similar. Duck-typing
  only; bad instantiations are caught at compile time but the message points
  at the use site, not a trait bound.
- **Default type parameters** -- `struct Foo<T = int>` is not supported.
- **Variance annotations** -- no covariance or contravariance; each
  instantiation is a fully independent type.

These are planned post-v1 features. The syntax placeholders above are not
reserved -- do not write them expecting a useful diagnostic.

## In the engine

The engine's component helpers are templates under the hood. On the C++ side
`zues::Add<Health>(e, ...)`, `zues::Get<Health>(e)`, and
`zues::Each<Health>(cb)` are all template instantiations generated from the
registration macro. On the Lync side the compiler emits free functions
(`AddHealth`, `GetHealth`, `EachHealth`) that are each a monomorphised
specialisation -- you do not write the `<T>` yourself because the code
generator has already done it.

The standard library follows the same pattern. `std.list` exposes `List<T>`,
`std.math` exposes helpers like `clamp<T>`, and so on. Import the module
then instantiate with an explicit type argument.

```lync
import std.list;

scores: List<int>;
```

```cpp
#include <zues/std/list.h>

zues::std::List<int> scores{};
```

## See also

- **Components** -- explains `Add<T>`, `Get<T>`, `Each<T>`, the component
  registration macros, UFCS shorthand, and the asset-ref field types.
- **Quickstart** -- minimal project setup before writing template code.
- **Systems** -- how to use template helpers inside system tick functions.
- **Prefabs** -- typed `PrefabRef` field bindings and `InstantiatePrefab`.
