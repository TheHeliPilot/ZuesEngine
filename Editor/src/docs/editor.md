# Editor panels

The dockspace is freeform -- drag tabs anywhere, layout persists per-user
across all projects. **View -> Reset Layout** restores the default.

| Panel             | Purpose                                                            |
| ----------------- | ------------------------------------------------------------------ |
| Hierarchy         | tree of entities, with pinned Globals section for singletons.      |
| Inspector         | components on the selected entity. Add / Remove / edit.            |
| Scene             | viewport + gizmos. Drag images / prefabs from Assets to spawn.     |
| Game              | what the camera sees during Play.                                  |
| Lync Editor       | tabbed code editor. Source pane on the left.                       |
| Assets            | files under `assets/`. Double-click `.zworld` to load.             |
| Console           | log lines, color-coded by level + per-source hash, with filter.    |
| Systems           | live list of registered systems by phase + domain.                 |
| TODOs             | scans the project src tree for TODO/FIXME/XXX/HACK; click to jump. |
| Search            | project-wide text search across .lync/.cpp/.h/.hpp. Ctrl+Shift+F.  |
| Docs              | this panel.                                                        |
| Project Settings  | dropdown for default world + per-project prefs.                    |

## Hierarchy

Tree of entities. Right-click for context-menu actions:

- **Set as Selected** / **Create Empty Child** / **Rename** /
  **Save as Prefab** / **Delete** (delete is greyed out for protected
  entities like `Main Camera` and singletons).
- Drag-drop a row onto another row. The drop position decides the
  action -- Unity-style:
  - **Top edge** (~25% of the row): reorder *before* this entity
    (same parent). A blue line appears at the edge.
  - **Middle**: reparent -- make this entity the new parent. The
    target row is outlined in blue.
  - **Bottom edge** (~25%): reorder *after* this entity. Blue line at
    the bottom edge.
- Drop on the `(scene root)` sentinel at the bottom to unparent.

### Globals section

Entities marked as the host of one or more `[Singleton]` components
collapse into a pinned **Globals** header at the top of the tree.
Useful for `GameManager`, `InputConfig`, audio mixers, etc. -- anything
project-wide. The header is collapsible; double-click any entity in
it to inspect / edit.

### Toolbar

`+ New Entity` (or **Ctrl+N** while the panel is focused) creates a
fresh root entity with `Transform2D` + `Name` auto-attached. **Delete
Selected** removes the selected entity (greyed out for protected
entities).

### Keyboard shortcuts

| Shortcut        | Action                                              |
| --------------- | --------------------------------------------------- |
| Delete          | Delete selected entity (with confirm popup)         |
| F2              | Rename selected entity inline                       |
| Ctrl+N          | New root entity                                     |

## Inspector

Two-column table layout: label | control. Components stack vertically;
each one is a collapsing header you can fold. **Add Component** at the
bottom opens a search picker filtered by category.

### Drag targets

Reference field types (`EntityRef`, `PrefabRef`, `SpriteRef`,
`TextureRef`, `AudioRef`, `FontRef`) render as a button-style drop
target. Right-click the button to **Clear** the slot. Asset-ref drops
are kind-checked: dropping a `.png` on a `PrefabRef` slot is silently
rejected.

- `EntityRef` -- drop a row from the **Hierarchy** panel.
- `*Ref` (asset) -- drop a file from the **Assets** panel.

### Undo / Redo

Inspector edits are wrapped in undo: ImGui's `IsItemActivated` ->
`IsItemDeactivatedAfterEdit` brackets each commit so you get one
undo entry per slider drag, not one per micro-tick.

| Shortcut    | Action |
| ----------- | ------ |
| Ctrl+Z      | Undo   |
| Ctrl+Y      | Redo   |
| Ctrl+Shift+Z| Redo   |

The same shortcuts work for transform-gizmo drags and collider edit
handles in the Scene viewport.

## Scene viewport

Drag images and prefabs from the Asset Browser into the viewport:

- `.png` / `.jpg` -> spawns a sprite entity at the cursor's world
  position.
- `.zprefab` -> instantiates the prefab at the cursor's world position.
- Drag image asset onto an existing entity? -- currently treated as a
  scene-wide drop; per-entity drop targets land in v2.

