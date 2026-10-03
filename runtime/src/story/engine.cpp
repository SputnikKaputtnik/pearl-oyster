#include "story/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "render/render_graph.h"
#include "core/reader.h"
#include "render/renderer.h"

namespace oyster::story {

// ---------------------------------------------------------------------------------------------
// SGNode

SRT srtCombine(const SRT& c, const SRT& par) {
    SRT o;
    o.s = Vec3(c.s.x * par.s.x, c.s.y * par.s.y, c.s.z * par.s.z);
    // FUN_180020570: parent * child, then normalised
    const Quat& a = par.r;
    const Quat& b = c.r;
    Quat q;
    q.w = ((b.w * a.w - a.x * b.x) - b.y * a.y) - b.z * a.z;
    q.x = (b.w * a.x + b.x * a.w + b.z * a.y) - b.y * a.z;
    q.y = (b.y * a.w - b.z * a.x) + a.y * b.w + a.z * b.x;
    q.z = ((b.z * a.w + b.y * a.x) - a.y * b.x) + a.z * b.w;
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (0.0f < len) {
        float inv = 1.0f / len;
        q.x = inv * q.x; q.y = inv * q.y; q.z = inv * q.z; q.w = inv * q.w;
    }
    o.r = q;
    // position: parent.p + parent.r * (parent.s * child.p)
    const float qx = par.r.x, qy = par.r.y, qz = par.r.z, ww = par.r.w + par.r.w;
    const float vx = c.p.x * par.s.x, vy = c.p.y * par.s.y, vz = c.p.z * par.s.z;
    const float ax = vz * qy - vy * qz, ay = vx * qz - vz * qx, az = vy * qx - vx * qy;  // v x q
    const float bx = ax * qz - az * qx, by = az * qy - ay * qz, bz = ay * qx - ax * qy;
    o.p.x = vx + ax * ww + by + by + par.p.x;
    o.p.y = ay * ww + vy + bx + bx + par.p.y;
    o.p.z = az * ww + vz + bz + bz + par.p.z;
    return o;
}

SRT srtFromMatrix(const Mat4& m) {
    SRT o;
    float c0[3] = {m.m[0], m.m[4], m.m[8]}, c1[3] = {m.m[1], m.m[5], m.m[9]}, c2[3] = {m.m[2], m.m[6], m.m[10]};
    auto norm = [](float* c, float& len) {
        len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (len == 0.0f) { len = 0.0f; return; }
        if (0.0f < len) {
            float inv = 1.0f / len;
            for (int i = 0; i < 3; ++i) c[i] *= inv;
        }
    };
    norm(c0, o.s.x);
    norm(c1, o.s.y);
    norm(c2, o.s.z);
    if (0.0f < o.s.x) {
        float det = (m.m[8] * m.m[1] - m.m[0] * m.m[9]) * m.m[6] + (m.m[4] * m.m[9] - m.m[8] * m.m[5]) * m.m[2] +
                    (m.m[0] * m.m[5] - m.m[4] * m.m[1]) * m.m[10];
        if (det < 0.0f) {  // negative determinant: flip the first axis
            for (int i = 0; i < 3; ++i) c0[i] = -c0[i];
            o.s.x = -o.s.x;
        }
    }
    float r[3][3] = {{c0[0], c1[0], c2[0]}, {c0[1], c1[1], c2[1]}, {c0[2], c1[2], c2[2]}};
    // Quaternion::setMatrix (trace method)
    float tr = r[0][0] + r[1][1] + r[2][2];
    Quat q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s};
    }
    o.r = q;
    o.p = m.translation();
    return o;
}

SRT SGNode::worldSRT() const {
    SRT l = localSRT();
    if (!parent) return l;
    if (parentBone >= 0 && parent->kind == Kind::Actor) {
        const auto* a = static_cast<const ActorNode*>(parent);
        if (a->inst && static_cast<size_t>(parentBone) < a->inst->nodeCount()) {
            SRT proxy = srtCombine(srtFromMatrix(a->inst->nodeModel(static_cast<size_t>(parentBone))), a->worldSRT());
            return srtCombine(l, proxy);
        }
    }
    return srtCombine(l, parent->worldSRT());
}

