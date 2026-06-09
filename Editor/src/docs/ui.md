# In-game UI

HUD overlays -- score readouts, health bars, dialogue boxes, button
icons -- are built from two components: `UIAnchor` and one of `Text`
or `Sprite`. The engine renders them as a screen-space pass on top of
the world view, in both the editor's Game panel and the exported
runtime, using the same code path. What you see in the editor matches
what your shipped game shows pixel-for-pixel.

UI is rendered by `ui_render_system` (registered automatically -- not
something you wire up). World-space sprite rendering excludes any entity
that has `UIAnchor`, so a HUD element never accidentally appears inside
the game world.

## Components

### `UIAnchor`

Place an entity in screen-space pixels. Required on any HUD entity.

| Field          | Kind | Meaning                                                                |
| -------------- | ---- | ---------------------------------------------------------------------- |
| `anchor`       | Vec2 | 0..1 normalized point on the viewport. (0,0) = top-left, (1,1) = bottom-right, (0.5,0.5) = center. The element STICKS to this corner as the window resizes. |
| `pixel_offset` | Vec2 | raw-pixel offset from the anchor. Use to nudge a few pixels off the edge. |
| `pivot`        | Vec2 | 0..1 within the rendered element. (0,0) = its top-left aligns with the anchor; (0.5,0.5) = center; (1,1) = bottom-right. |

### `Text`

Renders UTF-8 text (ASCII for v5; multi-byte glyphs render as a
half-em advance). Pair with `UIAnchor`.

| Field      | Kind     | Notes                                              |
| ---------- | -------- | -------------------------------------------------- |
| `font`     | FontHandle | 0 = use the engine's default font (Exo2)         |
| `utf8`     | char[256] | the displayed string                              |
| `size_px`  | f32      | pixel size; 0 falls back to ~16 px                 |
| `color`    | Color    | RGBA 0..1                                          |
| `h_align`  | u8       | 0 = left, 1 = center, 2 = right (overrides pivot.x) |
| `layer`    | i32      | sort layer (higher = on top)                       |
| `order`    | i32      | tiebreaker within layer                            |

### `Sprite` (HUD-mode)

A regular `Sprite` paired with `UIAnchor` becomes a HUD icon. Sprite
`size` is interpreted as **pixels** in HUD mode (vs world units in
the world pass). Texture, tint, and pivot work identically.

## Common layouts

### Score in the top-left

```lync
e: EntityRef = CreateEntity();
e.Add<UIAnchor>(UIAnchor{
    anchor:       Vec2{ x: 0.0, y: 0.0 },   // top-left
    pixel_offset: Vec2{ x: 16.0, y: 16.0 }, // 16 px in
    pivot:        Vec2{ x: 0.0, y: 0.0 }    // text top-left
});
e.Add<Text>(Text{
    utf8:    "Score: 0",
    size_px: 24.0,
    color:   Color{ r: 1, g: 1, b: 1, a: 1 },
    h_align: 0           // left
});
```

### Centered title that grows from a number

```lync
e.Add<UIAnchor>(UIAnchor{
    anchor:       Vec2{ x: 0.5, y: 0.5 },   // screen center
    pixel_offset: Vec2{ x: 0.0, y: -64.0 }, // 64 px above center
    pivot:        Vec2{ x: 0.5, y: 0.5 }    // ignored: h_align=center wins on x
});
e.Add<Text>(Text{
    utf8:    "0",
    size_px: 48.0,
    color:   Color{ r: 1, g: 1, b: 0.2, a: 1 },
    h_align: 1           // center -- the label stays centered as it grows
});
```

### HUD icon in the corner

```lync
e.Add<UIAnchor>(UIAnchor{
    anchor:       Vec2{ x: 1.0, y: 1.0 },   // bottom-right
    pixel_offset: Vec2{ x: -16.0, y: -16.0 },
    pivot:        Vec2{ x: 1.0, y: 1.0 }    // sprite's bottom-right at the anchor
});
// Sprite size is in PIXELS for HUD-mode entities.
e.Add<Sprite>(Sprite{
    texture: my_icon,                       // SpriteRef -> TextureHandle
    size:    Vec2{ x: 64.0, y: 64.0 },
    tint:    Color{ r: 1, g: 1, b: 1, a: 1 }
});
```

## Updating text from a system

```lync
[System("Update", "Game")]
def update_score(eng: ptr, dt: float, user: ptr): void {
    match GameManager() {
        some(mgr): {
            // Find the score Text entity (we stored a ref on the singleton).
            match mgr.score_text.Get<Text>() {
                some(t): SetText<int>(t, mgr.score);
                null:    { }
            }
        }
        null: { }
    }
}
```

`SetText<T>` covers `int`, `float`, `bool` -- the typed forms format the
value into the `utf8` buffer. Use `SetText` directly for an existing
string. `SetTextColor(t, r, g, b, a)` for color updates.

## Default font + custom fonts

Text with `font.index == 0` uses the engine's bundled default font
(Exo2). The font is loaded at editor / runtime startup from the first
of these paths that exists:

1. `<project>/assets/fonts/default.ttf`
2. `<project>/assets/fonts/Exo2-VariableFont_wght.ttf`
3. `<editor_dir>/assets/fonts/...` (bundled with the engine itself)

To ship a custom default, drop a TTF/OTF at
`<project>/assets/fonts/default.ttf` -- the next editor restart picks
it up. The export step copies your project's `assets/` verbatim, so
the runtime loads the same file.

For per-text custom fonts, load via the renderer service and assign
the resulting `FontHandle` to `Text.font`. This isn't yet exposed to
Lync; C++ projects can call `IRenderer_2D_v1::load_font_from_file`
directly.

## Coordinate system

(0, 0) is the top-left of the viewport. X grows right; Y grows down.
This matches CSS / ImGui conventions but is the *opposite* of the
world camera (world Y grows up). HUD math is screen-space throughout
-- you never deal with world coordinates inside a HUD entity.

## Limits + footguns

- `Text.utf8` is a 256-byte fixed buffer including the null terminator.
  Longer strings get truncated.
- ASCII (32..126) is fully baked at startup. Non-ASCII bytes draw as
  blank advances; full Unicode lands in v6.
- The default font bake size is 32 px. Drawing at 12 px downscales
  (looks fine); drawing at 96 px upsamples and gets a touch blurry.
  If you need crisp huge text, load a second font handle at the
  larger pixel_height and assign it to `Text.font`.
- `Text` and `Sprite` on the SAME entity both render -- the text
  draws on top of the sprite quad. Useful for buttons. Use separate
  entities if you want independent layering.
- Entities with `UIAnchor` are excluded from the world sprite render
  pass; if you tag a world entity by mistake, it disappears from the
  scene and reappears in the HUD.
