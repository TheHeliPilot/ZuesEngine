#include "sprite_batcher.h"

#include <zues/log.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace zr {

namespace {
    constexpr const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
out vec2 v_uv;
out vec4 v_color;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
}
)GLSL";

    constexpr const char* FRAG_SRC = R"GLSL(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D u_tex;
void main() {
    frag_color = texture(u_tex, v_uv) * v_color;
}
)GLSL";

    GLuint compile(GLenum type, const char* src) {
        GLuint sh = gl_CreateShader(type);
        gl_ShaderSource(sh, 1, &src, nullptr);
        gl_CompileShader(sh);
        GLint ok = 0;
        gl_GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512] = {};
            GLsizei len = 0;
            gl_GetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
            char msg[640];
            std::snprintf(msg, sizeof(msg), "sprite shader compile failed: %s", log);
            Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", msg);
            gl_DeleteShader(sh);
            return 0;
        }
        return sh;
    }

    GLuint link(const char* vert, const char* frag) {
        GLuint v = compile(GL_VERTEX_SHADER, vert);
        GLuint f = compile(GL_FRAGMENT_SHADER, frag);
        if (!v || !f) {
            if (v) gl_DeleteShader(v);
            if (f) gl_DeleteShader(f);
            return 0;
        }
        GLuint p = gl_CreateProgram();
        gl_AttachShader(p, v);
        gl_AttachShader(p, f);
        gl_LinkProgram(p);
        GLint ok = 0;
        gl_GetProgramiv(p, GL_LINK_STATUS, &ok);
        gl_DeleteShader(v);
        gl_DeleteShader(f);
        if (!ok) {
            char log[512] = {};
            GLsizei len = 0;
            gl_GetProgramInfoLog(p, sizeof(log) - 1, &len, log);
            char msg[640];
            std::snprintf(msg, sizeof(msg), "sprite program link failed: %s", log);
            Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", msg);
            gl_DeleteProgram(p);
            return 0;
        }
        return p;
    }
}

bool SpriteBatcher::init() {
    if (!gl_GenBuffers) return false;

    m_program = link(VERT_SRC, FRAG_SRC);
    if (!m_program) return false;
    m_u_proj = gl_GetUniformLocation(m_program, "u_proj");
    m_u_tex  = gl_GetUniformLocation(m_program, "u_tex");

    gl_GenVertexArrays(1, &m_vao);
    gl_BindVertexArray(m_vao);

    gl_GenBuffers(1, &m_vbo);
    gl_BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl_BufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(MAX_SPRITES) * 4 * sizeof(SpriteVertex),
                  nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = sizeof(SpriteVertex);
    gl_VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<void*>(offsetof(SpriteVertex, x)));
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<void*>(offsetof(SpriteVertex, u)));
    gl_EnableVertexAttribArray(1);
    gl_VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                           reinterpret_cast<void*>(offsetof(SpriteVertex, r)));
    gl_EnableVertexAttribArray(2);

    gl_GenBuffers(1, &m_ebo);
    gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    ensure_index_buffer(MAX_SPRITES);

    gl_BindVertexArray(0);

    m_verts.reserve(static_cast<size_t>(MAX_SPRITES) * 4);
    return true;
}

void SpriteBatcher::ensure_index_buffer(int sprite_count) {
    if (sprite_count <= m_index_capacity) return;

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<size_t>(sprite_count) * 6);
    for (int i = 0; i < sprite_count; ++i) {
        const unsigned int base = static_cast<unsigned int>(i) * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
    gl_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    gl_BufferData(GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                  indices.data(), GL_STATIC_DRAW);
    m_index_capacity = sprite_count;
}

void SpriteBatcher::shutdown() {
    if (m_vao)     gl_DeleteVertexArrays(1, &m_vao);
    if (m_vbo)     gl_DeleteBuffers(1, &m_vbo);
    if (m_ebo)     gl_DeleteBuffers(1, &m_ebo);
    if (m_program) gl_DeleteProgram(m_program);
    m_vao = m_vbo = m_ebo = m_program = 0;
}

