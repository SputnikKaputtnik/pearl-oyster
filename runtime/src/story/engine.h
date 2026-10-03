// Story engine: the native side of the original Lua story (moxie.v2 semantics, see
// docs/native-api.md and docs/player.md). Owns the Lua host, the scene graph the scripts build,
// story time and the render manager state; renders through oyster::Renderer / RenderGraph.
//
// Frame (LApplication loop):  update(dt)  -> Application.onUpdate (scheduler, FSMs, scene:update)
//                             render()    -> Application.onRender -> RenderManager.draw
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/anim.h"
#include "audio/audio_engine.h"
#include "assets/model.h"
#include "core/json.h"
#include "core/math.h"
#include "core/pkgfs.h"
#include "scene/animation.h"
#include "scene/model_instance.h"
#include "scene/particles.h"
#include "story/lua_host.h"
#include "story/prefetcher.h"

namespace oyster {
class Renderer;
class RenderGraph;
struct RenderTarget;
}  // namespace oyster

namespace oyster::story {

class Engine;
struct AnimationObj;

// Transform (scale, rotation, position) and the engine's composition rules.
struct SRT {
    Vec3 s{1, 1, 1};
    Quat r;
    Vec3 p{0, 0, 0};
    Mat4 matrix() const { return Mat4::trs(p, r, s); }  // Transform::getMatrix
};
SRT srtCombine(const SRT& child, const SRT& parent);  // FUN_180088e70 (SGTransform world update)
SRT srtFromMatrix(const Mat4& m);                     // Transform::setMatrix

// Scene graph node (SGTransform). Local transform as SRT; world composed through the parent
// chain with srtCombine (SGTransform::updateTransformIfDirty) - attachments go through a bone of
// an actor (SGAttachmentProxy: local = bone model matrix as SRT, parent = the actor).
struct SGNode : NativeObject {
    enum class Kind { Transform, Actor, Camera, Light, RenderGraphInstance, Atmospherics, Particles, Other };
    Kind kind = Kind::Transform;
    std::string name;
    Vec3 position{0, 0, 0};
    Quat rotation;
    Vec3 scale{1, 1, 1};
    SGNode* parent = nullptr;
    int parentBone = -1;  // >= 0: attached to this node of the parent actor
    std::vector<SGNode*> children;
    uint32_t viewFlags = 0xFFFFFFFFu;
    bool visible = true, active = true;
    bool luaComponents = false;  // has an LComponent (Transform.__onUpdate etc. are called)
    bool alive = true;
    uint64_t order = 0;  // creation order (scene traversal order)

