// Records shared by Moxie containers: Uri, Material (+ passes, parameters, render state).
// Layouts: docs/file-formats.md (verified against all shipped files by tools/mxm.py).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/reader.h"

namespace oyster {

constexpr uint32_t kMoxieMagic = 0x0D00D135;
constexpr uint32_t kTypeModel = 0xB6181A32;
constexpr uint32_t kTypeAnim = 0x42A975E4;
constexpr uint32_t kTypeParticles = 0x88F89426;

struct Uri {
    uint8_t kind = 0;
    uint32_t hash = 0;
    std::string uri;  // "package:path" (third part of the stored triple)
};
Uri readUri(Reader& r);

// Engine enum index table @0x1802322e0 (RenderDeviceGL::setRenderStateCached).
enum class GLEnumIndex : uint32_t {
    Zero, One, SrcColor, SrcAlpha, DstColor, DstAlpha, OneMinusSrcColor, OneMinusSrcAlpha,
    OneMinusDstColor, OneMinusDstAlpha, SrcAlphaSaturate, ConstantColor, ConstantAlpha,
    FuncAdd, FuncSubtract, FuncReverseSubtract, Min, Max, Never, Always, Less, LEqual, Greater,
    GEqual, NotEqual, Keep, Replace, Invert, Incr, Decr, Front, Back, FrontAndBack, CW, CCW,
    Point, Line, Fill, Count
};
uint32_t glEnumFromIndex(uint32_t index);  // -> GLenum value

struct RenderState {  // MOXIE::RenderState, 0x94 bytes
    bool blend = false;
    uint32_t blendSrcRGB = 1, blendSrcAlpha = 1, blendDstRGB = 0, blendDstAlpha = 0;
    float blendColor[4] = {0, 0, 0, 0};
    uint32_t blendEqRGB = 13, blendEqAlpha = 13;
    bool depthTest = true, depthWrite = true;
    uint32_t depthFunc = 20;
    float depthRange[2] = {0, 1};
    bool scissor = false;
    int32_t scissorRect[4] = {0, 0, 0, 0};
    bool stencil = false;
    uint32_t stencilMask = 0, stencilFunc = 19, stencilRef = 0, stencilReadMask = 0;
    uint32_t stencilOps[3] = {25, 25, 25};
    bool colorMask[4] = {true, true, true, true};
    bool cull = true;
    uint32_t cullFace = 31;
    uint32_t polygonMode = 37;
    bool polygonOffset = false;
    float polygonOffsetFactor = 0, polygonOffsetUnits = 0;
    bool dither = false;
    float f90 = 1.0f;
    // RenderState::DEFAULT (@0x18030ccd0): no blend, no depth test, depth write, no cull
    static RenderState engineDefault() {
        RenderState s;
        s.depthTest = false;
        s.cull = false;
        s.stencilMask = 255;
        s.stencilReadMask = 255;
        s.stencilFunc = 22;  // GREATER
        return s;
    }
};
RenderState readRenderState(Reader& r);

struct Sampler {  // 28 bytes: 6 x u32 + f32 (Pearl: all (1,1,0,0,0,0,1.0))
    uint32_t minFilter = 1, magFilter = 1, mipFilter = 0, wrapS = 0, wrapT = 0, wrapR = 0;
    float f = 1.0f;
};

struct MaterialParam {
    uint16_t type = 0;          // 0 = constant floats, else texture
    uint32_t nameHash = 0;      // FNV-1a of the uniform name without "u_"
    uint16_t uniformIndex = 0;  // index into the shader's uniform table
    uint32_t size = 0;
    uint32_t u14 = 0;
    uint8_t u1c = 0;
    std::vector<float> values;  // constant
    Uri texture;                // texture
    uint16_t texKind = 0;
    std::vector<Sampler> samplers;
};

struct MaterialPass {
    Uri shader;
    RenderState state;
    uint32_t ud4 = 0;
    std::vector<MaterialParam> params;
};

struct Material {
    std::string name;
    uint32_t nameHash = 0;
    std::vector<MaterialPass> passes;
};
Material readMaterial(Reader& r);
// .pfb material library: u8 count + Material[count]
std::vector<Material> loadMaterialLibrary(const std::vector<uint8_t>& data);

}  // namespace oyster
