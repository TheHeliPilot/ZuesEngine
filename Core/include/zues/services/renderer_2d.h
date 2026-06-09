#ifndef ZUES_SERVICE_RENDERER_2D_H
#define ZUES_SERVICE_RENDERER_2D_H

/*
 * 2D rendering service. Module-implemented (e.g. zues_renderer_gl) so the
 * 3D renderer can drop in later under a different service ID.
 *
 * Phase 3 v1 surface: just clear-screen. begin_frame sets the clear color
 * and clears the framebuffer; end_frame is a no-op (the editor calls
 * IWindow_v1::swap_buffers afterwards).
 *
 * Sprite batcher + texture loading + text rendering ship in the next pass
 * via additional fields appended to this struct (and a version bump).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* TextureHandle is the GL texture id under the hood (sufficient for v1 —
 * we'll wrap it in a generational handle if/when we need it). 0 = invalid.
 * Guarded so project_api.h can re-declare it without a conflict when both
 * headers are included in the same translation unit. */
#ifndef ZUES_TEXTURE_HANDLE_DEFINED
#define ZUES_TEXTURE_HANDLE_DEFINED
typedef uint32_t ZuesTextureHandle;
#endif

/* RenderTargetHandle is the FBO id under the hood. 0 = default (window). */
typedef uint32_t ZuesRenderTargetHandle;

/* FontHandle indexes the renderer's font_registry. 0 = invalid; the
 * ui_render_system substitutes the engine default font for Text components
 * whose Text.font is still 0. */
#ifndef ZUES_FONT_HANDLE_DEFINED
#define ZUES_FONT_HANDLE_DEFINED
typedef uint32_t ZuesFontHandle;
#endif

typedef struct IRenderer_2D_v1 {
    uint32_t abi_version;

    /* ---- v1 fields (always present) -------------------------------------- */

    void (*begin_frame)(struct IRenderer_2D_v1* self,
                        float clear_r, float clear_g, float clear_b, float clear_a);
    void (*end_frame)  (struct IRenderer_2D_v1* self);

    /* Solid-color rectangle in pixel coordinates ([0,0] = top-left). */
    void (*draw_quad)  (struct IRenderer_2D_v1* self,
                        float x, float y, float w, float h,
                        float r, float g, float b, float a);

    /* ---- v2 fields (sprite batcher + textures, Phase 3.5b) --------------- */
    /* Available when SERVICE_VERSION >= 2. Old (v1) callers ignore these. */

    ZuesTextureHandle (*load_texture_from_memory)(struct IRenderer_2D_v1* self,
                                                   const void* pixels_rgba8,
                                                   int width, int height);
    ZuesTextureHandle (*load_texture_from_file)  (struct IRenderer_2D_v1* self,
                                                   const char* path);
    void              (*free_texture)            (struct IRenderer_2D_v1* self,
                                                   ZuesTextureHandle handle);
    void              (*get_texture_size)        (struct IRenderer_2D_v1* self,
                                                   ZuesTextureHandle handle,
                                                   int* out_w, int* out_h);

    /* Textured sprite. uv0/uv1 select a sub-rect of the texture (use 0,0,1,1
     * for the whole image). Color is multiplied with the sampled pixel. */
    void (*draw_sprite)(struct IRenderer_2D_v1* self,
                        ZuesTextureHandle texture,
                        float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        float r, float g, float b, float a);

    /* ---- v3 fields (offscreen render targets, Phase 3.8) ---------------- */

    /* Create an offscreen FBO sized (w, h) with an RGBA color attachment.
     * Returns 0 on failure. */
    ZuesRenderTargetHandle (*create_render_target) (struct IRenderer_2D_v1* self,
                                                     int width, int height);
    void                   (*destroy_render_target)(struct IRenderer_2D_v1* self,
                                                     ZuesRenderTargetHandle h);

    /* Resize the color texture in-place. Returns 0 on failure. */
    int                    (*resize_render_target) (struct IRenderer_2D_v1* self,
                                                     ZuesRenderTargetHandle h,
                                                     int new_width, int new_height);

    /* Pass 0 to bind the default framebuffer (the window). */
    void                   (*bind_render_target)   (struct IRenderer_2D_v1* self,
                                                     ZuesRenderTargetHandle h);

    /* Returns the underlying texture handle for the RT's color attachment.
     * Use this with draw_sprite OR pass to ImGui::Image. */
    ZuesTextureHandle      (*get_render_target_texture)(struct IRenderer_2D_v1* self,
                                                         ZuesRenderTargetHandle h);

    /* ---- v4 fields (rotated quads, Phase 4.4) -------------------------- */
    /* Anchor is the SPRITE CENTER (cx, cy). Angle is in radians. Used by
     * the sprite render system for rotated transforms AND by the gizmo
     * pipeline (lines = thin rotated quads). */
    void (*draw_sprite_rot)(struct IRenderer_2D_v1* self,
                            ZuesTextureHandle texture,
                            float cx, float cy, float w, float h, float angle,
                            float u0, float v0, float u1, float v1,
                            float r, float g, float b, float a);

    /* ---- v5 fields (text rendering, Phase 5) --------------------------- */
    /* Bake an ASCII glyph atlas from a TTF file at `pixel_height` pixels.
     * Returns 0 on failure. The atlas is shared by all draws of this font;
     * `pixel_size` at draw time scales it. Re-bake at a larger pixel_height
     * for crisper text at small sizes. UTF-8 input is treated as ASCII for
     * v5 -- non-ASCII bytes draw as a missing-glyph rect. */
    ZuesFontHandle (*load_font_from_file)(struct IRenderer_2D_v1* self,
                                           const char* path, int pixel_height);
    void           (*free_font)          (struct IRenderer_2D_v1* self,
                                           ZuesFontHandle font);

    /* Draw UTF-8 text at (x, y), where (x, y) is the TOP-LEFT of the
     * bounding box. Caller measures with measure_text and applies pivots /
     * h_align before calling this. Color is RGBA. */
    void (*draw_text)   (struct IRenderer_2D_v1* self,
                          ZuesFontHandle font,
                          const char* utf8,
                          float x, float y, float pixel_size,
                          float r, float g, float b, float a);

    /* Measure UTF-8 text in pixels (advance + ascent). For HUD layout. */
    void (*measure_text)(struct IRenderer_2D_v1* self,
                          ZuesFontHandle font,
                          const char* utf8, float pixel_size,
                          float* out_w, float* out_h);

    /* ---- v6 fields (per-texture filter + wrap, sprite cutter prep) ---- */

    /* Texture sampler tweaks. Apply to an already-loaded texture so the
     * sprite settings UI can change filter/wrap without re-loading the
     * pixels. Filter values: 0 = linear, 1 = nearest. Wrap values:
     * 0 = clamp, 1 = repeat, 2 = mirror. */
    void (*set_texture_filter)(struct IRenderer_2D_v1* self,
                                ZuesTextureHandle handle, int filter);
    void (*set_texture_wrap)  (struct IRenderer_2D_v1* self,
                                ZuesTextureHandle handle, int wrap);
} IRenderer_2D_v1;

#define ZUES_SERVICE_RENDERER_2D         "zues.renderer.2d"
#define ZUES_SERVICE_RENDERER_2D_VERSION 6

#ifdef __cplusplus
}
#endif

#endif /* ZUES_SERVICE_RENDERER_2D_H */
