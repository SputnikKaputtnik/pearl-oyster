// Render natives: RenderManager, RenderGraph, RenderGraphInstance, Renderer, RenderView.
// The active render graph renders the scene each RenderManager.draw (Story:render).
#include <cstdio>

#include "story/bind_util.h"

namespace oyster::story {

namespace {

int RM_createRenderGraph(lua_State* L) {
    // (scene, rgraphDef)
    Engine& e = engineOf(L);
    Json def = luaToJson(L, 2);
    pushObject(L, e.createRenderGraph(def));
    return 1;
}
int RM_createRenderGraphInstance(lua_State* L) {
    // (scene, graph name, model uri): carries the graph's animated post parameters
    Engine& e = engineOf(L);
    auto* scene = objectArg<SceneObj>(L, 1);
    ActorNode* a = e.createActor(scene, optString(L, 2), optString(L, 3), "RenderGraphInstance");
    pushObject(L, a);
    return 1;
}
int RM_getRenderGraphByName(lua_State* L) {
    pushObject(L, engineOf(L).findRenderGraph(optString(L, firstArg(L))));
    return 1;
}
int RM_getRenderGraphByIndex(lua_State* L) {
    Engine& e = engineOf(L);
    int i = static_cast<int>(optNumber(L, firstArg(L), -1));
    pushObject(L, i >= 0 && static_cast<size_t>(i) < e.graphs.size() ? e.graphs[static_cast<size_t>(i)] : nullptr);
    return 1;
}
int RM_setActiveRenderGraph(lua_State* L) {
    if (auto* g = objectArg<RenderGraphObj>(L, firstArg(L))) engineOf(L).setActiveRenderGraph(g);
    return 0;
}
int RM_draw(lua_State* L) {
    engineOf(L).draw();
    return 0;
}
int RM_resize(lua_State* L) {
    int a = firstArg(L);
    engineOf(L).resize(static_cast<int>(optNumber(L, a, 1280)), static_cast<int>(optNumber(L, a + 1, 720)));
    return 0;
}
int RM_getDisplayType(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}
int noop(lua_State* L) { return 0; }

int RG_findNodeByName(lua_State* L) {
    lua_pushnil(L);
    return 1;
}

int RGI_addAnimation(lua_State* L) {
    // same signature and semantics as Actor.addAnimation
    auto* a = objectArg<ActorNode>(L, 1);
    AnimationObj* an = a ? engineOf(L).addAnimation(a, optString(L, 2), optString(L, 3),
                                                    static_cast<int>(optNumber(L, 4, 0)),
                                                    static_cast<int>(optNumber(L, 5, 0)), optBool(L, 6, false),
                                                    optBool(L, 7, true))
                         : nullptr;
    pushObject(L, an);
    return 1;
}

int Renderer_createView(lua_State* L) {
    auto* v = engineOf(L).create<GenericObj>("RenderView");
    v->name = optString(L, firstArg(L));
    pushObject(L, v);
    return 1;
}
int Renderer_getRenderDebug(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

}  // namespace

void bindRender(Engine& e) {
    LuaHost& h = e.lua();
    static const luaL_Reg rm[] = {
        {"setupRenderGraphDisplay", noop}, {"resize", RM_resize}, {"getRenderGraphByIndex", RM_getRenderGraphByIndex},
        {"getRenderGraphByName", RM_getRenderGraphByName}, {"createRenderGraph", RM_createRenderGraph},
        {"createRenderGraphInstance", RM_createRenderGraphInstance}, {"destroyRenderGraph", noop},
        {"setActiveRenderGraph", RM_setActiveRenderGraph}, {"configureDisplay", noop}, {"draw", RM_draw},
        {"enableRenderToDisk", noop}, {"saveScreenshot", noop}, {"captureThumbnail", noop},
        {"getDisplayType", RM_getDisplayType}, {"setDisplayType", noop}, {"reset", noop}, {nullptr, nullptr}};
    h.bindType("RenderManager", rm);
    static const luaL_Reg rg[] = {{"findNodeByName", RG_findNodeByName}, {nullptr, nullptr}};
    h.bindType("RenderGraph", rg);
    static const luaL_Reg rgi[] = {{"addAnimation", RGI_addAnimation}, {nullptr, nullptr}};
    h.bindType("RenderGraphInstance", rgi, "Transform");
    static const luaL_Reg renderer[] = {{"setClearColor", noop}, {"clear", noop}, {"createView", Renderer_createView},
                                        {"display", noop}, {"drawText", noop}, {"setRenderDebug", noop},
                                        {"getRenderDebug", Renderer_getRenderDebug}, {nullptr, nullptr}};
    h.bindType("Renderer", renderer);
    h.bindType("RenderView", nullptr);
}

}  // namespace oyster::story