Gizmo overlay:

- **Selection outline** in cyan around the selected entity's bounds.
- **Move handles** -- green X axis, red Y axis, yellow center square
  for free move.
- **Rotation handle** -- cyan ring.
- **Collider handles** -- when the selected entity has a collider with
  `edit_in_scene = true`. Box: four mid-edge resize handles. Circle:
  one radius handle on the +X edge. **ALT** during drag mirrors the
  change (centered around offset).

Input priority on click: collider handle > transform gizmo > camera
pan > entity pick. Camera pan is middle-mouse-drag; zoom is wheel.

## Lync Editor

Tabbed editor over an embedded TextEditor (ImGuiColorTextEdit). Each
tab holds one `.lync` file. The auto-generated `_zues_main.lync`
manifest is hidden from the source tree.

### Sticky scroll

When a `def name(...)` line scrolls above the visible area but its
closing `}` is still visible, the def line is pinned as a header
overlay at the top of the editor (up to 3 nested headers). Useful for
long functions.

### Keybindings

| Shortcut         | Action                                |
| ---------------- | ------------------------------------- |
| Ctrl+S           | Save                                  |
| Ctrl+F / Ctrl+H  | Find / Replace                        |
| Ctrl+G           | Go to line                            |
| Ctrl+/           | Toggle line comment                   |
| Ctrl+D           | Duplicate line                        |
| Alt+Up / Alt+Down| Move line up / down                   |
| F2               | Rename (file or project-wide)         |
| F12 / Ctrl+Click | Go to definition (project-wide)       |
| Shift+F12        | Find references (project-wide)        |
| Ctrl+- (minus)   | Jump back (pop goto-def history)      |
| Ctrl+Alt+Click   | Jump to docs entry for the symbol     |
| F1               | Jump to docs (keyboard alternative)   |
| Tab              | Accept ghost-text completion          |
| Enter after `{`  | Auto-indent the new line one level    |

### Code intelligence

The editor reads the lync compiler's symbol export (`.zues/symbols.json`,
written automatically on every successful build by the watcher) and uses
it to drive:

- **Project-wide goto-def**: `F12` or `Ctrl+Click` jumps to the file +
  line where the symbol was defined. If the project hasn't built yet,
  a regex fallback still finds top-level `def` and `struct` decls in any
  `.lync` file under `<project>/<src>/`.
- **Find references**: `Shift+F12` opens a modal listing every
  word-boundary occurrence of the symbol across the project source
  tree. Click a row to jump (the original caret is pushed onto the
  back-stack first).
- **Type-aware hover**: hovering an identifier shows its full signature
  (e.g. `Foo(x: int, y: float?) -> bool`) sourced from the compiler
  output. Auto-generated component helpers (`AddX`, `EachX`, `GetX`,
  `RemoveX`, `HasX`, legacy `QueryEachX`) get synthesised hover text.
- **Outline (left pane)**: the active doc's struct + def list, with a
  filter input. `S` rows are structs, `f` rows are functions. Click to
  jump.
- **Breadcrumbs**: a thin strip above the editor body shows
  `<file> > <enclosing decl>` for the current caret line. Click the
  decl name to jump to its declaration. Toggle with View → Breadcrumbs.

### Rename refactor

`F2` opens the rename modal. The "across project (all .lync files)"
checkbox extends the rename to every `.lync` file under
`<project>/<src>/`: open docs are renamed in-buffer (and marked dirty),
closed files are rewritten on disk. The choice persists across opens
so consecutive project-wide renames don't need re-ticking.

### Smart editing

- **Auto-close brackets**: typing `{`, `(`, `[`, `"`, `'` inserts the
  matching closer with the caret between them. Smart-skip: typing the
  closer when one is already to the right just steps over it.
- **Auto-indent on Enter after `{`**: the new line lands one indent
  level deeper than the brace line.
- **`{|}` Enter expansion**: pressing Enter with the caret between an
  open + close brace pair drops the closer onto its own line and
  parks the caret on a fresh indented blank line between them.

### Attribute snippet expansion

Type one of these on its own line and press Enter; the matching
function skeleton is inserted with the caret inside the body:

