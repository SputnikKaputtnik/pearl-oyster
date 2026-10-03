// oyster_check: loads every asset of an installation with the C++ loaders and prints totals
// that can be compared with the Python reference tools (tools/mxm_geometry.py --check,
// tools/vanm_decode.py --check, tools/shd.py --check).
//
// Usage: oyster_check <install root> [--vanm] [--xform]
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "assets/anim.h"
#include "assets/dds.h"
#include "assets/model.h"
#include "assets/shader_file.h"
#include "core/pkgfs.h"
#include "scene/animation.h"

namespace fs = std::filesystem;
using namespace oyster;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: oyster_check <install root> [--vanm] [--xform]\n");
        return 2;
    }
    bool doVanm = false, doXform = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--vanm") doVanm = true;
        if (std::string(argv[i]) == "--xform") doXform = true;
    }
    std::map<std::string, int> ok, bad;
    uint64_t meshes = 0, verts = 0, tris = 0, subdiv = 0, remapMismatch = 0, idxBad = 0;
    uint64_t vanmMeshes = 0, vanmFrames = 0, vanmOverrun = 0;
    uint64_t xformChecked = 0, xformBad = 0;
    double xformWorst = 0;
    std::map<std::string, std::shared_ptr<ModelResource>> models;

    for (auto& e : fs::recursive_directory_iterator(fs::u8path(argv[1]))) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().u8string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string path = e.path().u8string();
        try {
            if (ext == ".mxm") {
                auto data = readFile(path);
                std::shared_ptr<ModelResource> m = loadModel(data);
                for (const Mesh& me : m->meshes) {
                    ++meshes;
                    // stored (pre-instancing) counts, comparable with the Python tools
                    verts += me.vertices.count / (me.instanceCount + 1);
                    tris += me.indices.size() / 3 / (me.instanceCount + 1);
                    for (uint16_t ix : me.indices)
                        if (ix >= me.vertices.count) { ++idxBad; break; }
                    if (me.subdiv) {
                        ++subdiv;
                        if (me.subdiv->remap.size() != me.vertices.count) ++remapMismatch;
                    }
                }
                models[fs::relative(e.path(), fs::u8path(argv[1])).generic_u8string()] = m;
                ok["mxm"]++;
            } else if (ext == ".mxa") {
                auto data = readFile(path);
                auto a = loadAnim(data);
                if (doVanm && a->vertexAnim) {
                    for (const VanmMesh& vm : a->vertexAnim->meshes) {
                        if (vm.chunks.empty() || vm.chunks[0].vertexCount == 0) continue;
                        ++vanmMeshes;
                        for (const VanmChunk& ch : vm.chunks) {
                            VanmDecoder d(&ch);
                            for (uint32_t f = 0; f < ch.frames; ++f) d.frame(f);
                            if (d.maxDataBit() > ch.dataBits) ++vanmOverrun;
                        }
                        vanmFrames += vm.chunks[0].frames;
                    }
                }
                ok["mxa"]++;
            } else if (ext == ".shd") {
                loadShaderFile(readFile(path));
                ok["shd"]++;
            } else if (ext == ".dds") {
                loadDDS(readFile(path));
                ok["dds"]++;
            }
        } catch (const std::exception& ex) {
            bad[ext]++;
            if (bad[ext] <= 5) std::printf("ERROR %s: %s\n", path.c_str(), ex.what());
        }
    }

    // Transform convention check: for every model with a matching animation of the same shot,
    // frame-0 TRS of a track must reproduce the node's stored local matrix only where the
    // node is not animated before frame 0 -- we just report the distribution.
    if (doXform) {
        PackageFS pfs(argv[1]);
        for (auto& e : fs::recursive_directory_iterator(fs::u8path(argv[1]) / "pearl_vrcam" / "anims")) {
            std::string name = e.path().filename().u8string();
            if (name.rfind("moxiecamera", 0) == 0) continue;
            // the model of an actor anim "<model>_<trigger>_<state>.mxa": find longest model prefix
            std::string stem = e.path().stem().u8string();
            std::shared_ptr<ModelResource> model;
            size_t best = 0;
            for (auto& kv : models) {
                std::string ms = fs::u8path(kv.first).stem().u8string();
                if (kv.first.rfind("pearl_vrcam/models/", 0) == 0 && stem.rfind(ms + "_", 0) == 0 && ms.size() > best &&
                    !kv.second->localXforms.empty()) {
                    best = ms.size();
                    model = kv.second;
                }
            }
            if (!model) continue;
            auto a = std::shared_ptr<AnimResource>(loadAnim(readFile(e.path().u8string())));
            AnimationPlayback pb;
            pb.setup(a, 0, -1, false, true);
            for (const AnimTrack& tr : a->tracks) {
                int node = model->findNode(tr.name);
                if (node < 0 || tr.channels.size() < 3 || tr.channels[0].keys.empty()) continue;
                ChannelCache c0, c1, c2;
                SamplingPolicy pol;
                Mat4 m = Mat4::trs(sampleVec3(tr, tr.channels[0], pb, c0, pol), sampleQuat(tr, tr.channels[1], pb, c1, pol),
                                   sampleVec3(tr, tr.channels[2], pb, c2, pol));
                const Mat4& l = model->localXforms[static_cast<size_t>(node)];
                double d = 0;
                for (int i = 0; i < 16; ++i) d = std::max(d, static_cast<double>(std::fabs(m.m[i] - l.m[i])));
                ++xformChecked;
                if (d > 1e-2) ++xformBad;
                if (d > xformWorst && d < 1e-2) xformWorst = d;
            }
        }
    }

    for (auto& kv : ok) std::printf("%s ok %d\n", kv.first.c_str(), kv.second);
    for (auto& kv : bad) std::printf("%s FAILED %d\n", kv.first.c_str(), kv.second);
    std::printf("meshes %llu, vertices %llu, triangles %llu, index errors %llu\n", (unsigned long long)meshes,
                (unsigned long long)verts, (unsigned long long)tris, (unsigned long long)idxBad);
    std::printf("subdiv meshes %llu, remap size != vertex count: %llu\n", (unsigned long long)subdiv,
                (unsigned long long)remapMismatch);
    if (doVanm)
        std::printf("VANM meshes %llu, frames %llu, data overruns %llu\n", (unsigned long long)vanmMeshes,
                    (unsigned long long)vanmFrames, (unsigned long long)vanmOverrun);
    if (doXform)
        std::printf("frame-0 TRS vs stored local matrix: %llu checked, %llu differ > 0.01, worst match %.6g\n",
                    (unsigned long long)xformChecked, (unsigned long long)xformBad, xformWorst);
    return bad.empty() ? 0 : 1;
}
