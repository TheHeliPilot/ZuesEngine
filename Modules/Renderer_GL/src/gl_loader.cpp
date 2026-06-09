#include "gl_loader.h"

#include <zues/log.h>

namespace zr {

gl_GenBuffers_t              gl_GenBuffers              = nullptr;
gl_BindBuffer_t              gl_BindBuffer              = nullptr;
gl_BufferData_t              gl_BufferData              = nullptr;
gl_DeleteBuffers_t           gl_DeleteBuffers           = nullptr;
gl_GenVertexArrays_t         gl_GenVertexArrays         = nullptr;
gl_BindVertexArray_t         gl_BindVertexArray         = nullptr;
gl_DeleteVertexArrays_t      gl_DeleteVertexArrays      = nullptr;
gl_VertexAttribPointer_t     gl_VertexAttribPointer     = nullptr;
gl_EnableVertexAttribArray_t gl_EnableVertexAttribArray = nullptr;
gl_CreateShader_t            gl_CreateShader            = nullptr;
gl_ShaderSource_t            gl_ShaderSource            = nullptr;
gl_CompileShader_t           gl_CompileShader           = nullptr;
gl_GetShaderiv_t             gl_GetShaderiv             = nullptr;
gl_GetShaderInfoLog_t        gl_GetShaderInfoLog        = nullptr;
gl_DeleteShader_t            gl_DeleteShader            = nullptr;
gl_CreateProgram_t           gl_CreateProgram           = nullptr;
gl_AttachShader_t            gl_AttachShader            = nullptr;
gl_LinkProgram_t             gl_LinkProgram             = nullptr;
gl_GetProgramiv_t            gl_GetProgramiv            = nullptr;
gl_GetProgramInfoLog_t       gl_GetProgramInfoLog       = nullptr;
gl_DeleteProgram_t           gl_DeleteProgram           = nullptr;
gl_UseProgram_t              gl_UseProgram              = nullptr;
gl_GetUniformLocation_t      gl_GetUniformLocation      = nullptr;
gl_UniformMatrix4fv_t        gl_UniformMatrix4fv        = nullptr;
gl_Uniform4f_t               gl_Uniform4f               = nullptr;
gl_DrawElements_t            gl_DrawElements            = nullptr;
gl_BlendFunc_t               gl_BlendFunc               = nullptr;
gl_GenTextures_t             gl_GenTextures             = nullptr;
gl_BindTexture_t             gl_BindTexture             = nullptr;
gl_TexImage2D_t              gl_TexImage2D              = nullptr;
gl_TexParameteri_t           gl_TexParameteri           = nullptr;
gl_ActiveTexture_t           gl_ActiveTexture           = nullptr;
gl_DeleteTextures_t          gl_DeleteTextures          = nullptr;
gl_PixelStorei_t             gl_PixelStorei             = nullptr;
gl_BufferSubData_t           gl_BufferSubData           = nullptr;
gl_Uniform1i_t               gl_Uniform1i               = nullptr;
gl_GenFramebuffers_t         gl_GenFramebuffers         = nullptr;
gl_BindFramebuffer_t         gl_BindFramebuffer         = nullptr;
gl_FramebufferTexture2D_t    gl_FramebufferTexture2D    = nullptr;
gl_CheckFramebufferStatus_t  gl_CheckFramebufferStatus  = nullptr;
gl_DeleteFramebuffers_t      gl_DeleteFramebuffers      = nullptr;

bool load_gl(void* (*get_proc)(const char*)) {
    if (!get_proc) return false;

    #define LOAD(VAR, NAME) \
        VAR = reinterpret_cast<VAR##_t>(get_proc(NAME)); \
        if (!VAR) { Engine::log_write(Engine::LogLevel::Error, "zues_renderer_gl", \
                                       "missing GL function: " NAME); return false; }

    LOAD(gl_GenBuffers,              "glGenBuffers")
    LOAD(gl_BindBuffer,              "glBindBuffer")
    LOAD(gl_BufferData,              "glBufferData")
    LOAD(gl_DeleteBuffers,           "glDeleteBuffers")
    LOAD(gl_GenVertexArrays,         "glGenVertexArrays")
    LOAD(gl_BindVertexArray,         "glBindVertexArray")
    LOAD(gl_DeleteVertexArrays,      "glDeleteVertexArrays")
    LOAD(gl_VertexAttribPointer,     "glVertexAttribPointer")
    LOAD(gl_EnableVertexAttribArray, "glEnableVertexAttribArray")
    LOAD(gl_CreateShader,            "glCreateShader")
    LOAD(gl_ShaderSource,            "glShaderSource")
    LOAD(gl_CompileShader,           "glCompileShader")
    LOAD(gl_GetShaderiv,             "glGetShaderiv")
    LOAD(gl_GetShaderInfoLog,        "glGetShaderInfoLog")
    LOAD(gl_DeleteShader,            "glDeleteShader")
    LOAD(gl_CreateProgram,           "glCreateProgram")
    LOAD(gl_AttachShader,            "glAttachShader")
    LOAD(gl_LinkProgram,             "glLinkProgram")
    LOAD(gl_GetProgramiv,            "glGetProgramiv")
    LOAD(gl_GetProgramInfoLog,       "glGetProgramInfoLog")
    LOAD(gl_DeleteProgram,           "glDeleteProgram")
    LOAD(gl_UseProgram,              "glUseProgram")
    LOAD(gl_GetUniformLocation,      "glGetUniformLocation")
    LOAD(gl_UniformMatrix4fv,        "glUniformMatrix4fv")
    LOAD(gl_Uniform4f,               "glUniform4f")
    LOAD(gl_DrawElements,            "glDrawElements")
    LOAD(gl_BlendFunc,               "glBlendFunc")

    LOAD(gl_GenTextures,             "glGenTextures")
    LOAD(gl_BindTexture,             "glBindTexture")
    LOAD(gl_TexImage2D,              "glTexImage2D")
    LOAD(gl_TexParameteri,           "glTexParameteri")
    LOAD(gl_ActiveTexture,           "glActiveTexture")
    LOAD(gl_DeleteTextures,          "glDeleteTextures")
    LOAD(gl_PixelStorei,             "glPixelStorei")
    LOAD(gl_BufferSubData,           "glBufferSubData")
    LOAD(gl_Uniform1i,               "glUniform1i")

    LOAD(gl_GenFramebuffers,         "glGenFramebuffers")
    LOAD(gl_BindFramebuffer,         "glBindFramebuffer")
    LOAD(gl_FramebufferTexture2D,    "glFramebufferTexture2D")
    LOAD(gl_CheckFramebufferStatus,  "glCheckFramebufferStatus")
    LOAD(gl_DeleteFramebuffers,      "glDeleteFramebuffers")

    #undef LOAD
    return true;
}

}  // namespace zr
