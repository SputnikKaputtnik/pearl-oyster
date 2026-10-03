// Scene graph natives: Scene, Transform, Actor, Camera, Light, Animation, Atmospherics, Fog,
// ParticleEmitter, Projector (moxie.v2 LApi* semantics, decompiled in pearl-work/ghidra/lapi_*.c).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "story/bind_util.h"

namespace oyster::story {

namespace {

// ---- value conversions -------------------------------------------------------------------------
Vec3 vec3Arg(lua_State* L, int idx, Vec3 def = {}) {
    if (lua_istable(L, idx)) {
        int a = idx < 0 ? lua_gettop(L) + idx + 1 : idx;
        return {fieldOr(L, a, "x", def.x), fieldOr(L, a, "y", def.y), fieldOr(L, a, "z", def.z)};
    }
    if (lua_isnumber(L, idx) && lua_isnumber(L, idx + 1) && lua_isnumber(L, idx + 2))
        return {static_cast<float>(lua_tonumber(L, idx)), static_cast<float>(lua_tonumber(L, idx + 1)),
                static_cast<float>(lua_tonumber(L, idx + 2))};
    return def;
}
Quat quatArg(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return Quat();
    int a = idx < 0 ? lua_gettop(L) + idx + 1 : idx;
    return {fieldOr(L, a, "x", 0), fieldOr(L, a, "y", 0), fieldOr(L, a, "z", 0), fieldOr(L, a, "w", 1)};
}
// SRT table {position, rotation, scale}; missing parts are identity (Transform::getIdentity)
void srtArg(lua_State* L, int idx, Vec3& p, Quat& r, Vec3& s) {
    p = {0, 0, 0};
    r = Quat();
    s = {1, 1, 1};
    if (!lua_istable(L, idx)) return;
    lua_getfield(L, idx, "position");
    if (lua_istable(L, -1)) p = vec3Arg(L, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "rotation");
    if (lua_istable(L, -1)) r = quatArg(L, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "scale");
    if (lua_istable(L, -1)) s = vec3Arg(L, lua_gettop(L), {1, 1, 1});
    lua_pop(L, 1);
}

SGNode* nodeArg(lua_State* L, int idx) {
    SGNode* n = objectArg<SGNode>(L, idx);
    return n && n->alive ? n : nullptr;
}
ActorNode* actorArg(lua_State* L, int idx) {
    SGNode* n = nodeArg(L, idx);
    return (n && (n->kind == SGNode::Kind::Actor || n->kind == SGNode::Kind::RenderGraphInstance))
               ? static_cast<ActorNode*>(n) : nullptr;
}

// ---- Scene -------------------------------------------------------------------------------------
int Scene_create(lua_State* L) {
    Engine& e = engineOf(L);
    auto* s = e.create<SceneObj>("Scene");
    e.scenes.push_back(s);
    pushObject(L, s);
    return 1;
}
int Scene_destroy(lua_State* L) {
    Engine& e = engineOf(L);
    if (auto* s = objectArg<SceneObj>(L, 1)) {
        for (SGNode* n : s->nodes) e.destroyNode(n);
        e.sceneUpdate(L, s);  // flush deletes
        e.scenes.erase(std::remove(e.scenes.begin(), e.scenes.end(), s), e.scenes.end());
        LuaHost::from(L).destroy(s->handle);
    }
    return 0;
}
template <typename T> int createNode(lua_State* L, const char* type, SGNode::Kind kind) {
    Engine& e = engineOf(L);
    auto* scene = objectArg<SceneObj>(L, 1);
    auto* n = e.create<T>(type);
    n->kind = kind;
    e.addNode(scene, n, optString(L, 2));
    pushObject(L, n);
    return 1;
}
int Scene_createTransform(lua_State* L) { return createNode<SGNode>(L, "Transform", SGNode::Kind::Transform); }
int Scene_createCamera(lua_State* L) {
    createNode<CameraNode>(L, "Camera", SGNode::Kind::Camera);
    Engine& e = engineOf(L);
    auto* c = objectArg<CameraNode>(L, -1);
    if (c && (!e.mainCamera || c->name == "MainCamera")) e.mainCamera = c;
    return 1;
}
int Scene_createLight(lua_State* L) { return createNode<LightNode>(L, "Light", SGNode::Kind::Light); }
int Scene_createProjector(lua_State* L) { return createNode<SGNode>(L, "Projector", SGNode::Kind::Other); }
int Scene_createDepth(lua_State* L) { return createNode<SGNode>(L, "Depth", SGNode::Kind::Other); }
int Scene_createAtmospheric(lua_State* L) {
    return createNode<SGNode>(L, "Atmospherics", SGNode::Kind::Atmospherics);
}
int Scene_createParticleEmitter(lua_State* L) {
    // (scene, name, particle system uri); createParentedParticleEmitter(scene, name, parent, uri)
    int uriArg = lua_gettop(L) >= 4 ? 4 : 3;
    std::string uri = optString(L, uriArg);
    createNode<ParticleNode>(L, "ParticleEmitter", SGNode::Kind::Particles);
    auto* p = objectArg<ParticleNode>(L, -1);
    if (p && !uri.empty())
        if (auto ps = engineOf(L).particleSystem(uri)) p->emitter = std::make_unique<ParticleEmitter>(ps);
    return 1;
}
ParticleEmitter* emitterArg(lua_State* L) {
    auto* p = objectArg<ParticleNode>(L, 1);
    return p ? p->emitter.get() : nullptr;
}
int PE_setEmitRate(lua_State* L) {
    if (ParticleEmitter* e = emitterArg(L)) {
        e->customRate = true;
        e->rate = static_cast<float>(optNumber(L, 2, 0));
    }
    return 0;
}
int PE_getEmitRate(lua_State* L) {
    ParticleEmitter* e = emitterArg(L);
    lua_pushnumber(L, !e ? 0.0 : e->customRate ? e->rate : e->resource().f(0x8));
    return 1;
}
int PE_setTimeScale(lua_State* L) {
    if (ParticleEmitter* e = emitterArg(L)) e->timeScale = static_cast<float>(optNumber(L, 2, 1));
    return 0;
}
int PE_getTimeScale(lua_State* L) {
    ParticleEmitter* e = emitterArg(L);
    lua_pushnumber(L, e ? e->timeScale : 1.0);
    return 1;
}
int PE_setColor(lua_State* L) {
    if (ParticleEmitter* e = emitterArg(L); e && lua_istable(L, 2)) {
        e->color[0] = static_cast<float>(fieldOr(L, 2, "r", 1));
        e->color[1] = static_cast<float>(fieldOr(L, 2, "g", 1));
        e->color[2] = static_cast<float>(fieldOr(L, 2, "b", 1));
        e->color[3] = static_cast<float>(fieldOr(L, 2, "a", 1));
    }
    return 0;
}
int PE_reset(lua_State* L) {
    if (ParticleEmitter* e = emitterArg(L)) e->reset();
    return 0;
}
int PE_setParticleEnable(lua_State* L) {
    if (ParticleEmitter* e = emitterArg(L)) e->enabled = lua_toboolean(L, 2) != 0;
    return 0;
}
int PE_getParticleEnable(lua_State* L) {
    ParticleEmitter* e = emitterArg(L);
    lua_pushboolean(L, e && e->enabled);
    return 1;
}
int Scene_createVideoCubePlayer(lua_State* L) {
    return createNode<SGNode>(L, "VideoCubePlayer", SGNode::Kind::Other);
}
int Scene_createActor(lua_State* L) {
    Engine& e = engineOf(L);
    auto* scene = objectArg<SceneObj>(L, 1);
    ActorNode* a = e.createActor(scene, optString(L, 2), optString(L, 3));
    a->viewFlags = 1;  // VF_MAIN_VIEW until setViewFlags
    pushObject(L, a);
    return 1;
}
int Scene_update(lua_State* L) {
    if (auto* s = objectArg<SceneObj>(L, 1)) engineOf(L).sceneUpdate(L, s);
    return 0;
}
int Scene_find(lua_State* L) {
    auto* s = objectArg<SceneObj>(L, 1);
    std::string name = optString(L, 2);
    if (s)
        for (SGNode* n : s->nodes)
            if (n->alive && n->name == name) {
                pushObject(L, n);
                return 1;
            }
    lua_pushnil(L);
    return 1;
}
int noop(lua_State* L) { return 0; }

// ---- Transform ---------------------------------------------------------------------------------
int Tr_getName(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    lua_pushstring(L, n ? n->name.c_str() : "");
    return 1;
}
int Tr_setPosition(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) n->position = vec3Arg(L, 2, n->position);
    return 0;
}
int Tr_getPosition(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    LuaHost::from(L).pushVec3(L, n ? n->position : Vec3());
    return 1;
}
int Tr_setRotation(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) n->rotation = quatArg(L, 2);
    return 0;
}
int Tr_getRotation(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    LuaHost::from(L).pushQuat(L, n ? n->rotation : Quat());
    return 1;
}
int Tr_setScale(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) {
        if (lua_isnumber(L, 2) && lua_isnoneornil(L, 3)) {
            float s = static_cast<float>(lua_tonumber(L, 2));
            n->scale = {s, s, s};
        } else {
            n->scale = vec3Arg(L, 2, n->scale);
        }
    }
    return 0;
}
int Tr_getScale(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    LuaHost::from(L).pushVec3(L, n ? n->scale : Vec3(1, 1, 1));
    return 1;
}
int Tr_setLocal(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) srtArg(L, 2, n->position, n->rotation, n->scale);
    return 0;
}
int Tr_getLocal(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    if (!n) return 0;
    LuaHost::from(L).pushSRT(L, n->position, n->rotation, n->scale);
    return 1;
}
int Tr_setWorld(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    if (!n) return 0;
    Vec3 p, s;
    Quat r;
    srtArg(L, 2, p, r, s);
    if (!n->parent) {
        n->position = p;
        n->rotation = r;
        n->scale = s;
    } else {
        SRT l = srtFromMatrix(n->parent->worldMatrix().inverse() * Mat4::trs(p, r, s));
        n->position = l.p;
        n->rotation = l.r;
        n->scale = l.s;
    }
    return 0;
}
int Tr_getWorld(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    if (!n) return 0;
    SRT w = n->worldSRT();
    LuaHost::from(L).pushSRT(L, w.p, w.r, w.s);
    return 1;
}
int Tr_addComponent(lua_State* L) {
    // native part of Transform.addComponent (common/scripts/scene.lua): attach an LComponent so
    // the scene update calls Transform.__onUpdate / __onLateUpdate / __onDestroy for this node
    if (SGNode* n = nodeArg(L, 1)) n->luaComponents = true;
    return 0;
}
int Tr_addChild(lua_State* L) {
    SGNode* p = nodeArg(L, 1);
    SGNode* c = nodeArg(L, 2);
    if (!p || !c || c == p) return 0;
    if (c->parent) {
        auto& sib = c->parent->children;
        sib.erase(std::remove(sib.begin(), sib.end(), c), sib.end());
    }
    c->parent = p;
    c->parentBone = -1;
    p->children.push_back(c);
    return 0;
}
int Tr_removeChild(lua_State* L) {
    SGNode* p = nodeArg(L, 1);
    SGNode* c = nodeArg(L, 2);
    if (!p || !c || c->parent != p) return 0;
    auto& sib = p->children;
    sib.erase(std::remove(sib.begin(), sib.end(), c), sib.end());
    c->parent = nullptr;
    c->parentBone = -1;
    return 0;
}
int Tr_getNumChildren(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    lua_pushnumber(L, n ? static_cast<double>(n->children.size()) : 0);
    return 1;
}
int Tr_getParent(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    pushObject(L, n ? n->parent : nullptr);
    return 1;
}
int Tr_setVisibility(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) n->visible = optBool(L, 2, true);
    return 0;
}
int Tr_isVisible(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    lua_pushboolean(L, n && n->visible);
    return 1;
}
int Tr_setActive(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) n->active = optBool(L, 2, true);
    return 0;
}
int Tr_isActive(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    lua_pushboolean(L, n && n->active);
    return 1;
}
int Tr_setViewFlags(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1))
        n->viewFlags = static_cast<uint32_t>(static_cast<int64_t>(optNumber(L, 2, -1)));
    return 0;
}
int Tr_getViewFlags(lua_State* L) {
    SGNode* n = nodeArg(L, 1);
    lua_pushnumber(L, n ? static_cast<double>(static_cast<int32_t>(n->viewFlags)) : 0);
    return 1;
}
int Tr_destroy(lua_State* L) {
    if (SGNode* n = nodeArg(L, 1)) engineOf(L).destroyNode(n);
    return 0;
}
int Tr_false(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// ---- Actor ---------------------------------------------------------------------------------------
int Actor_addAnimation(lua_State* L) {
    // (actor, name, uri, firstFrame, lastFrame, loop, relativeFrames)
    ActorNode* a = actorArg(L, 1);
    AnimationObj* an = a ? engineOf(L).addAnimation(a, optString(L, 2), optString(L, 3),
                                                    static_cast<int>(optNumber(L, 4, 0)),
                                                    static_cast<int>(optNumber(L, 5, 0)), optBool(L, 6, false),
                                                    optBool(L, 7, true))
                         : nullptr;
    pushObject(L, an);
    return 1;
}
int Actor_listTransforms(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    lua_newtable(L);
    if (a && a->inst) {
        const auto& names = a->inst->model().nodeNames;
        for (size_t i = 0; i < names.size(); ++i) {
            lua_pushstring(L, names[i].c_str());
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
    }
    return 1;
}
int Actor_getBoneIndex(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int idx = (a && a->inst) ? a->inst->model().findNode(optString(L, 2)) : -1;
    lua_pushnumber(L, idx);
    return 1;
}
bool boneOk(ActorNode* a, int i) { return a && a->inst && i >= 0 && static_cast<size_t>(i) < a->inst->nodeCount(); }
int Actor_getBoneWorld(lua_State* L) {
    // actor world (SRT) combined with the bone's model matrix (FUN_1800c20a0)
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    if (!boneOk(a, i)) return 0;
    SRT w = srtCombine(srtFromMatrix(a->inst->nodeModel(static_cast<size_t>(i))), a->worldSRT());
    LuaHost::from(L).pushSRT(L, w.p, w.r, w.s);
    return 1;
}
int Actor_getBoneModel(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    if (!boneOk(a, i)) return 0;
    SRT m = srtFromMatrix(a->inst->nodeModel(static_cast<size_t>(i)));
    LuaHost::from(L).pushSRT(L, m.p, m.r, m.s);
    return 1;
}
int Actor_getBoneLocal(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    if (!boneOk(a, i)) return 0;
    SRT l = srtFromMatrix(a->inst->nodeLocal(static_cast<size_t>(i)));
    LuaHost::from(L).pushSRT(L, l.p, l.r, l.s);
    return 1;
}
int Actor_setBoneVisibility(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    if (boneOk(a, i)) a->inst->setNodeVisibility(static_cast<size_t>(i), optBool(L, 3, true));
    return 0;
}
int Actor_getBoneVisibility(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    lua_pushboolean(L, boneOk(a, i) && a->inst->nodeOwnVisible(static_cast<size_t>(i)));
    return 1;
}
int Actor_getCustomAttribute(lua_State* L) {
    // (actor, bone, name, type, ...) -> current value of an animated custom attribute; Pearl's
    // camera rigs ("fov" on MoxieCamera|RootNode) have none, so 0 like the original
    lua_pushnumber(L, 0);
    return 1;
}
int Actor_setBoneRadius(lua_State* L) {
    if (ActorNode* a = actorArg(L, 1)) a->boneRadius = static_cast<float>(optNumber(L, 2, 0));
    return 0;
}
int Actor_getBoneRadius(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    lua_pushnumber(L, a ? a->boneRadius : 0);
    return 1;
}
int Actor_addAttachment(lua_State* L) {
    // SGModelInstance::addAttachment: child follows the bone (SGAttachmentProxy)
    ActorNode* a = actorArg(L, 1);
    SGNode* c = nodeArg(L, 3);
    if (!a || !a->inst || !c) return 0;
    int bone = a->inst->model().findNode(optString(L, 2));
    if (bone < 0) return 0;
    if (c->parent) {
        auto& sib = c->parent->children;
        sib.erase(std::remove(sib.begin(), sib.end(), c), sib.end());
    }
    c->parent = a;
    c->parentBone = bone;
    a->children.push_back(c);
    return 0;
}
int Actor_removeAttachment(lua_State* L) { return Tr_removeChild(L); }
int Actor_addFlipbook(lua_State* L) { return 0; }  // same native as addFlipbookDriver: returns nothing
int Actor_getMaterialCount(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    lua_pushnumber(L, a && a->inst ? static_cast<double>(a->inst->materials().size()) : 0);
    return 1;
}
int Actor_getMaterialName(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    int i = static_cast<int>(optNumber(L, 2, -1));
    if (!a || !a->inst || i < 0 || static_cast<size_t>(i) >= a->inst->materials().size()) return 0;
    lua_pushstring(L, a->inst->materials()[static_cast<size_t>(i)].name.c_str());
    return 1;
}
int Actor_listMaterials(lua_State* L) {
    ActorNode* a = actorArg(L, 1);
    lua_newtable(L);
    if (a && a->inst)
        for (size_t i = 0; i < a->inst->materials().size(); ++i) {
            lua_pushstring(L, a->inst->materials()[i].name.c_str());
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
    return 1;
}

// ---- Camera ------------------------------------------------------------------------------------
CameraNode* cameraArg(lua_State* L, int idx) {
    SGNode* n = nodeArg(L, idx);
    return n && n->kind == SGNode::Kind::Camera ? static_cast<CameraNode*>(n) : nullptr;
}
int Cam_setPerspective(lua_State* L) {
    // (camera, fovY degrees, near, far)
    if (CameraNode* c = cameraArg(L, 1)) {
        c->ortho = false;
        c->fovDeg = static_cast<float>(optNumber(L, 2, c->fovDeg));
        c->znear = static_cast<float>(optNumber(L, 3, c->znear));
        c->zfar = static_cast<float>(optNumber(L, 4, c->zfar));
    }
    return 0;
}
int Cam_setOrthographic(lua_State* L) {
    if (CameraNode* c = cameraArg(L, 1)) {
        c->ortho = true;
        c->left = static_cast<float>(optNumber(L, 2, -1));
        c->top = static_cast<float>(optNumber(L, 3, 1));
        c->right = static_cast<float>(optNumber(L, 4, 1));
        c->bottom = static_cast<float>(optNumber(L, 5, -1));
        c->znear = static_cast<float>(optNumber(L, 6, c->znear));
        c->zfar = static_cast<float>(optNumber(L, 7, c->zfar));
    }
    return 0;
}

// ---- Light -------------------------------------------------------------------------------------
LightNode* lightArg(lua_State* L, int idx) {
    SGNode* n = nodeArg(L, idx);
    return n && n->kind == SGNode::Kind::Light ? static_cast<LightNode*>(n) : nullptr;
}
int Light_setType(lua_State* L) {
    if (LightNode* l = lightArg(L, 1)) l->type = static_cast<int>(optNumber(L, 2, 0));
    return 0;
}
int Light_setWrap(lua_State* L) {
    if (LightNode* l = lightArg(L, 1)) l->wrap = static_cast<float>(optNumber(L, 2, 0));
    return 0;
}
int Light_setRange(lua_State* L) {
    if (LightNode* l = lightArg(L, 1)) {
        l->range[0] = static_cast<float>(optNumber(L, 2, 0));
        l->range[1] = static_cast<float>(optNumber(L, 3, 100000));
    }
    return 0;
}
int Light_setSpotAngle(lua_State* L) {
    if (LightNode* l = lightArg(L, 1)) {
        l->spot[0] = static_cast<float>(optNumber(L, 2, 0));
        l->spot[1] = static_cast<float>(optNumber(L, 3, 0));
    }
    return 0;
}
int Light_setColor(lua_State* L) {
    // Vector3 (def.diffuse) or Color
    LightNode* l = lightArg(L, 1);
    if (!l || !lua_istable(L, 2)) return 0;
    lua_getfield(L, 2, "x");
    bool isVec = lua_isnumber(L, -1);
    lua_pop(L, 1);
    const char* k[3] = {isVec ? "x" : "r", isVec ? "y" : "g", isVec ? "z" : "b"};
    for (int i = 0; i < 3; ++i) l->color[i] = fieldOr(L, 2, k[i], l->color[i]);
    return 0;
}

// ---- Animation ---------------------------------------------------------------------------------
AnimationObj* animArg(lua_State* L) { return objectArg<AnimationObj>(L, 1); }
int An_play(lua_State* L) {
    engineOf(L).playAnimation(animArg(L), lua_toboolean(L, 2) == 1);
    return 0;
}
int An_stop(lua_State* L) {
    engineOf(L).stopAnimation(animArg(L));
    return 0;
}
int An_destroy(lua_State* L) {
    engineOf(L).destroyAnimation(animArg(L));
    return 0;
}
int An_setPaused(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.paused = lua_toboolean(L, 2) != 0;
    return 0;
}
int An_isPaused(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushboolean(L, a && a->pb.paused);
    return 1;
}
int An_isPlaying(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushboolean(L, a && a->playing);
    return 1;
}
int An_isDone(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushboolean(L, !a || !a->playing);
    return 1;
}
int An_isLooping(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushboolean(L, a && a->pb.loop);
    return 1;
}
int An_setRate(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.speed = static_cast<float>(optNumber(L, 2, 1));
    return 0;
}
int An_getRate(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.speed : 1);
    return 1;
}
int An_setTime(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.time = static_cast<float>(optNumber(L, 2, 0));
    return 0;
}
int An_getTime(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.time : 0);
    return 1;
}
int An_getDuration(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.duration : 0);
    return 1;
}
int An_getFramesPerSecond(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.fps : 0);
    return 1;
}
int An_getStartFrame(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.start : 0);
    return 1;
}
int An_getStopFrame(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.end : 0);
    return 1;
}
int An_getNumFrames(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.end - a->pb.start + 1 : 0);
    return 1;
}
int An_getFrame(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? std::floor(a->pb.framePosition()) : 0);
    return 1;
}
int An_setFrame(lua_State* L) {
    if (AnimationObj* a = animArg(L))
        a->pb.time = (static_cast<float>(optNumber(L, 2, 0)) - static_cast<float>(a->pb.start)) / static_cast<float>(a->pb.fps);
    return 0;
}
int An_setProgress(lua_State* L) {
    // FUN_1800c6770: time = duration * p, clamped (or wrapped for looping clips)
    AnimationObj* a = animArg(L);
    if (!a) return 0;
    float d = a->pb.duration;
    float t = d * static_cast<float>(optNumber(L, 2, 0));
    if (!a->pb.loop) {
        if (t > d) t = d;
        if (t <= 0) t = 0;
    } else {
        t = std::fmod(t, d);
        if (t <= 0) t = 0;
    }
    a->pb.time = t;
    return 0;
}
int An_getProgress(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a && a->pb.duration > 0 ? a->pb.time / a->pb.duration : 0);
    return 1;
}
int An_setMaxLoopCount(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.maxLoops = static_cast<uint32_t>(optNumber(L, 2, 0));
    return 0;
}
int An_setWeight(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->weight = static_cast<float>(optNumber(L, 2, 1));
    return 0;
}
int An_getWeight(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->weight : 0);
    return 1;
}
int An_setInterpolate(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.interpolate = lua_toboolean(L, 2) != 0;
    return 0;
}
int An_isInterpolated(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushboolean(L, a && a->pb.interpolate);
    return 1;
}
int An_setFixedTimeStep(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.fixedDt = static_cast<float>(optNumber(L, 2, 0));
    return 0;
}
int An_getFixedTimeStep(lua_State* L) {
    AnimationObj* a = animArg(L);
    lua_pushnumber(L, a ? a->pb.fixedDt : 0);
    return 1;
}
int An_rewind(lua_State* L) {
    if (AnimationObj* a = animArg(L)) a->pb.time = 0;
    return 0;
}

