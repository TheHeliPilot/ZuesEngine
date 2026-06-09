# Sprites, slicing, 9-slice, and animation

The Zues sprite pipeline is one feature in three layers: a Sprite
component (what to draw), a slice/9-slice description on each texture
(how to carve and stretch it), and an Animator (when to swap frames).

This page covers all three end-to-end.


## Sprite component

A `Sprite` references a Texture asset and tells the renderer how to
sample + draw it. Add it from the Inspector or via `add_sprite_default`
/ `add_sprite_textured` in Lync.

Key fields:

- `texture` — Texture asset reference. Drag a `.png` from the Asset
  Browser straight onto the slot, or pick from the dropdown.
- `size`, `pivot`, `tint`, `flip_x`, `flip_y` — usual stuff.
- `layer`, `order` — sort key inside the camera's sort mode.
- `slice_x/y/w/h` — sub-rect of the texture to sample. Driven by the
  slice picker (below); zeroed for whole-texture mode.
- `border_l/r/t/b`, `scale_mode`, `center_mode` — 9-slice config (below).
- `texture_ppu` — asset's pixels-per-unit, copied from the texture's
  `.meta` so the renderer can size 9-slice borders + tile counts at the
  correct screen pixel ratio.

The Inspector hides `slice_*`, `border_*`, `scale_mode`, `center_mode`,
and `texture_ppu` from raw int rows because they're driven by the
slice picker. Use the picker; don't edit them by hand.


## Slicing a texture (Sprite Cutter)

Double-click any `.png` in the Asset Browser to open the **Sprite Cutter**
modal. It edits the texture's `.meta` sidecar (slices array + per-asset
PPU / filter / wrap / pivot).

Auto-cut modes:

- **Single (whole image)** — one slice covering the entire texture.
  Useful when the file is one sprite that you still want to be able to
  9-slice.
- **By cell size** — set cell W/H/padding, the cutter tiles the image.
- **By cell count** — set cols/rows, the cutter divides evenly.
- **By alpha** — flood-fills connected non-transparent regions and
  bounds each one. Good for atlases laid out freely.

Click `Slice` to apply. You can also draw rects manually:

- Empty space + drag → create a new slice.
- Click an existing slice → select.
- Drag corners → resize.
- Drag inside → move.
- `Alt+click` → cycle through stacked slices when they overlap.

Saved slices show in the right panel with rename / numeric rect /
delete. The selected slice gets four green **mid-edge handles** for
9-slice borders (drag inward to introduce them).


## 9-slice

Each slice can carry four border values + two scale modes.

- **Borders** (`border_l/r/t/b`) — pixels of the source rect that stay
  un-stretched at each edge. The slice splits into 9 regions: 4 corners
  (never stretch), 4 edges (stretch on one axis), 1 center (stretches
  both ways).
- **Scale mode (Edges)** — how the four edges fill their stretchable
  axis. `Stretch` linear-interpolates; `Tile` repeats source pixels
  with a partial last tile; `TileFit` rounds to a whole-tile count
  then squeezes/stretches uniformly so no half-tiles appear.
- **Scale mode (Center)** — same three options for the center region,
  independent of the edges. Common combo: edges = Tile, center = Stretch.

The renderer's fast path skips the 9-quad emit when all four borders
are zero. Tile / TileFit need a stable "1 source pixel = N screen
pixels" ratio which comes from the slice picker copying the asset's
PPU into `Sprite.texture_ppu`.

The cutter live-broadcasts edits to every matching `Sprite` in the
scene as you drag the handles, so the scene preview updates instantly
without having to re-pick the slice on each entity.


## Animation assets (.zanim)

A `.zanim` is a list of frames, each pointing at a `(texture, slice,
duration)` triple. Create one via right-click in the Asset Browser →
**Create → Animation (.zanim)**.

Double-click a `.zanim` to open the **Animation Editor** (a regular
dockable window so the Asset Browser stays interactive — drag `.png`s
right onto the editor's drop zone or onto a frame's texture slot).

Editor surface:

- **Toolbar** — name, FPS, Loop, Play/Pause, reset, speed.
- **Preview pane** — plays the clip continuously; shows `Frame N/M`.
- **Timeline scrubber** — per-frame segments scaled by duration. Click
  to scrub; orange = currently playing, blue = selected.
- **Frame strip** — thumbnails. Click to scrub + select.
- **Per-frame editor** — texture (drag-drop), slice combo, duration.
  Move ←/→, Duplicate, Delete.

Per-frame `duration` of `0` means "use 1 / FPS." The editor honors the
slice's UV when rendering thumbnails so atlas-based animations show
correct frames.


## Animator component

Add an `Animator` to an entity with a `Sprite` to make it animate.
The Inspector shows:

- **Current** dropdown — which clip is active.
- **Clips table** — named entries, each pointing at a `.zanim`. Click
  the orange index badge to set that row as current. Drop a `.zanim` on
  the empty-state card or onto a row's slot.
- **+ Add clip** — appends a blank entry.

Stored inline as a single `clips: char[1024]` buffer (newline rows of
`"<name>\t<guid_hex>"`). Persisted natively via reflection — no special
case in the world serializer.


## Runtime playback (animator system)

A built-in **animator system** ticks during Play (`SystemDomain::Game`,
`Phase::PreUpdate`). For each entity with `Animator + Sprite`, when
`Animator.playing == true`:

1. Advance `Animator.time` by `dt * time_scale`.
2. Find the active frame inside the resolved `.zanim`.
3. Write that frame's texture handle + slice rect into `Sprite`.

So sprites animate the moment Play starts. The `playing` toggle on the
Animator is the auto-play switch — checked = starts on Play.


## Lync API (PlayByName / SetPlaying / Seek)

```lync
// Switch to a named clip + restart from frame 0.
PlayByName(player, "Walk");

// Pause / resume without changing the active clip.
SetPlaying(player, false);
SetPlaying(player, true);

// Jump to a specific time in seconds (0 = restart).
Seek(player, 0.0);
```

`PlayByName` returns 1 on hit, 0 if the name isn't in the entity's
clip table (or the entity has no Animator).


## File formats

- `<image>.png.meta` — JSON sidecar holding `pixels_per_unit`, filter,
  wrap, pivot, and the `slices` array (each with name, rect, pivot,
  optional `border` / `scale_mode` / `center_mode`).
- `<file>.zanim` — JSON file with `guid`, `name`, `loop`, `fps`, and
  `frames` (each `texture` GUID + `slice` index + `duration`).
- `.zworld` saves every Animator's `clips` buffer as a single string
  field via reflection's `CharBuffer` kind, so the named-clip list
  round-trips through save/load with no special handling.


## Common patterns

- **Tile a textured background**: import the tile, sprite-cut as Single
  (whole image), set Edges = Tile, drop the sprite into the world,
  scale to taste. The center tiles cleanly because `texture_ppu` is
  authoritative.
- **9-slice UI panel**: import the panel art, sprite-cut as Single,
  drag the four green handles inward to define the corner regions,
  Edges = Stretch, Center = Stretch (or Tile for a textured center).
- **Sprite atlas animation**: import the atlas, sprite-cut By cell size
  or By alpha, create a `.zanim`, drop each slice into a frame.
- **Multi-clip character**: one Animator with `Idle / Walk / Jump`
  entries; gameplay code calls `PlayByName(player, "Walk")` to switch.
