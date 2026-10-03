// Minimal OpenGL ES 3.0 loader (function pointers resolved at runtime through the platform's
// GetProcAddress: SDL_GL_GetProcAddress on desktop, eglGetProcAddress on Android).
#pragma once
#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_DITHER 0x0BD0
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_CCW 0x0901
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_RGBA8 0x8058
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#define GL_MAX_SAMPLES 0x8D57
#define GL_SAMPLES 0x80A9

#define OYSTER_GL_FUNCS(X)                                                                         \
    X(GLenum, glGetError, (void))                                                                  \
    X(const GLubyte*, glGetString, (GLenum))                                                       \
    X(const GLubyte*, glGetStringi, (GLenum, GLuint))                                              \
    X(void, glGetIntegerv, (GLenum, GLint*))                                                       \
    X(void, glEnable, (GLenum))                                                                    \
    X(void, glDisable, (GLenum))                                                                   \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                 \
    X(void, glBlendEquationSeparate, (GLenum, GLenum))                                             \
    X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                    \
    X(void, glDepthFunc, (GLenum))                                                                 \
    X(void, glDepthMask, (GLboolean))                                                              \
    X(void, glDepthRangef, (GLfloat, GLfloat))                                                     \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                             \
    X(void, glCullFace, (GLenum))                                                                  \
    X(void, glFrontFace, (GLenum))                                                                 \
    X(void, glPolygonOffset, (GLfloat, GLfloat))                                                   \
    X(void, glStencilFunc, (GLenum, GLint, GLuint))                                                \
    X(void, glStencilOp, (GLenum, GLenum, GLenum))                                                 \
    X(void, glStencilMask, (GLuint))                                                               \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                           \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                                          \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                    \
    X(void, glClearDepthf, (GLfloat))                                                              \
    X(void, glClearStencil, (GLint))                                                               \
    X(void, glClear, (GLbitfield))                                                                 \
    X(void, glGenTextures, (GLsizei, GLuint*))                                                     \
    X(void, glDeleteTextures, (GLsizei, const GLuint*))                                            \
    X(void, glBindTexture, (GLenum, GLuint))                                                       \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                              \
    X(void, glActiveTexture, (GLenum))                                                             \
    X(void, glPixelStorei, (GLenum, GLint))                                                        \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                 \
    X(void, glGenBuffers, (GLsizei, GLuint*))                                                      \
    X(void, glDeleteBuffers, (GLsizei, const GLuint*))                                             \
    X(void, glBindBuffer, (GLenum, GLuint))                                                        \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))                               \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                          \
    X(void, glGenVertexArrays, (GLsizei, GLuint*))                                                 \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint*))                                        \
    X(void, glBindVertexArray, (GLuint))                                                           \
    X(void, glEnableVertexAttribArray, (GLuint))                                                   \
    X(void, glDisableVertexAttribArray, (GLuint))                                                  \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))       \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                        \
    X(GLuint, glCreateShader, (GLenum))                                                            \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                 \
    X(void, glCompileShader, (GLuint))                                                             \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*))                                               \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                              \
    X(void, glDeleteShader, (GLuint))                                                              \
    X(GLuint, glCreateProgram, (void))                                                             \
    X(void, glAttachShader, (GLuint, GLuint))                                                      \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*))                                 \
    X(void, glLinkProgram, (GLuint))                                                               \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*))                                              \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                             \
    X(void, glUseProgram, (GLuint))                                                                \
    X(void, glDeleteProgram, (GLuint))                                                             \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*))                                        \
    X(GLint, glGetAttribLocation, (GLuint, const GLchar*))                                        \
    X(void, glUniform1i, (GLint, GLint))                                                           \
    X(void, glUniform1iv, (GLint, GLsizei, const GLint*))                                          \
    X(void, glUniform1fv, (GLint, GLsizei, const GLfloat*))                                        \
    X(void, glUniform2fv, (GLint, GLsizei, const GLfloat*))                                        \
    X(void, glUniform3fv, (GLint, GLsizei, const GLfloat*))                                        \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat*))                                        \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                       \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void*))                                \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                                \
    X(void, glGenFramebuffers, (GLsizei, GLuint*))                                                 \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*))                                        \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                   \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                       \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                           \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                                  \
    X(void, glGenRenderbuffers, (GLsizei, GLuint*))                                                \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint*))                                       \
    X(void, glBindRenderbuffer, (GLenum, GLuint))                                                  \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                             \
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))         \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glFinish, (void))

namespace oyster::gl {
#define OYSTER_GL_DECLARE(ret, name, args) using PFN_##name = ret(*) args; extern PFN_##name name;
OYSTER_GL_FUNCS(OYSTER_GL_DECLARE)
#undef OYSTER_GL_DECLARE

// Resolves all functions; returns the name of the first missing one or nullptr on success.
const char* load(void* (*getProc)(const char*));
bool hasExtension(const char* name);
}  // namespace oyster::gl