// ---- Trigger (LookAtTrigger component) ----------------------------------------------------------
// Ray/sphere test of the camera's forward ray against a trigger volume (FUN_180102db0 /
// FUN_180102b70): centre = M * offset; hit when dp >= 0 and r^2 - (|d|^2 - dp^2) > 0.
bool lookAtHit(const Mat4& m, const Vec3& offset, float radius, const Vec3& campos, const Vec3& camfwd) {
    Vec3 d = m.transformPoint(offset) - campos;
    float dp = d.dot(camfwd);
    float len = std::sqrt(d.dot(d));
    return 0.0f <= dp && 0.0f < radius * radius - (len * len - dp * dp);
}
Mat4 srtMatrixArg(lua_State* L, int idx) {
    Vec3 p, s;
    Quat r;
    srtArg(L, idx, p, r, s);
    return Mat4::trs(p, r, s);
}
int Trig_withoutParent(lua_State* L) {
    // (ownerSRT, offset, radius, campos, camfwd)
    bool hit = lookAtHit(srtMatrixArg(L, 1), vec3Arg(L, 2), static_cast<float>(optNumber(L, 3, 0)), vec3Arg(L, 4),
                         vec3Arg(L, 5));
    lua_pushboolean(L, hit);
    return 1;
}
int Trig_withParent(lua_State* L) {
    // (actor, ownerSRT, offset, radius, bone, campos, camfwd); a valid bone replaces the owner
    // transform by the bone's MODEL matrix (no actor world - as in the original)
    ActorNode* a = actorArg(L, 1);
    Mat4 m = srtMatrixArg(L, 2);
    int bone = static_cast<int>(optNumber(L, 5, -1));
    if (boneOk(a, bone)) m = srtFromMatrix(a->inst->nodeModel(static_cast<size_t>(bone))).matrix();
    bool hit = lookAtHit(m, vec3Arg(L, 3), static_cast<float>(optNumber(L, 4, 0)), vec3Arg(L, 6), vec3Arg(L, 7));
    lua_pushboolean(L, hit);
    return 1;
}

