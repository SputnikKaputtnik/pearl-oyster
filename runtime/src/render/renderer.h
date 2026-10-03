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
    float zfar = 1.0f;               // camera far plane (sort depth normalization)
    float time = 0.0f;
    int viewIndex = 0;
    uint32_t viewFlag = 2;           // render view flag (RV1 = 2 main, RV2 = 4 warp, RV3 = 8 shadow)
    int passId = 2;                  // material pass (0 warp, 1 shadow, 2 color)
    std::vector<Light> lights;
    std::map<std::string, GLuint> globalSamplers;  // e.g. "u_shadowPass"
};

struct RenderTarget {
    GLuint fbo = 0, color = 0, depth = 0;  // resolved single-sample colour texture (+ depth)
    GLuint msFbo = 0, msColor = 0, msDepth = 0;  // multisampled draw target (samples > 1)
    int width = 0, height = 0, samples = 1;
    GLuint drawFbo() const { return msFbo ? msFbo : fbo; }
    void resolve() const;  // MSAA -> texture, glBlitFramebuffer(GL_NEAREST) as in the original
    void destroy();
};
RenderTarget createRenderTarget(int w, int h, bool depth, int samples = 1);

struct SceneItem {
    const ModelInstance* inst = nullptr;
    uint32_t viewFlags = 0xFFFFFFFF;
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
    size_t drawCalls() const { return drawCalls_; }
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
        size_t mesh = 0, sub = 0, pass = 0;
        uint64_t key = 0;
        uint32_t seq = 0;
    };
    void lightBlock(const ViewParams& vp, LightBlock& lb) const;
    static uint64_t sortKey(const MaterialPass& pass, float depth);
    void executeDraw(const DrawItem& d, const ViewParams& vp, const LightBlock& lb);
    struct GpuMesh {
        GLuint vao = 0, ibo = 0;
        GLuint vbo[10] = {};  // per attribute slot
        GLuint dynPos = 0, dynNrm = 0;
        uint64_t revision = ~0ull;
        uint32_t vertexCount = 0;
    };
    GpuMesh& gpuMesh(const ModelInstance& inst, size_t meshIndex);
    void applyRenderState(const RenderState& s);
    void warnOnce(const std::string& w);

    const PackageFS& fs_;
    std::unordered_map<std::string, GLuint> textures_;
    std::unordered_map<std::string, std::unique_ptr<Program>> programs_;
    std::map<std::pair<const void*, size_t>, GpuMesh> meshes_;
    std::map<std::string, bool> warned_;
    size_t drawCalls_ = 0;
    GLuint white_ = 0;
    GLuint quadVao_ = 0, quadVbo_ = 0;
};

}  // namespace oyster
