# Rendering

2D first. Swappable module — 3D renderer replaces it later as a different module implementing `IRenderer_3D_v1`.

## Service: `IRenderer_2D_v1`

Registered by `zues_renderer_gl.dll` (or any module providing `ZUES_SERVICE_RENDERER_2D`).

Shape (pseudo-C, see shared header `Engine/services/renderer_2d.h`):

```c
typedef struct IRenderer_2D_v1 {
    uint32_t abi_version;

    // Frame
    void (*begin_frame)(struct IRenderer_2D_v1*, const Camera2D*, Rect viewport);
    void (*end_frame)  (struct IRenderer_2D_v1*);

    // Primitives
    void (*draw_sprite)(struct IRenderer_2D_v1*, TextureHandle, Vec2 pos, Vec2 size, float rot, Color tint);
    void (*draw_quad)  (struct IRenderer_2D_v1*, Vec2 pos, Vec2 size, Color);
    void (*draw_line)  (struct IRenderer_2D_v1*, Vec2 a, Vec2 b, Color, float thickness);
    void (*draw_text)  (struct IRenderer_2D_v1*, FontHandle, const char* utf8, Vec2 pos, Color, float size);

    // Resources
    TextureHandle (*load_texture)(struct IRenderer_2D_v1*, const char* path);
    void          (*free_texture)(struct IRenderer_2D_v1*, TextureHandle);
    FontHandle    (*load_font)   (struct IRenderer_2D_v1*, const char* path, int px_size);
    void          (*free_font)   (struct IRenderer_2D_v1*, FontHandle);

    // Render targets (for editor viewport & post-fx later)
    RenderTargetHandle (*create_rt)    (struct IRenderer_2D_v1*, int w, int h);
    void               (*free_rt)      (struct IRenderer_2D_v1*, RenderTargetHandle);
    void               (*set_rt)       (struct IRenderer_2D_v1*, RenderTargetHandle);   // null = default framebuffer
    TextureHandle      (*rt_as_texture)(struct IRenderer_2D_v1*, RenderTargetHandle);   // for ImGui display
} IRenderer_2D_v1;
```

## Sprite batcher

Group draw calls by `{texture, blend_mode, shader}`. Append to a single VBO until state change or max batch size (~8192 sprites). Flush once per group. Target: 10k sprites < 1ms on 2020-class GPU.

## Window + GL context

`zues_window_glfw.dll` owns:
- The `GLFWwindow*`.
- The GL context creation.
- Exposes the GL function loader (`glad`) pointer as a service so the renderer module can use it without linking glad itself.

`zues_renderer_gl.dll` consumes the loader service at `on_load`, creates shaders/buffers.

## Editor viewport rendering

- Game world renders into an offscreen render target (FBO).
- Editor's Scene panel displays that RT as `ImGui::Image(rt_as_texture(...))`.
- Editor camera is separate from game camera. Editor draws its own gizmos (selection outline, grid) on top.

## Not in v1

- Post-processing (bloom, tone mapping) — separate module later
- Lighting / shadows — 3D module concern
- Compute shaders
- Instancing for particle systems (probably Phase 2 of renderer)
