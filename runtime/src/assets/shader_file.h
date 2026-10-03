// .shd shader permutation: master URI, GLSL ES 1.00 vertex + fragment text and the uniform table
// (layout established empirically, see tools/shd.py).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace oyster {

// Engine-supplied uniform values (semantic ids seen in the shipped shaders).
enum class Semantic : uint16_t {
    WorldMatrix = 0, InvWorldMatrix = 1, ViewMatrix = 2, InvViewMatrix = 3, ProjMatrix = 4,
    WorldViewMatrix = 6, WorldViewProjMatrix = 8, NormalMatrix = 10, BlendPalette = 12,
    Time = 13, ScreenSize = 14, AspectRatio = 15, Gamma = 16, InvGamma = 17, ViewIndex = 18,
    ViewMid = 19, LightCount = 20, LightPosition = 21, LightDirection = 22,
    LightRangeSpotAngle = 23, LightColor = 24, FogColor = 33, Material = 0xFFFF
};

struct ShaderUniform {
    std::string name;
    uint16_t cls = 0;   // 0 int, 1 float, 2 matrix, 3 sampler
    uint16_t type = 0;  // components (1..4), 5 = mat4
    uint16_t semantic = 0xFFFF;
    uint16_t count = 1;
    uint16_t size = 0, size2 = 0;
    int32_t unit = -1;  // samplers: texture unit
};

struct ShaderFile {
    std::string master;
    std::string vs, fs;
    uint32_t flags = 0, hash = 0, nparams = 0;
    std::vector<ShaderUniform> uniforms;
};

ShaderFile loadShaderFile(const std::vector<uint8_t>& data);

}  // namespace oyster
