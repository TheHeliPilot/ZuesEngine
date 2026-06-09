#pragma once

// Minimal hand-rolled GL 2.0+ function loader. Just the entry points we
// actually call — anything new gets one new line each in three places
// (typedef, extern, LOAD macro). No glad / GLEW dependency.
//
// GL 1.0/1.1 functions (glClear, glClearColor, glViewport) come from the
// platform GL library that's statically linked via OpenGL::GL.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
    // gl.h on Windows wants windows.h types. Pull them in here so module
    // code that #includes this header doesn't need WIN32_LEAN_AND_MEAN
    // boilerplate.
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif
#include <GL/gl.h>

// On many systems gl.h doesn't define these GL 2.0+ types; provide them.
#ifndef GL_ARRAY_BUFFER
    using GLchar     = char;
    using GLintptr   = std::ptrdiff_t;
    using GLsizeiptr = std::ptrdiff_t;

    inline constexpr GLenum GL_ARRAY_BUFFER         = 0x8892;
    inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
    inline constexpr GLenum GL_STATIC_DRAW          = 0x88E4;
    inline constexpr GLenum GL_DYNAMIC_DRAW         = 0x88E8;
    inline constexpr GLenum GL_FRAGMENT_SHADER      = 0x8B30;
    inline constexpr GLenum GL_VERTEX_SHADER        = 0x8B31;
    inline constexpr GLenum GL_COMPILE_STATUS       = 0x8B81;
    inline constexpr GLenum GL_LINK_STATUS          = 0x8B82;
    inline constexpr GLenum GL_INFO_LOG_LENGTH      = 0x8B84;
#endif

// Texture / sampler / format constants we use. Each is guarded against
// gl.h having defined the same name as a macro (in which case our constexpr
// would expand to "constexpr GLenum 0x... = ..." — a parse error). On
// systems where gl.h defines them, we just use those.
#ifndef GL_TEXTURE0
    inline constexpr GLenum GL_TEXTURE0             = 0x84C0;
#endif
#ifndef GL_TEXTURE_2D
    inline constexpr GLenum GL_TEXTURE_2D           = 0x0DE1;
#endif
#ifndef GL_CLAMP_TO_EDGE
    inline constexpr GLenum GL_CLAMP_TO_EDGE        = 0x812F;
#endif
#ifndef GL_RGB
    inline constexpr GLenum GL_RGB                  = 0x1907;
#endif
#ifndef GL_RGBA
    inline constexpr GLenum GL_RGBA                 = 0x1908;
#endif
#ifndef GL_UNSIGNED_BYTE
    inline constexpr GLenum GL_UNSIGNED_BYTE        = 0x1401;
#endif
#ifndef GL_TEXTURE_MIN_FILTER
    inline constexpr GLenum GL_TEXTURE_MIN_FILTER   = 0x2801;
#endif
#ifndef GL_TEXTURE_MAG_FILTER
    inline constexpr GLenum GL_TEXTURE_MAG_FILTER   = 0x2800;
#endif
#ifndef GL_TEXTURE_WRAP_S
    inline constexpr GLenum GL_TEXTURE_WRAP_S       = 0x2802;
#endif
#ifndef GL_TEXTURE_WRAP_T
    inline constexpr GLenum GL_TEXTURE_WRAP_T       = 0x2803;
#endif
#ifndef GL_LINEAR
    inline constexpr GLenum GL_LINEAR               = 0x2601;
#endif
#ifndef GL_NEAREST
    inline constexpr GLenum GL_NEAREST              = 0x2600;
#endif
#ifndef GL_REPEAT
    inline constexpr GLenum GL_REPEAT               = 0x2901;
#endif
#ifndef GL_MIRRORED_REPEAT
    inline constexpr GLenum GL_MIRRORED_REPEAT      = 0x8370;
#endif
#ifndef GL_UNPACK_ALIGNMENT
    inline constexpr GLenum GL_UNPACK_ALIGNMENT     = 0x0CF5;
#endif
#ifndef GL_TRIANGLES
    inline constexpr GLenum GL_TRIANGLES            = 0x0004;
#endif
#ifndef GL_UNSIGNED_INT
    inline constexpr GLenum GL_UNSIGNED_INT         = 0x1405;
#endif
#ifndef GL_FLOAT
    inline constexpr GLenum GL_FLOAT                = 0x1406;
#endif
#ifndef GL_FRAMEBUFFER
    inline constexpr GLenum GL_FRAMEBUFFER          = 0x8D40;
#endif
#ifndef GL_COLOR_ATTACHMENT0
    inline constexpr GLenum GL_COLOR_ATTACHMENT0    = 0x8CE0;
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
    inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
#endif

// Function-pointer typedefs.
// Type names match the variable names below so the LOAD(VAR, NAME) macro
// in gl_loader.cpp can do `VAR##_t` token pasting cleanly.
using gl_GenBuffers_t              = void (*)(GLsizei, GLuint*);
using gl_BindBuffer_t              = void (*)(GLenum, GLuint);
using gl_BufferData_t              = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using gl_DeleteBuffers_t           = void (*)(GLsizei, const GLuint*);
using gl_GenVertexArrays_t         = void (*)(GLsizei, GLuint*);
using gl_BindVertexArray_t         = void (*)(GLuint);
using gl_DeleteVertexArrays_t      = void (*)(GLsizei, const GLuint*);
using gl_VertexAttribPointer_t     = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using gl_EnableVertexAttribArray_t = void (*)(GLuint);

