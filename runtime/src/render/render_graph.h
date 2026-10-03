// Render graph (story/scripts rgraph definitions, RenderManager::createRenderGraph): scene nodes
// render a material pass of the scene into a render view target, image nodes run a post-effect
// material (.pfb) over their inputs. The last node is the graph output.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "assets/common.h"
#include "core/json.h"
#include "core/pkgfs.h"
#include "render/renderer.h"

namespace oyster {

struct RenderViewDef {
    std::string name;
    float scale = 1.0f;
    uint32_t viewFlag = 2;
    float clear[4] = {0, 0, 0, 1};
};

class RenderGraph {
public:
    // rgraphDef = def.rgraph[name]; renderviews = def.renderviews
    RenderGraph(const Json& rgraphDef, const Json& renderviews, const PackageFS& fs, const std::string& branch = "default");
    ~RenderGraph();

    void resize(int w, int h, int msaa = 2);  // scene views use MSAA like the original (-msaa 2)
    // Renders the whole graph; `anim` (optional) carries the graph's animated parameters.
    void execute(Renderer& r, const std::vector<SceneItem>& items, const ViewParams& base, const ModelInstance* anim);
    const RenderTarget& output() const;
    const RenderTarget* nodeTarget(const std::string& name) const;
    const std::string& name() const { return name_; }

private:
    struct Node {
        std::string name;
        bool scene = false;
        int passId = 0;
        RenderViewDef view;
        std::string globalSampler;
        Material material;
        int divX = 1, divY = 1;
        std::vector<std::string> inputs;
        RenderTarget target;
    };
    std::string name_;
    std::vector<Node> nodes_;
    int w_ = 0, h_ = 0, msaa_ = 0;
};

}  // namespace oyster
