#include "render_target.h"

#include <zues/log.h>

#include <cstdio>

namespace zr {

namespace {
    GLuint create_color_texture(int w, int h) {
        if (!gl_GenTextures || w <= 0 || h <= 0) return 0;

        GLuint tex = 0;
        gl_GenTextures(1, &tex);
        if (tex == 0) return 0;

        gl_BindTexture(GL_TEXTURE_2D, tex);
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
        gl_BindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    bool attach_and_check(GLuint fbo, GLuint color_tex) {
        gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, color_tex, 0);
        const GLenum status = gl_CheckFramebufferStatus(GL_FRAMEBUFFER);
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "FBO incomplete: status=0x%X", status);
            Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", msg);
            return false;
        }
        return true;
    }
}

GLuint RenderTargetRegistry::create(int w, int h) {
    if (!gl_GenFramebuffers || w <= 0 || h <= 0) return 0;

    GLuint fbo = 0;
    gl_GenFramebuffers(1, &fbo);
    if (fbo == 0) return 0;

    GLuint tex = create_color_texture(w, h);
    if (tex == 0) {
        gl_DeleteFramebuffers(1, &fbo);
        return 0;
    }
    if (!attach_and_check(fbo, tex)) {
        gl_DeleteTextures(1, &tex);
        gl_DeleteFramebuffers(1, &fbo);
        return 0;
    }

    m_records[fbo] = {fbo, tex, w, h};
    return fbo;
}

void RenderTargetRegistry::destroy(GLuint handle) {
    auto it = m_records.find(handle);
    if (it == m_records.end()) return;
    gl_DeleteTextures(1, &it->second.color_tex);
    GLuint fbo = it->second.fbo;
    gl_DeleteFramebuffers(1, &fbo);
    m_records.erase(it);
}

bool RenderTargetRegistry::resize(GLuint handle, int new_w, int new_h) {
    auto it = m_records.find(handle);
    if (it == m_records.end()) return false;
    if (it->second.width == new_w && it->second.height == new_h) return true;
    if (new_w <= 0 || new_h <= 0) return false;

    // Recreate color texture; FBO can be reused.
    gl_DeleteTextures(1, &it->second.color_tex);
    GLuint new_tex = create_color_texture(new_w, new_h);
    if (new_tex == 0) {
        m_records.erase(it);
        return false;
    }
    if (!attach_and_check(it->second.fbo, new_tex)) {
        gl_DeleteTextures(1, &new_tex);
        m_records.erase(it);
        return false;
    }
    it->second.color_tex = new_tex;
    it->second.width  = new_w;
    it->second.height = new_h;
    return true;
}

bool RenderTargetRegistry::get_size(GLuint handle, int* out_w, int* out_h) const {
    auto it = m_records.find(handle);
    if (it == m_records.end()) return false;
    if (out_w) *out_w = it->second.width;
    if (out_h) *out_h = it->second.height;
    return true;
}

GLuint RenderTargetRegistry::texture_of(GLuint handle) const {
    auto it = m_records.find(handle);
    return (it == m_records.end()) ? 0u : it->second.color_tex;
}

const RenderTargetRecord* RenderTargetRegistry::find(GLuint handle) const {
    auto it = m_records.find(handle);
    return (it == m_records.end()) ? nullptr : &it->second;
}

void RenderTargetRegistry::destroy_all() {
    if (gl_DeleteFramebuffers) {
        for (auto& [fbo, rec] : m_records) {
            GLuint t = rec.color_tex;
            if (gl_DeleteTextures)     gl_DeleteTextures(1, &t);
            GLuint f = fbo;
            gl_DeleteFramebuffers(1, &f);
        }
    }
    m_records.clear();
}

}  // namespace zr