using gl_CreateShader_t            = GLuint (*)(GLenum);
using gl_ShaderSource_t            = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using gl_CompileShader_t           = void (*)(GLuint);
using gl_GetShaderiv_t             = void (*)(GLuint, GLenum, GLint*);
using gl_GetShaderInfoLog_t        = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using gl_DeleteShader_t            = void (*)(GLuint);

using gl_CreateProgram_t           = GLuint (*)();
using gl_AttachShader_t            = void (*)(GLuint, GLuint);
using gl_LinkProgram_t             = void (*)(GLuint);
using gl_GetProgramiv_t            = void (*)(GLuint, GLenum, GLint*);
using gl_GetProgramInfoLog_t       = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using gl_DeleteProgram_t           = void (*)(GLuint);
using gl_UseProgram_t              = void (*)(GLuint);

using gl_GetUniformLocation_t      = GLint (*)(GLuint, const GLchar*);
using gl_UniformMatrix4fv_t        = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
using gl_Uniform4f_t               = void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);

using gl_DrawElements_t            = void (*)(GLenum, GLsizei, GLenum, const void*);
using gl_BlendFunc_t               = void (*)(GLenum, GLenum);

// Texture / sampler entry points for sprites (Phase 3.5b).
using gl_GenTextures_t             = void (*)(GLsizei, GLuint*);
using gl_BindTexture_t             = void (*)(GLenum, GLuint);
using gl_TexImage2D_t              = void (*)(GLenum target, GLint level, GLint internalfmt,
                                              GLsizei w, GLsizei h, GLint border,
                                              GLenum format, GLenum type, const void* data);
using gl_TexParameteri_t           = void (*)(GLenum, GLenum, GLint);
using gl_ActiveTexture_t           = void (*)(GLenum);
using gl_DeleteTextures_t          = void (*)(GLsizei, const GLuint*);
using gl_PixelStorei_t             = void (*)(GLenum, GLint);

// One missing buffer + uniform entry that the batcher needs.
using gl_BufferSubData_t           = void (*)(GLenum, GLintptr, GLsizeiptr, const void*);
using gl_Uniform1i_t               = void (*)(GLint, GLint);

// Framebuffer object (FBO) entry points for offscreen render targets.
using gl_GenFramebuffers_t         = void   (*)(GLsizei, GLuint*);
using gl_BindFramebuffer_t         = void   (*)(GLenum, GLuint);
using gl_FramebufferTexture2D_t    = void   (*)(GLenum target, GLenum attachment,
                                                 GLenum textarget, GLuint texture, GLint level);
using gl_CheckFramebufferStatus_t  = GLenum (*)(GLenum target);
using gl_DeleteFramebuffers_t      = void   (*)(GLsizei, const GLuint*);

namespace zr {

// Function-pointer storage. All declared with the trailing `_p` to dodge
// collisions with the C symbols some platform GL headers may already
// declare (glGenBuffers etc.).
extern gl_GenBuffers_t              gl_GenBuffers;
extern gl_BindBuffer_t              gl_BindBuffer;
extern gl_BufferData_t              gl_BufferData;
extern gl_DeleteBuffers_t           gl_DeleteBuffers;
extern gl_GenVertexArrays_t         gl_GenVertexArrays;
extern gl_BindVertexArray_t         gl_BindVertexArray;
extern gl_DeleteVertexArrays_t      gl_DeleteVertexArrays;
extern gl_VertexAttribPointer_t     gl_VertexAttribPointer;
extern gl_EnableVertexAttribArray_t gl_EnableVertexAttribArray;
extern gl_CreateShader_t            gl_CreateShader;
extern gl_ShaderSource_t            gl_ShaderSource;
extern gl_CompileShader_t           gl_CompileShader;
extern gl_GetShaderiv_t             gl_GetShaderiv;
extern gl_GetShaderInfoLog_t        gl_GetShaderInfoLog;
extern gl_DeleteShader_t            gl_DeleteShader;
extern gl_CreateProgram_t           gl_CreateProgram;
extern gl_AttachShader_t            gl_AttachShader;
extern gl_LinkProgram_t             gl_LinkProgram;
extern gl_GetProgramiv_t            gl_GetProgramiv;
extern gl_GetProgramInfoLog_t       gl_GetProgramInfoLog;
extern gl_DeleteProgram_t           gl_DeleteProgram;
extern gl_UseProgram_t              gl_UseProgram;
extern gl_GetUniformLocation_t      gl_GetUniformLocation;
extern gl_UniformMatrix4fv_t        gl_UniformMatrix4fv;
extern gl_Uniform4f_t               gl_Uniform4f;
extern gl_DrawElements_t            gl_DrawElements;
extern gl_BlendFunc_t               gl_BlendFunc;
extern gl_GenTextures_t             gl_GenTextures;
extern gl_BindTexture_t             gl_BindTexture;
extern gl_TexImage2D_t              gl_TexImage2D;
extern gl_TexParameteri_t           gl_TexParameteri;
extern gl_ActiveTexture_t           gl_ActiveTexture;
extern gl_DeleteTextures_t          gl_DeleteTextures;
extern gl_PixelStorei_t             gl_PixelStorei;
extern gl_BufferSubData_t           gl_BufferSubData;
extern gl_Uniform1i_t               gl_Uniform1i;
extern gl_GenFramebuffers_t         gl_GenFramebuffers;
extern gl_BindFramebuffer_t         gl_BindFramebuffer;
extern gl_FramebufferTexture2D_t    gl_FramebufferTexture2D;
extern gl_CheckFramebufferStatus_t  gl_CheckFramebufferStatus;
extern gl_DeleteFramebuffers_t      gl_DeleteFramebuffers;

// Returns true if every entry point loaded successfully.
bool load_gl(void* (*get_proc)(const char*));

}  // namespace zr