void SpriteBatcher::begin_frame(int fb_w, int fb_h) {
    sprites_last_frame   = m_sprites_this_frame;
    drawcalls_last_frame = m_drawcalls_this_frame;
    m_sprites_this_frame   = 0;
    m_drawcalls_this_frame = 0;

    m_current_texture = 0;
    m_verts.clear();

    if (fb_w <= 0 || fb_h <= 0) return;

    // Top-left origin orthographic projection. Column-major.
    const float fx = static_cast<float>(fb_w);
    const float fy = static_cast<float>(fb_h);
    const float proj[16] = {
        2.0f / fx, 0.0f,         0.0f, 0.0f,
        0.0f,    -2.0f / fy,     0.0f, 0.0f,
        0.0f,     0.0f,         -1.0f, 0.0f,
       -1.0f,     1.0f,          0.0f, 1.0f,
    };

    gl_UseProgram(m_program);
    gl_UniformMatrix4fv(m_u_proj, 1, GL_FALSE, proj);
    gl_Uniform1i(m_u_tex, 0);   // sampler unit 0
}

void SpriteBatcher::add(GLuint texture,
                        float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        float r, float g, float b, float a) {
    if (texture == 0) return;
    if (m_current_texture == 0) m_current_texture = texture;
    if (texture != m_current_texture
        || m_verts.size() / 4 >= static_cast<size_t>(MAX_SPRITES)) {
        flush();
        m_current_texture = texture;
    }

    m_verts.push_back({x,     y,     u0, v0, r, g, b, a});
    m_verts.push_back({x + w, y,     u1, v0, r, g, b, a});
    m_verts.push_back({x + w, y + h, u1, v1, r, g, b, a});
    m_verts.push_back({x,     y + h, u0, v1, r, g, b, a});
    ++m_sprites_this_frame;
}

void SpriteBatcher::add_rot(GLuint texture,
                            float cx, float cy, float w, float h, float angle,
                            float u0, float v0, float u1, float v1,
                            float r, float g, float b, float a) {
    if (texture == 0) return;
    if (m_current_texture == 0) m_current_texture = texture;
    if (texture != m_current_texture
        || m_verts.size() / 4 >= static_cast<size_t>(MAX_SPRITES)) {
        flush();
        m_current_texture = texture;
    }

    // Rotate the four corners around (cx, cy). hw/hh are half-extents in
    // sprite-local space. Standard 2D rotation matrix; cs/sn computed once.
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float cs = std::cos(angle);
    const float sn = std::sin(angle);

    auto xf = [&](float lx, float ly, float& ox, float& oy) {
        ox = cx + lx * cs - ly * sn;
        oy = cy + lx * sn + ly * cs;
    };

    float x0, y0, x1, y1, x2, y2, x3, y3;
    xf(-hw, -hh, x0, y0);   // local TL
    xf( hw, -hh, x1, y1);   // local TR
    xf( hw,  hh, x2, y2);   // local BR
    xf(-hw,  hh, x3, y3);   // local BL

    m_verts.push_back({x0, y0, u0, v0, r, g, b, a});
    m_verts.push_back({x1, y1, u1, v0, r, g, b, a});
    m_verts.push_back({x2, y2, u1, v1, r, g, b, a});
    m_verts.push_back({x3, y3, u0, v1, r, g, b, a});
    ++m_sprites_this_frame;
}

void SpriteBatcher::flush() {
    if (m_verts.empty() || m_current_texture == 0) {
        m_verts.clear();
        return;
    }

    const int sprite_count = static_cast<int>(m_verts.size() / 4);
    ensure_index_buffer(sprite_count);

    gl_BindVertexArray(m_vao);
    gl_BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    gl_BufferSubData(GL_ARRAY_BUFFER, 0,
                     static_cast<GLsizeiptr>(m_verts.size() * sizeof(SpriteVertex)),
                     m_verts.data());

    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, m_current_texture);

    gl_DrawElements(GL_TRIANGLES,
                    sprite_count * 6, GL_UNSIGNED_INT, nullptr);

    gl_BindVertexArray(0);
    m_verts.clear();
    ++m_drawcalls_this_frame;
}

}  // namespace zr