| Attribute typed       | Inserted skeleton                                                |
| --------------------- | ---------------------------------------------------------------- |
| `[OnLoad]`            | `def on_load(eng: ptr, host: ptr): int { ... return 0; }`        |
| `[OnUnload]`          | `def on_unload(eng: ptr): int { ... return 0; }`                 |
| `[OnUpdate]`          | `def on_update(eng: ptr, dt: float, user: ptr): void { ... }`    |
| `[OnCollision]`       | `def on_collision(eng: ptr, a: EntityRef, b: EntityRef): void { ... }` |
| `[OnTriggerEnter]`    | `def on_trigger_enter(eng: ptr, self: EntityRef, other: EntityRef): void { ... }` |
| `[OnTriggerExit]`     | `def on_trigger_exit(eng: ptr, self: EntityRef, other: EntityRef): void { ... }`  |
| `[System("Phase","Domain")]` | `def system(eng: ptr, dt: float, user: ptr): void { ... }` (name pre-selected) |

### Autocomplete

Ghost-text completion (Tab to accept) for built-in helpers, attributes,
and project-scoped symbols. Type-aware where possible (the Lync
compiler emits a per-build symbol table that the editor reads).
Special modes:

- **Template-name dropdown.** After typing `Add<`, `Has<`, `Get<`,
  `Each<`, or `Remove<`, a dropdown lists every registered component
  in the world. Pick one and the closing `>` is inserted automatically
  (`Add<Health>`).
- **UFCS-aware dot completion.** After `e.` (where `e` is known to be
  an entity), the dropdown filters to entity-style methods
  (`Get<T>`, `Has<T>`, `Add<T>`, etc.) plus engine wrappers like
  `ApplyImpulse`, `SetVelocity`. After `mgr.` (where `mgr` is a
  singleton or a typed struct), filters to that struct's fields.
- **Word completion** for identifiers in the open file plus the
  built-in API surface (`LogInfo`, `CreateEntity`, `KeyCode`,
  `Instantiate`, ...).

## Console

Each row renders as `HH:MM:SS  LVL  source: message`. Three things make
long log streams readable:

- **Per-source hue.** A deterministic hash maps the source string to an
  HSL color, so `editor.assets`, `core`, and `lync.MyGame` always
  render in distinct colors.
- **Dot-depth indent.** A source like `editor.assets.thumb` indents
  under `editor.assets` (one level per dot, capped at 5).
- **Row banding.** Subtle alternating background tint for skim-ability.

Toolbar: **Clear**, **Copy All**, free-text **Filter**, and per-level
checkboxes (Trace / Debug / Info / Warn / Error / Fatal). Right-click
any row for "Copy line" / "Copy message only".

## Search panel

**View -> Search** or **Ctrl+Shift+F** opens the panel and focuses the
query input. Searches every `.lync`, `.cpp`, `.h`, `.hpp` file under
the project directory (skipping `build/`, `.zues/`, `_zombie_bin*`,
`_zues_*`, `*.__live.*`).

Options: **Whole word**, **Case sensitive**, **Regex** (ECMAScript
syntax). Results capped at 2000. Click any result row to open the file
and jump to that line in the Lync editor. Results are cached; rescan
on query change or when you click **Refresh**.

## Source pane

The source tree (left pane of the Lync Editor) supports:

- **Collapse/expand** folder nodes (click the arrow).
- **v v** = Expand All, **^ ^** = Collapse All.
- Folders sort before files at each level, then alphabetically.
- Right-click any item for New File / New Folder / Rename / Delete.

## TODOs panel

Scans `<project>/` recursively (every 4 seconds + on Refresh) for `//
TODO`, `// FIXME`, `// XXX`, `// HACK` comments in `.lync`, `.cpp`,
`.h`, `.hpp`. Each row shows a colored marker badge, `file:line`, and
the comment text. Click `file:line` to open the file in the Lync
editor at that line. Filters: per-marker checkboxes + a free-text
substring filter against text + path.

## Asset browser

Toolbar slider switches between list view (default) and grid view
(Unity-style thumbnails) at the chosen tile size. Drag-drop:

- Drag an asset row out of the panel to drop on Scene (spawn) or
  another folder (move).
