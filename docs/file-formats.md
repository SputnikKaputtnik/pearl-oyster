# File formats — initial characterization

Survey: `tools/format_probe.py` → `research/format_probe.txt`. Byte offsets are hexadecimal.
**[F]** observed · **[I]** inference · **[?]** unknown. Nothing here is a full spec yet.

## Overview and difficulty estimate

| Ext | Count | Container | Content | Difficulty | Notes |
|---|---|---|---|---|---|
| `.dds` | 2241 (1.75 GB) | standard DDS | 2027 DXT5, 197 RGBA32, 9 "ETA8", 8 ETC2; mip count field always 0 (37 files carry extra levels anyway) | **trivial** | Quest: DXT5 needs transcoding (ASTC/ETC2) or runtime decode — a *technical* re-encode, flag for preservation policy |
| `.ogg` | 19 (20 MB) | Ogg Vorbis 48 kHz | 6 mono (dialogue), 5 stereo (music), **8 × 4-channel (FOA ambisonics beds)** | trivial (decode) / medium (FOA→binaural renderer) | |
| `.shd` | 303 | Moxie header + **GLSL ES 1.00 text** (VS + FS) | permutations of 47 `.msh` masters (229 × `pearl2_color`) | **easy** | directly usable on GLES; header holds master path + flags |
| `.pfb` | 8 | Moxie material record (same layout as material block in `.mxm`) | post-effect / override materials | easy–medium | |
| `.lua` | 179 | source text | runtime + data | trivial | run as-is on LuaJIT/Lua 5.1 |
| `.mxm` | 742 (95 MB) | Moxie container, type `0xB6181A32`, version 46 | materials, meshes, transform hierarchy | **medium** | |
| `.mxa` | 953 (235 MB) | Moxie container, type `0x42A975E4`, kind 1 or 2 | kind 1 = baked per-mesh vertex animation; kind 2 = transform tracks | **medium (kind 2) / medium-hard (kind 1, quantization)** | |
| `.mxb` | 10 | Moxie container, type `0x88F89426` | Maya-style particle system (`ForceField`, `TurbulenceField3D`, collider) | **hard to match exactly** (simulation + RNG) | |
| `.fnt` | 12 | headerless, u32 count first | bitmap fonts (debug/UI) | easy, low priority | |
| `.mxhrtf` | 1 | magic `HRTF`, version 2 | biquad HRTF filters, CIPIC subject 015, order 12, FOA | medium | needed for faithful spatial audio |
| `.dat` (saves) | 37 | Lua-table text | FSM + camera rig + audio state | trivial | useful for capture |
| `.manifest` | 4 | Lua-table text | build provenance (output ← source, timestamps) | trivial | |

## Moxie binary container (shared by .mxm/.mxa/.mxb)

**[F]** All 1705 files start with u32 LE `0x0D00D135` (`35 D1 00 0D`), then a u32 type id:

| Type id | Ext | u16 @08 |
|---|---|---|
| `0xB6181A32` | `.mxm` | 46 (all 742) — version |
| `0x42A975E4` | `.mxa` | 1 (171 files, 200 MB) or 2 (782 files, 36 MB) |
| `0x88F89426` | `.mxb` | 2 (all 10) |

**[F]** Little-endian (floats appear as `00 00 80 3F` = 1.0), fields are **packed/unaligned**,
strings are u32-length-prefixed (no terminator in the count), asset references are stored as
`package\0path\0package:path\0` triples (e.g. `common\0shaders/simple_a23a3031.shd\0common:shaders/simple_a23a3031.shd`).
**[I]** The type id is a hash of the type name (the shader file names use the same 8-hex-digit
hash style).

## `.mxm` model

**[F]** header: `00 magic`, `04 typeid`, `08 u16 version=46`, `0A u16 =2`, `0C u32 0`,
`10 u16 material count` (verified: unitcube 1 "Mat_Cube", endingcard 2, Sara-kid 7 =
seven `*_MTL` records), `12 u32 =2`, `16 u32 name length` + first material name.

Material record **[F]** (Sara-kid model): name (`:SaraKid_Hair_MTL`), then *pass* entries each
naming a shader (`pearlpackage:shaders/pearl2_warp_…shd`, `pearl2_shadow_…`, `pearl2_color_…`)
and textures (`…_diffuse.dds`, `…_shadow.dds`, `…_highlight.dds`, shared
`pearlpackage:textures/toothf_1024.dds` (paper tooth) and `film_grain_512.dds`). Between the
references are small integer blocks that look like render state (values `0x0D`, `0x14`,
`0x0510`, `0x0326`, `0xFF`, `0x16`, `0x19`) **[?]**.

After the material block: mesh names (`:saraKM_pupils`, `:saraKM_eyes`, …, 300+ strings) and
then vertex/index data **[?]**. Exported engine API confirms the model contains meshes,
materials, a local-transform hierarchy (names, parent indices, properties), bind transforms,
optional skeleton/bone groups, "patch" geometry and bounds (`MOXIE::ModelResource::*` in
`research/pe/moxie.v2.win64.exports.tsv`). `buildVertexLayout(Mesh&, VertexAttributeInfo*)`
indicates a self-describing vertex layout **[I]**. Shader attributes used: `a_position`,
`a_normal`, `a_tangent`, `a_texcoord` (+ others in pearl2 shaders, to be listed).