bool SGNode::effectiveVisible() const {
    for (const SGNode* n = this; n; n = n->parent)
        if (!n->visible || !n->active || !n->alive) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Engine

Engine::Engine(const PackageFS& fs, const EngineOptions& opt) : fs_(fs), opt_(opt) {
    lua_ = std::make_unique<LuaHost>(fs);
    renderer_ = std::make_unique<Renderer>(fs);
    audio_ = std::make_unique<audio::Engine>();
    width_ = opt.width;
    height_ = opt.height;
    lua_State* L = lua_->L();
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "oyster.engine");
    bindAll();
}

Engine::~Engine() {
    for (auto* g : graphs)
        if (g->graph) g->graph->release();
    lua_.reset();  // closes the state before the objects it references go away
}

Engine& engineOf(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "oyster.engine");
    auto* e = static_cast<Engine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return *e;
}

void Engine::bindAll() {
    bindCore(*this);
    bindScene(*this);
    bindRender(*this);
    bindAudio(*this);
    lua_->finishBindings();
}

bool Engine::boot() {
    lua_State* L = lua_->L();
    // Platform table as LEnvMoxie fills it for the Windows candidate build (luareg_refs.c):
    // string options default to "" - Story:start runs the FSMs because loadFile is "" (truthy)
    // and restoring that save fails.
    lua_newtable(L);
    auto setBool = [&](const char* k, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, k); };
    auto setStr = [&](const char* k, const std::string& v) { lua_pushstring(L, v.c_str()); lua_setfield(L, -2, k); };
    setBool("win32", true);
    setBool("mac", false);
    setBool("android", false);
    setBool("iOS", false);
    setBool("youtube", false);
    setBool("debug", false);
    setBool("release", false);
    setBool("profile", false);
    setBool("candidate", true);
    setStr("description", "");
    setStr("tempdir", "");
    setBool("videoSplitYUV", true);
    setBool("primeTime", false);
    setStr("package", opt_.package);
    for (const char* k : {"scene", "segment", "config", "root", "shared", "reason", "record", "replay", "checkpoint"})
        setStr(k, "");
    setBool("generateCheckpoints", false);
    setStr("loadFile", "");
    setBool("autosave", false);
    lua_pushnumber(L, 15.0);
    lua_setfield(L, -2, "autosaveInterval");
    setBool("renderToDisk", false);
    lua_setglobal(L, "Platform");
    if (!lua_->require("story/scripts/app")) return false;
    // The story scheduler drops failed tasks silently (its Log.error is commented out); report
    // them without changing behaviour.
    lua_->doString(R"lua(
local resume = _coroutine.resume
_coroutine.resume = function(co, ...)
  local r = {resume(co, ...)}
  if not r[1] and type(r[2]) == "string" then
    Log.error("scheduler task failed: " .. r[2] .. "\n" .. debug.traceback(co))
  end
  return unpack(r)
end)lua", "=oyster.taskerrors");
    if (!lua_->callApplication("onInitialize")) return false;
    return lua_->callApplication("onReshape", {static_cast<double>(width_), static_cast<double>(height_)});
}

bool Engine::frame(double dtSeconds) {
    int64_t dt = time_.fixedStepMs > 0 ? static_cast<int64_t>(std::llround(time_.fixedStepMs * 1000.0))
                                       : static_cast<int64_t>(std::llround(dtSeconds * 1e6));
    if (time_.paused) dt = 0;
    dt = static_cast<int64_t>(static_cast<double>(dt) * time_.scale);
    time_.dtUs = dt;
    time_.elapsedUs += dt;
    bool ok = lua_->callApplication("onUpdate");
    ok = lua_->callApplication("onRender") && ok;
    ++frameIndex;
    return ok;
}

SGNode* Engine::addNode(SceneObj* scene, SGNode* node, const std::string& name) {
    node->name = name;
    node->order = nextOrder_++;
    if (scene) scene->nodes.push_back(node);
    return node;
}

void Engine::destroyNode(SGNode* node) {
    if (!node || !node->alive) return;
    node->alive = false;
    for (SGNode* c : node->children) {
        c->parent = nullptr;
        destroyNode(c);
    }
    node->children.clear();
    if (node->parent) {
        auto& sib = node->parent->children;
        sib.erase(std::remove(sib.begin(), sib.end(), node), sib.end());
        node->parent = nullptr;
    }
    if (node->kind == SGNode::Kind::Actor || node->kind == SGNode::Kind::RenderGraphInstance) {
        auto* a = static_cast<ActorNode*>(node);
        for (AnimationObj* an : a->animations) an->owner = nullptr;
        a->animations.clear();
        a->bound = nullptr;
    }
    if (node == mainCamera) mainCamera = nullptr;
    pendingDelete_.push_back(node);
}

