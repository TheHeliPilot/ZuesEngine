#pragma once

// Sprite batcher. Groups draw_sprite calls by texture, flushes one drawcall
// per group. Single-buffer, single-shader. Vertex format = pos2 + uv2 + rgba.

#include "gl_loader.h"

#include <cstdint>
#include <vector>

namespace zr {

struct SpriteVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

class SpriteBatcher {
public:
    bool init();             // creates VAO/VBO/EBO + shader. Requires GL loaded.
    void shutdown();

    // Resets state for a new frame and uploads the orthographic projection
    // matrix (top-left origin: [0..fb_w, 0..fb_h]).
    void begin_frame(int fb_w, int fb_h);

    // Append a sprite to the batch. Flushes implicitly when the texture
    // changes or the batch fills up.
    void add(GLuint texture,
             float x, float y, float w, float h,
             float u0, float v0, float u1, float v1,
             float r, float g, float b, float a);

    // Append a rotated sprite. Anchor is the SPRITE CENTER (cx, cy);
    // (w, h) is the sprite's full size; angle is in radians (CCW in
    // a Y-down screen-space, i.e. "normal" math rotation appears CW
    // because Y is flipped — caller should pre-negate if needed).
    void add_rot(GLuint texture,
                 float cx, float cy, float w, float h, float angle,
                 float u0, float v0, float u1, float v1,
                 float r, float g, float b, float a);

    // Issues drawcalls for any pending sprites. Called by end_frame.
    void flush();

    // Stats (last frame).
    int drawcalls_last_frame = 0;
    int sprites_last_frame   = 0;

private:
    void ensure_index_buffer(int sprite_count);

    static constexpr int MAX_SPRITES = 4096;

    GLuint               m_vao     = 0;
    GLuint               m_vbo     = 0;
    GLuint               m_ebo     = 0;
    GLuint               m_program = 0;
    GLint                m_u_proj  = -1;
    GLint                m_u_tex   = -1;

    int                  m_index_capacity = 0;     // how many sprites the EBO covers
    GLuint               m_current_texture = 0;
    std::vector<SpriteVertex> m_verts;             // 4 per sprite
    int                  m_drawcalls_this_frame = 0;
    int                  m_sprites_this_frame   = 0;
};

}  // namespace zr