- Drop a folder onto another folder to move the whole tree.
- New folder via the toolbar button or right-click -> New Subfolder.

Per-file context menu varies by extension (`.lync` opens in editor,
`.zworld` loads as world, `.png` "Spawn as Sprite Entity", `.zprefab`
instantiate).

The **Asset Registry** scans `assets/` once at project load and
indexes every recognised asset by GUID. JSON-owned formats
(`.zprefab`, `.zsprite`, `.zworld`) embed the GUID at top level;
binary formats (`.png`, `.wav`, `.ttf`, ...) get a `<file>.meta`
sidecar minted on first scan. References (`PrefabRef`, `SpriteRef`,
...) survive renames and moves because they store the GUID, not the
path.

## Project Settings

**View -> Project Settings** opens a panel for per-project prefs that
get persisted into `<Name>.zuesproject`.

| Setting             | What it controls                                        |
| ------------------- | ------------------------------------------------------- |
| Start world         | Which `.zworld` the runtime / editor opens on launch.   |
| Runtime size (w x h)| Window size for the exported runtime; the Game panel previews this aspect ratio. |
| Lock size           | Disables user-drag corner resize on the runtime window. |
| Launch fullscreen   | Runtime starts fullscreen on the primary monitor at the size above. Implies no resize. |

The "Runtime" settings affect the **exported game only**; the editor's
own window is the dock-space and ignores them. The Game panel
letterboxes to the same `w:h` ratio, so editing in the editor matches
what the shipped game shows.

A small `(unsaved)` marker appears next to changed settings until the
next frame writes them to disk.

## Layout + prefs

Window / dock layout persists to a per-user file:

- Windows: `%APPDATA%/Zues/imgui.ini`
- macOS:   `~/Library/Application Support/Zues/imgui.ini`
- Linux:   `$XDG_CONFIG_HOME/zues/imgui.ini`

Same file across every project, so the arrangement you set up follows
you. **View -> Reset Layout** rebuilds the default Unity-ish layout.

## Build pipeline

The editor watches every `.lync` file under `<project>/<source_dir>`
(default `src/`). On any save it:

1. Regenerates `src/_zues_main.lync` if the file set changed.
2. Spawns `lync.exe` (silently, no flashing cmd window) to compile
   the manifest into `<project>/build/<name>.dll`.
3. Hot-reloads the new DLL into the live world.

The DLL is loaded via a per-PID shadow file
(`<name>.loaded.<pid>.dll`) so multiple editor instances and zombie
processes don't lock each other out. Hot-reload preserves entity
state through `world.save_json()` -> DLL swap -> `world.load_json()`,
schema-tolerant (see `worlds`).

Compile errors land in the Console + as red wavy underlines on the
offending lines in the Lync editor.

## Build -> Export Game

The **Build** menu has two items:

- **Export Debug**   -> `<project>/dist/<Name>-Debug/`
- **Export Release** -> `<project>/dist/<Name>-Release/`

Each copies a self-contained game folder you can zip and ship. The two
configs land in separate dist subfolders so iterating one doesn't blow
away the other.

| Config  | Console window | Use for                                |
| ------- | -------------- | -------------------------------------- |
| Debug   | visible        | dev iteration, reading runtime errors  |
| Release | hidden         | the version you actually ship          |

Save any open `.lync` file before exporting (the auto-watcher rebuilds
the project DLL on save) and stop play mode -- the menu item is
greyed out otherwise.

For full details on what's bundled, what your friend needs to play,
and a troubleshooting checklist, see the **Distributing** topic.

## Shortcuts cheat-sheet (engine-wide)

| Shortcut         | Where                | Action                       |
| ---------------- | -------------------- | ---------------------------- |
| Ctrl+S           | global               | Save world (or active doc when Lync editor focused) |
| Ctrl+Z / Ctrl+Y  | global               | Undo / Redo                  |
| Ctrl+N           | Hierarchy focused    | New root entity              |
| Ctrl+Shift+F     | global               | Open Search panel            |
| Delete           | Hierarchy / Inspector| Delete selected entity / component |
| F2               | Hierarchy            | Rename                       |
| Tab              | Lync editor          | Accept ghost completion      |
