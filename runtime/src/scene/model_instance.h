// A placed model with its animated state (SGModelInstance equivalent, minimal).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "assets/anim.h"
#include "assets/model.h"
#include "scene/animation.h"

namespace oyster {

struct MeshState {
    // current per-render-vertex positions/normals when vertex-animated (empty = use rest data)
    std::vector<float> position;
    std::vector<float> normal;
    bool animated = false;
    bool visible = true;
    uint64_t revision = 0;  // bumps whenever position/normal change
};

class ModelInstance {
public:
    // geometry: the model holding meshes/nodes (base model for patches); materials: from the
    // patch model if it overrides them, else from geometry.
    ModelInstance(std::shared_ptr<const ModelResource> geometry, std::vector<Material> materials);

    const ModelResource& model() const { return *geom_; }
    uint64_t id() const { return id_; }  // unique per instance (GPU caches; addresses get reused)
    const std::vector<Material>& materials() const { return materials_; }

    Mat4 root;  // actor transform (scene graph node)

    // Play exactly one clip (the story plays one clip per actor and state).
    void setAnimation(const AnimationPlayback& playback);
    // Same clip, new time state (story Animation objects own the playback, see story/scene.h).
    void setPlaybackState(const AnimationPlayback& playback) { if (anim_) *anim_ = playback; }
    void clearAnimation() { anim_.reset(); }  // pose, visibility and parameters keep their values
    AnimationPlayback* animation() { return anim_ ? &*anim_ : nullptr; }
    void advance(float dt) { if (anim_) anim_->advance(dt); }

    // Evaluate pose, world matrices, material parameter animation and vertex animation.
    void evaluate(const SamplingPolicy& policy);

    const Mat4& nodeWorld(size_t i) const { return world_[i]; }
    const Mat4& nodeLocal(size_t i) const { return local_[i]; }
    // Effective visibility (updateModelMatrices): own flag (Actor.setBoneVisibility) && animated
    // flag && effective visibility of the parent.
    bool nodeVisible(size_t i) const { return effVisible_[i] != 0; }
    void setNodeVisibility(size_t i, bool v) { ownVisible_[i] = v ? 1 : 0; }
    bool nodeOwnVisible(size_t i) const { return ownVisible_[i] != 0; }
    size_t nodeCount() const { return local_.size(); }
    const MeshState& meshState(size_t i) const { return meshes_[i]; }

    // Animated material parameter override for (material, param); pass is always 0 (the engine
    // binds channels per material, all passes see the value); mask = components set.
    // Render-graph parameters (customA == 1) are stored under (0, param, 0).
    bool materialOverride(uint32_t materialHash, uint32_t paramHash, uint32_t pass, float out[4], uint32_t* mask) const;

private:
    void bindAnimation();
    void evaluateVertexAnim(const SamplingPolicy& policy);
    uint32_t vanmFrame(const VanmMesh& vm) const;  // VertexAnimator frame index (clamped)

    uint64_t id_ = 0;
    std::shared_ptr<const ModelResource> geom_;
    std::vector<Material> materials_;
    std::vector<Mat4> local_, world_;
    std::vector<uint8_t> nodeVisible_;  // animated (pose) visibility
    std::vector<uint8_t> ownVisible_, effVisible_;
    std::vector<MeshState> meshes_;

    std::unique_ptr<AnimationPlayback> anim_;
    std::vector<int> trackNode_;  // per track: node index or -1
    std::vector<std::vector<ChannelCache>> caches_, customCaches_;
    struct VanmBinding {
        int mesh = -1;
        int node = -1;
        const VanmMesh* vm = nullptr;
        std::unique_ptr<VanmDecoder> pos0, pos1, nrm0, nrm1;
    };
    std::vector<VanmBinding> vanm_;
    struct ParamValue { float v[4] = {0, 0, 0, 0}; uint32_t mask = 0; };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, ParamValue> params_;
};

// Applies baked subdivision stencils + remap to cage data (pos & normal, 3 floats each).
void evaluateSubdiv(const SubdivSurface& s, const float* cagePos, const float* cageNrm,
                    std::vector<float>& outPos, std::vector<float>& outNrm);

}  // namespace oyster