std::shared_ptr<const audio::PcmClip> Engine::audioClip(const std::string& uri) {
    auto it = clips_.find(uri);
    if (it != clips_.end()) return it->second;
    std::shared_ptr<const audio::PcmClip> c;
    try {
        c = audio::decodeVorbis(fs_.read(uri));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "audio %s: %s\n", uri.c_str(), ex.what());
    }
    clips_[uri] = c;
    return c;
}

void Engine::loadHrtf(const std::string& uri) {
    if (hrtfLoaded_) return;
    try {
        hrtfLoaded_ = audio_->loadHrtf(fs_.read(uri));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "hrtf %s: %s\n", uri.c_str(), ex.what());
    }
}

std::shared_ptr<ModelResource> Engine::model(const std::string& uri) {
    auto it = models_.find(uri);
    if (it != models_.end()) return it->second;
    std::shared_ptr<ModelResource> m = loadModel(fs_.read(uri));
    models_[uri] = m;
    return m;
}

std::shared_ptr<AnimResource> Engine::anim(const std::string& uri) {
    auto it = anims_.find(uri);
    if (it != anims_.end()) return it->second;
    std::shared_ptr<AnimResource> a = loadAnim(fs_.read(uri));
    anims_[uri] = a;
    return a;
}

std::unique_ptr<ModelInstance> Engine::instantiate(const std::string& uri) {
    std::shared_ptr<ModelResource> m = model(uri);
    std::shared_ptr<ModelResource> geom = m;
    if (m->isPatch()) geom = model(m->baseModel);
    std::vector<Material> mats = (m->isPatch() && m->overridesMaterials) ? m->materials : geom->materials;
    return std::make_unique<ModelInstance>(geom, mats);
}

ActorNode* Engine::createActor(SceneObj* scene, const std::string& name, const std::string& uri, const char* type) {
    auto* a = create<ActorNode>(type);
    a->kind = std::string(type) == "RenderGraphInstance" ? SGNode::Kind::RenderGraphInstance : SGNode::Kind::Actor;
    a->drawable = a->kind == SGNode::Kind::Actor;
    a->uri = uri;
    if (!uri.empty()) {
        try {
            a->inst = instantiate(uri);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "model %s: %s\n", uri.c_str(), ex.what());
        }
    }
    addNode(scene, a, name);
    return a;
}

AnimationObj* Engine::addAnimation(ActorNode* actor, const std::string& name, const std::string& uri, int first, int last,
                                   bool loop, bool relative) {
    if (!actor || !actor->inst) return nullptr;
    std::shared_ptr<AnimResource> res;
    try {
        res = anim(uri);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "animation %s: %s\n", uri.c_str(), ex.what());
        return nullptr;
    }
    // Animator::addAnimation replaces a clip of the same name
    for (AnimationObj* old : actor->animations)
        if (old->name == name) {
            destroyAnimation(old);
            break;
        }
    auto* a = create<AnimationObj>("Animation");
    a->owner = actor;
    a->name = name;
    a->pb.setup(res, first, last, loop, relative);
    actor->animations.push_back(a);
    return a;
}

void Engine::playAnimation(AnimationObj* a, bool restart) {
    // Animator::play: stop the other playing clips (same layer), restart this one
    if (!a || !a->owner) return;
    for (AnimationObj* o : a->owner->animations)
        if (o != a && o->playing) o->playing = false;
    if (a->playing && !restart) return;
    a->weight = 1.0f;
    a->pb.time = 0;
    a->pb.loopsDone = 0;
    a->playing = true;
}

void Engine::stopAnimation(AnimationObj* a) {
    if (a) a->playing = false;
}

void Engine::destroyAnimation(AnimationObj* a) {
    if (!a) return;
    if (ActorNode* o = a->owner) {
        auto& v = o->animations;
        v.erase(std::remove(v.begin(), v.end(), a), v.end());
        if (o->bound == a) {
            o->bound = nullptr;
            if (o->inst) o->inst->clearAnimation();
        }
    }
    a->owner = nullptr;
    a->playing = false;
    lua_->destroy(a->handle);
}