// ---- Atmospherics / Fog ------------------------------------------------------------------------
int Atm_createFog(lua_State* L) {
    Engine& e = engineOf(L);
    auto* f = e.create<GenericObj>("Fog");
    f->name = optString(L, 2);
    pushObject(L, f);
    return 1;
}
int Fog_setFogParams(lua_State* L) {
    // (fog, type, color, start, end) - stored; Pearl's materials take fog from u_fogColor
    auto* f = objectArg<GenericObj>(L, 1);
    if (!f) return 0;
    f->numbers["type"] = optNumber(L, 2, 0);
    if (lua_istable(L, 3)) {
        f->numbers["r"] = fieldOr(L, 3, "x", fieldOr(L, 3, "r", 0));
        f->numbers["g"] = fieldOr(L, 3, "y", fieldOr(L, 3, "g", 0));
        f->numbers["b"] = fieldOr(L, 3, "z", fieldOr(L, 3, "b", 0));
    }
    f->numbers["start"] = optNumber(L, 4, 10);
    f->numbers["end"] = optNumber(L, 5, 100);
    return 0;
}
int Generic_destroy(lua_State* L) {
    LuaHost& h = LuaHost::from(L);
    if (SGNode* n = nodeArg(L, 1)) engineOf(L).destroyNode(n);
    else if (auto* g = objectArg<GenericObj>(L, 1)) h.destroy(g->handle);
    return 0;
}

}  // namespace

