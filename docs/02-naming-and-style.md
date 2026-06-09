# Naming and Style

## Engine name

**Zues** (z-u-e-s). Not "Zeus". Never autocorrect. Applies to:
- Namespace: `Engine`
- Macros: `ZUES_*`
- Git repo: `ZuesEngine`
- File/folder names: `zues_core`, `ZuesEngine`, `zues_window_glfw`, etc.

## C++ namespaces

- Root: `Engine::`
- Subsystems: `Engine::math`, `Engine::ecs`, `Engine::net`, `Engine::ui`, `Engine::render`, `Engine::input`
- Common types aliased into `Engine::` — so `Engine::vec2`, `Engine::Entity` work directly
- Reflection attributes: `[[Engine::replicated]]`, `[[Engine::predicted_input]]`

## Macros

- `ZUES_API` — symbol visibility for cross-DLL boundaries (see [04-dll-safety](04-dll-safety.md))
- `ZUES_MODULE_EXPORT` — for a module's `zues_module_entry` function
- `ZUES_BUILDING_CORE` — defined when building `zues_core.dll`
- `ZUES_BUILDING_MODULE` — defined when building any module
- `ZUES_MODULE_ABI_VERSION` — compile-time constant for module ABI
- `ZUES_HAS_REFLECTION` — set by `ZUES_USE_REFLECTION` CMake option

## File naming

- Headers: `.h`, `snake_case`: `entity_id.h`, `sprite_renderer.h`
- Sources: `.cpp`, same case as their header
- Templates with long impls: `foo.h` + `foo.inl`
- One public class/struct per header where practical

## Code style

See `.clang-format`. Summary:
- 4 spaces, no tabs
- `BreakBeforeBraces: Attach`
- Column limit 120
- `NamespaceIndentation: None`
- `PointerAlignment: Left` (`Foo* x`, not `Foo *x`)

## Identifier style

- `snake_case` — functions, variables, file names, namespace names
- `PascalCase` — types, concepts, enums, enum values
- `SCREAMING_SNAKE` — macros, compile-time constants
- `m_` prefix — class member fields (`m_count`, not `count_`)
- `g_` prefix — file-local / translation-unit globals only

## Comments

- Explain WHY, not WHAT. Well-named code documents itself.
- Public API headers: brief doc comment above types and public functions.
- Never reference current task/PR/caller ("added for Y flow", "used by Z").
- Never narrate obvious code ("// increment counter" above `count++`).