void Engine::updateActor(ActorNode* a, float dt) {
    if (!a->inst) return;
    a->inst->root = a->worldMatrix();
    AnimationObj* p = nullptr;
    for (AnimationObj* o : a->animations)
        if (o->playing) { p = o; break; }
    if (p) {
        if (a->bound != p) {
            a->inst->setAnimation(p->pb);
            a->bound = p;
        } else {
            a->inst->setPlaybackState(p->pb);
        }
        a->inst->evaluate(opt_.policy);
        p->pb.advance(dt);  // FUN_1800a3dd0 after sampling
    } else {
        if (a->bound) {
            a->inst->clearAnimation();
            a->bound = nullptr;
        }
        a->inst->evaluate(opt_.policy);
    }
}

// SGLightAnimator: a light attached to an actor bone takes the bone track's custom attributes
// "wrap" (+0x14c), "range" (+0x150, 2), "angle" (+0x158, 2) and "color" (+0x160, rgba) from the
// actor's animation every frame.
void Engine::applyAttachedLights(ActorNode* a) {
    static const uint32_t kColor = fnv1a("color"), kRange = fnv1a("range"), kAngle = fnv1a("angle"),
                          kWrap = fnv1a("wrap");
    for (SGNode* c : a->children) {
        if (c->kind != SGNode::Kind::Light || c->parentBone < 0 || !c->alive) continue;
        auto* l = static_cast<LightNode*>(c);
        float v[4];
        uint32_t mask = 0;
        if (a->inst->nodeCustom(c->parentBone, kColor, v, &mask))
            for (int i = 0; i < 3; ++i) if (mask & (1u << i)) l->color[i] = v[i];
        if (a->inst->nodeCustom(c->parentBone, kRange, v, &mask))
            for (int i = 0; i < 2; ++i) if (mask & (1u << i)) l->range[i] = v[i];
        if (a->inst->nodeCustom(c->parentBone, kAngle, v, &mask))
            for (int i = 0; i < 2; ++i) if (mask & (1u << i)) l->spot[i] = v[i];
        if (a->inst->nodeCustom(c->parentBone, kWrap, v, &mask) && (mask & 1u)) l->wrap = v[0];
    }
}