void bindScene(Engine& e) {
    LuaHost& h = e.lua();

    static const luaL_Reg scene[] = {
        {"create", Scene_create}, {"destroy", Scene_destroy}, {"createTransform", Scene_createTransform},
        {"createActor", Scene_createActor}, {"createCamera", Scene_createCamera},
        {"createParticleEmitter", Scene_createParticleEmitter},
        {"createParentedParticleEmitter", Scene_createParticleEmitter}, {"createLight", Scene_createLight},
        {"createProjector", Scene_createProjector}, {"createDepth", Scene_createDepth},
        {"createVideoCubePlayer", Scene_createVideoCubePlayer}, {"createAtmospheric", Scene_createAtmospheric},
        {"render", noop}, {"update", Scene_update}, {"find", Scene_find}, {"setLocked", noop},
        {"setActiveSegment", noop}, {nullptr, nullptr}};
    h.bindType("Scene", scene);

    static const luaL_Reg transform[] = {
        {"getName", Tr_getName}, {"setTranslation", Tr_setPosition}, {"getTranslation", Tr_getPosition},
        {"setPosition", Tr_setPosition}, {"getPosition", Tr_getPosition}, {"setRotation", Tr_setRotation},
        {"getRotation", Tr_getRotation}, {"setScale", Tr_setScale}, {"getScale", Tr_getScale},
        {"setLocal", Tr_setLocal}, {"getLocal", Tr_getLocal}, {"setWorld", Tr_setWorld}, {"getWorld", Tr_getWorld},
        {"addComponent", Tr_addComponent}, {"addChild", Tr_addChild}, {"removeChild", Tr_removeChild},
        {"getNumChildren", Tr_getNumChildren}, {"getParent", Tr_getParent}, {"setVisibility", Tr_setVisibility},
        {"isVisible", Tr_isVisible}, {"setActive", Tr_setActive}, {"isActive", Tr_isActive},
        {"setViewFlags", Tr_setViewFlags}, {"getViewFlags", Tr_getViewFlags}, {"destroy", Tr_destroy},
        {"setDrawDebug", noop}, {"setRenderDebug", noop}, {"forceWorldUpdate", noop},
        {"isRecording", Tr_false}, {"isReplaying", Tr_false}, {"setMarker", noop}, {nullptr, nullptr}};
    h.bindType("Transform", transform);

    static const luaL_Reg actor[] = {
        {"addFlipbookDriver", Actor_addFlipbook}, {"addFlipbook", Actor_addFlipbook},
        {"addAnimation", Actor_addAnimation}, {"listMaterials", Actor_listMaterials},
        {"getMaterialCount", Actor_getMaterialCount}, {"getMaterialName", Actor_getMaterialName},
        {"listTransforms", Actor_listTransforms}, {"setBoneVisibility", Actor_setBoneVisibility},
        {"getBoneVisibility", Actor_getBoneVisibility}, {"getBoneLocal", Actor_getBoneLocal},
        {"getBoneModel", Actor_getBoneModel}, {"getBoneWorld", Actor_getBoneWorld},
        {"getCustomAttribute", Actor_getCustomAttribute}, {"getBoneIndex", Actor_getBoneIndex},
        {"setBoneRadius", Actor_setBoneRadius}, {"getBoneRadius", Actor_getBoneRadius},
        {"addAttachment", Actor_addAttachment}, {"removeAttachment", Actor_removeAttachment}, {nullptr, nullptr}};
    h.bindType("Actor", actor, "Transform");

    static const luaL_Reg camera[] = {{"setPerspective", Cam_setPerspective},
                                      {"setOrthographic", Cam_setOrthographic},
                                      {"reshape", noop},
                                      {nullptr, nullptr}};
    h.bindType("Camera", camera, "Transform");

    static const luaL_Reg light[] = {{"setType", Light_setType},   {"setWrap", Light_setWrap},
                                     {"setRange", Light_setRange}, {"setSpotAngle", Light_setSpotAngle},
                                     {"setColor", Light_setColor}, {"setPriority", noop},
                                     {nullptr, nullptr}};
    h.bindType("Light", light, "Transform");

    static const luaL_Reg anim[] = {
        {"setRate", An_setRate}, {"getRate", An_getRate}, {"setPaused", An_setPaused}, {"isPaused", An_isPaused},
        {"rewind", An_rewind}, {"setTime", An_setTime}, {"getTime", An_getTime}, {"setFrame", An_setFrame},
        {"getFrame", An_getFrame}, {"getStartFrame", An_getStartFrame}, {"getStopFrame", An_getStopFrame},
        {"setProgress", An_setProgress}, {"getProgress", An_getProgress}, {"getDuration", An_getDuration},
        {"getFramesPerSecond", An_getFramesPerSecond}, {"getNumFrames", An_getNumFrames},
        {"getWeight", An_getWeight}, {"setWeight", An_setWeight}, {"setInterpolate", An_setInterpolate},
        {"isInterpolated", An_isInterpolated}, {"isPlaying", An_isPlaying}, {"isLooping", An_isLooping},
        {"setMaxLoopCount", An_setMaxLoopCount}, {"isDone", An_isDone}, {"setFixedTimeStep", An_setFixedTimeStep},
        {"getFixedTimeStep", An_getFixedTimeStep}, {"play", An_play}, {"stop", An_stop}, {"destroy", An_destroy},
        {nullptr, nullptr}};
    h.bindType("Animation", anim);

    static const luaL_Reg atm[] = {{"destroy", Generic_destroy}, {"createFog", Atm_createFog}, {nullptr, nullptr}};
    h.bindType("Atmospherics", atm, "Transform");
    static const luaL_Reg fog[] = {{"setFogParams", Fog_setFogParams}, {"destroy", Generic_destroy}, {nullptr, nullptr}};
    h.bindType("Fog", fog);
    static const luaL_Reg particles[] = {{"destroy", Generic_destroy}, {"setEmitRate", PE_setEmitRate},
                                         {"getEmitRate", PE_getEmitRate}, {"setTimeScale", PE_setTimeScale},
                                         {"getTimeScale", PE_getTimeScale}, {"setColor", PE_setColor},
                                         {"reset", PE_reset}, {"setParticleEnable", PE_setParticleEnable},
                                         {"getParticleEnable", PE_getParticleEnable}, {nullptr, nullptr}};
    h.bindType("ParticleEmitter", particles, "Transform");
    h.bindType("Projector", nullptr, "Transform");
    h.bindType("Depth", nullptr, "Transform");
    h.bindType("VideoCubePlayer", nullptr, "Transform");
    static const luaL_Reg trigger[] = {{"evaluateLookAtTriggerWithParent", Trig_withParent},
                                       {"evaluateLookAtTriggerWithoutParent", Trig_withoutParent},
                                       {nullptr, nullptr}};
    h.bindType("Trigger", trigger);
}

}  // namespace oyster::story
