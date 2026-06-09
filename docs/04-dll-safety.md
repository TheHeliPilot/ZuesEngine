# DLL Safety

Past work on this engine got bitten hard by cross-DLL memory bugs. Read before touching any cross-module code.

## The five crash classes

1. **CRT mismatch.** Two DLLs compiled with different CRT (static vs dynamic, debug vs release) = two heaps. `new` in one, `delete` in the other = crash.
2. **STL across DLL boundary.** `std::string`, `std::vector`, `std::function` have unstable ABI. Layout/allocator state differs per build.
3. **Ownership crossing modules.** DLL A allocates, DLL B frees. Wrong heap.
4. **Hot-reload dangling pointers.** Old DLL's function pointers or vtables held in engine state; DLL unloads; pointer now invalid.
5. **ODR / stale headers.** Same class, different layouts across modules after a header change.

## Rules enforced by the scaffold

- **Single dynamic CRT.** `CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` at project scope. Every target inherits.
- **`_ITERATOR_DEBUG_LEVEL` pinned.** Set as a PUBLIC compile definition so STL debug layout can't drift between modules.
- **`ZUES_API` wraps every cross-DLL symbol.** Hidden visibility by default (`CXX_VISIBILITY_PRESET hidden`). Nothing leaks by accident.
- **Project.dll → Core boundary is pure C.** `ProjectAPI/include/Engine/project_api.h`. No STL, no virtuals, no exceptions. Function pointers in a versioned struct.
- **Project.dll does NOT link Core at build time.** Only the `ZuesHostApi` table crosses, handed in at `on_load`.
- **State lives in Core.** Modules own code; Core owns allocations. Hot-reload discards code, state survives.
- **ABI version checked at load.** Mismatch = refuse to load + clear error. Prevents stale-DLL-on-disk bugs.

## Module → module rules

Modules load together, rebuild together. Service interfaces between modules CAN be C++ class-style as long as:
- The class is defined in a shared header (e.g., `Engine/service_renderer.h`).
- The class has no inline non-trivial members.
- Ownership always stays with the owning module.
- Objects returned across modules are freed by a release function on the same module.

When in doubt, make it C-style. `IRenderer_v1` as a struct of function pointers is always safe.

## Checklist for new cross-boundary API

- [ ] Passed only POD + pointers + C-callable function pointers?
- [ ] All `std::*` types stay inside one module?
- [ ] No `std::string` or `std::vector` in the API — use `const char*` + `size_t`, or `span`-like POD?
- [ ] Exceptions can't escape? (`noexcept` on C entry points)
- [ ] Allocation ↔ deallocation pairs both live on the same side?
- [ ] ABI version bumped when anything changes size or order?

If any answer is "no", redesign.

## Module hot reload (future)

Not in v1. When we add it:
- Before unload: swap any held function pointers to a "stub that returns error" so in-flight calls fail cleanly.
- Serialize the old module's state (via reflection) to a byte buffer in Core.
- Unload DLL.
- Load new DLL.
- Deserialize state into new module.
- Resume.

Until then: modules are load-once.