void Engine::callNodeHook(lua_State* L, SGNode* n, const char* hook) {
    // LComponent::onUpdate: Transform.<hook>(object), on the calling Lua thread
    lua_getglobal(L, "Transform");
    lua_getfield(L, -1, hook);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_->pushObject(L, n);
    if (lua_pcall(L, 1, 0, 0) != 0) {
        std::fprintf(stderr, "Transform.%s: %s\n", hook, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void Engine::sceneUpdate(lua_State* L, SceneObj* scene) {
    float dt = static_cast<float>(static_cast<double>(time_.dtUs) * 1e-6);
    // creation order snapshot: nodes created by callbacks join next frame
    std::vector<SGNode*> nodes = scene->nodes;
    for (SGNode* n : nodes)
        if (n->alive && n->luaComponents && n->active) callNodeHook(L, n, "__onUpdate");
    for (SGNode* n : nodes)
        if (n->alive && (n->kind == SGNode::Kind::Actor || n->kind == SGNode::Kind::RenderGraphInstance))
        {
            updateActor(static_cast<ActorNode*>(n), dt);
            if (static_cast<ActorNode*>(n)->inst) applyAttachedLights(static_cast<ActorNode*>(n));
        }
    for (SGNode* n : nodes)
        if (n->alive && n->luaComponents && n->active) callNodeHook(L, n, "__onLateUpdate");
    // deferred deletes
    std::vector<SGNode*> dead;
    dead.swap(pendingDelete_);
    for (SceneObj* s : scenes) {
        auto& v = s->nodes;
        v.erase(std::remove_if(v.begin(), v.end(), [](SGNode* n) { return !n->alive; }), v.end());
    }
    for (SGNode* n : dead) {
        if (n->luaComponents) callNodeHook(L, n, "__onDestroy");
        if (n->kind == SGNode::Kind::Actor || n->kind == SGNode::Kind::RenderGraphInstance) {
            auto* a = static_cast<ActorNode*>(n);
            if (a->inst) renderer_->releaseInstance(a->inst->id());
        }
        lua_->destroy(n->handle);
    }
}

std::vector<LightNode*> Engine::lights() const {
    std::vector<LightNode*> out;
    for (SceneObj* s : scenes)
        for (SGNode* n : s->nodes)
            if (n->kind == SGNode::Kind::Light && n->effectiveVisible()) out.push_back(static_cast<LightNode*>(n));
    return out;
}

RenderGraphObj* Engine::createRenderGraph(const Json& def) {
    auto* g = create<RenderGraphObj>("RenderGraph");
    g->name = def["name"].str();
    g->def = def;
    lua_State* L = lua_->L();
    lua_getglobal(L, "def");
    lua_getfield(L, -1, "renderviews");
    Json views = luaToJson(L, -1);
    lua_pop(L, 2);
    try {
        g->graph = std::make_unique<RenderGraph>(def, views, fs_);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "render graph %s: %s\n", g->name.c_str(), ex.what());
    }
    graphs.push_back(g);
    return g;
}

RenderGraphObj* Engine::findRenderGraph(const std::string& name) {
    for (auto* g : graphs)
        if (g->name == name) return g;
    return nullptr;
}

void Engine::resize(int w, int h) {
    width_ = w;
    height_ = h;
}

const RenderTarget* Engine::output() const {
    if (!activeGraph_ || !activeGraph_->graph) return nullptr;
    return &activeGraph_->graph->output();
}

void Engine::draw() {
    if (!activeGraph_ || !activeGraph_->graph) return;
    for (auto* g : graphs)
        if (g != activeGraph_ && g->graph) g->graph->release();
    RenderGraph& graph = *activeGraph_->graph;
    graph.resize(width_, height_, opt_.msaa);

    std::vector<SceneItem> items;
    const ModelInstance* graphAnim = nullptr;
    for (SceneObj* s : scenes)
        for (SGNode* n : s->nodes) {
            if (n->kind == SGNode::Kind::RenderGraphInstance && n->alive && n->name == activeGraph_->name)
                graphAnim = static_cast<ActorNode*>(n)->inst.get();
            if (n->kind != SGNode::Kind::Actor || !n->effectiveVisible()) continue;
            auto* a = static_cast<ActorNode*>(n);
            if (a->drawable && a->inst) items.push_back({a->inst.get(), a->viewFlags});
        }

    ViewParams vp;
    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    if (mainCamera) {
        CameraNode& c = *mainCamera;
        vp.view = c.worldMatrix().inverse();
        float zn = c.znear, zf = c.zfar;
        if (zn > 1.1920929e-07f && zf / zn > 500000.0f) zf = zn * 500000.0f;  // Camera::update clamp
        float f = 1.0f / std::tan(c.fovDeg * 3.14159265358979f / 180.0f * 0.5f);
        Mat4 proj;
        proj.setIdentity();
        proj.m[0] = f / aspect;
        proj.m[5] = f;
        proj.m[10] = (zf + zn) / (zn - zf);
        proj.m[11] = 2.0f * zf * zn / (zn - zf);
        proj.m[14] = -1.0f;
        proj.m[15] = 0.0f;
        vp.proj = proj;
        vp.zfar = zf;
    }
    vp.aspect = aspect;
    vp.time = static_cast<float>(static_cast<double>(time_.elapsedUs) * 1e-6);
    for (LightNode* l : lights()) {
        Light li;
        li.type = l->type;
        li.world = l->worldMatrix();
        li.color[0] = l->color[0];
        li.color[1] = l->color[1];
        li.color[2] = l->color[2];
        li.color[3] = l->wrap;
        li.range[0] = l->range[0];
        li.range[1] = l->range[1];
        li.spot[0] = l->spot[0];
        li.spot[1] = l->spot[1];
        li.viewFlags = l->viewFlags;
        vp.lights.push_back(li);
        static const bool debugLights = std::getenv("OYSTER_DEBUG_LIGHTS") != nullptr;
        if (debugLights) {
            Vec3 dir = li.world.transformDir({0, 0, 1});
            std::fprintf(stderr, "frame %llu light %s type %d color %.3f %.3f %.3f wrap %.3f flags %x dir %.3f %.3f %.3f bone %d\n",
                         static_cast<unsigned long long>(frameIndex), l->name.c_str(), l->type, l->color[0], l->color[1],
                         l->color[2], l->wrap, l->viewFlags, dir.x, dir.y, dir.z, l->parentBone);
        }
    }
    renderer_->resetStats();
    graph.execute(*renderer_, items, vp, graphAnim);
}

}  // namespace oyster::story
