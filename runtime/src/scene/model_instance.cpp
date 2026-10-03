#include "scene/model_instance.h"

#include <cmath>
#include <unordered_map>

namespace oyster {

void evaluateSubdiv(const SubdivSurface& s, const float* cp, const float* cn, std::vector<float>& outPos,
                    std::vector<float>& outNrm) {
    // FUN_1801b6db0: out_j = sum_i w_i * cage[idx_i]; FUN_1801b7160: render[v] = out[remap[v]]
    const size_t nOut = s.stencilSize.size();
    std::vector<float> op(3 * nOut), on(3 * nOut);
    size_t k = 0;
    for (size_t j = 0; j < nOut; ++j) {
        uint32_t n = s.stencilSize[j];
        float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
        for (uint32_t i = 0; i < n; ++i, ++k) {
            float w = s.stencilWeight[k];
            uint32_t c = s.stencilIndex[k] * 3;
            if (i == 0) {
                px = w * cp[c]; py = w * cp[c + 1]; pz = w * cp[c + 2];
                nx = w * cn[c]; ny = w * cn[c + 1]; nz = w * cn[c + 2];
            } else {
                px += w * cp[c]; py += w * cp[c + 1]; pz += w * cp[c + 2];
                nx += w * cn[c]; ny += w * cn[c + 1]; nz += w * cn[c + 2];
            }
        }
        op[3 * j] = px; op[3 * j + 1] = py; op[3 * j + 2] = pz;
        on[3 * j] = nx; on[3 * j + 1] = ny; on[3 * j + 2] = nz;
    }
    outPos.resize(3 * s.remap.size());
    outNrm.resize(3 * s.remap.size());
    for (size_t v = 0; v < s.remap.size(); ++v) {
        uint32_t j = s.remap[v] * 3;
        for (int c = 0; c < 3; ++c) {
            outPos[3 * v + c] = op[j + c];
            outNrm[3 * v + c] = on[j + c];
        }
    }
}

ModelInstance::ModelInstance(std::shared_ptr<const ModelResource> geometry, std::vector<Material> materials)
    : geom_(std::move(geometry)), materials_(std::move(materials)) {
    size_t n = geom_->localXforms.size();
    local_ = geom_->localXforms;
    world_.resize(n);
    nodeVisible_.assign(n, 1);
    meshes_.resize(geom_->meshes.size());
}

void ModelInstance::setAnimation(const AnimationPlayback& playback) {
    anim_ = std::make_unique<AnimationPlayback>(playback);
    bindAnimation();
}

void ModelInstance::bindAnimation() {
    const AnimResource& a = *anim_->res;
    std::unordered_map<std::string, int> byName;
    for (size_t i = 0; i < geom_->nodeNames.size(); ++i) byName[geom_->nodeNames[i]] = static_cast<int>(i);
    trackNode_.assign(a.tracks.size(), -1);
    caches_.assign(a.tracks.size(), {});
    customCaches_.assign(a.tracks.size(), {});
    for (size_t t = 0; t < a.tracks.size(); ++t) {
        auto it = byName.find(a.tracks[t].name);
        if (it != byName.end()) trackNode_[t] = it->second;
        caches_[t].resize(a.tracks[t].channels.size());
        customCaches_[t].resize(a.tracks[t].custom.size());
    }
    vanm_.clear();
    local_ = geom_->localXforms;
    nodeVisible_.assign(local_.size(), 1);
    for (auto& m : meshes_) m = MeshState();
    if (!a.vertexAnim) return;
    for (const VanmMesh& vm : a.vertexAnim->meshes) {
        int node = -1;
        auto it = byName.find(vm.name);
        if (it != byName.end()) node = it->second;
        int mesh = -1;
        for (size_t m = 0; m < geom_->meshes.size() && node >= 0; ++m)
            if (!geom_->meshes[m].nodes.empty() && geom_->meshes[m].nodes[0] == node) { mesh = static_cast<int>(m); break; }
        if (mesh < 0 || vm.chunks.size() < 2) continue;
        VanmBinding b;
        b.mesh = mesh;
        b.vm = &vm;
        b.pos0 = std::make_unique<VanmDecoder>(&vm.chunks[0]);
        b.pos1 = std::make_unique<VanmDecoder>(&vm.chunks[0]);
        b.nrm0 = std::make_unique<VanmDecoder>(&vm.chunks[1]);
        b.nrm1 = std::make_unique<VanmDecoder>(&vm.chunks[1]);
        vanm_.push_back(std::move(b));
    }
}

bool ModelInstance::materialOverride(uint32_t mh, uint32_t ph, uint32_t pass, float out[4], uint32_t* mask) const {
    auto it = params_.find({mh, ph, pass});
    if (it == params_.end()) return false;
    for (int i = 0; i < 4; ++i) out[i] = it->second.v[i];
    *mask = it->second.mask;
    return true;
}

void ModelInstance::evaluate(const SamplingPolicy& policy) {
    if (anim_) {
        const AnimResource& a = *anim_->res;
        params_.clear();
        for (size_t t = 0; t < a.tracks.size(); ++t) {
            const AnimTrack& tr = a.tracks[t];
            int node = trackNode_[t];
            if (node >= 0 && tr.channels.size() >= 3 && !tr.channels[0].keys.empty()) {
                Vec3 tl = sampleVec3(tr, tr.channels[0], *anim_, caches_[t][0], policy);
                Quat rt = sampleQuat(tr, tr.channels[1], *anim_, caches_[t][1], policy);
                Vec3 sc = sampleVec3(tr, tr.channels[2], *anim_, caches_[t][2], policy);
                local_[node] = Mat4::trs(tl, rt, sc);
                if (tr.channels.size() > 3 && !tr.channels[3].keys.empty())
                    nodeVisible_[node] = sampleBool(tr, tr.channels[3], *anim_, caches_[t][3]) != 0;
            }
            for (size_t c = 0; c < tr.custom.size(); ++c) {
                const AnimChannel& ch = tr.custom[c];
                if (ch.keys.empty() || ch.customVals.size() < 4) continue;
                float v = sampleFloat(tr, ch, *anim_, customCaches_[t][c], policy);
                ParamValue& pv = params_[{ch.customVals[0], ch.customVals[1], ch.customVals[2]}];
                uint32_t comp = ch.customVals[3] & 3;
                pv.v[comp] = v;
                pv.mask |= 1u << comp;
            }
        }
    }
    const auto& parents = geom_->parents;
    for (size_t i = 0; i < local_.size(); ++i) {
        int p = i < parents.size() ? parents[i] : -1;
        world_[i] = (p >= 0 ? world_[static_cast<size_t>(p)] : root) * local_[i];
    }
    evaluateVertexAnim(policy);
}

void ModelInstance::evaluateVertexAnim(const SamplingPolicy& policy) {
    if (!anim_) return;
    for (auto& b : vanm_) {
        const VanmMesh& vm = *b.vm;
        MeshState& ms = meshes_[static_cast<size_t>(b.mesh)];
        const Mesh& mesh = geom_->meshes[static_cast<size_t>(b.mesh)];
        // VertexAnimator::update: f = elapsed * fps + start, clamped to the last frame
        float f = anim_->time * static_cast<float>(anim_->fps) + static_cast<float>(anim_->start);
        float last = static_cast<float>(vm.frames > 0 ? vm.frames - 1 : 0);
        if (f > last) f = last;
        if (f < 0) f = 0;
        uint32_t f0 = static_cast<uint32_t>(std::floor(f));
        uint32_t f1 = f0 + 1;
        if (f1 >= vm.frames) f1 = anim_->loop ? 0 : vm.frames - 1;
        float t = f - static_cast<float>(f0);
        ms.visible = vm.visible(f0);
        bool stepped = vm.stepped() && !policy.interpolateSteppedVertexAnim;
        std::vector<float> cp = b.pos0->frame(f0);
        std::vector<float> cn = b.nrm0->frame(f0);
        if (!stepped && t > 0.0f) {
            const std::vector<float>& p1 = b.pos1->frame(f1);
            const std::vector<float>& n1 = b.nrm1->frame(f1);
            for (size_t i = 0; i < cp.size(); ++i) {
                cp[i] = (p1[i] - cp[i]) * t + cp[i];
                cn[i] = (n1[i] - cn[i]) * t + cn[i];
            }
        }
        if (mesh.subdiv && mesh.subdiv->cageVertices * 3 == cp.size()) {
            evaluateSubdiv(*mesh.subdiv, cp.data(), cn.data(), ms.position, ms.normal);
        } else {
            ms.position = std::move(cp);
            ms.normal = std::move(cn);
        }
        ms.animated = true;
        ++ms.revision;
    }
}

}  // namespace oyster
