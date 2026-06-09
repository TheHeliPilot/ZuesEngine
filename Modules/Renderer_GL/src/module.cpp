#include <zues/api.h>
#include <zues/log.h>
#include <zues/module.h>
#include <zues/service.h>
#include <zues/services/renderer_2d.h>
#include <zues/services/window.h>

#include "font_registry.h"
#include "gl_loader.h"
#include "render_target.h"
#include "sprite_batcher.h"
#include "texture_registry.h"

#include <cstring>

using namespace Engine;
using namespace zr;

// =============================================================================
// Module-local state.
// =============================================================================

namespace {
    IWindow_v1* g_window = nullptr;

    struct RenderState {
        bool                  gl_loaded = false;
        bool                  ready     = false;
        SpriteBatcher         batcher;
        TextureRegistry       textures;
        RenderTargetRegistry  rts;
        FontRegistry          fonts;

        // Tracks the currently-bound target so begin_frame can pick the
        // right viewport + projection size without an extra GL query.
        // current_fbo == 0 means the default framebuffer (window).
        GLuint                current_fbo = 0;
        int                   current_w   = 0;
        int                   current_h   = 0;
    };
    RenderState g_rs;

    void* loader_thunk(const char* name) {
        return g_window ? g_window->get_proc_address(g_window, name) : nullptr;
    }

    // Update current_w/h to match `current_fbo`. Call after binding a
    // different FBO (or after window resize).
    void refresh_target_size() {
        if (g_rs.current_fbo == 0) {
            int w = 0, h = 0;
            if (g_window) g_window->get_size(g_window, &w, &h);
            g_rs.current_w = w;
            g_rs.current_h = h;
        } else {
            int w = 0, h = 0;
            g_rs.rts.get_size(g_rs.current_fbo, &w, &h);
            g_rs.current_w = w;
            g_rs.current_h = h;
        }
    }
}

// =============================================================================
// IRenderer_2D_v1 implementation.
// =============================================================================

static void rg_begin_frame(IRenderer_2D_v1*,
                           float r, float g, float b, float a) {
    if (g_rs.current_w == 0 || g_rs.current_h == 0) refresh_target_size();
    glViewport(0, 0, g_rs.current_w, g_rs.current_h);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_rs.ready) g_rs.batcher.begin_frame(g_rs.current_w, g_rs.current_h);
}

static void rg_end_frame(IRenderer_2D_v1*) {
    if (g_rs.ready) g_rs.batcher.flush();
}

static void rg_draw_quad(IRenderer_2D_v1*,
                         float x, float y, float w, float h,
                         float r, float g, float b, float a) {
    if (!g_rs.ready) return;
    g_rs.batcher.add(g_rs.textures.white_texture(),
                     x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
                     r, g, b, a);
}

static ZuesTextureHandle rg_load_tex_mem(IRenderer_2D_v1*,
                                          const void* pixels, int w, int h) {
    return g_rs.ready ? g_rs.textures.load_from_memory(pixels, w, h) : 0u;
}

static ZuesTextureHandle rg_load_tex_file(IRenderer_2D_v1*, const char* path) {
    return g_rs.ready ? g_rs.textures.load_from_file(path) : 0u;
}

static void rg_free_texture(IRenderer_2D_v1*, ZuesTextureHandle h) {
    if (g_rs.ready) g_rs.textures.free(h);
}

static void rg_get_texture_size(IRenderer_2D_v1*, ZuesTextureHandle h,
                                 int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!g_rs.ready) return;
    g_rs.textures.size_of(h, out_w, out_h);
}

static void rg_draw_sprite(IRenderer_2D_v1*,
                            ZuesTextureHandle texture,
                            float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1,
                            float r, float g, float b, float a) {
    if (!g_rs.ready) return;
    if (texture == 0) texture = g_rs.textures.white_texture();
    g_rs.batcher.add(texture, x, y, w, h, u0, v0, u1, v1, r, g, b, a);
}

static void rg_draw_sprite_rot(IRenderer_2D_v1*,
                                ZuesTextureHandle texture,
                                float cx, float cy, float w, float h, float angle,
                                float u0, float v0, float u1, float v1,
                                float r, float g, float b, float a) {
    if (!g_rs.ready) return;
    if (texture == 0) texture = g_rs.textures.white_texture();
    g_rs.batcher.add_rot(texture, cx, cy, w, h, angle,
                         u0, v0, u1, v1, r, g, b, a);
}

// ---- Render targets ---------------------------------------------------------

static ZuesRenderTargetHandle rg_create_rt(IRenderer_2D_v1*, int w, int h) {
    return g_rs.ready ? g_rs.rts.create(w, h) : 0u;
}

static void rg_destroy_rt(IRenderer_2D_v1*, ZuesRenderTargetHandle h) {
    if (g_rs.ready) g_rs.rts.destroy(h);
}

