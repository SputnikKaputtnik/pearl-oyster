// GLES 3.0 renderer for Moxie materials and models (scene passes of a render graph).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "assets/dds.h"
#include "assets/shader_file.h"
#include "core/math.h"
#include "core/pkgfs.h"
#include "render/gl.h"
#include "scene/model_instance.h"
#include "scene/particles.h"

namespace oyster {

struct Light {
    int type = 0;                    // 0 directional, 3 ambient (engine light type ids)
    Mat4 world;
    float color[4] = {1, 1, 1, 0};   // diffuse rgb + w (param_2[1] of the light, see docs)
    float range[2] = {0, 100000};
    float spot[2] = {0, 0};
    uint32_t viewFlags = 0xFFFFFFFF;
};

struct ViewParams {
    Mat4 view, proj;
    float aspect = 1.0f;
    float screenSize[2] = {1, 1};  // u_screenSize: pixel size of the current render target
    float zfar = 1.0f;               // camera far plane (sort depth normalization)
    float time = 0.0f;
    int viewIndex = 0;
    float viewMid[2] = {0, 0};       // u_viewMid: optical centre of the eye in NDC (0 in mono)
    uint32_t viewFlag = 2;           // render view flag (RV1 = 2 main, RV2 = 4 warp, RV3 = 8 shadow)
    int passId = 2;                  // material pass (0 warp, 1 shadow, 2 color)
    std::vector<Light> lights;
    std::map<std::string, GLuint> globalSamplers;  // e.g. "u_shadowPass"
};

struct RenderTarget {
    GLuint fbo = 0, color = 0, depth = 0;  // resolved single-sample colour texture (+ depth)
    GLuint msFbo = 0, msColor = 0, msDepth = 0;  // multisampled draw target (samples > 1)
    int width = 0, height = 0, samples = 1;
    bool implicitMsaa = false;  // EXT_multisampled_render_to_texture: resolve happens on store
    GLuint drawFbo() const { return msFbo ? msFbo : fbo; }
    void resolve() const;  // MSAA -> texture, glBlitFramebuffer(GL_NEAREST) as in the original
    void discardDepth() const;  // depth/stencil are not needed after the pass (tile GPUs: no store)
    void destroy();
};
RenderTarget createRenderTarget(int w, int h, bool depth, int samples = 1);

struct SceneItem {
    const ModelInstance* inst = nullptr;
    uint32_t viewFlags = 0xFFFFFFFF;
    const ParticleEmitter* particles = nullptr;  // particle emitter instead of a model
    Mat4 particleWorld;                          // emitter world (local-space systems only)
};

class Renderer {
public:
    explicit Renderer(const PackageFS& fs);
    ~Renderer();

    // Collects all draws of the items for one render view, sorts them with the engine's sort
    // key (RenderDispatcher::makeSortKey) and executes them.
    void drawScene(const std::vector<SceneItem>& items, const ViewParams& vp);
    void clear(const float color[4]);
    // Post-effect material over a fullscreen quad; inputs bound to u_texture0.., their texel
    // sizes to u_texelSize[i]; render-graph animation overrides from `anim` (customA == 1).
    void drawImage(const Material& mat, const std::vector<const RenderTarget*>& inputs, const ViewParams& vp,
                   const ModelInstance* anim);

    GLuint texture(const std::string& uri);
    // Texture memory: every texture remembers the frame it was last bound in. With a budget
    // (bytes, 0 = unlimited) beginFrame() frees the least recently used textures that were not
    // used for `minIdleFrames` until the total fits (they are reloaded on demand).
    void beginFrame();
    void setTextureBudget(size_t bytes, uint32_t minIdleFrames = 90) { texBudget_ = bytes; texMinIdle_ = minIdleFrames; }
    size_t textureBytes() const { return texBytes_; }
    size_t textureBytesUsedSince(uint64_t frame) const;
    uint64_t frameCounter() const { return frame_; }
    void releaseInstance(uint64_t instanceId);  // frees the GPU buffers of a destroyed instance
    size_t drawCalls() const { return drawCalls_; }
    double prepareMs = 0, executeMs = 0, imageMs = 0, uploadMs = 0;  // CPU time split (accumulated, Engine timing)
    size_t uploadBytes = 0;
    double texReadMs = 0, texDecodeMs = 0, texUploadMs = 0;  // texture loads (accumulated)
    size_t texLoads = 0;
    void resetStats() { drawCalls_ = 0; }
    std::vector<std::string> warnings;

