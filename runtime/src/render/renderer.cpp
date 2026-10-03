#include "render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oyster {

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
    if (msColor) glDeleteRenderbuffers(1, &msColor);
    if (msDepth) glDeleteRenderbuffers(1, &msDepth);
    *this = RenderTarget();
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    if (depth && samples <= 1) {
        glGenRenderbuffers(1, &t.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "framebuffer incomplete\n");
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
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "MSAA framebuffer incomplete\n");
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
    for (auto& kv : meshes_) {
        GpuMesh& g = kv.second;
        glDeleteVertexArrays(1, &g.vao);
        glDeleteBuffers(10, g.vbo);
        glDeleteBuffers(1, &g.ibo);
        if (g.dynPos) glDeleteBuffers(1, &g.dynPos);
        if (g.dynNrm) glDeleteBuffers(1, &g.dynNrm);
    }
    glDeleteTextures(1, &white_);
    if (quadVao_) { glDeleteVertexArrays(1, &quadVao_); glDeleteBuffers(1, &quadVbo_); }
}

void Renderer::warnOnce(const std::string& w) {
    if (warned_[w]) return;
    warned_[w] = true;
    warnings.push_back(w);
    std::fprintf(stderr, "warning: %s\n", w.c_str());
}

GLuint Renderer::texture(const std::string& uri) {
    auto it = textures_.find(uri);
    if (it != textures_.end()) return it->second;
    GLuint t = 0;
    try {
        TextureData d = loadDDS(fs_.read(uri));
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
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
        if (glGetError() != GL_NO_ERROR) warnOnce("GL error uploading " + uri);
    } catch (const std::exception& e) {
        warnOnce(std::string("texture ") + uri + ": " + e.what());
        t = white_;
    }
    textures_[uri] = t;
    return t;
}

Renderer::Program* Renderer::program(const std::string& uri) {
    auto it = programs_.find(uri);
    if (it != programs_.end()) return it->second.get();
    auto p = std::make_unique<Program>();
    try {
        p->meta = loadShaderFile(fs_.read(uri));
        std::string log;
        GLuint vs = compileShader(GL_VERTEX_SHADER, p->meta.vs, log);
        if (!vs) throw std::runtime_error("VS: " + log);
        GLuint fsh = compileShader(GL_FRAGMENT_SHADER, p->meta.fs, log);
        if (!fsh) { glDeleteShader(vs); throw std::runtime_error("FS: " + log); }
        p->id = glCreateProgram();
        glAttachShader(p->id, vs);
        glAttachShader(p->id, fsh);
        for (GLuint i = 0; i < 10; ++i) glBindAttribLocation(p->id, i, kAttribNames[i]);
        glLinkProgram(p->id);
        glDeleteShader(vs);
        glDeleteShader(fsh);
        GLint ok = 0;
        glGetProgramiv(p->id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[4096];
            GLsizei n = 0;
            glGetProgramInfoLog(p->id, sizeof(buf), &n, buf);
            throw std::runtime_error("link: " + std::string(buf, static_cast<size_t>(n)));
        }
        for (const ShaderUniform& u : p->meta.uniforms) p->loc.push_back(glGetUniformLocation(p->id, u.name.c_str()));
        p->ok = true;
    } catch (const std::exception& e) {
        warnOnce("shader " + uri + ": " + e.what());
    }
    Program* raw = p.get();
    programs_[uri] = std::move(p);
    return raw;
}

Renderer::GpuMesh& Renderer::gpuMesh(const ModelInstance& inst, size_t mi) {
    auto key = std::make_pair(static_cast<const void*>(&inst), mi);
    auto it = meshes_.find(key);
    if (it != meshes_.end()) return it->second;
    GpuMesh& g = meshes_[key];
    const Mesh& m = inst.model().meshes[mi];
    const VertexData& v = m.vertices;
    g.vertexCount = v.count;
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    auto upload = [&](GLuint slot, const std::vector<float>& data, int comps) {
        if (data.empty()) return;
        glGenBuffers(1, &g.vbo[slot]);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo[slot]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * 4), data.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(slot);
        glVertexAttribPointer(slot, comps, GL_FLOAT, GL_FALSE, 0, nullptr);
    };
    upload(0, v.position, 3);
    upload(1, v.normal, 3);
    upload(2, v.tangent, 3);
    for (int k = 0; k < 4; ++k) upload(3 + static_cast<GLuint>(k), v.uv[k], 2);
    upload(7, v.color, 4);
    upload(8, v.weights, 4);
    upload(9, v.joints, 4);
    glGenBuffers(1, &g.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.indices.size() * 2), m.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    return g;
}

void Renderer::applyRenderState(const RenderState& s) {
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
        glBindVertexArray(quadVao_);
        glGenBuffers(1, &quadVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glUseProgram(prog->id);
    applyRenderState(pass.state);
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
        } else if (u.semantic == static_cast<uint16_t>(Semantic::ViewMid)) {
            const float mid[2] = {0, 0};  // [I] mono: view centre = 0
            glUniform2fv(loc, 1, mid);
        } else if (u.semantic == static_cast<uint16_t>(Semantic::Time)) {
            glUniform1fv(loc, 1, &vp.time);
        }
    }
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    ++drawCalls_;
}