static int rg_resize_rt(IRenderer_2D_v1*, ZuesRenderTargetHandle h,
                         int w, int new_h) {
    return (g_rs.ready && g_rs.rts.resize(h, w, new_h)) ? 1 : 0;
}

static void rg_bind_rt(IRenderer_2D_v1*, ZuesRenderTargetHandle h) {
    if (!g_rs.ready) return;
    // Flush any pending sprites against the previous target before binding
    // the new one — avoids a sprite intended for the scene RT spilling onto
    // the window's framebuffer.
    g_rs.batcher.flush();
    gl_BindFramebuffer(GL_FRAMEBUFFER, h);
    g_rs.current_fbo = h;
    refresh_target_size();
}

static ZuesTextureHandle rg_rt_texture(IRenderer_2D_v1*, ZuesRenderTargetHandle h) {
    return g_rs.ready ? g_rs.rts.texture_of(h) : 0u;
}

// ---- Text rendering (v5) ---------------------------------------------------

static ZuesFontHandle rg_load_font(IRenderer_2D_v1*, const char* path,
                                    int pixel_height) {
    return g_rs.ready ? g_rs.fonts.load_from_file(path, pixel_height) : 0u;
}

static void rg_free_font(IRenderer_2D_v1*, ZuesFontHandle h) {
    if (g_rs.ready) g_rs.fonts.free(h);
}

// ---- v6: texture sampler tweaks --------------------------------------------

static void rg_set_texture_filter(IRenderer_2D_v1*, ZuesTextureHandle h,
                                    int filter) {
    if (!g_rs.ready || h == 0) return;
    const GLenum f = (filter == 1) ? GL_NEAREST : GL_LINEAR;
    gl_BindTexture(GL_TEXTURE_2D, h);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    gl_BindTexture(GL_TEXTURE_2D, 0);
}

static void rg_set_texture_wrap(IRenderer_2D_v1*, ZuesTextureHandle h,
                                  int wrap) {
    if (!g_rs.ready || h == 0) return;
    GLenum w;
    switch (wrap) {
        case 1:  w = GL_REPEAT; break;
        case 2:  w = GL_MIRRORED_REPEAT; break;
        case 0:
        default: w = GL_CLAMP_TO_EDGE; break;
    }
    gl_BindTexture(GL_TEXTURE_2D, h);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, w);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, w);
    gl_BindTexture(GL_TEXTURE_2D, 0);
}

// Walk UTF-8 (treated as ASCII for v5) and emit one batched quad per
// glyph. (x, y) is the user-facing top-left of the bounding box.
//
// stbtt_GetPackedQuad takes a cursor (xpos, ypos) where ypos is the
// BASELINE, mutates xpos by the glyph advance, and writes pixel-space
// quad coordinates (q.x0..x1, q.y0..y1) at the BAKE size into `q`. For
// our scaling model we run the cursor in native-bake units, then map
// each output quad into user pixels at the requested pixel_size.
static void rg_draw_text(IRenderer_2D_v1*,
                          ZuesFontHandle font_h,
                          const char* utf8,
                          float x, float y, float pixel_size,
                          float r, float g, float b, float a) {
    if (!g_rs.ready || font_h == 0 || !utf8) return;
    const FontRecord* rec = g_rs.fonts.get(font_h);
    if (!rec) return;
    const float scale = (rec->pixel_height > 0)
        ? pixel_size / static_cast<float>(rec->pixel_height) : 1.0f;
    // Run the cursor in native bake-pixel space starting at (0, ascent),
    // so y == 0 in native space lines up with the user's top-edge after
    // scaling.
    float native_x = 0.0f;
    float native_y = rec->ascent_px;

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
         *p != 0; ++p) {
        // Non-ASCII (incl. UTF-8 continuation bytes): advance by half a
        // pixel-size to keep layout sane; v5 doesn't bake those glyphs.
        if (*p < FontRecord::kFirst ||
            *p >= FontRecord::kFirst + FontRecord::kCount) {
            native_x += static_cast<float>(rec->pixel_height) * 0.5f;
            continue;
        }
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(rec->chars.data(), rec->atlas_w, rec->atlas_h,
                             *p - FontRecord::kFirst,
                             &native_x, &native_y, &q,
                             /*align_to_integer=*/0);

        // q.x*/y* are in native pixels relative to the cursor's starting
        // (0, ascent). Convert to user pixels: scale * native, then offset
        // by (x, y) which is the user-facing TOP-LEFT.
        const float sx0 = x + q.x0 * scale;
        const float sy0 = y + q.y0 * scale;
        const float sx1 = x + q.x1 * scale;
        const float sy1 = y + q.y1 * scale;

        g_rs.batcher.add(rec->texture,
                          sx0, sy0,
                          sx1 - sx0, sy1 - sy0,
                          q.s0, q.t0, q.s1, q.t1,
                          r, g, b, a);
    }
}