    SRT localSRT() const { return {scale, rotation, position}; }
    Mat4 localMatrix() const { return Mat4::trs(position, rotation, scale); }
    SRT worldSRT() const;
    Mat4 worldMatrix() const { return worldSRT().matrix(); }
    bool effectiveVisible() const;  // own visibility and active flag, inherited from parents
};

// SGModelInstance with its Animator: one playing clip at a time (Animator::play stops other
// playing clips of the same layer; DefaultAnimMixer::update samples it, then advances its time).
struct ActorNode : SGNode {
    std::string uri;
    std::unique_ptr<ModelInstance> inst;
    std::vector<AnimationObj*> animations;
    AnimationObj* bound = nullptr;  // clip currently bound to the ModelInstance
    float boneRadius = 0;
    bool drawable = true;           // false for render graph instances
};

// SGParticleEmitter: particle system resource + simulation state (scene/particles.h)
struct ParticleNode : SGNode {
    std::unique_ptr<ParticleEmitter> emitter;
};

struct CameraNode : SGNode {
    bool ortho = false;
    float fovDeg = 60.0f, znear = 0.01f, zfar = 10000.0f;
    float left = -1, top = 1, right = 1, bottom = -1;
};

struct LightNode : SGNode {
    int type = 0;  // LT_DIRECTIONAL 0, LT_POINT 1, LT_SPOT 2, LT_AMBIENT 3
    float color[3] = {1, 1, 1};
    float wrap = 0;
    float range[2] = {0, 100000};
    float spot[2] = {0, 0};
};

// Animation object (Actor.addAnimation / RenderGraphInstance.addAnimation).
struct AnimationObj : NativeObject {
    ActorNode* owner = nullptr;
    std::string name;
    AnimationPlayback pb;
    bool playing = false;  // Animation +0xc4
    float weight = 1.0f;
};

struct SceneObj : NativeObject {
    std::vector<SGNode*> nodes;  // creation order
};

// System.createResourceGroup: resources the story will need for a coming state
struct ResourceGroupObj : NativeObject {
    std::string name;
    std::vector<std::pair<Prefetcher::Kind, std::string>> items;
};

struct RenderGraphObj : NativeObject {
    std::string name;
    Json def, views;
    std::unique_ptr<RenderGraph> graph;
    std::unique_ptr<RenderGraph> eyeGraph[2];  // stereo: one instance (own targets) per eye
};

// Head-mounted display as the original's OpenVR display device presents it (DisplayDeviceHMD,
// docs/vr.md): pose in the tracking space (metres, floor origin), the user's IPD, per-eye field of
// view as tangents and the per-eye render size. Filled by the platform layer before each frame.
struct HmdState {
    bool active = false;
    Quat orientation;
    Vec3 position{0, 0, 0};
    float ipd = 0.063f;
    struct Eye {
        float tanLeft = -1, tanRight = 1, tanUp = 1, tanDown = -1;
    } eye[2];
    int width = 1024, height = 1024;
};

// Plain handle types the scripts only pass around (resource groups, FSMs, stats, views, ...).
struct GenericObj : NativeObject {
    std::string name;
    std::map<std::string, double> numbers;
};

struct TimeState {
    double fixedStepMs = 0;      // Time.setFixedTimeStep / -fixedtimestep (0 = real time)
    int64_t elapsedUs = 0;       // story clock
    int64_t dtUs = 0;            // current frame
    bool paused = false;
    float scale = 1.0f;
    float errorCorrectRate = 0;  // Time.setErrorCorrectParams (+0x60 / +0x64)
    bool errorCorrect = false;
};

// Mouse state as MOXIE::InputManager keeps it (platform record +0x40 position, +0x4c buttons,
// +0x58 "moving"; InputManager +0x10 delta). Fed by the host once per frame.
struct InputState {
    float mouseX = 0, mouseY = 0;  // window pixels
    bool buttons[3] = {};          // MB_L_BUTTON, MB_M_BUTTON, MB_R_BUTTON
    float deltaX = 0, deltaY = 0;  // kept from the last frame with the left button held
    bool moving = false;
    float wheel = 0;

    // InputManager::onUpdate: the delta only counts while the left button is held in this and
    // the previous frame
    void update(float x, float y, const bool b[3], float wheelDelta) {
        float ox = mouseX, oy = mouseY;
        bool oldLeft = buttons[0];
        mouseX = x;
        mouseY = y;
        for (int i = 0; i < 3; ++i) buttons[i] = b[i];
        wheel = wheelDelta;
        if (!buttons[0] || oldLeft != buttons[0]) {
            moving = false;
        } else {
            // Deliberate deviation (user decision 2026-10-03): the delta is inverted, so dragging
            // turns the view like a first-person camera instead of the original's "grab the scene"
            deltaX = ox - mouseX;
            deltaY = oy - mouseY;
            moving = true;
        }
    }
};

struct EngineOptions {
    std::string package = "pearl_vrcam";
    SamplingPolicy policy = SamplingPolicy::original();
    int width = 1280, height = 720;
    int msaa = 2;
    std::string shaderCacheDir;      // program binary cache (empty: none)
    bool precompileShaders = false;  // build every shader of the package at boot
};

class Engine {
public:
    Engine(const PackageFS& fs, const EngineOptions& opt);
    ~Engine();

    bool boot();                 // loads story/scripts/app, Application.onInitialize, onReshape
    bool frame(double dtSeconds);  // one update + render (dt ignored with a fixed time step)
    const RenderTarget* output() const;  // last rendered frame (active graph output)

