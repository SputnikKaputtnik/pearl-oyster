#include "story/engine.h"

#include <chrono>

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
    renderer_->setShaderCache(opt.shaderCacheDir);
    prefetch_ = std::make_unique<Prefetcher>(fs, 2);
    renderer_->fileSource = [this](const std::string& uri, std::vector<uint8_t>& bytes) {
        if (prefetch_->takeFile(uri, bytes)) {
            if (prefetch_->lastTakeWaited) recordHint(Prefetcher::Kind::File, uri);
            return true;
        }
        recordHint(Prefetcher::Kind::File, uri);
        return false;
    };
    loadHints();
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
    if (opt_.precompileShaders) {
        auto t0 = std::chrono::steady_clock::now();
        size_t n = renderer_->precompileAll();
        std::fprintf(stderr, "shaders: %zu programs ready in %.0f ms (%zu from the binary cache)\n", n,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                     renderer_->programsFromCache);
    }
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

// OYSTER_DEBUG_TIMING: average CPU time per frame of the main phases, every 300 frames
namespace {
struct PhaseTimer {
    const char* name;
    double total = 0;
};
PhaseTimer gPhases[] = {{"lua update (excl. scene)"}, {"actors"}, {"particles+hooks"}, {"draw: collect"}, {"draw: graphs"}};
double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool timingOn() {
    static const bool on = std::getenv("OYSTER_DEBUG_TIMING") != nullptr;  // read on first use
    return on;
}
}  // namespace

// loads that happened on the main thread during the current frame (OYSTER_DEBUG_TIMING)
namespace {
struct LoadStats {
    double ms[5] = {0, 0, 0, 0, 0};  // model, anim, audio, instantiate, particles
    int n[5] = {0, 0, 0, 0, 0};
} gLoads;
struct LoadTimer {
    int k;
    double t0;
    explicit LoadTimer(int kind) : k(kind), t0(nowMs()) {}
    ~LoadTimer() {
        gLoads.ms[k] += nowMs() - t0;
        ++gLoads.n[k];
    }
};
}  // namespace

bool Engine::frame(double dtSeconds) {
    double frameStart = timingOn() ? nowMs() : 0;
    double texBefore = renderer_->texReadMs + renderer_->texDecodeMs + renderer_->texUploadMs;
    size_t texLoadsBefore = renderer_->texLoads;
    double progBefore = renderer_->programMs;
    size_t progLoadsBefore = renderer_->programLoads;
    gLoads = LoadStats();
    prefetch_->expire(120.0);
    int64_t dt = time_.fixedStepMs > 0 ? static_cast<int64_t>(std::llround(time_.fixedStepMs * 1000.0))
                                       : static_cast<int64_t>(std::llround(dtSeconds * 1e6));
    if (time_.paused) dt = 0;
    dt = static_cast<int64_t>(static_cast<double>(dt) * time_.scale);
    time_.dtUs = dt;
    time_.elapsedUs += dt;
    double t0 = timingOn() ? nowMs() : 0;
    double sceneBefore = timingOn() ? gPhases[1].total + gPhases[2].total : 0;
    bool ok = lua_->callApplication("onUpdate");
    if (timingOn()) gPhases[0].total += nowMs() - t0 - (gPhases[1].total + gPhases[2].total - sceneBefore);
    ok = lua_->callApplication("onRender") && ok;
    trackStates();
    if (!skipRender) preloadTextures();
    ++frameIndex;
    if (timingOn()) {
        double total = nowMs() - frameStart;
        if (total > 50.0)
            std::fprintf(stderr,
                         "slow frame %llu: %.0f ms - models %.0f ms (%d), anims %.0f (%d), audio %.0f (%d), instances %.0f (%d), "
                         "particles %.0f (%d), textures %.0f (%zu), shaders %.0f (%zu)\n",
                         static_cast<unsigned long long>(frameIndex), total, gLoads.ms[0], gLoads.n[0], gLoads.ms[1],
                         gLoads.n[1], gLoads.ms[2], gLoads.n[2], gLoads.ms[3], gLoads.n[3], gLoads.ms[4], gLoads.n[4],
                         renderer_->texReadMs + renderer_->texDecodeMs + renderer_->texUploadMs - texBefore,
                         renderer_->texLoads - texLoadsBefore, renderer_->programMs - progBefore,
                         renderer_->programLoads - progLoadsBefore);
    }
    if (timingOn() && frameIndex % 300 == 0) {
        std::string line = "timing/frame:";
        for (auto& ph : gPhases) {
            char b[96];
            std::snprintf(b, sizeof(b), "  %s %.2f ms", ph.name, ph.total / 300.0);
            line += b;
            ph.total = 0;
        }
        std::fprintf(stderr, "%s  draw calls %zu  [scene prepare %.2f ms, execute %.2f ms, uploads %.2f ms %.2f MB]\n",
                     line.c_str(), renderer_->drawCalls(), renderer_->prepareMs / 300.0, renderer_->executeMs / 300.0,
                     renderer_->uploadMs / 300.0, static_cast<double>(renderer_->uploadBytes) / 300.0 / 1048576.0);
        renderer_->uploadMs = 0;
        renderer_->uploadBytes = 0;
        renderer_->prepareMs = renderer_->executeMs = 0;
    }
    return ok;
}