    struct Program {
        GLuint id = 0;
        ShaderFile meta;
        std::vector<GLint> loc;  // per uniform of meta.uniforms
        bool ok = false;
    };
    Program* program(const std::string& uri);

private:
    struct LightBlock {
        int count = 0;
        float pos[16] = {}, dir[16] = {}, rs[16] = {}, col[16] = {};
    };
    struct DrawItem {
        const ModelInstance* inst = nullptr;
        const SceneItem* particles = nullptr;
        size_t mesh = 0, sub = 0, pass = 0;
        uint64_t key = 0;
        uint32_t seq = 0;
    };
    void lightBlock(const ViewParams& vp, LightBlock& lb) const;
    static uint64_t sortKey(const MaterialPass& pass, float depth);
    void executeDraw(const DrawItem& d, const ViewParams& vp, const LightBlock& lb);
    void drawParticles(const SceneItem& it, const ViewParams& vp);
    struct GpuMesh {
        GLuint vao = 0, ibo = 0;
        GLuint vbo[10] = {};  // per attribute slot
        GLuint dynPos = 0, dynNrm = 0;
        uint64_t revision = ~0ull;
        uint32_t vertexCount = 0;
    };
    GpuMesh& gpuMesh(const ModelInstance& inst, size_t meshIndex);
    static void freeMesh(GpuMesh& g);
    void applyRenderState(const RenderState& s);
    void useProgram(GLuint id);
    struct TexUse;
    // Per (instance, mesh, submesh, pass): everything executeDraw would otherwise look up by name
    // on every draw - program, uniform locations, texture objects, which parameters animation
    // channels may override. Texture objects are re-resolved after an eviction (texGen_).
    struct PassCache {
        Program* prog = nullptr;
        uint64_t texGen = ~0ull;
        struct Param {
            GLint loc = -1;
            bool texture = false;
            int n = 0;          // float count of a constant
            int unit = 0;
            bool bound = false;  // Material::getParameter binds this pass (animated overrides)
            GLuint tex = 0;
            TexUse* use = nullptr;
            const MaterialParam* src = nullptr;
        };
        std::vector<Param> params;
        struct Sem {
            GLint loc;
            uint16_t semantic;
            int unit;
            const ShaderUniform* u;
        };
        std::vector<Sem> sems;
    };
    struct PassKey {
        uint64_t inst;
        uint32_t mesh, sub, pass;
        bool operator==(const PassKey& o) const { return inst == o.inst && mesh == o.mesh && sub == o.sub && pass == o.pass; }
    };
    struct PassKeyHash {
        size_t operator()(const PassKey& k) const {
            return std::hash<uint64_t>()(k.inst * 0x9E3779B97F4A7C15ull ^ (uint64_t(k.mesh) << 32 | uint64_t(k.sub) << 8 | k.pass));
        }
    };
    PassCache& passCache(const DrawItem& d, const MaterialPass& pass, const Material& mat);
    void warnOnce(const std::string& w);

    const PackageFS& fs_;
    std::unordered_map<std::string, GLuint> textures_;
    std::unordered_map<std::string, std::pair<int, int>> textureSizes_;
    struct TexUse {
        size_t bytes = 0;
        uint64_t lastUse = 0;
    };
    std::unordered_map<std::string, TexUse> texUse_;
    size_t texBytes_ = 0, texBudget_ = 0;
    uint32_t texMinIdle_ = 90;
    uint64_t frame_ = 0;
    uint64_t texGen_ = 0;
    std::unordered_map<PassKey, PassCache, PassKeyHash> passCache_;
    GLuint curProgram_ = 0;
    bool stateValid_ = false;
    RenderState curState_;
    GLuint particleVao_ = 0, particleVbo_ = 0, particleIbo_ = 0;
    std::vector<float> particleVerts_;
    std::unordered_map<std::string, std::unique_ptr<Program>> programs_;
    std::map<std::pair<uint64_t, size_t>, GpuMesh> meshes_;  // (ModelInstance::id, mesh)
    struct MeshBounds {  // local bounds of a mesh's current vertices (sort depth)
        uint64_t revision = ~0ull;
        bool animated = false;
        Vec3 lo, hi;
    };
    std::map<std::pair<uint64_t, size_t>, MeshBounds> bounds_;
    std::map<std::string, bool> warned_;
    size_t drawCalls_ = 0;
    GLuint white_ = 0;
    GLuint quadVao_ = 0, quadVbo_ = 0;
};

}  // namespace oyster
