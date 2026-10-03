// oyster_viewer: renders one frame of a story state from the user's Pearl installation.
//
// Until the original Lua runs inside the player, the scene of a state is assembled from the
// story definition exported by tools/lua_data_dump.py (def JSON): actors and their clips for
// the state, the camera rig (mono camera: CameraRigController without sensors, see
// docs/viewer.md), and the state's lights.
//
// Usage: oyster_viewer --root <install> --def <def.json> --state <name> --time <seconds>
//                      [--size 1280x720] [--out frame.tga] [--utime <u_time>] [--view main|shadow|warp]
//                      [--remaster]
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/anim.h"
#include "assets/model.h"
#include "core/json.h"
#include "core/pkgfs.h"
#include "render/gl.h"
#include "render/renderer.h"
#include "scene/model_instance.h"

using namespace oyster;
using namespace oyster::gl;

namespace {

struct Args {
    std::string root, def, state, out = "frame.tga", view = "main";
    float time = 0, utime = -1;
    int w = 1280, h = 720;
    bool remaster = false;
};

Vec3 jsonVec3(const Json& j, Vec3 def) {
    if (!j.isObject() || !j["args"].isArray()) return def;
    const Json& a = j["args"];
    return {static_cast<float>(a[0].num()), static_cast<float>(a[1].num()), static_cast<float>(a[2].num())};
}
Quat jsonQuat(const Json& j) {
    if (!j.isObject() || !j["args"].isArray() || j["args"].size() < 4) return Quat();
    const Json& a = j["args"];
    return Quat(static_cast<float>(a[0].num()), static_cast<float>(a[1].num()), static_cast<float>(a[2].num()),
                static_cast<float>(a[3].num()));
}
Mat4 jsonTransform(const Json& t) {
    if (!t.isObject()) return Mat4();
    return Mat4::trs(jsonVec3(t["position"], {0, 0, 0}), jsonQuat(t["rotation"]), jsonVec3(t["scale"], {1, 1, 1}));
}

void writeTGA(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary);
    uint8_t hdr[18] = {};
    hdr[2] = 2;
    hdr[12] = static_cast<uint8_t>(w & 255); hdr[13] = static_cast<uint8_t>(w >> 8);
    hdr[14] = static_cast<uint8_t>(h & 255); hdr[15] = static_cast<uint8_t>(h >> 8);
    hdr[16] = 24;
    f.write(reinterpret_cast<char*>(hdr), 18);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {  // GL rows are bottom-up = TGA default origin
        const uint8_t* src = &rgba[static_cast<size_t>(y) * w * 4];
        for (int x = 0; x < w; ++x) {
            row[3 * x] = src[4 * x + 2];
            row[3 * x + 1] = src[4 * x + 1];
            row[3 * x + 2] = src[4 * x];
        }
        f.write(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

class ModelCache {
public:
    explicit ModelCache(const PackageFS& fs) : fs_(fs) {}
    std::shared_ptr<ModelResource> get(const std::string& uri) {
        auto it = cache_.find(uri);
        if (it != cache_.end()) return it->second;
        std::shared_ptr<ModelResource> m = loadModel(fs_.read(uri));
        cache_[uri] = m;
        return m;
    }
    std::unique_ptr<ModelInstance> instantiate(const std::string& uri) {
        std::shared_ptr<ModelResource> m = get(uri);
        std::shared_ptr<ModelResource> geom = m;
        if (m->isPatch()) geom = get(m->baseModel);
        std::vector<Material> mats = (m->isPatch() && m->overridesMaterials) ? m->materials : geom->materials;
        return std::make_unique<ModelInstance>(geom, mats);
    }
    std::shared_ptr<AnimResource> anim(const std::string& uri) {
        auto it = anims_.find(uri);
        if (it != anims_.end()) return it->second;
        std::shared_ptr<AnimResource> a = loadAnim(fs_.read(uri));
        anims_[uri] = a;
        return a;
    }

private:
    const PackageFS& fs_;
    std::map<std::string, std::shared_ptr<ModelResource>> cache_;
    std::map<std::string, std::shared_ptr<AnimResource>> anims_;
};

struct Actor {
    std::string name;
    std::unique_ptr<ModelInstance> inst;
    uint32_t viewFlags = 0xFFFFFFFF;
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (k == "--root") a.root = next();
        else if (k == "--def") a.def = next();
        else if (k == "--state") a.state = next();
        else if (k == "--time") a.time = std::stof(next());
        else if (k == "--utime") a.utime = std::stof(next());
        else if (k == "--out") a.out = next();
        else if (k == "--view") a.view = next();
        else if (k == "--remaster") a.remaster = true;
        else if (k == "--size") {
            std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &a.w, &a.h);
        } else {
            std::fprintf(stderr, "unknown argument %s\n", k.c_str());
            return false;
        }
    }
    return !a.root.empty() && !a.def.empty() && !a.state.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        std::fprintf(stderr, "usage: oyster_viewer --root <install> --def <def.json> --state <name> --time <s> [--size WxH] [--out f.tga]\n");
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* win = SDL_CreateWindow("oyster", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 64, 64,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "GLES 3.0 context: %s\n", SDL_GetError()); return 1; }
    if (const char* missing = gl::load(SDL_GL_GetProcAddress)) { std::fprintf(stderr, "missing GL function %s\n", missing); return 1; }
    std::printf("GL: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (!gl::hasExtension("GL_EXT_texture_compression_s3tc")) std::fprintf(stderr, "warning: no S3TC support\n");

    int rc = 0;
    try {
        PackageFS fs(args.root);
        std::ifstream jf(args.def, std::ios::binary);
        std::stringstream ss;
        ss << jf.rdbuf();
        Json def = Json::parse(ss.str());
        const Json& state = def["Main"]["states"][args.state];
        if (!state.isObject()) throw std::runtime_error("unknown state " + args.state);

        SamplingPolicy policy = args.remaster ? SamplingPolicy::remaster() : SamplingPolicy::original();
        ModelCache cache(fs);
        std::vector<Actor> actors;
        std::vector<Light> lights;
        std::unique_ptr<ModelInstance> rig;
        const Json* rigDef = nullptr;

        auto setupAnim = [&](ModelInstance& inst, const Json& adef) {
            const Json& clip = adef["animation"][args.state];
            if (!clip.isArray()) return;
            AnimationPlayback pb;
            pb.setup(cache.anim(clip[0].str()), static_cast<int>(clip[1].num()), static_cast<int>(clip[2].num()),
                     clip[3].boolean(), true);
            pb.advance(args.time);
            inst.setAnimation(pb);
        };

        for (const Json& n : state["active"].arr()) {
            const std::string& name = n.str();
            if (def["actors"].has(name)) {
                const Json& a = def["actors"][name];
                if (!a["model"].isString()) continue;
                Actor act;
                act.name = name;
                act.inst = cache.instantiate(a["model"].str());
                act.inst->root = jsonTransform(a["transform"]);
                act.viewFlags = a["viewFlags"].isNumber() ? static_cast<uint32_t>(a["viewFlags"].num()) : 0xFFFFFFFFu;
                setupAnim(*act.inst, a);
                actors.push_back(std::move(act));
            } else if (def["cameras"].has(name) && def["cameras"][name]["model"].isString()) {
                rigDef = &def["cameras"][name];
                rig = cache.instantiate((*rigDef)["model"].str());
                rig->root = jsonTransform((*rigDef)["transform"]);
                setupAnim(*rig, *rigDef);
            } else if (def["lights"].has(name)) {
                const Json& l = def["lights"][name];
                Light li;
                const std::string& type = l["type"].str();
                li.type = type == "AmbientLight" ? 3 : type == "PointLight" ? 1 : type == "SpotLight" ? 2 : 0;
                li.world = jsonTransform(l["transform"]);
                Vec3 c = jsonVec3(l["diffuse"], {1, 1, 1});
                li.color[0] = c.x; li.color[1] = c.y; li.color[2] = c.z;
                li.color[3] = static_cast<float>(l["wrap"].num(0));
                if (l["range"].isArray()) { li.range[0] = static_cast<float>(l["range"][0].num()); li.range[1] = static_cast<float>(l["range"][1].num()); }
                li.viewFlags = l["viewFlags"].isNumber() ? static_cast<uint32_t>(l["viewFlags"].num()) : 0xFFFFFFFFu;
                lights.push_back(li);
            }
        }
        std::printf("state %s: %zu actors, %zu lights, camera rig %s\n", args.state.c_str(), actors.size(), lights.size(),
                    rig ? "yes" : "no");

        for (auto& a : actors) a.inst->evaluate(policy);

        // Mono camera (CameraRigController, cameraOrientationMode 1, no sensor/mouse input):
        // orientation = boom origin world rotation; position = boom origin + R * (0,0,-L),
        // L = -boomEnd.local.z (squash factors 1 -> unchanged length).
        Mat4 camWorld;
        if (rig) {
            rig->evaluate(policy);
            const Json& comp = (*rigDef)["components"][0];
            int origin = rig->model().findNode(comp["boomOrigin"].str());
            int end = rig->model().findNode(comp["boomEnd"].str());
            if (origin < 0 || end < 0) throw std::runtime_error("camera rig bones not found");
            const Mat4& ow = rig->nodeWorld(static_cast<size_t>(origin));
            Vec3 cx = Vec3(ow.m[0], ow.m[4], ow.m[8]).normalized();
            Vec3 cy = Vec3(ow.m[1], ow.m[5], ow.m[9]).normalized();
            Vec3 cz = Vec3(ow.m[2], ow.m[6], ow.m[10]).normalized();
            float L = -rig->nodeLocal(static_cast<size_t>(end)).m[11];
            Vec3 pos = ow.translation() + cz * (-L);
            camWorld.m[0] = cx.x; camWorld.m[1] = cy.x; camWorld.m[2] = cz.x; camWorld.m[3] = pos.x;
            camWorld.m[4] = cx.y; camWorld.m[5] = cy.y; camWorld.m[6] = cz.y; camWorld.m[7] = pos.y;
            camWorld.m[8] = cx.z; camWorld.m[9] = cy.z; camWorld.m[10] = cz.z; camWorld.m[11] = pos.z;
            std::printf("camera at (%.2f %.2f %.2f) boom %.2f\n", pos.x, pos.y, pos.z, L);
        }
        const Json& cam = def["cameras"]["MainCamera"]["proj"];
        float fov = static_cast<float>(cam["fov"].num(48.7955)) * 3.14159265358979f / 180.0f;
        float zn = static_cast<float>(cam["znear"].num(5)), zf = static_cast<float>(cam["zfar"].num(1e7));
        if (zn > 1.1920929e-07f && zf / zn > 500000.0f) zf = zn * 500000.0f;  // Camera::update clamp
        float aspect = static_cast<float>(args.w) / static_cast<float>(args.h);
        float f = 1.0f / std::tan(fov * 0.5f);
        Mat4 proj;
        proj.setIdentity();
        proj.m[0] = f / aspect; proj.m[5] = f;
        proj.m[10] = (zf + zn) / (zn - zf); proj.m[11] = 2.0f * zf * zn / (zn - zf);
        proj.m[14] = -1.0f; proj.m[15] = 0.0f;

        Renderer r(fs);
        ViewParams vp;
        vp.view = camWorld.inverse();
        vp.proj = proj;
        vp.aspect = aspect;
        vp.zfar = zf;
        std::vector<SceneItem> items;
        for (auto& a : actors) items.push_back({a.inst.get(), a.viewFlags});
        vp.time = args.utime >= 0 ? args.utime : args.time;
        vp.lights = lights;

        // shadowview (RV3: viewFlag 8, pass 1, clear (0,0,0.5,0)) -> u_shadowPass
        RenderTarget shadow = createRenderTarget(args.w, args.h, true);
        glBindFramebuffer(GL_FRAMEBUFFER, shadow.fbo);
        glViewport(0, 0, args.w, args.h);
        const float shadowClear[4] = {0, 0, 0.5f, 0};
        r.clear(shadowClear);
        vp.viewFlag = 8;
        vp.passId = 1;
        r.drawScene(items, vp);

        RenderTarget main = createRenderTarget(args.w, args.h, true);
        RenderTarget* out = &main;
        glBindFramebuffer(GL_FRAMEBUFFER, main.fbo);
        glViewport(0, 0, args.w, args.h);
        const float mainClear[4] = {0.5f, 0.5f, 0.5f, 1};
        r.clear(mainClear);
        vp.viewFlag = 2;
        vp.passId = 2;
        vp.globalSamplers["u_shadowPass"] = shadow.color;
        if (args.view == "shadow") out = &shadow;
        else r.drawScene(items, vp);

        std::printf("draw calls %zu\n", r.drawCalls());
        std::vector<uint8_t> px(static_cast<size_t>(args.w) * args.h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, out->fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, args.w, args.h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        writeTGA(args.out, args.w, args.h, px);
        std::printf("wrote %s\n", args.out.c_str());
        shadow.destroy();
        main.destroy();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        rc = 1;
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}