// Textures and mesh buffers of the models the prefetcher has loaded for coming states go to the
// GPU a few per frame before they are needed, instead of all at once in the first frame of the
// next shot (the render thread would spend tens of milliseconds in the driver there). Same data,
// same result.
void Engine::preloadTextures() {
    constexpr size_t kPerFrame = 3;
    constexpr double kBudgetMs = 2.0;
    constexpr size_t kGeometryBytes = size_t(4) << 20;
    constexpr size_t kGeometryMeshes = 48;  // the driver's cost is per buffer
    std::vector<std::string> ready;
    prefetch_->readyFiles(ready, 64);
    double t0 = nowMs();
    size_t n = 0;
    for (const std::string& uri : ready) {
        if (renderer_->hasTexture(uri)) {
            prefetch_->drop(Prefetcher::Kind::File, uri);  // loaded meanwhile from disk
            continue;
        }
        renderer_->texture(uri);
        ++texturesPreloaded;
        if (++n >= kPerFrame || nowMs() - t0 > kBudgetMs) return;
    }
    std::vector<std::shared_ptr<const ModelResource>> models;
    prefetch_->readyModels(models);
    for (const auto& m : models) {
        if (nowMs() - t0 > kBudgetMs) return;
        if (renderer_->preloadGeometry(m, kGeometryBytes, kGeometryMeshes)) return;  // one model's share per frame
    }
}

// Learned prefetch. Hint file lines: <state>\t<kind>\t<uri> - entering <state> requests the
// resource (it was loaded on demand within the next two states of an earlier run).
void Engine::loadHints() {
    if (opt_.prefetchHints.empty()) return;
    std::FILE* f = std::fopen(opt_.prefetchHints.c_str(), "rb");
    if (!f) return;
    char line[2048];
    size_t n = 0;
    while (std::fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
        if (t2 == std::string::npos || !hintLines_.insert(l).second) continue;
        int kind = std::atoi(l.substr(t1 + 1, t2 - t1 - 1).c_str());
        if (kind < 0 || kind > 3) continue;
        hints_[l.substr(0, t1)].emplace_back(static_cast<Prefetcher::Kind>(kind), l.substr(t2 + 1));
        ++n;
    }
    std::fclose(f);
    std::fprintf(stderr, "prefetch hints: %zu from %s\n", n, opt_.prefetchHints.c_str());
}

