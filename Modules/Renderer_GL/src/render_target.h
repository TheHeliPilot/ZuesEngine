#pragma once

// Render target = FBO + color texture attachment, sized to (w, h).
// Handle is the FBO id directly. Lookup gives the color texture id (which
// the editor passes to ImGui::Image to display the offscreen render).

#include "gl_loader.h"

#include <unordered_map>

namespace zr {

struct RenderTargetRecord {
    GLuint fbo;
    GLuint color_tex;
    int    width;
    int    height;
};

class RenderTargetRegistry {
public:
    GLuint create(int w, int h);                  // returns FBO id; 0 on failure
    void   destroy(GLuint handle);
    bool   resize(GLuint handle, int new_w, int new_h);   // recreates color tex
    bool   get_size(GLuint handle, int* out_w, int* out_h) const;
    GLuint texture_of(GLuint handle) const;       // 0 if unknown
    const RenderTargetRecord* find(GLuint handle) const;

    void destroy_all();

private:
    std::unordered_map<GLuint, RenderTargetRecord> m_records;
};

}  // namespace zr
