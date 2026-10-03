#include "render/renderer.h"

#include "render/gl_thread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oyster {

namespace {
// Completeness of the bound framebuffer; on the render thread when it is active (a query from
// the engine thread would wait for it).
void checkFramebuffer(const char* what) {
    auto check = [what] {
        if (gl::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "%s incomplete\n", what);
    };
    if (gl::threaded::active()) gl::threaded::enqueue(check);
    else check();
}
}  // namespace

namespace {
double cpuMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace


using namespace gl;

namespace {

// Vertex attribute slots = ShaderGL_ES2::assignVertexAttributeIndices table @0x180232680.
const char* kAttribNames[10] = {"a_position", "a_normal", "a_tangent", "a_texcoord", "a_texcoord1",
                                "a_texcoord2", "a_texcoord3", "a_color", "a_blendWeights", "a_blendIndices"};

GLuint compileShader(GLenum type, const std::string& src, std::string& log) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[4096];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(buf), &n, buf);
        log.assign(buf, static_cast<size_t>(n));
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Matrices are row-major with column-vector math; GLSL computes `v * M`, so the raw array is
// uploaded without transposition (see core/math.h).
void uploadMat(GLint loc, const Mat4& m) { glUniformMatrix4fv(loc, 1, GL_FALSE, m.m); }

Mat4 normalMatrix(const Mat4& worldView) {
    Mat4 n = worldView.inverse().transposed();
    n.m[3] = n.m[7] = n.m[11] = 0;
    n.m[12] = n.m[13] = n.m[14] = 0;
    n.m[15] = 1;
    return n;
}

}  // namespace

void RenderTarget::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (color) glDeleteTextures(1, &color);
    if (depth) glDeleteRenderbuffers(1, &depth);
    if (msFbo) glDeleteFramebuffers(1, &msFbo);
    if (plainFbo) glDeleteFramebuffers(1, &plainFbo);
    if (msColor) glDeleteRenderbuffers(1, &msColor);
    if (msDepth) glDeleteRenderbuffers(1, &msDepth);
    *this = RenderTarget();
}

// Tile-based GPUs (Quest/Adreno) multisample in tile memory with EXT_multisampled_render_to_texture
// and resolve while storing the tile; same 2x box resolve as the blit, without the extra pass and
// the bandwidth of a separate multisampled buffer. OYSTER_NO_MSRTT=1 forces the blit path.
static bool useMsrtt() {
    static const bool on = [] {
        if (std::getenv("OYSTER_NO_MSRTT")) return false;
        if (!glFramebufferTexture2DMultisampleEXT || !glRenderbufferStorageMultisampleEXT) return false;
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        return ext && std::strstr(ext, "GL_EXT_multisampled_render_to_texture") != nullptr;
    }();
    return on;
}

void RenderTarget::discardDepth() const {
    if (!glInvalidateFramebuffer || (!depth && !msDepth)) return;
    const GLenum att[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    glBindFramebuffer(GL_FRAMEBUFFER, drawFbo());
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, att);
}