// Story states from the FSM transition log ("[old]->[new]"); entering a state requests its hints.
void Engine::trackStates() {
    const auto& log = lua_->log;
    for (; logSeen_ < log.size(); ++logSeen_) {
        const std::string& l = log[logSeen_];
        size_t arrow = l.find("]->[");
        if (arrow == std::string::npos) continue;
        size_t end = l.find(']', arrow + 4);
        if (end == std::string::npos) continue;
        std::string dest = l.substr(arrow + 4, end - arrow - 4);
        if (dest == state_[0]) continue;
        state_[2] = state_[1];
        state_[1] = state_[0];
        state_[0] = dest;
        auto h = hints_.find(dest);
        if (h != hints_.end())
            for (const auto& kv : h->second) prefetch_->request(kv.first, kv.second);
    }
}

void Engine::recordHint(Prefetcher::Kind kind, const std::string& uri) {
    if (opt_.prefetchHints.empty()) return;
    trackStates();  // a transition logged earlier in this frame
    for (int k = 1; k <= 2; ++k) {
        if (state_[k].empty()) continue;
        std::string l = state_[k] + "\t" + std::to_string(static_cast<int>(kind)) + "\t" + uri;
        if (!hintLines_.insert(l).second) continue;
        hints_[state_[k]].emplace_back(kind, uri);
        if (std::FILE* f = std::fopen(opt_.prefetchHints.c_str(), "ab")) {
            std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    }
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
    LoadTimer lt(2);
    bool found = false;
    c = prefetch_->takeAudio(uri, found);
    if (found && prefetch_->lastTakeWaited) recordHint(Prefetcher::Kind::Audio, uri);
    if (found) {
        clips_[uri] = c;
        return c;
    }
    try {
        recordHint(Prefetcher::Kind::Audio, uri);
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
    LoadTimer lt(0);
    bool found = false;
    std::shared_ptr<ModelResource> m = prefetch_->takeModel(uri, found);
    if (found && prefetch_->lastTakeWaited) recordHint(Prefetcher::Kind::Model, uri);
    if (!found) {
        recordHint(Prefetcher::Kind::Model, uri);
        m = loadModel(fs_.read(uri));
    }
    models_[uri] = m;
    return m;
}

std::shared_ptr<AnimResource> Engine::anim(const std::string& uri) {
    auto it = anims_.find(uri);
    if (it != anims_.end()) return it->second;
    LoadTimer lt(1);
    bool found = false;
    std::shared_ptr<AnimResource> a = prefetch_->takeAnim(uri, found);
    if (found && prefetch_->lastTakeWaited) recordHint(Prefetcher::Kind::Anim, uri);
    if (!found) {
        recordHint(Prefetcher::Kind::Anim, uri);
        a = loadAnim(fs_.read(uri));
    }
    anims_[uri] = a;
    return a;
}

std::shared_ptr<const ParticleSystemResource> Engine::particleSystem(const std::string& uri) {
    auto it = particleSystems_.find(uri);
    if (it != particleSystems_.end()) return it->second;
    std::shared_ptr<const ParticleSystemResource> ps;
    LoadTimer lt(4);
    try {
        ps = loadParticleSystem(fs_.read(uri));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "particle system %s: %s\n", uri.c_str(), ex.what());
    }
    particleSystems_[uri] = ps;
    return ps;
}

std::unique_ptr<ModelInstance> Engine::instantiate(const std::string& uri) {
    std::shared_ptr<ModelResource> m = model(uri);
    std::shared_ptr<ModelResource> geom = m;
    if (m->isPatch()) geom = model(m->baseModel);
    std::vector<Material> mats = (m->isPatch() && m->overridesMaterials) ? m->materials : geom->materials;
    LoadTimer lt(3);
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
    double ta = timingOn() ? nowMs() : 0;
    for (SGNode* n : nodes)
        if (n->alive && (n->kind == SGNode::Kind::Actor || n->kind == SGNode::Kind::RenderGraphInstance))
        {
            updateActor(static_cast<ActorNode*>(n), dt);
            if (static_cast<ActorNode*>(n)->inst) applyAttachedLights(static_cast<ActorNode*>(n));
        }
    double tb = timingOn() ? nowMs() : 0;
    if (timingOn()) gPhases[1].total += tb - ta;
    // SGParticleEmitter::internalUpdate: global dt (us) x 1e-6 x the emitter's time scale, after
    // the actors have posed the bones the emitters hang on
    for (SGNode* n : nodes) {
        if (!n->alive || n->kind != SGNode::Kind::Particles) continue;
        auto* p = static_cast<ParticleNode*>(n);
        if (!p->emitter) continue;
        bool active = true;
        for (const SGNode* q = n; q; q = q->parent) active = active && q->active;
        if (!active) continue;
        SRT w = n->worldSRT();
        float pdt = static_cast<float>(time_.dtUs) * 1e-06f * p->emitter->timeScale;
        p->emitter->update(pdt, w.p, w.r, w.matrix());
    }
    for (SGNode* n : nodes)
        if (n->alive && n->luaComponents && n->active) callNodeHook(L, n, "__onLateUpdate");
    if (timingOn()) gPhases[2].total += nowMs() - tb;
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
    g->views = views;
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

const RenderTarget* Engine::eyeOutput(int eye) const {
    if (!activeGraph_ || eye < 0 || eye > 1 || !activeGraph_->eyeGraph[eye]) return nullptr;
    return &activeGraph_->eyeGraph[eye]->output();
}

void Engine::draw() {
    if (!activeGraph_ || !activeGraph_->graph || skipRender) return;
    double td0 = timingOn() ? nowMs() : 0;
    for (auto* g : graphs)
        if (g != activeGraph_) {
            if (g->graph) g->graph->release();
            for (auto& eg : g->eyeGraph)
                if (eg) eg->release();
        }

    std::vector<SceneItem> items;
    const ModelInstance* graphAnim = nullptr;
    for (SceneObj* s : scenes)
        for (SGNode* n : s->nodes) {
            if (n->kind == SGNode::Kind::RenderGraphInstance && n->alive && n->name == activeGraph_->name)
                graphAnim = static_cast<ActorNode*>(n)->inst.get();
            static const bool debugParticles = std::getenv("OYSTER_DEBUG_PARTICLES") != nullptr;
            if (debugParticles && n->kind == SGNode::Kind::Particles && n->alive) {
                auto* p = static_cast<ParticleNode*>(n);
                Vec3 lo = p->emitter ? p->emitter->boundsMin() : Vec3(), hi = p->emitter ? p->emitter->boundsMax() : Vec3();
                Vec3 w = p->worldSRT().p;
                std::fprintf(stderr, "frame %llu emitter %s vis %d flags %x n %zu at %.1f %.1f %.1f bounds %.1f %.1f %.1f .. %.1f %.1f %.1f\n",
                             static_cast<unsigned long long>(frameIndex), n->name.c_str(), static_cast<int>(n->effectiveVisible()),
                             n->viewFlags, p->emitter ? p->emitter->particles().size() : 0, w.x, w.y, w.z, lo.x, lo.y,
                             lo.z, hi.x, hi.y, hi.z);
            }
            if (n->kind == SGNode::Kind::Particles && n->effectiveVisible()) {
                auto* p = static_cast<ParticleNode*>(n);
                static const bool hideParticles = std::getenv("OYSTER_DEBUG_NOPARTICLES") != nullptr;
                if (p->emitter && !hideParticles) {
                    SceneItem it;
                    it.particles = p->emitter.get();
                    it.viewFlags = p->viewFlags;
                    it.particleWorld = p->worldMatrix();
                    items.push_back(it);
                }
                continue;
            }
            if (n->kind != SGNode::Kind::Actor || !n->effectiveVisible()) continue;
            auto* a = static_cast<ActorNode*>(n);
            if (a->drawable && a->inst) {
                SceneItem it;
                it.inst = a->inst.get();
                it.viewFlags = a->viewFlags;
                items.push_back(it);
            }
        }

    ViewParams vp;
    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    if (hmd_.active) aspect = static_cast<float>(hmd_.width) / static_cast<float>(hmd_.height);
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
    double td1 = timingOn() ? nowMs() : 0;
    if (timingOn()) gPhases[3].total += td1 - td0;
    struct GraphTimer {
        double t0;
        ~GraphTimer() {
            if (timingOn()) gPhases[4].total += nowMs() - t0;
        }
    } graphTimer{td1};
    renderer_->beginFrame();
    renderer_->resetStats();
    static const bool debugTexMem = std::getenv("OYSTER_DEBUG_TEXMEM") != nullptr;
    if (debugTexMem && frameIndex % 150 == 0) {
        uint64_t fc = renderer_->frameCounter();
        std::fprintf(stderr, "frame %llu textures %.1f MB, used in the last 30 frames %.1f MB\n",
                     static_cast<unsigned long long>(frameIndex), renderer_->textureBytes() / 1048576.0,
                     renderer_->textureBytesUsedSince(fc > 30 ? fc - 30 : 0) / 1048576.0);
    }
    if (!hmd_.active) {
        RenderGraph& graph = *activeGraph_->graph;
        graph.resize(width_, height_, opt_.msaa);
        graph.execute(*renderer_, items, vp, graphAnim);
        return;
    }
    // Stereo (CameraVR, FUN_18018a5f0 / FUN_18018a740): each eye views from the centre camera
    // shifted sideways by half the user's IPD times the story's IPD scalar (left eye: view
    // translated by +half), with the runtime's per-eye frustum at the camera's near/far planes.
    // Every eye runs the whole render graph into its own targets (RenderView::beginEye).
    float zn = mainCamera ? mainCamera->znear : 0.01f;
    float zf = vp.zfar;
    float half = ipdScalar * hmd_.ipd * 0.5f;
    for (int eye = 0; eye < 2; ++eye) {
        auto& eg = activeGraph_->eyeGraph[eye];
        if (!eg) {
            try {
                eg = std::make_unique<RenderGraph>(activeGraph_->def, activeGraph_->views, fs_);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "render graph %s: %s\n", activeGraph_->name.c_str(), ex.what());
                return;
            }
        }
        eg->resize(hmd_.width, hmd_.height, opt_.msaa);
        ViewParams ev = vp;
        Mat4 shift;
        shift.m[3] = eye == 0 ? half : -half;
        Mat4 lift;  // HmdState::eyeRaise: camera moved up in world space (story units per metre = IPD scalar)
        lift.m[7] = -hmd_.eyeRaise * ipdScalar;
        ev.view = shift * (vp.view * lift);
        const HmdState::Eye& e = hmd_.eye[eye];
        float l = e.tanLeft * zn, r = e.tanRight * zn, t = e.tanUp * zn, b = e.tanDown * zn;
        Mat4 proj;  // Matrix4::makeFrustum
        proj.setIdentity();
        proj.m[0] = 2.0f * zn / (r - l);
        proj.m[2] = (r + l) / (r - l);
        proj.m[5] = 2.0f * zn / (t - b);
        proj.m[6] = (t + b) / (t - b);
        proj.m[10] = (zf + zn) / (zn - zf);
        proj.m[11] = 2.0f * zf * zn / (zn - zf);
        proj.m[14] = -1.0f;
        proj.m[15] = 0.0f;
        ev.proj = proj;
        ev.viewIndex = eye;
        ev.viewMid[0] = (r + l) / (r - l);  // optical axis in NDC (DisplayDeviceHMD slot +0x38)
        ev.viewMid[1] = (t + b) / (t - b);
        eg->execute(*renderer_, items, ev, graphAnim);
    }
}

}  // namespace oyster::story
