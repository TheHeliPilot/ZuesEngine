# Shipping your game

Export builds a self-contained folder that runs on any matching
Windows machine without the editor. Zip it, upload it to itch /
Steam / your own download page, and players just unzip and run.

## Export

**Build -> Export** from the main menu. Two configurations:

| Menu item        | Output folder                          | Use for                                     |
| ---------------- | -------------------------------------- | ------------------------------------------- |
| Export Debug     | `<project>/dist/<Name>-Debug/`        | Internal builds. Console window stays open so you can read runtime errors. |
| Export Release   | `<project>/dist/<Name>-Release/`      | Public builds. No console; logs stripped; smallest exe size. |

Each config has its own dist subfolder; re-exporting one doesn't touch
the other. The destination is wiped before each export so removed
assets and renamed worlds don't linger.

Ship the **Release** build. Use Debug only when you need to read
runtime logs to track down a bug a player reported.

## Pre-flight

Before exporting:

- **Save any open `.lync` file.** The editor rebuilds the project DLL
  on save; export copies whatever DLL is on disk. If the editor toast
  for the build is still showing "Compiling...", wait for it to clear.
- **Stop play mode.** Export is blocked during Play.
- **Set the start world.** Project Settings -> Start World picks the
  `.zworld` the runtime loads on launch. If it points at a world you
  deleted or renamed, the player gets a black screen.
- **Confirm window settings.** Project Settings -> Runtime window
  (size, lock, fullscreen). The Game panel previews the exact aspect
  ratio the player will see.

## What's in the bundle

```
<Name>-Release/
  <Name>.exe                    the runtime, renamed to your project
  <Name>.zuesproject            project metadata (start world, window size)
  zues_core.dll                 engine
  zues_renderer_gl.dll
  zues_window_glfw.dll
  zues_physics_box2d.dll
  glfw3.dll
  build/<Project>.dll           your compiled project DLL
  assets/
    worlds/                     every .zworld in your project
    prefabs/                    every .zprefab
    sprites/                    every image you used
    fonts/
      Exo2-VariableFont_wght.ttf   default font (only added if you don't ship one)
```

The runtime resolves every path relative to the exe location, so the
folder works wherever it ends up on the player's machine -- Steam
install dir, Program Files, Desktop, USB stick. No registry, no
%APPDATA% writes, no install step.

## What the player needs

- **Windows x64** (matching the OS/arch you built on).
- **Microsoft Visual C++ 2015-2022 redistributable.** A one-time
  install from microsoft.com (~13 MB). Almost every Windows machine
  already has it from another game. If a player reports
  `VCRUNTIME140.dll missing`, that's the fix; either point them at
  the Microsoft download or include `vc_redist.x64.exe` next to your
  exe with a README saying "run this once if the game won't start".

That's everything. No editor, no Lync compiler, no .NET, no Java
runtime, no engine source.

## Window settings (Project Settings)

These affect the runtime only; the editor's own window ignores them.

| Setting           | Effect on the exported game                                |
| ----------------- | ---------------------------------------------------------- |
| Size (w x h)      | Window resolution at launch. Game panel previews this aspect ratio. |
| Lock size         | Player can't drag the window corner to resize.             |
| Launch fullscreen | Starts in borderless fullscreen on the primary monitor.    |

## Asset paths

The runtime reads `default_world` out of `<Name>.zuesproject` and
loads `<exe_dir>/<default_world>` on launch. To swap the start world
in a shipped build, edit the `.zuesproject` next to the exe:

```json
{
  "default_world": "assets/worlds/MainMenu.zworld",
  ...
}
```

No re-export needed for that one change.

## Troubleshooting

### Black screen on launch (Debug build)

The visible console will tell you. The most common ones:

| Console message                       | Cause + fix                                                   |
| ------------------------------------- | ------------------------------------------------------------- |
| `default_world load failed: <path>`   | Start world is wrong or the file wasn't copied. Project Settings -> Start World, re-export. |
| `project DLL failed to load`          | DLL missing or ABI-mismatched. Save a `.lync` to force a fresh build, re-export. |
| `default font not found`              | Drop any `.ttf` at `assets/fonts/default.ttf` in the project, re-export. |

### Black screen on launch (Release build)

No console to read. Export Debug, reproduce, read the message there.

### Player reports the game won't start

99% the missing VC++ redistributable. Have them install it from
microsoft.com or run `vc_redist.x64.exe` if you bundled one.

### Window opens at the wrong size

The runtime always honours `<Name>.zuesproject`. Open Project Settings,
fix the size, re-export.