void RenderTarget::resolve() const {
    if (!msFbo) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

RenderTarget createRenderTarget(int w, int h, bool depth, int samples) {
    RenderTarget t;
    t.width = w;
    t.height = h;
    t.samples = samples;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    if (samples > 1 && useMsrtt()) {
        t.implicitMsaa = true;
        glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0, samples);
        if (depth) {
            glGenRenderbuffers(1, &t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
            glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
        }
        checkFramebuffer("MSRTT framebuffer");
        glGenFramebuffers(1, &t.plainFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, t.plainFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
        checkFramebuffer("MSRTT read framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        return t;
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    if (depth && samples <= 1) {
        glGenRenderbuffers(1, &t.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
    checkFramebuffer("framebuffer");
    if (samples > 1) {
        glGenFramebuffers(1, &t.msFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, t.msFbo);
        glGenRenderbuffers(1, &t.msColor);
        glBindRenderbuffer(GL_RENDERBUFFER, t.msColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t.msColor);
        if (depth) {
            glGenRenderbuffers(1, &t.msDepth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.msDepth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.msDepth);
        }
        checkFramebuffer("MSAA framebuffer");
    }
    return t;
}

Renderer::Renderer(const PackageFS& fs) : fs_(fs) {
    uint8_t px[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_);
    glBindTexture(GL_TEXTURE_2D, white_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

Renderer::~Renderer() {
    for (auto& kv : textures_) glDeleteTextures(1, &kv.second);
    for (auto& kv : programs_) if (kv.second->id) glDeleteProgram(kv.second->id);
    for (auto& kv : meshes_)
        for (GpuMesh& g : kv.second.meshes) freeMesh(g);
    for (auto& kv : geometry_)
        for (StaticMesh& sm : kv.second.meshes) freeStatic(sm);
    flushDeletes(~size_t(0));
    glDeleteTextures(1, &white_);
    if (quadVao_) { glDeleteVertexArrays(1, &quadVao_); glDeleteBuffers(1, &quadVbo_); }
    if (particleVao_) {
        glDeleteVertexArrays(1, &particleVao_);
        glDeleteBuffers(1, &particleVbo_);
        glDeleteBuffers(1, &particleIbo_);
    }
}

void Renderer::freeMesh(GpuMesh& g) {
    if (!g.vao) return;
    deadVaos_.push_back(g.vao);
    if (g.dynPos) deadBuffers_.push_back(g.dynPos);
    if (g.dynNrm) deadBuffers_.push_back(g.dynNrm);
    g = GpuMesh();
}

void Renderer::freeStatic(StaticMesh& sm) {
    if (sm.vbo) deadBuffers_.push_back(sm.vbo);
    if (sm.ibo) deadBuffers_.push_back(sm.ibo);
    sm = StaticMesh();
}

void Renderer::flushDeletes(size_t max) {
    auto flush = [max](std::vector<GLuint>& names, void (*del)(GLsizei, const GLuint*)) {
        if (names.empty()) return;
        size_t n = std::min(max, names.size());
        del(static_cast<GLsizei>(n), names.data() + names.size() - n);
        names.resize(names.size() - n);
    };
    for (GLuint v : deadVaos_)
        if (v == boundVao_) boundVao_ = 0;  // deleting the bound vertex array binds 0
    flush(deadVaos_, gl::glDeleteVertexArrays);
    flush(deadBuffers_, gl::glDeleteBuffers);
}

namespace {
double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

// Geometry no instance has drawn for two minutes (models of finished shots, prefetched branches
// the story did not take) leaves the GPU.
void Renderer::expireGeometry() {
    double now = nowSeconds();
    for (auto it = geometry_.begin(); it != geometry_.end();) {
        SharedGeometry& sg = it->second;
        if (sg.users == 0 && now - sg.idleSince > 120.0) {
            for (StaticMesh& sm : sg.meshes) freeStatic(sm);
            it = geometry_.erase(it);
        } else {
            ++it;
        }
    }
}

void Renderer::releaseInstance(uint64_t instanceId) {
    for (auto pc = passCache_.begin(); pc != passCache_.end();)
        pc = pc->first.inst == instanceId ? passCache_.erase(pc) : std::next(pc);
    bounds_.erase(instanceId);
    auto it = meshes_.find(instanceId);
    if (it != meshes_.end()) {
        for (GpuMesh& g : it->second.meshes) freeMesh(g);
        auto sg = geometry_.find(it->second.geometry);
        if (sg != geometry_.end() && --sg->second.users == 0) sg->second.idleSince = nowSeconds();
        meshes_.erase(it);
    }
}

void Renderer::warnOnce(const std::string& w) {
    if (warned_[w]) return;
    warned_[w] = true;
    warnings.push_back(w);
    std::fprintf(stderr, "warning: %s\n", w.c_str());
}

void Renderer::beginFrame() {
    ++frame_;
    if (frame_ % 256 == 0) expireGeometry();
    flushDeletes(512);
    stateValid_ = false;  // the platform layer may have changed GL state between frames
    curProgram_ = 0;
    if (texBudget_ == 0 || texBytes_ <= texBudget_) return;
    std::vector<std::pair<uint64_t, std::string>> idle;
    for (const auto& kv : texUse_)
        if (kv.second.lastUse + texMinIdle_ < frame_) idle.emplace_back(kv.second.lastUse, kv.first);
    std::sort(idle.begin(), idle.end());
    for (const auto& e : idle) {
        if (texBytes_ <= texBudget_) break;
        auto t = textures_.find(e.second);
        if (t != textures_.end()) {
            if (t->second != white_) glDeleteTextures(1, &t->second);
            textures_.erase(t);
        }
        texBytes_ -= texUse_[e.second].bytes;
        ++texGen_;
        texUse_.erase(e.second);
    }
}

size_t Renderer::textureBytesUsedSince(uint64_t frame) const {
    size_t n = 0;
    for (const auto& kv : texUse_)
        if (kv.second.lastUse >= frame) n += kv.second.bytes;
    return n;
}

GLuint Renderer::texture(const std::string& uri) {
    auto it = textures_.find(uri);
    if (it != textures_.end()) {
        texUse_[uri].lastUse = frame_;
        return it->second;
    }
    GLuint t = 0;
    double tl0 = cpuMs();
    ++texLoads;
    try {
        std::vector<uint8_t> file;
        if (!(fileSource && fileSource(uri, file))) file = fs_.read(uri);
        TextureData d = loadDDS(file);
        double tl1 = cpuMs();
        texReadMs += tl1 - tl0;
        tl0 = tl1;
        textureSizes_[uri] = {static_cast<int>(d.width), static_cast<int>(d.height)};
        glGenTextures(1, &t);
        texBindValid_ = false;
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // DXT5 without S3TC support (Quest/Adreno), or forced for testing: lossless CPU decode.
        // Normally the installer step (apps/prepare, oyster_prepare) has converted the textures
        // beforehand with the same code; decoding here is only the fallback for raw data.
        static const char* forceDecode = std::getenv("OYSTER_DECODE_DXT");
        static const bool hasS3tc = [] {
            const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            return ext && (std::strstr(ext, "GL_EXT_texture_compression_s3tc") || std::strstr(ext, "GL_EXT_texture_compression_dxt1"));
        }();
        if (d.format == TexFormat::DXT5 && (!hasS3tc || forceDecode)) {
            if (!forceDecode) warnOnce("DXT5 textures are decoded at load time - prepare the data with oyster_prepare");
            std::vector<uint8_t> rgba(static_cast<size_t>(d.width) * d.height * 4);
            decodeDXT5(d.level0.data(), d.width, d.height, rgba.data(), (forceDecode && *forceDecode) ? std::atoi(forceDecode) : 4);
            d.level0.swap(rgba);
            d.format = TexFormat::RGBA8;
        }
        tl1 = cpuMs();
        texDecodeMs += tl1 - tl0;
        tl0 = tl1;
        TexUse& use = texUse_[uri];
        use.lastUse = frame_;
        use.bytes = d.format == TexFormat::RGBA8 ? static_cast<size_t>(d.width) * d.height * 4 : d.level0.size();
        texBytes_ += use.bytes;
        if (d.format == TexFormat::RGBA8) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, d.level0.data());
        } else {
            GLenum f = d.format == TexFormat::DXT5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
                     : d.format == TexFormat::ETC2_RGB8 ? GL_COMPRESSED_RGB8_ETC2 : GL_COMPRESSED_RGBA8_ETC2_EAC;
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, f, static_cast<GLsizei>(d.width), static_cast<GLsizei>(d.height), 0,
                                   static_cast<GLsizei>(d.level0.size()), d.level0.data());
        }
        // Pearl's only sampler state: LINEAR / LINEAR, no mipmaps, REPEAT, anisotropy 1
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (gl::threaded::active()) {
            // checked on the render thread: a query from here would wait for it every texture
            gl::threaded::enqueue([uri] {
                if (glGetError() != GL_NO_ERROR) std::fprintf(stderr, "warning: GL error uploading %s\n", uri.c_str());
            });
        } else if (glGetError() != GL_NO_ERROR) {
            warnOnce("GL error uploading " + uri);
        }
        texUploadMs += cpuMs() - tl0;
    } catch (const std::exception& e) {
        warnOnce(std::string("texture ") + uri + ": " + e.what());
        t = white_;
    }
    textures_[uri] = t;
    return t;
}

namespace {
uint64_t fnv64(const void* data, size_t n, uint64_t h = 0xcbf29ce484222325ull) {
    const auto* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}
}  // namespace

size_t Renderer::precompileAll() {
    size_t n = 0;
    for (const std::string& uri : fs_.list(".shd")) {
        program(uri);
        ++n;
    }
    return n;
}

void Renderer::beginPrecompile() {
    precompileList_ = fs_.list(".shd");
    precompileNext_ = 0;
}

bool Renderer::precompileStep(double budgetMs) {
    double t0 = cpuMs();
    while (precompileNext_ < precompileList_.size()) {
        program(precompileList_[precompileNext_++]);
        if (cpuMs() - t0 >= budgetMs) break;
    }
    return precompileNext_ >= precompileList_.size();
}

// Links p from the cached binary if one matches this source and driver, else compiles the
// GLSL and stores the binary. Attribute bindings are part of the binary.
bool Renderer::linkProgram(Program& p, const std::string& key, const std::vector<uint8_t>& source) {
    std::string cacheFile;
    uint64_t srcHash = fnv64(source.data(), source.size());
    if (!shaderCacheDir_.empty() && glProgramBinary && glGetProgramBinary) {
        const char* drv = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        srcHash = fnv64(drv, drv ? std::strlen(drv) : 0, srcHash);
        srcHash = fnv64(ver, ver ? std::strlen(ver) : 0, srcHash);
        char name[64];
        std::snprintf(name, sizeof(name), "/%016llx.bin", static_cast<unsigned long long>(fnv64(key.data(), key.size())));
        cacheFile = shaderCacheDir_ + name;
        std::FILE* f = std::fopen(cacheFile.c_str(), "rb");
        if (f) {
            uint64_t h = 0;
            uint32_t fmt = 0;
            std::vector<uint8_t> bin;
            if (std::fread(&h, 8, 1, f) == 1 && std::fread(&fmt, 4, 1, f) == 1 && h == srcHash) {
                std::fseek(f, 0, SEEK_END);
                long end = std::ftell(f);
                bin.resize(static_cast<size_t>(std::max(0L, end - 12)));
                std::fseek(f, 12, SEEK_SET);
                if (!bin.empty() && std::fread(bin.data(), 1, bin.size(), f) == bin.size()) {
                    p.id = glCreateProgram();
                    glProgramBinary(p.id, fmt, bin.data(), static_cast<GLsizei>(bin.size()));
                    GLint ok = 0;
                    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
                    if (ok) {
                        std::fclose(f);
                        ++programsFromCache;
                        return true;
                    }
                    glDeleteProgram(p.id);
                    p.id = 0;
                }
            }
            std::fclose(f);
        }
    }
    std::string log;
    GLuint vs = compileShader(GL_VERTEX_SHADER, p.meta.vs, log);
    if (!vs) throw std::runtime_error("VS: " + log);
    GLuint fsh = compileShader(GL_FRAGMENT_SHADER, p.meta.fs, log);
    if (!fsh) { glDeleteShader(vs); throw std::runtime_error("FS: " + log); }
    p.id = glCreateProgram();
    glAttachShader(p.id, vs);
    glAttachShader(p.id, fsh);
    for (GLuint i = 0; i < 10; ++i) glBindAttribLocation(p.id, i, kAttribNames[i]);
    if (!cacheFile.empty() && glProgramParameteri) glProgramParameteri(p.id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(p.id);
    glDeleteShader(vs);
    glDeleteShader(fsh);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[4096];
        GLsizei n = 0;
        glGetProgramInfoLog(p.id, sizeof(buf), &n, buf);
        throw std::runtime_error("link: " + std::string(buf, static_cast<size_t>(n)));
    }
    if (!cacheFile.empty()) {
        GLint len = 0;
        glGetProgramiv(p.id, GL_PROGRAM_BINARY_LENGTH, &len);
        if (len > 0) {
            std::vector<uint8_t> bin(static_cast<size_t>(len));
            GLenum fmt = 0;
            GLsizei got = 0;
            glGetProgramBinary(p.id, len, &got, &fmt, bin.data());
            if (got > 0) {
                if (std::FILE* f = std::fopen(cacheFile.c_str(), "wb")) {
                    uint32_t fmt32 = fmt;
                    std::fwrite(&srcHash, 8, 1, f);
                    std::fwrite(&fmt32, 4, 1, f);
                    std::fwrite(bin.data(), 1, static_cast<size_t>(got), f);
                    std::fclose(f);
                }
            }
        }
    }
    return true;
}

Renderer::Program* Renderer::program(const std::string& uriIn) {
    const std::string uri = PackageFS::normalize(uriIn);  // one program per file, however it is spelled
    auto it = programs_.find(uri);
    if (it != programs_.end()) return it->second.get();
    auto p = std::make_unique<Program>();
    double tp0 = cpuMs();
    ++programLoads;
    struct ProgTimer {
        double t0;
        double& acc;
        ~ProgTimer() { acc += cpuMs() - t0; }
    } progTimer{tp0, programMs};
    try {
        std::vector<uint8_t> src = fs_.read(uri);
        p->meta = loadShaderFile(src);
        linkProgram(*p, uri, src);
        for (const ShaderUniform& u : p->meta.uniforms) p->loc.push_back(glGetUniformLocation(p->id, u.name.c_str()));
        p->ok = true;
    } catch (const std::exception& e) {
        warnOnce("shader " + uri + ": " + e.what());
    }
    Program* raw = p.get();
    programs_[uri] = std::move(p);
    return raw;
}

namespace {
constexpr int kSlotComps[10] = {3, 3, 3, 2, 2, 2, 2, 4, 4, 4};  // position normal tangent uv0-3 color weights joints
}  // namespace

Renderer::SharedGeometry& Renderer::sharedGeometry(const std::shared_ptr<const ModelResource>& model) {
    SharedGeometry& sg = geometry_[model.get()];
    if (!sg.model) {
        sg.model = model;
        sg.meshes.resize(model->meshes.size());
        sg.idleSince = nowSeconds();
    }
    return sg;
}

size_t Renderer::uploadStatic(const ModelResource& model, StaticMesh& sm, size_t mi) {
    if (sm.uploaded) return 0;
    sm.uploaded = true;
    const Mesh& m = model.meshes[mi];
    const VertexData& v = m.vertices;
    const std::vector<float>* slots[10] = {&v.position, &v.normal, &v.tangent, &v.uv[0], &v.uv[1],
                                           &v.uv[2],    &v.uv[3],  &v.color,   &v.weights, &v.joints};
    std::vector<uint8_t> blob;
    for (int s = 0; s < 10; ++s) {
        const std::vector<float>& data = *slots[s];
        if (data.empty()) continue;
        sm.has[s] = true;
        sm.offset[s] = blob.size();
        blob.resize(blob.size() + data.size() * 4);
        std::memcpy(blob.data() + sm.offset[s], data.data(), data.size() * 4);
    }
    // [I] Observed: where a mesh has no second UV set, the original feeds uv0 to a_texcoord1
    // (Pearl's warp-pass shaders read a_texcoord1). Verified against the reference frame that shows
    // the warp pass directly (mean difference 4.87 -> 1.15); the engine's own code would disable
    // the attribute, so the mechanism is on the driver side (NVIDIA compatibility context).
    if (!sm.has[4] && sm.has[3]) {
        sm.has[4] = true;
        sm.offset[4] = sm.offset[3];
    }
    bindVao(0);  // the element buffer binding below must not land in some instance's VAO
    if (!blob.empty()) {
        glGenBuffers(1, &sm.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(blob.size()), blob.data(), GL_STATIC_DRAW);
    }
    glGenBuffers(1, &sm.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.indices.size() * 2), m.indices.data(), GL_STATIC_DRAW);
    return blob.size() + m.indices.size() * 2;
}

bool Renderer::preloadGeometry(const std::shared_ptr<const ModelResource>& model, size_t maxBytes, size_t maxMeshes) {
    if (!model || model->meshes.empty()) return false;
    SharedGeometry& sg = sharedGeometry(model);
    size_t bytes = 0, meshes = 0;
    bool any = false;
    for (size_t mi = 0; mi < sg.meshes.size(); ++mi) {
        if (sg.meshes[mi].uploaded) continue;
        const Mesh& m = model->meshes[mi];
        if (m.vertices.count == 0 || m.indices.empty()) {
            sg.meshes[mi].uploaded = true;  // never drawn
            continue;
        }
        if (bytes >= maxBytes || meshes >= maxMeshes) return true;
        ++meshes;
        size_t b = uploadStatic(*model, sg.meshes[mi], mi);
        static const bool debugPreload = std::getenv("OYSTER_DEBUG_PRELOAD") != nullptr;
        if (debugPreload)
            std::fprintf(stderr, "frame %llu preload mesh %zu of %p: %.2f MB\n", static_cast<unsigned long long>(frame_), mi,
                         static_cast<const void*>(model.get()), static_cast<double>(b) / 1048576.0);
        bytes += b;
        any = true;
    }
    return any;
}

Renderer::GpuMesh& Renderer::gpuMesh(const ModelInstance& inst, size_t mi) {
    InstanceMeshes& im = meshes_[inst.id()];
    if (im.meshes.empty()) {
        im.meshes.resize(inst.model().meshes.size());
        im.geometry = &inst.model();
        ++sharedGeometry(inst.geometry()).users;
    }
    GpuMesh& g = im.meshes[mi];
    if (g.vao) return g;
    SharedGeometry& sg = geometry_[im.geometry];
    StaticMesh& sm = sg.meshes[mi];
    uploadStatic(inst.model(), sm, mi);
    g.vertexCount = inst.model().meshes[mi].vertices.count;
    glGenVertexArrays(1, &g.vao);
    bindVao(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
    for (GLuint slot = 0; slot < 10; ++slot) {
        if (!sm.has[slot]) continue;
        glEnableVertexAttribArray(slot);
        glVertexAttribPointer(slot, kSlotComps[slot], GL_FLOAT, GL_FALSE, 0, reinterpret_cast<const void*>(sm.offset[slot]));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ibo);
    return g;
}

namespace {
bool sameState(const RenderState& a, const RenderState& b) {
    return a.blend == b.blend && a.blendSrcRGB == b.blendSrcRGB && a.blendSrcAlpha == b.blendSrcAlpha &&
           a.blendDstRGB == b.blendDstRGB && a.blendDstAlpha == b.blendDstAlpha &&
           !std::memcmp(a.blendColor, b.blendColor, sizeof(a.blendColor)) && a.blendEqRGB == b.blendEqRGB &&
           a.blendEqAlpha == b.blendEqAlpha && a.depthTest == b.depthTest && a.depthWrite == b.depthWrite &&
           a.depthFunc == b.depthFunc && !std::memcmp(a.depthRange, b.depthRange, sizeof(a.depthRange)) &&
           a.scissor == b.scissor && a.stencil == b.stencil && a.stencilMask == b.stencilMask &&
           a.stencilFunc == b.stencilFunc && a.stencilRef == b.stencilRef && a.stencilReadMask == b.stencilReadMask &&
           !std::memcmp(a.stencilOps, b.stencilOps, sizeof(a.stencilOps)) &&
           !std::memcmp(a.colorMask, b.colorMask, sizeof(a.colorMask)) && a.cull == b.cull && a.cullFace == b.cullFace &&
           a.polygonOffset == b.polygonOffset && a.polygonOffsetFactor == b.polygonOffsetFactor &&
           a.polygonOffsetUnits == b.polygonOffsetUnits && a.dither == b.dither;
}
}  // namespace

bool Renderer::uniformUnchanged(Program& p, GLint loc, const void* data, int n) {
    if (loc < 0 || n > 16) return false;
    size_t i = static_cast<size_t>(loc);
    if (i >= p.ucache.size()) p.ucache.resize(i + 1);
    Program::Cached& c = p.ucache[i];
    if (c.n == n && std::memcmp(c.v, data, static_cast<size_t>(n) * 4) == 0) return true;
    c.n = n;
    std::memcpy(c.v, data, static_cast<size_t>(n) * 4);
    return false;
}

void Renderer::bindTexture(int unit, GLuint tex) {
    if (unit < 0 || unit >= 32) unit = 0;
    if (texBindValid_ && boundTex_[unit] == tex) return;
    if (!texBindValid_) {
        std::memset(boundTex_, 0xFF, sizeof(boundTex_));
        texBindValid_ = true;
    }
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    boundTex_[unit] = tex;
}

void Renderer::useProgram(GLuint id) {
    if (id == curProgram_) return;
    glUseProgram(id);
    curProgram_ = id;
}

void Renderer::applyRenderState(const RenderState& s) {
    if (stateValid_ && sameState(s, curState_)) return;
    curState_ = s;
    stateValid_ = true;
    auto en = [](GLenum cap, bool on) { on ? glEnable(cap) : glDisable(cap); };
    en(GL_BLEND, s.blend);
    glBlendFuncSeparate(glEnumFromIndex(s.blendSrcRGB), glEnumFromIndex(s.blendDstRGB), glEnumFromIndex(s.blendSrcAlpha),
                        glEnumFromIndex(s.blendDstAlpha));
    glBlendEquationSeparate(glEnumFromIndex(s.blendEqRGB), glEnumFromIndex(s.blendEqAlpha));
    glBlendColor(s.blendColor[0], s.blendColor[1], s.blendColor[2], s.blendColor[3]);
    en(GL_DEPTH_TEST, s.depthTest);
    glDepthMask(s.depthWrite ? GL_TRUE : GL_FALSE);
    glDepthFunc(glEnumFromIndex(s.depthFunc));
    glDepthRangef(s.depthRange[0], s.depthRange[1]);
    en(GL_SCISSOR_TEST, s.scissor);
    en(GL_STENCIL_TEST, s.stencil);
    if (s.stencil) {
        glStencilMask(s.stencilMask);
        glStencilFunc(glEnumFromIndex(s.stencilFunc), static_cast<GLint>(s.stencilRef), s.stencilReadMask);
        glStencilOp(glEnumFromIndex(s.stencilOps[0]), glEnumFromIndex(s.stencilOps[1]), glEnumFromIndex(s.stencilOps[2]));
    }
    glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
    en(GL_CULL_FACE, s.cull);
    glCullFace(glEnumFromIndex(s.cullFace));
    glFrontFace(GL_CCW);
    en(GL_POLYGON_OFFSET_FILL, s.polygonOffset);
    glPolygonOffset(s.polygonOffsetFactor, s.polygonOffsetUnits);
    en(GL_DITHER, s.dither);
}

void Renderer::drawImage(const Material& mat, const std::vector<const RenderTarget*>& inputs, const ViewParams& vp,
                         const ModelInstance* anim) {
    if (mat.passes.empty()) return;
    const MaterialPass& pass = mat.passes[0];
    Program* prog = program(pass.shader.uri);
    if (!prog || !prog->ok) return;
    if (!quadVao_) {
        const float quad[12] = {-1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0};
        glGenVertexArrays(1, &quadVao_);
        bindVao(quadVao_);
        glGenBuffers(1, &quadVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    useProgram(prog->id);
    applyRenderState(pass.state);
    prog->ucache.clear();
    texBindValid_ = false;
    const auto& un = prog->meta.uniforms;
    for (const MaterialParam& p : pass.params) {
        if (p.uniformIndex >= un.size() || p.type != 0) continue;
        GLint loc = prog->loc[p.uniformIndex];
        if (loc < 0) continue;
        float v[4] = {0, 0, 0, 0};
        size_t n = std::min<size_t>(p.values.size(), 4);
        for (size_t i = 0; i < n; ++i) v[i] = p.values[i];
        float ov[4];
        uint32_t mask = 0;
        if (anim && anim->materialOverride(0, p.nameHash, 0, ov, &mask))
            for (int c = 0; c < 4; ++c) if (mask & (1u << c)) v[c] = ov[c];
        static const bool debugParams = std::getenv("OYSTER_DEBUG_PARAMS") != nullptr;
        if (debugParams)
            std::fprintf(stderr, "image %s param %08x = %g %g %g %g (anim mask %x)\n", mat.name.c_str(), p.nameHash, v[0],
                         v[1], v[2], v[3], mask);
        switch (n) {
            case 1: glUniform1fv(loc, 1, v); break;
            case 2: glUniform2fv(loc, 1, v); break;
            case 3: glUniform3fv(loc, 1, v); break;
            default: glUniform4fv(loc, 1, v); break;
        }
    }
    float texel[16] = {};
    for (size_t i = 0; i < inputs.size() && i < 8; ++i) {
        texel[2 * i] = 1.0f / static_cast<float>(inputs[i]->width);
        texel[2 * i + 1] = 1.0f / static_cast<float>(inputs[i]->height);
    }
    for (size_t i = 0; i < un.size(); ++i) {
        GLint loc = prog->loc[i];
        if (loc < 0) continue;
        const ShaderUniform& u = un[i];
        if (u.name.rfind("u_texture", 0) == 0 && u.cls >= 3) {
            size_t k = static_cast<size_t>(std::atoi(u.name.c_str() + 9));
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(u.unit));
            glBindTexture(GL_TEXTURE_2D, k < inputs.size() ? inputs[k]->color : white_);
            glUniform1i(loc, u.unit);
        } else if (u.name == "u_texelSize") {
            glUniform2fv(loc, 8, texel);
        } else if (u.semantic == static_cast<uint16_t>(Semantic::AspectRatio)) {
            glUniform1fv(loc, 1, &vp.aspect);
        } else if (u.semantic == static_cast<uint16_t>(Semantic::ScreenSize)) {
            glUniform2fv(loc, 1, vp.screenSize);
        } else if (u.semantic == static_cast<uint16_t>(Semantic::ViewMid)) {
            glUniform2fv(loc, 1, vp.viewMid);
        } else if (u.semantic == static_cast<uint16_t>(Semantic::Time)) {
            glUniform1fv(loc, 1, &vp.time);
        }
    }
    bindVao(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    bindVao(0);
    ++drawCalls_;
}

void Renderer::clear(const float c[4]) {
    stateValid_ = false;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(c[0], c[1], c[2], c[3]);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::lightBlock(const ViewParams& vp, LightBlock& lb) const {
    // FUN_180190830 / FUN_1801a2230: position/direction in view space, direction = light +Z
    // axis, w = 1 for ambient lights; at most 4 lights.
    lb = LightBlock();
    for (const Light& l : vp.lights) {
        if (lb.count >= 4 || !(l.viewFlags & vp.viewFlag)) continue;
        Mat4 m = vp.view * l.world;
        Vec3 d = Vec3(m.m[2], m.m[6], m.m[10]).normalized();
        float w = l.type == 3 ? 1.0f : 0.0f;
        int i = lb.count++;
        lb.pos[4 * i] = m.m[3]; lb.pos[4 * i + 1] = m.m[7]; lb.pos[4 * i + 2] = m.m[11]; lb.pos[4 * i + 3] = w;
        lb.dir[4 * i] = d.x; lb.dir[4 * i + 1] = d.y; lb.dir[4 * i + 2] = d.z; lb.dir[4 * i + 3] = w;
        lb.rs[4 * i] = l.range[0]; lb.rs[4 * i + 1] = l.range[1]; lb.rs[4 * i + 2] = std::cos(l.spot[0]); lb.rs[4 * i + 3] = std::cos(l.spot[1]);
        for (int c = 0; c < 4; ++c) lb.col[4 * i + c] = l.color[c];
    }
}

uint64_t Renderer::sortKey(const MaterialPass& pass, float depth) {
    // RenderDispatcher::makeSortKey(pass, depth): group = pass sort priority (u16, <= 255),
    // layer 0x7f, blend bit; opaque: shader key then depth (front to back); blended: inverted
    // depth (back to front) then shader key. The shader key here is FNV of the shader URI
    // (the engine uses the compiled shader's unique id) -- only affects ties.
    if (1.0f <= depth) depth = 1.0f;
    uint32_t group = std::min<uint32_t>(pass.ud4 & 0xFFFF, 0xFF);
    if (depth <= 0.0f) depth = 0.0f;
    uint32_t lo = static_cast<uint32_t>(static_cast<int64_t>(depth * 32767.0f));
    uint32_t mid = static_cast<uint32_t>(static_cast<int64_t>(static_cast<float>(fnv1a(pass.shader.uri)) * 7.6291617e-06f));
    bool blend = pass.state.blend;
    if (blend) {
        uint32_t inv = ~lo;
        lo = mid;
        mid = inv;
    }
    return ((static_cast<uint64_t>(group & 0xFF) | (0x7Full << 8)) * 2 | (blend ? 1u : 0u)) << 30 |
           static_cast<uint64_t>(mid & 0x7FFF) << 15 | static_cast<uint64_t>(lo & 0x7FFF);
}

void Renderer::drawScene(const std::vector<SceneItem>& items, const ViewParams& vp) {
    double t0 = cpuMs();
    texBindValid_ = false;
    std::vector<DrawItem> draws;
    uint32_t seq = 0;
    for (const SceneItem& it : items) {
        if (!(it.viewFlags & vp.viewFlag)) continue;
        if (it.particles) {
            // SGParticleEmitter::internalRender: depth = (view-z of the particle bounds' centre
            // minus their largest half extent) / far; nothing is drawn for empty or flat bounds
            const ParticleEmitter& em = *it.particles;
            const Material& pmat = em.resource().material;
            if (em.particles().empty() || !em.boundsValid() || pmat.passes.empty()) continue;
            Vec3 c = (em.boundsMin() + em.boundsMax()) * 0.5f;
            Vec3 e = (em.boundsMax() - em.boundsMin()) * 0.5f;
            float ext = e.x;
            if (ext <= e.y) ext = e.y;
            if (ext <= e.z) ext = e.z;
            if (ext == 0.0f) continue;
            const float* m = vp.view.m;
            float z = (c.y * m[9] + c.x * m[8] + c.z * m[10] + m[11]) - ext;
            float pdepth = 0.0f;
            if (0.0f < z) {
                pdepth = z / vp.zfar;
                if (1.0f <= pdepth) pdepth = 1.0f;
            }
            DrawItem d;
            d.particles = &it;
            d.key = sortKey(pmat.passes[0], pdepth);
            d.seq = seq++;
            draws.push_back(d);
            continue;
        }
        const ModelInstance& inst = *it.inst;
        const ModelResource& model = inst.model();
        const std::vector<Material>& mats = inst.materials();
        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const Mesh& mesh = model.meshes[mi];
            if (mesh.vertices.count == 0 || mesh.indices.empty()) continue;
            size_t node = mesh.nodes.empty() ? 0 : mesh.nodes[0];
            static const bool debugDraws = std::getenv("OYSTER_DEBUG_DRAWS") != nullptr;
            if (debugDraws) {
                Mat4 w = node < model.localXforms.size() ? inst.nodeWorld(node) : inst.root;
                std::fprintf(stderr, "view %u mesh %zu node %zu (%s) nodeVis %d meshVis %d sub %zu pos %.1f %.1f %.1f\n",
                             vp.viewFlag, mi, node, node < model.nodeNames.size() ? model.nodeNames[node].c_str() : "?",
                             node < model.localXforms.size() ? static_cast<int>(inst.nodeVisible(node)) : -1,
                             static_cast<int>(inst.meshState(mi).visible), mesh.subMeshes.size(), w.m[3], w.m[7], w.m[11]);
            }
            if (node < model.localXforms.size() && !inst.nodeVisible(node)) continue;
            const MeshState& ms = inst.meshState(mi);
            if (!ms.visible) continue;
            // sort depth: view-space distance of the transformed bounds' centre / far + mesh bias
            bool animatedPos = ms.animated && !ms.position.empty();
            std::vector<MeshBounds>& perB = bounds_[inst.id()];
            if (perB.size() != model.meshes.size()) perB.resize(model.meshes.size());
            MeshBounds& mb = perB[mi];
            if (mb.revision == ~0ull || mb.animated != animatedPos || (animatedPos && mb.revision != ms.revision)) {
                const std::vector<float>& pos = animatedPos ? ms.position : mesh.vertices.position;
                Vec3 blo(1e30f, 1e30f, 1e30f), bhi(-1e30f, -1e30f, -1e30f);
                for (size_t v = 0; v + 2 < pos.size(); v += 3) {
                    blo.x = std::min(blo.x, pos[v]); blo.y = std::min(blo.y, pos[v + 1]); blo.z = std::min(blo.z, pos[v + 2]);
                    bhi.x = std::max(bhi.x, pos[v]); bhi.y = std::max(bhi.y, pos[v + 1]); bhi.z = std::max(bhi.z, pos[v + 2]);
                }
                mb.lo = blo;
                mb.hi = bhi;
                mb.animated = animatedPos;
                mb.revision = animatedPos ? ms.revision : 0;
            }
            const Vec3 lo = mb.lo, hi = mb.hi;
            Mat4 world = node < model.localXforms.size() ? inst.nodeWorld(node) : inst.root;
            Vec3 wlo(1e30f, 1e30f, 1e30f), whi(-1e30f, -1e30f, -1e30f);
            for (int c = 0; c < 8; ++c) {
                Vec3 p = world.transformPoint({(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z});
                wlo.x = std::min(wlo.x, p.x); wlo.y = std::min(wlo.y, p.y); wlo.z = std::min(wlo.z, p.z);
                whi.x = std::max(whi.x, p.x); whi.y = std::max(whi.y, p.y); whi.z = std::max(whi.z, p.z);
            }
            Vec3 centre = (wlo + whi) * 0.5f;
            float bias;
            std::memcpy(&bias, &mesh.u120, 4);
            float depth = vp.view.transformPoint(centre).length() * (1.0f / vp.zfar) + bias;
            if (1.0f <= depth) depth = 1.0f;
            if (depth <= 1.1920929e-07f) depth = 1.1920929e-07f;
            for (size_t si = 0; si < mesh.subMeshes.size(); ++si) {
                const SubMesh& sm = mesh.subMeshes[si];
                if (sm.material >= mats.size()) continue;
                const Material& mat = mats[sm.material];
                // ModelResource::drawMesh: single-pass materials draw in every view the actor is
                // flagged for; multi-pass materials only draw the pass whose index = view pass id.
                size_t passIndex = mat.passes.size() < 2 ? 0 : static_cast<size_t>(vp.passId);
                if (mat.passes.empty() || vp.passId < 0 || passIndex >= mat.passes.size()) continue;
                DrawItem d;
                d.inst = &inst;
                d.mesh = mi;
                d.sub = si;
                d.pass = passIndex;
                d.key = sortKey(mat.passes[passIndex], depth);
                d.seq = seq++;
                draws.push_back(d);
            }
        }
    }
    std::stable_sort(draws.begin(), draws.end(), [](const DrawItem& a, const DrawItem& b) { return a.key < b.key; });
    LightBlock lb;
    lightBlock(vp, lb);
    double t1 = cpuMs();
    prepareMs += t1 - t0;
    for (const DrawItem& d : draws) executeDraw(d, vp, lb);
    executeMs += cpuMs() - t1;
}

Renderer::PassCache& Renderer::passCache(const DrawItem& d, const MaterialPass& pass, const Material& mat) {
    PassKey key{d.inst->id(), static_cast<uint32_t>(d.mesh), static_cast<uint32_t>(d.sub), static_cast<uint32_t>(d.pass)};
    auto it = passCache_.find(key);
    if (it != passCache_.end() && it->second.texGen == texGen_) return it->second;
    PassCache& pc = passCache_[key];
    pc = PassCache();
    pc.texGen = texGen_;
    pc.prog = program(pass.shader.uri);
    if (!pc.prog || !pc.prog->ok) return pc;
    const auto& un = pc.prog->meta.uniforms;
    for (const MaterialParam& p : pass.params) {
        if (p.uniformIndex >= un.size()) continue;
        GLint loc = pc.prog->loc[p.uniformIndex];
        if (loc < 0) continue;
        PassCache::Param cp;
        cp.loc = loc;
        cp.src = &p;
        if (p.type == 0) {
            cp.n = static_cast<int>(std::min<size_t>(p.values.size(), 16));
            // Material::getParameter binds the first pass that has the parameter
            size_t boundPass = 0;
            for (size_t q = 0; q < mat.passes.size(); ++q) {
                bool has = false;
                for (const MaterialParam& pp : mat.passes[q].params) has = has || pp.nameHash == p.nameHash;
                if (has) { boundPass = q; break; }
            }
            cp.bound = boundPass == d.pass;
        } else {
            cp.texture = true;
            cp.unit = un[p.uniformIndex].unit >= 0 ? un[p.uniformIndex].unit : 0;
            cp.tex = texture(p.texture.uri);
            auto u = texUse_.find(p.texture.uri);
            cp.use = u != texUse_.end() ? &u->second : nullptr;
        }
        pc.params.push_back(cp);
    }
    for (size_t i = 0; i < un.size(); ++i)
        if (pc.prog->loc[i] >= 0) pc.sems.push_back({pc.prog->loc[i], un[i].semantic, un[i].unit, &un[i]});
    return pc;
}

void Renderer::executeDraw(const DrawItem& d, const ViewParams& vp, const LightBlock& lb) {
    if (d.particles) {
        drawParticles(*d.particles, vp);
        return;
    }
    const ModelInstance& inst = *d.inst;
    const ModelResource& model = inst.model();
    const std::vector<Material>& mats = inst.materials();
    size_t mi = d.mesh;
    const Mesh& mesh = model.meshes[mi];
    size_t node = mesh.nodes.empty() ? 0 : mesh.nodes[0];
    const MeshState& ms = inst.meshState(mi);
    int lightCount = lb.count;
    const float *lpos = lb.pos, *ldir = lb.dir, *lrs = lb.rs, *lcol = lb.col;
    {
        GpuMesh& g = gpuMesh(inst, mi);
        bindVao(g.vao);
        if (ms.animated && ms.position.size() == 3ull * g.vertexCount) {
            if (!g.dynPos) { glGenBuffers(1, &g.dynPos); glGenBuffers(1, &g.dynNrm); }
            if (g.revision != ms.revision) {
                double tu = cpuMs();
                uploadBytes += (ms.position.size() + ms.normal.size()) * 4;
                struct Done {
                    double t;
                    double& acc;
                    ~Done() { acc += cpuMs() - t; }
                } done{tu, uploadMs};
                glBindBuffer(GL_ARRAY_BUFFER, g.dynPos);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(ms.position.size() * 4), ms.position.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, g.dynNrm);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(ms.normal.size() * 4), ms.normal.data(), GL_DYNAMIC_DRAW);
                g.revision = ms.revision;
            }
            glBindBuffer(GL_ARRAY_BUFFER, g.dynPos);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glBindBuffer(GL_ARRAY_BUFFER, g.dynNrm);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        }

        Mat4 world = node < model.localXforms.size() ? inst.nodeWorld(node) : inst.root;
        bool skinned = mesh.boneGroup != 0xFFFF && mesh.boneGroup < model.boneGroups.size();
        std::vector<float> palette;
        if (skinned) {
            const auto& bones = model.boneGroups[mesh.boneGroup];
            palette.resize(16 * bones.size());
            for (size_t b = 0; b < bones.size(); ++b) {
                Mat4 pm = inst.nodeWorld(bones[b]) * (b < mesh.skinMatrices.size() ? mesh.skinMatrices[b] : Mat4());
                std::memcpy(&palette[16 * b], pm.m, 64);
            }
            world = Mat4();  // [I] skinned vertices are transformed to world space by the palette
        }
        Mat4 wv = vp.view * world;
        Mat4 wvp = vp.proj * wv;

        const SubMesh& sm = mesh.subMeshes[d.sub];
        const Material& mat = mats[sm.material];
        size_t passIndex = d.pass;
        const MaterialPass& pass = mat.passes[passIndex];
        {
            (void)passIndex;
            PassCache& pc = passCache(d, pass, mat);
            Program* prog = pc.prog;
            if (!prog || !prog->ok) return;
            useProgram(prog->id);
            applyRenderState(pass.state);

            // material parameters (with animated overrides from custom channels)
            for (const PassCache::Param& cp : pc.params) {
                if (!cp.texture) {
                    const MaterialParam& p = *cp.src;
                    float v[16] = {};
                    for (int i = 0; i < cp.n; ++i) v[i] = p.values[static_cast<size_t>(i)];
                    float ov[4];
                    uint32_t mask = 0;
                    if (cp.bound && inst.hasMaterialOverrides() && inst.materialOverride(mat.nameHash, p.nameHash, 0, ov, &mask))
                        for (int c = 0; c < 4; ++c) if (mask & (1u << c)) v[c] = ov[c];
                    if (uniformUnchanged(*prog, cp.loc, v, cp.n)) continue;
                    switch (cp.n) {
                        case 1: glUniform1fv(cp.loc, 1, v); break;
                        case 2: glUniform2fv(cp.loc, 1, v); break;
                        case 3: glUniform3fv(cp.loc, 1, v); break;
                        case 4: glUniform4fv(cp.loc, 1, v); break;
                        case 16: glUniformMatrix4fv(cp.loc, 1, GL_FALSE, v); break;
                        default: warnOnce("param size " + std::to_string(cp.n));
                    }
                } else {
                    if (cp.use) cp.use->lastUse = frame_;
                    bindTexture(cp.unit, cp.tex);
                    if (!uniformUnchanged(*prog, cp.loc, &cp.unit, 1)) glUniform1i(cp.loc, cp.unit);
                }
            }
            // engine-supplied and global values
            for (const PassCache::Sem& cs : pc.sems) {
                GLint loc = cs.loc;
                const ShaderUniform& u = *cs.u;
                if (cs.semantic == 0xFFFF) {
                    if (u.cls >= 3) {
                        auto gs = vp.globalSamplers.find(u.name);
                        if (gs != vp.globalSamplers.end()) {
                            bindTexture(u.unit, gs->second);
                            if (!uniformUnchanged(*prog, loc, &u.unit, 1)) glUniform1i(loc, u.unit);
                        }
                    }
                    continue;
                }
                auto mat4 = [&](const Mat4& m) {
                    if (!uniformUnchanged(*prog, loc, m.m, 16)) uploadMat(loc, m);
                };
                auto vecf = [&](const float* v, int n, int count) {
                    if (uniformUnchanged(*prog, loc, v, n * count)) return;
                    if (n == 1) glUniform1fv(loc, count, v);
                    else if (n == 2) glUniform2fv(loc, count, v);
                    else glUniform4fv(loc, count, v);
                };
                auto int1 = [&](int v) {
                    if (!uniformUnchanged(*prog, loc, &v, 1)) glUniform1i(loc, v);
                };
                switch (static_cast<Semantic>(u.semantic)) {
                    case Semantic::WorldMatrix: mat4(world); break;
                    case Semantic::InvWorldMatrix: mat4(world.inverse()); break;
                    case Semantic::ViewMatrix: mat4(vp.view); break;
                    case Semantic::InvViewMatrix: mat4(vp.view.inverse()); break;
                    case Semantic::ProjMatrix: mat4(vp.proj); break;
                    case Semantic::WorldViewMatrix: mat4(wv); break;
                    case Semantic::WorldViewProjMatrix: mat4(wvp); break;
                    case Semantic::NormalMatrix: mat4(normalMatrix(wv)); break;
                    case Semantic::BlendPalette:
                        if (!palette.empty()) {
                            if (loc >= 0 && static_cast<size_t>(loc) < prog->ucache.size()) prog->ucache[static_cast<size_t>(loc)].n = -1;
                            glUniformMatrix4fv(loc, static_cast<GLsizei>(palette.size() / 16), GL_FALSE, palette.data());
                        }
                        break;
                    case Semantic::Time: vecf(&vp.time, 1, 1); break;
                    case Semantic::AspectRatio: vecf(&vp.aspect, 1, 1); break;
                    case Semantic::ScreenSize: vecf(vp.screenSize, 2, 1); break;
                    case Semantic::ViewMid: vecf(vp.viewMid, 2, 1); break;
                    case Semantic::ViewIndex: int1(vp.viewIndex); break;
                    case Semantic::LightCount: int1(lightCount); break;
                    case Semantic::LightPosition: vecf(lpos, 4, 4); break;
                    case Semantic::LightDirection: vecf(ldir, 4, 4); break;
                    case Semantic::LightRangeSpotAngle: vecf(lrs, 4, 4); break;
                    case Semantic::LightColor: vecf(lcol, 4, 4); break;
                    default: warnOnce("unhandled semantic " + std::to_string(u.semantic) + " (" + u.name + ")");
                }
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sm.indexCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(static_cast<uintptr_t>(sm.indexStart) * 2));
            ++drawCalls_;
        }
    }
}

// ParticleBuffer::draw: four vertices per particle in the original layout (0x30 bytes):
// a_pos (xyz, w = corner code 0.1/1.1/1.0/0.0), a_size (half size +-x/+-y, rotation, size life),
// a_col (RGBA8), a_frame (frame, next frame, blend*255), a_colorLife, a_rotLife; quads 0,1,2 0,2,3.
void Renderer::drawParticles(const SceneItem& it, const ViewParams& vp) {
    const ParticleEmitter& em = *it.particles;
    const ParticleSystemResource& d = em.resource();
    const MaterialPass& pass = d.material.passes[0];
    Program* prog = program(pass.shader.uri);
    if (!prog || !prog->ok) return;
    constexpr uint32_t kCapacity = 0x4000;  // ParticleBuffer size limit
    if (!particleVao_) {
        glGenVertexArrays(1, &particleVao_);
        glGenBuffers(1, &particleVbo_);
        glGenBuffers(1, &particleIbo_);
        std::vector<uint16_t> idx;
        idx.reserve(kCapacity * 6);
        for (uint32_t q = 0; q < kCapacity; ++q) {
            uint16_t b = static_cast<uint16_t>(q * 4);
            for (uint16_t o : {0, 1, 2, 0, 2, 3}) idx.push_back(static_cast<uint16_t>(b + o));
        }
        bindVao(particleVao_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, particleIbo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * 2), idx.data(), GL_STATIC_DRAW);
        bindVao(0);
    }

    // life parameter (mode 0: age / lifetime, capped at 0.999, with optional fade-in/out ramps)
    auto lifeParam = [&](const Particle& p, size_t block, size_t fade) {
        if (d.u(block) != 0) warnOnce("particle life mode " + std::to_string(d.u(block)) + " not implemented");
        float t = p.age / p.life;
        if (0.999f <= t) t = 0.999f;
        float r = t;
        uint8_t fadeIn = d.b(fade);
        float fadeInT = d.f(fade + 4);
        uint8_t fadeOut = d.b(fade + 8);
        float fadeOutT = d.f(fade + 12);
        if (fadeIn == 0 || fadeInT <= p.age) {
            if (fadeOut != 0 && p.life - p.age < fadeOutT) {
                r = ((p.age - (p.life - fadeOutT)) / fadeOutT) * 0.33333334f + 0.6666667f;
                if (r <= t) r = t;
            }
        } else {
            r = (p.age / fadeInT) * 0.33333334f;
            if (t <= r) r = t;
        }
        return r;
    };
    if (d.b(0x13c) != 0) warnOnce("particle vertex variant 0x13c not implemented");
    const std::vector<Particle>& ps = em.particles();
    uint32_t numFrames = d.u(0x1d8);
    particleVerts_.resize(ps.size() * 48);
    float* out = particleVerts_.data();
    for (const Particle& p : ps) {
        float sizeLife = lifeParam(p, 0xc8, 0xe8);
        float colorLife = lifeParam(p, 0x74, 0x94);
        float rotLife = lifeParam(p, 0x10c, 0x12c);
        float hw = p.size[0] * 0.5f, hh = p.size[1] * 0.5f;
        uint8_t fb[4] = {0, 0, 0, 0};
        if (numFrames != 0) {
            int fi = static_cast<int>(std::floor(p.frame));
            fb[0] = static_cast<uint8_t>(fi);
            fb[1] = static_cast<uint8_t>((static_cast<uint32_t>(fi) + 1u) % numFrames);
            fb[2] = static_cast<uint8_t>(static_cast<int>((p.frame - static_cast<float>(fi)) * 255.0f));
        }
        float colBits, frameBits;
        std::memcpy(&colBits, p.col, 4);
        std::memcpy(&frameBits, fb, 4);
        const float corner[4] = {0.1f, 1.1f, 1.0f, 0.0f};
        const float sx[4] = {-hw, hw, hw, -hw}, sy[4] = {-hh, -hh, hh, hh};
        for (int v = 0; v < 4; ++v) {
            float* o = out + v * 12;
            o[0] = p.pos[0];
            o[1] = p.pos[1];
            o[2] = p.pos[2];
            o[3] = corner[v];
            o[4] = sx[v];
            o[5] = sy[v];
            o[6] = p.rot;
            o[7] = sizeLife;
            o[8] = colBits;
            o[9] = frameBits;
            o[10] = colorLife;
            o[11] = rotLife;
        }
        out += 48;
    }

    useProgram(prog->id);
    applyRenderState(pass.state);
    prog->ucache.clear();
    texBindValid_ = false;
    // material parameters; the diffuse texture also defines the flipbook geometry
    static const uint32_t kDiffuse = fnv1a("DiffuseMap");
    std::pair<int, int> texSize{1, 1};
    const auto& un = prog->meta.uniforms;
    for (const MaterialParam& mp : pass.params) {
        if (mp.uniformIndex >= un.size()) continue;
        GLint loc = prog->loc[mp.uniformIndex];
        if (mp.type != 0) {
            GLuint t = texture(mp.texture.uri);
            if (mp.nameHash == kDiffuse) {
                auto sz = textureSizes_.find(mp.texture.uri);
                if (sz != textureSizes_.end()) texSize = sz->second;
            }
            if (loc < 0) continue;
            int unit = un[mp.uniformIndex].unit >= 0 ? un[mp.uniformIndex].unit : 0;
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, t);
            glUniform1i(loc, unit);
        } else if (loc >= 0 && !mp.values.empty()) {
            float v[4] = {0, 0, 0, 0};
            for (size_t i = 0; i < mp.values.size() && i < 4; ++i) v[i] = mp.values[i];
            glUniform4fv(loc, 1, v);
        }
    }
    float texW = static_cast<float>(texSize.first), texH = static_cast<float>(texSize.second);
    float frameW = static_cast<float>(d.u(0x1d0)), frameH = static_cast<float>(d.u(0x1d4));
    if (frameW <= 0.0f || frameH <= 0.0f) {
        frameW = texW;
        frameH = texH;
    }
    float numF = static_cast<float>(numFrames);
    float texSz[2] = {texW, texH}, frameSz[2] = {frameW, frameH};
    float stride = std::floor((1.0f / frameW) * texW);
    float ratio[2] = {frameW / texW, frameH / texH};
    float scale[2] = {1.0f - 1.0f / frameW, 1.0f - 1.0f / frameH};
    float sizeX[4] = {d.f(0xa8), d.f(0xb0), d.f(0xb8), d.f(0xc0)};
    float sizeY[4] = {d.f(0xac), d.f(0xb4), d.f(0xbc), d.f(0xc4)};
    float rotation[4] = {d.f(0xfc), d.f(0x100), d.f(0x104), d.f(0x108)};
    float colors[16];  // column = channel over the four colour keys at +0x34
    for (size_t ch = 0; ch < 4; ++ch)
        for (size_t key = 0; key < 4; ++key) colors[ch * 4 + key] = d.f(0x34 + 16 * key + 4 * ch);
    float curvature = d.f(0x20), emissive = d.f(0x24);
    auto loc = [&](const char* n) {
        auto f = prog->namedUniform.find(n);
        if (f == prog->namedUniform.end()) f = prog->namedUniform.emplace(n, glGetUniformLocation(prog->id, n)).first;
        return f->second;
    };
    glUniform1fv(loc("u_numFrames"), 1, &numF);
    glUniform2fv(loc("u_textureSize"), 1, texSz);
    glUniform2fv(loc("u_frameSize"), 1, frameSz);
    glUniform1fv(loc("u_frameStride"), 1, &stride);
    glUniform2fv(loc("u_frameRatio"), 1, ratio);
    glUniform2fv(loc("u_frameScale"), 1, scale);
    glUniform4fv(loc("u_particleSizeScaleX"), 1, sizeX);
    glUniform4fv(loc("u_particleSizeScaleY"), 1, sizeY);
    glUniform4fv(loc("u_particleRotation"), 1, rotation);
    glUniformMatrix4fv(loc("u_particleColors"), 1, GL_FALSE, colors);
    glUniform4fv(loc("u_color"), 1, em.color);
    glUniform1fv(loc("u_curvature"), 1, &curvature);
    glUniform1fv(loc("u_emissive"), 1, &emissive);
    // world-space systems (definition +0x1e9) draw with an identity world matrix
    Mat4 world = d.b(0x1e9) ? Mat4() : it.particleWorld;
    Mat4 wv = vp.view * world;
    for (size_t i = 0; i < un.size(); ++i) {
        GLint l = prog->loc[i];
        if (l < 0 || un[i].semantic == 0xFFFF) continue;
        switch (static_cast<Semantic>(un[i].semantic)) {
            case Semantic::WorldMatrix: uploadMat(l, world); break;
            case Semantic::ViewMatrix: uploadMat(l, vp.view); break;
            case Semantic::ProjMatrix: uploadMat(l, vp.proj); break;
            case Semantic::WorldViewMatrix: uploadMat(l, wv); break;
            case Semantic::WorldViewProjMatrix: uploadMat(l, vp.proj * wv); break;
            case Semantic::Time: glUniform1fv(l, 1, &vp.time); break;
            default: break;
        }
    }

    bindVao(particleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, particleVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(particleVerts_.size() * 4), particleVerts_.data(), GL_STREAM_DRAW);
    struct Attr {
        const char* name;
        GLint comps;
        GLenum type;
        GLboolean norm;
        size_t offset;
        GLint loc;
    };
    Attr attrs[] = {{"a_pos", 4, GL_FLOAT, GL_FALSE, 0x00, -1},          {"a_size", 4, GL_FLOAT, GL_FALSE, 0x10, -1},
                    {"a_col", 4, GL_UNSIGNED_BYTE, GL_TRUE, 0x20, -1},    {"a_frame", 4, GL_UNSIGNED_BYTE, GL_FALSE, 0x24, -1},
                    {"a_colorLife", 1, GL_FLOAT, GL_FALSE, 0x28, -1},     {"a_rotLife", 1, GL_FLOAT, GL_FALSE, 0x2c, -1}};
    for (Attr& a : attrs) {
        auto f = prog->namedAttrib.find(a.name);
        if (f == prog->namedAttrib.end()) f = prog->namedAttrib.emplace(a.name, glGetAttribLocation(prog->id, a.name)).first;
        a.loc = f->second;
        if (a.loc >= 0) glEnableVertexAttribArray(static_cast<GLuint>(a.loc));
    }
    for (size_t first = 0; first < ps.size(); first += kCapacity) {
        size_t n = std::min<size_t>(kCapacity, ps.size() - first);
        for (const Attr& a : attrs)
            if (a.loc >= 0)
                glVertexAttribPointer(static_cast<GLuint>(a.loc), a.comps, a.type, a.norm, 0x30,
                                      reinterpret_cast<const void*>(a.offset + first * 4 * 0x30));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(n * 6), GL_UNSIGNED_SHORT, nullptr);
        ++drawCalls_;
    }
    for (const Attr& a : attrs)
        if (a.loc >= 0) glDisableVertexAttribArray(static_cast<GLuint>(a.loc));
    bindVao(0);
}

}  // namespace oyster
