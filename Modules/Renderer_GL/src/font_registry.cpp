#include "font_registry.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <zues/log.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace zr {

namespace {

// Read entire file into a heap buffer. Returns empty vector on failure.
std::vector<unsigned char> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) return {};
    return bytes;
}

// Pick an atlas size that fits a kCount-glyph bake at `px`. Conservative:
// a 32-px bake fits in 256x256, doubling per ~32 px. We over-allocate
// rather than fail mid-bake -- a 1024x1024 alpha atlas is 1 MB, cheap.
int pick_atlas_dim(int pixel_height) {
    if (pixel_height <= 24) return 256;
    if (pixel_height <= 48) return 512;
    return 1024;
}

}  // namespace

uint32_t FontRegistry::load_from_file(const char* path, int pixel_height) {
    if (!path || pixel_height <= 0 || pixel_height > 256) return 0;

    auto bytes = read_file(path);
    if (bytes.empty()) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "font_registry: failed to read %s", path);
        Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", buf);
        return 0;
    }

    // Bake an alpha-only atlas with stb_truetype's pack API. The pack
    // helper picks per-glyph rects via skyline algorithm; we just hand it
    // a buffer big enough for the requested size class.
    const int atlas_dim = pick_atlas_dim(pixel_height);
    std::vector<unsigned char> alpha(static_cast<size_t>(atlas_dim * atlas_dim), 0);

    stbtt_pack_context pc{};
    if (!stbtt_PackBegin(&pc, alpha.data(), atlas_dim, atlas_dim, 0, 1, nullptr)) {
        Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl",
                          "font_registry: stbtt_PackBegin failed");
        return 0;
    }
    // 1px oversample on both axes -- crisper than no oversample, and
    // doesn't blow up the atlas for ASCII-only.
    stbtt_PackSetOversampling(&pc, 1, 1);

    FontRecord rec;
    rec.chars.resize(FontRecord::kCount);
    if (!stbtt_PackFontRange(&pc, bytes.data(), 0,
                              static_cast<float>(pixel_height),
                              FontRecord::kFirst, FontRecord::kCount,
                              rec.chars.data())) {
        stbtt_PackEnd(&pc);
        Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl",
                          "font_registry: PackFontRange failed (atlas too small?)");
        return 0;
    }
    stbtt_PackEnd(&pc);

    // Capture font ascent so callers can position text by top-edge rather
    // than baseline. ascent is in unscaled font units; convert with the
    // pixel-height scale.
    stbtt_fontinfo info{};
    if (stbtt_InitFont(&info, bytes.data(),
                       stbtt_GetFontOffsetForIndex(bytes.data(), 0))) {
        int asc = 0, desc = 0, line = 0;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &line);
        const float scale = stbtt_ScaleForPixelHeight(
            &info, static_cast<float>(pixel_height));
        rec.ascent_px = asc * scale;
    } else {
        rec.ascent_px = static_cast<float>(pixel_height) * 0.8f;  // sane fallback
    }

    // Expand R8 -> RGBA8 so the existing sprite batcher's RGBA shader
    // path renders glyphs with the tint colour. Cost: 4x atlas memory
    // (1 MB at 1024x1024). Worth it to avoid a separate text shader.
    const int N = atlas_dim * atlas_dim;
    std::vector<unsigned char> rgba(static_cast<size_t>(N) * 4u);
    for (int i = 0; i < N; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }

    // Upload as RGBA8 with linear filtering -- text scales across DPI ranges.
    if (!gl_GenTextures) return 0;
    GLuint tex = 0;
    gl_GenTextures(1, &tex);
    if (tex == 0) return 0;
    gl_BindTexture(GL_TEXTURE_2D, tex);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                   atlas_dim, atlas_dim, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    gl_BindTexture(GL_TEXTURE_2D, 0);

    rec.texture      = tex;
    rec.atlas_w      = atlas_dim;
    rec.atlas_h      = atlas_dim;
    rec.pixel_height = pixel_height;

    m_records[tex] = std::move(rec);
    return tex;
}

void FontRegistry::free(uint32_t handle) {
    if (handle == 0) return;
    auto it = m_records.find(handle);
    if (it == m_records.end()) return;
    if (gl_DeleteTextures) {
        GLuint id = handle;
        gl_DeleteTextures(1, &id);
    }
    m_records.erase(it);
}

void FontRegistry::free_all() {
    if (!gl_DeleteTextures) { m_records.clear(); return; }
    for (auto& [id, _] : m_records) {
        GLuint x = id;
        gl_DeleteTextures(1, &x);
    }
    m_records.clear();
}

const FontRecord* FontRegistry::get(uint32_t handle) const {
    auto it = m_records.find(handle);
    return it == m_records.end() ? nullptr : &it->second;
}

}  // namespace zr
