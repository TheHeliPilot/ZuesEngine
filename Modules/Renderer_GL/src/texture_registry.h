#pragma once

// Texture loading + lifetime tracking for the GL renderer module. The handle
// IS the GL texture id — keeps things simple. Registry stores width/height
// per handle so callers can query without touching GL.

#include "gl_loader.h"

#include <cstdint>
#include <unordered_map>

namespace zr {

struct TextureRecord {
    int width  = 0;
    int height = 0;
};

class TextureRegistry {
public:
    // RGBA8 from an in-memory buffer. Returns the new GL texture id, or 0 on
    // failure.
    GLuint load_from_memory(const void* pixels, int width, int height);

    // Load from a file path. Decodes via stb_image, then forwards to
    // load_from_memory.
    GLuint load_from_file(const char* path);

    void free(GLuint id);
    void free_all();

    bool size_of(GLuint id, int* out_w, int* out_h) const;

    // 1x1 white texture used as the default when draw_sprite gets handle 0 OR
    // for the underlying draw_quad implementation. Lazy-initialised.
    GLuint white_texture();

private:
    std::unordered_map<GLuint, TextureRecord> m_records;
    GLuint m_white = 0;
};

}  // namespace zr
