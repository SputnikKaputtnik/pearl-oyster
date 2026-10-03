#include "render/render_graph.h"

#include <algorithm>
#include <stdexcept>

namespace oyster {

using namespace gl;

namespace {
// Node type: RN_SCENE (= 0, common/scripts/graphics.lua) as a number (definition read from Lua)
// or as {"__const": "RN_SCENE"} (JSON export of tools/lua_data_dump.py).
bool isSceneNode(const Json& t) { return t.isNumber() ? t.num() == 0 : t["__const"].str() == "RN_SCENE"; }
// Colour component i: Lua Color object {r,g,b,a} or exported constructor call {"args": [...]}.
float colorComponent(const Json& c, int i, float def) {
    if (c["args"].isArray()) return static_cast<float>(c["args"][static_cast<size_t>(i)].num(def));
    static const char* keys[4] = {"r", "g", "b", "a"};
    return static_cast<float>(c[keys[i]].num(def));
}
}  // namespace

RenderGraph::RenderGraph(const Json& def, const Json& renderviews, const PackageFS& fs, const std::string& branch) {
    name_ = def["name"].str();
    // Pearl's graphs carry branches; the story's default graph is a plain node list.
    const Json& rg = def["rendergraph"];
    const Json& br = rg.isArray() ? rg : rg["branches"][branch];
    if (!br.isArray()) throw std::runtime_error("render graph " + name_ + " has no branch " + branch);
    for (const Json& n : br.arr()) {
        Node node;
        node.name = n["name"].str();
        node.scene = isSceneNode(n["type"]);
        if (node.scene) {
            node.passId = static_cast<int>(n["passId"].num());
            node.camera = n["camera"].str();
            const Json& ib = n["inputBindings"][0];
            const std::string& rv = ib["arg0"].str();
            const Json& v = renderviews[rv];
            node.view.name = rv;
            node.view.scale = static_cast<float>(v["scale"].num(1));
            node.view.viewFlag = static_cast<uint32_t>(static_cast<int64_t>(v["viewFlag"].num(2)));
            for (int i = 0; i < 4; ++i) node.view.clear[i] = colorComponent(v["clearcolor"], i, i == 3 ? 1.0f : 0.0f);
            node.globalSampler = n["outputBinding"]["globalSampler"].str();
        } else {
            const Json& m = n["material"];
            std::vector<Material> lib = loadMaterialLibrary(fs.read(m["pfx"].str()));
            bool found = false;
            for (auto& mat : lib)
                if (mat.name == m["materialName"].str()) { node.material = mat; found = true; }
            if (!found) throw std::runtime_error("material " + m["materialName"].str() + " not in " + m["pfx"].str());
            // image node materials run with RenderState::DEFAULT (FUN_1801a3790)
            for (auto& p : node.material.passes) p.state = RenderState::engineDefault();
            const Json& sd = n["outputBinding"]["sizeDivisor"];
            node.divX = static_cast<int>(sd["x"].num(1));
            node.divY = static_cast<int>(sd["y"].num(1));
            for (const Json& ib : n["inputBindings"].arr()) node.inputs.push_back(ib["arg0"].str());
        }
        nodes_.push_back(std::move(node));
    }
}

RenderGraph::~RenderGraph() { release(); }

void RenderGraph::release() {
    for (auto& n : nodes_) n.target.destroy();
    w_ = h_ = msaa_ = 0;
}

void RenderGraph::resize(int w, int h, int msaa) {
    if (w == w_ && h == h_ && msaa == msaa_) return;
    w_ = w;
    h_ = h;
    msaa_ = msaa;
    for (auto& n : nodes_) {
        n.target.destroy();
        int tw, th;
        if (n.scene) {
            tw = std::max(1, static_cast<int>(static_cast<float>(w) * n.view.scale));
            th = std::max(1, static_cast<int>(static_cast<float>(h) * n.view.scale));
        } else {
            tw = std::max(1, w / n.divX);
            th = std::max(1, h / n.divY);
        }
        n.target = createRenderTarget(tw, th, n.scene, n.scene ? msaa : 1);
    }
}

const RenderTarget& RenderGraph::output() const { return nodes_.back().target; }

const RenderTarget* RenderGraph::nodeTarget(const std::string& name) const {
    for (auto& n : nodes_)
        if (n.name == name) return &n.target;
    return nullptr;
}

void RenderGraph::execute(Renderer& r, const std::vector<SceneItem>& items, const ViewParams& base, const ModelInstance* anim) {
    ViewParams vp = base;
    for (auto& n : nodes_) {
        glBindFramebuffer(GL_FRAMEBUFFER, n.target.drawFbo());
        glViewport(0, 0, n.target.width, n.target.height);
        vp.screenSize[0] = static_cast<float>(n.target.width);  // FUN_1801903f0 / FUN_1801a3b60
        vp.screenSize[1] = static_cast<float>(n.target.height);
        if (n.scene) {
            r.clear(n.view.clear);
            vp.viewFlag = n.view.viewFlag;
            vp.passId = n.passId;
            r.drawScene(items, vp);
            n.target.discardDepth();
            n.target.resolve();
            if (!n.globalSampler.empty()) vp.globalSamplers[n.globalSampler] = n.target.color;
        } else {
            std::vector<const RenderTarget*> inputs;
            for (const auto& in : n.inputs) {
                const RenderTarget* t = nodeTarget(in);
                if (!t) throw std::runtime_error("render graph input " + in + " missing");
                inputs.push_back(t);
            }
            r.drawImage(n.material, inputs, vp, anim);
        }
    }
}

}  // namespace oyster
