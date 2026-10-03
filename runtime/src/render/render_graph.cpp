#include "render/render_graph.h"

#include <algorithm>
#include <stdexcept>

namespace oyster {

using namespace gl;

namespace {
const std::string& constName(const Json& j) { return j["__const"].str(); }
}  // namespace

RenderGraph::RenderGraph(const Json& def, const Json& renderviews, const PackageFS& fs, const std::string& branch) {
    name_ = def["name"].str();
    const Json& br = def["rendergraph"]["branches"][branch];
    if (!br.isArray()) throw std::runtime_error("render graph " + name_ + " has no branch " + branch);
    for (const Json& n : br.arr()) {
        Node node;
        node.name = n["name"].str();
        node.scene = constName(n["type"]) == "RN_SCENE";
        if (node.scene) {
            node.passId = static_cast<int>(n["passId"].num());
            const Json& ib = n["inputBindings"][0];
            const std::string& rv = ib["arg0"].str();
            const Json& v = renderviews[rv];
            node.view.name = rv;
            node.view.scale = static_cast<float>(v["scale"].num(1));
            node.view.viewFlag = static_cast<uint32_t>(v["viewFlag"].num(2));
            const Json& cc = v["clearcolor"]["args"];
            for (int i = 0; i < 4; ++i) node.view.clear[i] = static_cast<float>(cc[static_cast<size_t>(i)].num(i == 3 ? 1 : 0));
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

RenderGraph::~RenderGraph() {
    for (auto& n : nodes_) n.target.destroy();
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
        if (n.scene) {
            r.clear(n.view.clear);
            vp.viewFlag = n.view.viewFlag;
            vp.passId = n.passId;
            r.drawScene(items, vp);
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
