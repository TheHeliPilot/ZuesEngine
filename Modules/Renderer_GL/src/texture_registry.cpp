#include "texture_registry.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include <zues/log.h>

#include <cstdio>
#include <cstring>

namespace zr {

GLuint TextureRegistry::load_from_memory(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return 0;
    if (!gl_GenTextures) return 0;   // GL not loaded

    GLuint id = 0;
    gl_GenTextures(1, &id);
    if (id == 0) return 0;

    gl_BindTexture(GL_TEXTURE_2D, id);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    gl_BindTexture(GL_TEXTURE_2D, 0);

    m_records[id] = {w, h};
    return id;
}

GLuint TextureRegistry::load_from_file(const char* path) {
    if (!path) return 0;

    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load(path, &w, &h, &channels, 4);
    if (!px) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "stbi_load failed: %s (%s)", path, stbi_failure_reason());
        Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", buf);
        return 0;
    }

    GLuint id = load_from_memory(px, w, h);
    stbi_image_free(px);
    return id;
}

void TextureRegistry::free(GLuint id) {
    if (id == 0) return;
    auto it = m_records.find(id);
    if (it == m_records.end()) return;
    if (gl_DeleteTextures) gl_DeleteTextures(1, &id);
    m_records.erase(it);
}

void TextureRegistry::free_all() {
    if (!gl_DeleteTextures) { m_records.clear(); m_white = 0; return; }
    for (const auto& [id, _] : m_records) {
        GLuint x = id;
        gl_DeleteTextures(1, &x);
    }
    m_records.clear();
    if (m_white) {
        gl_DeleteTextures(1, &m_white);
        m_white = 0;
    }
}

bool TextureRegistry::size_of(GLuint id, int* out_w, int* out_h) const {
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (out_w) *out_w = it->second.width;
    if (out_h) *out_h = it->second.height;
    return true;
}

GLuint TextureRegistry::white_texture() {
    if (m_white != 0) return m_white;
    const std::uint32_t white_pixel = 0xFFFFFFFFu;
    m_white = load_from_memory(&white_pixel, 1, 1);
    return m_white;
}

}  // namespace zr