void Renderer::clear(const float c[4]) {
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
    std::vector<DrawItem> draws;
    uint32_t seq = 0;
    for (const SceneItem& it : items) {
        if (!(it.viewFlags & vp.viewFlag)) continue;
        const ModelInstance& inst = *it.inst;
        const ModelResource& model = inst.model();
        const std::vector<Material>& mats = inst.materials();
        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const Mesh& mesh = model.meshes[mi];
            if (mesh.vertices.count == 0 || mesh.indices.empty()) continue;
            size_t node = mesh.nodes.empty() ? 0 : mesh.nodes[0];
            if (node < model.localXforms.size() && !inst.nodeVisible(node)) continue;
            const MeshState& ms = inst.meshState(mi);
            if (!ms.visible) continue;
            // sort depth: view-space distance of the transformed bounds' centre / far + mesh bias
            const std::vector<float>& pos = (ms.animated && !ms.position.empty()) ? ms.position : mesh.vertices.position;
            Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
            for (size_t v = 0; v + 2 < pos.size(); v += 3) {
                lo.x = std::min(lo.x, pos[v]); lo.y = std::min(lo.y, pos[v + 1]); lo.z = std::min(lo.z, pos[v + 2]);
                hi.x = std::max(hi.x, pos[v]); hi.y = std::max(hi.y, pos[v + 1]); hi.z = std::max(hi.z, pos[v + 2]);
            }
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
    for (const DrawItem& d : draws) executeDraw(d, vp, lb);
}

void Renderer::executeDraw(const DrawItem& d, const ViewParams& vp, const LightBlock& lb) {
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
        glBindVertexArray(g.vao);
        if (ms.animated && ms.position.size() == 3ull * g.vertexCount) {
            if (!g.dynPos) { glGenBuffers(1, &g.dynPos); glGenBuffers(1, &g.dynNrm); }
            if (g.revision != ms.revision) {
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
            Program* prog = program(pass.shader.uri);
            if (!prog || !prog->ok) { glBindVertexArray(0); return; }
            glUseProgram(prog->id);
            applyRenderState(pass.state);

            const auto& un = prog->meta.uniforms;
            // material parameters (with animated overrides from custom channels)
            for (const MaterialParam& p : pass.params) {
                if (p.uniformIndex >= un.size()) continue;
                GLint loc = prog->loc[p.uniformIndex];
                const ShaderUniform& u = un[p.uniformIndex];
                if (loc < 0) continue;
                if (p.type == 0) {
                    float v[16] = {};
                    size_t n = std::min<size_t>(p.values.size(), 16);
                    for (size_t i = 0; i < n; ++i) v[i] = p.values[i];
                    float ov[4];
                    uint32_t mask = 0;
                    if (inst.materialOverride(mat.nameHash, p.nameHash, static_cast<uint32_t>(passIndex), ov, &mask))
                        for (int c = 0; c < 4; ++c) if (mask & (1u << c)) v[c] = ov[c];
                    switch (n) {
                        case 1: glUniform1fv(loc, 1, v); break;
                        case 2: glUniform2fv(loc, 1, v); break;
                        case 3: glUniform3fv(loc, 1, v); break;
                        case 4: glUniform4fv(loc, 1, v); break;
                        case 16: glUniformMatrix4fv(loc, 1, GL_FALSE, v); break;
                        default: warnOnce("param size " + std::to_string(n) + " for " + u.name);
                    }
                } else {
                    int unit = u.unit >= 0 ? u.unit : 0;
                    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                    glBindTexture(GL_TEXTURE_2D, texture(p.texture.uri));
                    glUniform1i(loc, unit);
                }
            }
            // engine-supplied and global values
            for (size_t i = 0; i < un.size(); ++i) {
                GLint loc = prog->loc[i];
                if (loc < 0) continue;
                const ShaderUniform& u = un[i];
                if (u.semantic == 0xFFFF) {
                    if (u.cls >= 3) {
                        auto gs = vp.globalSamplers.find(u.name);
                        if (gs != vp.globalSamplers.end()) {
                            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(u.unit));
                            glBindTexture(GL_TEXTURE_2D, gs->second);
                            glUniform1i(loc, u.unit);
                        }
                    }
                    continue;
                }
                switch (static_cast<Semantic>(u.semantic)) {
                    case Semantic::WorldMatrix: uploadMat(loc, world); break;
                    case Semantic::InvWorldMatrix: uploadMat(loc, world.inverse()); break;
                    case Semantic::ViewMatrix: uploadMat(loc, vp.view); break;
                    case Semantic::InvViewMatrix: uploadMat(loc, vp.view.inverse()); break;
                    case Semantic::ProjMatrix: uploadMat(loc, vp.proj); break;
                    case Semantic::WorldViewMatrix: uploadMat(loc, wv); break;
                    case Semantic::WorldViewProjMatrix: uploadMat(loc, wvp); break;
                    case Semantic::NormalMatrix: uploadMat(loc, normalMatrix(wv)); break;
                    case Semantic::BlendPalette:
                        if (!palette.empty()) glUniformMatrix4fv(loc, static_cast<GLsizei>(palette.size() / 16), GL_FALSE, palette.data());
                        break;
                    case Semantic::Time: glUniform1fv(loc, 1, &vp.time); break;
                    case Semantic::AspectRatio: glUniform1fv(loc, 1, &vp.aspect); break;
                    case Semantic::ViewIndex: glUniform1i(loc, vp.viewIndex); break;
                    case Semantic::LightCount: glUniform1i(loc, lightCount); break;
                    case Semantic::LightPosition: glUniform4fv(loc, 4, lpos); break;
                    case Semantic::LightDirection: glUniform4fv(loc, 4, ldir); break;
                    case Semantic::LightRangeSpotAngle: glUniform4fv(loc, 4, lrs); break;
                    case Semantic::LightColor: glUniform4fv(loc, 4, lcol); break;
                    default: warnOnce("unhandled semantic " + std::to_string(u.semantic) + " (" + u.name + ")");
                }
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sm.indexCount), GL_UNSIGNED_SHORT,
                           reinterpret_cast<const void*>(static_cast<uintptr_t>(sm.indexStart) * 2));
            ++drawCalls_;
        }
        glBindVertexArray(0);
    }
}

}  // namespace oyster