    LuaHost& lua() { return *lua_; }
    const PackageFS& fs() const { return fs_; }
    const EngineOptions& options() const { return opt_; }
    TimeState& time() { return time_; }
    InputState& input() { return input_; }
    HmdState& hmd() { return hmd_; }
    float ipdScalar = 1.0f;  // DisplayDevice.setInterpupillaryDistanceScalar (display +0x24)
    float fovScalar = 1.0f;  // DisplayDevice.setFovScalar (display +0x20)
    const RenderTarget* eyeOutput(int eye) const;  // stereo frame of the active graph
    Renderer& renderer() { return *renderer_; }
    audio::Engine& audio() { return *audio_; }
    std::shared_ptr<const audio::PcmClip> audioClip(const std::string& uri);  // decoded, cached
    void loadHrtf(const std::string& uri);  // first call wins (AudioSystem::setListenerHRTF)

    // --- object model used by the bindings ---------------------------------------------
    template <typename T> T* create(const char* type) {
        auto o = lua_->create<T>(type);
        return o.get();
    }
    SGNode* addNode(SceneObj* scene, SGNode* node, const std::string& name);
    void destroyNode(SGNode* node);
    ActorNode* createActor(SceneObj* scene, const std::string& name, const std::string& modelUri, const char* type = "Actor");
    AnimationObj* addAnimation(ActorNode* actor, const std::string& name, const std::string& uri, int first, int last,
                               bool loop, bool relative);
    void playAnimation(AnimationObj* a, bool restart);
    void stopAnimation(AnimationObj* a);
    void destroyAnimation(AnimationObj* a);
    RenderGraphObj* createRenderGraph(const Json& def);
    RenderGraphObj* findRenderGraph(const std::string& name);
    void setActiveRenderGraph(RenderGraphObj* g) { activeGraph_ = g; }
    void resize(int w, int h);
    void sceneUpdate(lua_State* L, SceneObj* scene);  // Scene.update (SceneGraph::update), L = caller
    void draw();                        // RenderManager.draw
    std::shared_ptr<ModelResource> model(const std::string& uri);
    std::shared_ptr<AnimResource> anim(const std::string& uri);
    std::unique_ptr<ModelInstance> instantiate(const std::string& uri);
    std::shared_ptr<const ParticleSystemResource> particleSystem(const std::string& uri);

    std::vector<SceneObj*> scenes;
    std::vector<RenderGraphObj*> graphs;
    std::vector<LightNode*> lights() const;
    CameraNode* mainCamera = nullptr;  // first camera created (story.def.camera)
    std::map<std::string, std::string> platformStrings;
    std::vector<std::string> fsmNames;
    uint64_t frameIndex = 0;
    bool exitRequested = false;  // System.exit

private:
    void bindAll();
    void callNodeHook(lua_State* L, SGNode* n, const char* hook);
    void updateActor(ActorNode* a, float dt);
    void applyAttachedLights(ActorNode* a);

    const PackageFS& fs_;
    EngineOptions opt_;
    std::unique_ptr<LuaHost> lua_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<audio::Engine> audio_;
public:
    Prefetcher& prefetcher() { return *prefetch_; }
private:
    std::unique_ptr<Prefetcher> prefetch_;
    std::map<std::string, std::shared_ptr<const audio::PcmClip>> clips_;
    std::map<std::string, std::shared_ptr<const ParticleSystemResource>> particleSystems_;
    bool hrtfLoaded_ = false;
    TimeState time_;
    InputState input_;
    HmdState hmd_;
    RenderGraphObj* activeGraph_ = nullptr;
    std::map<std::string, std::shared_ptr<ModelResource>> models_;
    std::map<std::string, std::shared_ptr<AnimResource>> anims_;
    std::vector<SGNode*> pendingDelete_;
    uint64_t nextOrder_ = 0;
    int width_ = 0, height_ = 0;
};

// Bindings (bind_*.cpp)
void bindCore(Engine& e);
void bindScene(Engine& e);
void bindRender(Engine& e);
void bindAudio(Engine& e);
Engine& engineOf(lua_State* L);

// Lua table at idx -> Json (numbers, strings, booleans, nested tables; functions dropped).
Json luaToJson(lua_State* L, int idx, int depth = 0);

}  // namespace oyster::story