static void rg_measure_text(IRenderer_2D_v1*, ZuesFontHandle font_h,
                              const char* utf8, float pixel_size,
                              float* out_w, float* out_h) {
    if (out_w) *out_w = 0.0f;
    if (out_h) *out_h = pixel_size;
    if (!g_rs.ready || font_h == 0 || !utf8) return;
    const FontRecord* rec = g_rs.fonts.get(font_h);
    if (!rec) return;
    const float scale = (rec->pixel_height > 0)
        ? pixel_size / static_cast<float>(rec->pixel_height) : 1.0f;
    float native_x = 0.0f, native_y = 0.0f;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
         *p != 0; ++p) {
        if (*p < FontRecord::kFirst ||
            *p >= FontRecord::kFirst + FontRecord::kCount) {
            native_x += static_cast<float>(rec->pixel_height) * 0.5f;
            continue;
        }
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(rec->chars.data(), rec->atlas_w, rec->atlas_h,
                             *p - FontRecord::kFirst,
                             &native_x, &native_y, &q, 0);
    }
    if (out_w) *out_w = native_x * scale;
    if (out_h) *out_h = pixel_size;
}

// =============================================================================
// Service vtable.
// =============================================================================

static IRenderer_2D_v1 g_iface = {
    /* abi_version              */ ZUES_SERVICE_RENDERER_2D_VERSION,
    /* begin_frame              */ rg_begin_frame,
    /* end_frame                */ rg_end_frame,
    /* draw_quad                */ rg_draw_quad,
    /* load_texture_from_memory */ rg_load_tex_mem,
    /* load_texture_from_file   */ rg_load_tex_file,
    /* free_texture             */ rg_free_texture,
    /* get_texture_size         */ rg_get_texture_size,
    /* draw_sprite              */ rg_draw_sprite,
    /* create_render_target     */ rg_create_rt,
    /* destroy_render_target    */ rg_destroy_rt,
    /* resize_render_target     */ rg_resize_rt,
    /* bind_render_target       */ rg_bind_rt,
    /* get_render_target_texture*/ rg_rt_texture,
    /* draw_sprite_rot          */ rg_draw_sprite_rot,
    /* load_font_from_file      */ rg_load_font,
    /* free_font                */ rg_free_font,
    /* draw_text                */ rg_draw_text,
    /* measure_text             */ rg_measure_text,
    /* set_texture_filter       */ rg_set_texture_filter,
    /* set_texture_wrap         */ rg_set_texture_wrap,
};

// =============================================================================
// Module lifecycle.
// =============================================================================

static void on_load(ModuleContext* ctx) {
    ZUES_LOG_INFO("renderer_gl on_load");
    if (ctx && ctx->services) {
        ctx->services->register_service(ZUES_SERVICE_RENDERER_2D,
                                        ZUES_SERVICE_RENDERER_2D_VERSION,
                                        &g_iface);
    }
}

static void on_ready(ModuleContext* ctx) {
    if (!ctx || !ctx->services) return;

    g_window = static_cast<IWindow_v1*>(
        ctx->services->get_service(ZUES_SERVICE_WINDOW_V1, ZUES_SERVICE_WINDOW_V1_VERSION));

    if (!g_window) {
        ZUES_LOG_WARN("renderer_gl: no IWindow_v1; running headless");
        return;
    }

    g_rs.gl_loaded = load_gl(loader_thunk);
    if (!g_rs.gl_loaded) {
        ZUES_LOG_ERROR("renderer_gl: GL function load failed");
        return;
    }

    if (!g_rs.batcher.init()) {
        ZUES_LOG_ERROR("renderer_gl: sprite batcher init failed");
        return;
    }

    glEnable(GL_BLEND);
    gl_BlendFunc(0x0302 /* GL_SRC_ALPHA */,
                 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */);

    g_rs.ready = true;
    refresh_target_size();
    ZUES_LOG_INFO("renderer_gl on_ready — sprite batcher + render targets live");
}

static void on_unload(ModuleContext*) {
    ZUES_LOG_INFO("renderer_gl on_unload");
    if (g_rs.gl_loaded) {
        g_rs.rts.destroy_all();
        g_rs.textures.free_all();
        g_rs.fonts.free_all();
        g_rs.batcher.shutdown();
    }
    g_rs = {};
    g_window = nullptr;
}

static const ModuleInfo INFO = {
    .name        = "zues_renderer_gl",
    .version     = "0.1.0",
    .abi_version = ZUES_MODULE_ABI_VERSION,
    .on_load     = on_load,
    .on_ready    = on_ready,
    .on_update   = nullptr,
    .on_unload   = on_unload,
};

ZUES_MODULE_EXPORT const ModuleInfo* zues_module_entry() { return &INFO; }
