# clang-p2996 Toolchain (C++26 Reflection)

Zues uses [clang-p2996](https://github.com/bloomberg/clang-p2996) — Bloomberg's experimental clang fork — for C++26 P2996 reflection. Stock Windows compilers don't ship P2996 yet, so we run the fork inside Docker.

## One-time setup

1. Docker Desktop installed and running.
2. Build the toolchain image (clang-p2996 base + cmake + ninja):
   ```pwsh
   docker build -t zues-toolchain:latest -f tools\Dockerfile.zues-toolchain tools
   ```
   Or simpler — the helper script below auto-builds it on first use.

## Build & run from the command line

```pwsh
# Configure + build (default preset is clang-reflection)
.\tools\zues-build.ps1

# Other presets work too:
.\tools\zues-build.ps1 -Preset debug

# Force clean rebuild:
.\tools\zues-build.ps1 -Clean

# Run the editor (inside the container, since the binary is a Linux ELF)
.\tools\zues-run.ps1
```

Build artifacts land on the host at `build\<preset>\bin\` even though they're produced inside the container (the source tree is mounted as a volume).

## CLion integration

CLion 2022.3+ has native Docker toolchain support.

1. **Add Docker toolchain.**
   File → Settings → Build, Execution, Deployment → **Toolchains** → ➕ → Docker.
   - Server: `Docker` (default Linux engine).
   - Image: `zues-toolchain:latest`.
   - C compiler: `clang`. C++ compiler: `clang++`. Debugger: bundled GDB.

2. **Add a CMake profile for reflection.**
   File → Settings → Build, Execution, Deployment → **CMake** → ➕.
   - Name: `clang-reflection (docker)`.
   - Build type: Debug.
   - Toolchain: the Docker toolchain just added.
   - Generator: Ninja.
   - CMake options: `--preset clang-reflection`.

3. Build / run from CLion. The container is mounted at `/work`, builds run there, output appears in `build\clang-reflection\bin\` on the host.

## When to use which preset

| Preset | Compiler | Reflection | When |
|---|---|---|---|
| `debug` / `release` | Your installed clang or MSVC (Windows-native) | OFF — uses `ZUES_COMPONENT_FIELDS` macros | Day-to-day iteration. Fast. Produces Windows-native binaries. |
| `clang-reflection` | clang-p2996 inside Docker | ON — `std::meta` auto-introspection | Developing/testing reflection-dependent features. Produces Linux ELF. |

The same source code compiles under both. With reflection on, `ZUES_COMPONENT_FIELDS(...)` macros become no-ops and the primary `ComponentFieldsOf<T>` template auto-derives every field via `std::meta`. With it off, the macros emit explicit specializations.

## Producing Windows-native release builds

The Docker path is **dev-time only**. Shipping Windows binaries today must use the macro path (`debug` / `release` presets). When P2996 lands in MSVC or upstream clang, we drop Docker.

This split is acceptable: reflection generates compile-time metadata used by inspector/serialization/network codegen. That metadata is baked into the binary at build time. A macro-based build produces the same metadata via the `ZUES_COMPONENT_FIELDS` declarations — same downstream API, same runtime data.

## Updating the toolchain

When clang-p2996 ships a new commit worth picking up:
```pwsh
docker pull vsavkov/clang-p2996:amd64
docker build --no-cache -t zues-toolchain:latest -f tools\Dockerfile.zues-toolchain tools
```

## Known sharp edges

- clang-p2996 ships with a "highly experimental — occasional crashes expected" warning. Trust but verify. Clean rebuilds usually fix flaky behavior.
- `<experimental/meta>` requires `-std=c++2c` + libc++. CMake's reflection branch sets both automatically when `ZUES_USE_REFLECTION=ON`.
- Build artifacts inside `build/clang-reflection/` are Linux-ELF — running them from Windows requires the container.
