#include "render/gl.h"

#include <cstring>

namespace oyster::gl {

#define OYSTER_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
OYSTER_GL_FUNCS(OYSTER_GL_DEFINE)
#undef OYSTER_GL_DEFINE

PFN_glInvalidateFramebuffer glInvalidateFramebuffer = nullptr;
PFN_glFramebufferTexture2DMultisampleEXT glFramebufferTexture2DMultisampleEXT = nullptr;
PFN_glRenderbufferStorageMultisampleEXT glRenderbufferStorageMultisampleEXT = nullptr;

const char* load(void* (*getProc)(const char*)) {
    glInvalidateFramebuffer = reinterpret_cast<PFN_glInvalidateFramebuffer>(getProc("glInvalidateFramebuffer"));
    glFramebufferTexture2DMultisampleEXT =
        reinterpret_cast<PFN_glFramebufferTexture2DMultisampleEXT>(getProc("glFramebufferTexture2DMultisampleEXT"));
    glRenderbufferStorageMultisampleEXT =
        reinterpret_cast<PFN_glRenderbufferStorageMultisampleEXT>(getProc("glRenderbufferStorageMultisampleEXT"));
#define OYSTER_GL_LOAD(ret, name, args)                                   \
    name = reinterpret_cast<PFN_##name>(getProc(#name));                  \
    if (!name) return #name;
    OYSTER_GL_FUNCS(OYSTER_GL_LOAD)
#undef OYSTER_GL_LOAD
    return nullptr;
}

bool hasExtension(const char* ext) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (e && std::strcmp(e, ext) == 0) return true;
    }
    return false;
}

}  // namespace oyster::gl