## `.mxa` animation

**[F]** header (example `camerarig_anim01.mxa` / Sara-kid `seq1_shot20`):

| Off | Field | camerarig | sarakid shot20 | Interpretation |
|---|---|---|---|---|
| 08 | u16 kind | 2 | 1 | 1 = vertex animation, 2 = transform animation [I] |
| 0C | u32 | 0x178 = 376 | 0x141 = **321** | frame count — shot20 lasts 10.7 s = 321/30 [F match] |
| 10 | u32 | 30 | 30 | frames per second [F match] |
| 14 | u32 | 3 | 20 | track count [I] |
| 18 | u32 + str | `CameraRig:CameraRoot\|CameraRig:Camera_Skeleton\|…` | `:saraKM_pupils` | track/node name |

Kind 1 tracks are named after the model's meshes followed by a flag byte (`P`=0x50 or
`X`=0x58) and pairs of floats (1,1 · 15,15 · 0.4,0.4 · 0.5,0.5 · 0.6,0.6 …) — **[I]** per-track
quantization ranges for `MOXIE::VertexAnimQuantizer`. So Pearl's characters are played back as
**baked, quantized vertex animation**, not as a skinned rig evaluated at runtime (strong
inference; 171 files / 200 MB, all character/crowd/deforming props).
Kind 2: transform tracks by Maya DAG path, used for cameras, props, lights and the
per-shot render-graph animations.

Frame cadence is therefore explicit in the data: **30 fps** for every clip checked.
Whether the runtime interpolates between frames or steps at 30 Hz is **[?]** — this is the key
question for "keep the original animation cadence" and must be measured (Phase 2).

## `.shd` shader permutation

**[F]** `00 u32 0x00010001`, `04 u32 hash`, then u32-prefixed source-reference triple
(`common:shaders/simple.msh`), small flag block, then two GLSL ES programs as text with CRLF.
Uniform naming convention: `u_worldViewMatrix`, `u_worldViewProjMatrix`, `u_normalMatrix`,
`u_gamma`, `u_lightCount`, `u_lightPosition[4]`, `u_lightDirection[4]`, `u_lightColor[4]`,
`u_lightRangeSpotAngle[4]`. Gamma is approximated as `c*c` in `simple.msh` (comment shows
`pow(c, u_gamma)` was replaced) — must be kept bit-for-bit.

## `.pfb` post-effect / material

**[F]** first u32 is a small record count (`0x201`…`0x206` = u16 count + u16 0x02?), then
material records in the same layout as `.mxm` materials (`depth` → `common:shaders/override/depth_6522df69.shd`).
`pearlpackage:shaders/override/balloon.pfb` references `pearlpackage:textures/soft7_512_rg.dds`,
which is **not shipped** — the only unresolved runtime reference in the package
(`tools/reference_check.py`). **[?]** whether that override is ever activated.

## `.mxb` particle system

**[F]** container type `0x88F89426`, name (`SnowAFX_ParticleShape`), `materials` section
(particle shader + sprite DDS), then emitter/field blocks with names `ForceField`,
`TurbulenceField3D`, `collider`. Source format `.mxp` (manifest). Each `.mxb` has a sibling
`.lua` with its material definition (`common:shaders/particle.mtl`, `BlendMode="Transparent"`).

## `.dds` textures

**[F]** Every file has `dwMipMapCount=0` and no `DDSD_MIPMAPCOUNT` flag. By data size, 2204
files hold exactly one level; 37 DXT5 files hold more data than one level (**[I]** a mip chain
the header does not declare). Whether the runtime generates mips or samples without them is
**[?]** — this directly affects any "improved texture filtering" decision. Sizes mostly 1024², 128², 64²,
256², 512². ETC2/ETA8 files are standard DDS with those FourCCs.

## Reference integrity

`tools/reference_check.py` → `research/reference_check.tsv`: 3957 distinct `package:path`
URIs in Lua + binary assets. All resolve except build-time sources (`.msh`, `.mtl`, never
shipped) and `soft7_512_rg.dds` (see above). 382 shipped content files are not referenced by
any URI string (318 of them `pearl_vrcam/textures`, 29 `.shd`, 7 `.mxa`); many are certainly
loaded by computed names or by engine code (e.g. `common:shaders/ui/*`), so "unreferenced" ≠
"unused" until a runtime file-access trace confirms it.

`tools/package_manifest_check.py` → `research/package_manifest_check.txt`: the build manifests
are slightly stale (7 audio entries name `…_vive.ogg` / `…_ambi.ogg` files that shipped renamed
as `…_vr.ogg`; 3 dialogue OGGs and `override.lua` are not listed; shader permutations are not
tracked). Not a content gap — `audio.lua` references exactly the shipped files.
