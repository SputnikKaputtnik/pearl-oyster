# Reference capture plan

Goal: ground-truth data from the original SteamVR build to compare any new runtime against,
without modifying the original installation. All runs use a **separate, writable working copy**
(`C:\Tools\pearl-work\run\`, created from the read-only master) so the master and Steam folder
never change. Every run is announced beforehand (what, how many launches, how long).

## Built-in engine facilities (no patching needed)

| Facility | Source | Use |
|---|---|---|
| `-record <file>` / `-replay <file>` | `common/scripts/replay.lua` → native `camera:record/replay`, state markers via `Replay:setMarker(fsm:state)` | record a real headset session's camera poses once; replay it deterministically |
| `-fixedtimestep`, `-timescale` | CLI strings; Lua `Time.setFixedTimeStep(1000/fps)` | decouple story time from wall clock |
| `-rendertodisk`, `-playblast`, `-hqoffline`, `-supersample`, `-aasharpness` | CLI strings; `RenderManager.enableRenderToDisk(on, path, startFrame, w, h)` | write frames to disk (`screencapture[####].tga` default) |
| `-loadfile <save>` / `-checkpoint` / `-generateCheckpoints` | `Story:start/restore`, `saves/*.dat` | jump to a shot without playing from the start |
| `-latlong`, `-mono`, `-stereoscopic` | CLI | monoscopic / equirectangular views for comparison |
| `-stats`, `-perf`, `-dumpinfo` | CLI; `frames::*` stat names | frame timing statistics |
| Log | `[old]->[new]` FSM transitions, `****fsm:state****` per segment | state timing |

Exact argument syntax of these switches is **[?]** and is the first thing to establish
(Ghidra on the CLI parser in moxie.v2, then a short announced test run).

## Recommended reference scenes

| Scene (state) | Why |
|---|---|
| `start_standing` … `seq1_calibrate` | seated/standing detection, calibration, splash/fade |
| `S01_10_animB` (15.4 s) | long continuous shot: characters + car interior, timing drift check |
| `seq1_shotWait` + both branches (`S01_10_anim`, `S01_10_anim2`) | gaze trigger semantics |
| `seq2_shot40` / `seq2_shot50` | snow particles (`SnowAFX`/`SnowPlugAFX`) — particle determinism |
| `seq3_shot10_all_part1` → `310_Frame` → part2/part22 | camera-animation branch, render-graph switch |
| `Seq5_shot70_Part1` → `Seq1_shot60_flashback` | flashback, render-graph and lighting change |
| `seq6_shot30_Play` (22.8 s) | longest shot, music sync |
| `EndingCreditTemp` | 2D card, text, fade |

## What to record and how

| Quantity | Method |
|---|---|
| Camera pose (per frame) | `-record` file (native format **[?]**) + our own logger in a proxy `openvr_api.dll` (logs `WaitGetPoses` results and compositor submit timestamps) |
| Screenshots | `-rendertodisk` with fixed timestep at fixed resolution, mono and per-eye; additionally RenderDoc single-frame captures (GL) for exact render-target contents |
| Frame timing | proxy openvr_api.dll timestamps for `WaitGetPoses`/`Submit`; `-stats` output |
| Animation timing | fixed-timestep renders at 90 fps vs 30 fps of the same shot → determines whether vertex animation is interpolated or stepped; FSM log timestamps |
| Audio timing | WASAPI loopback capture of the mix during a recorded run, aligned to the FSM log; offline compare with decoded OGG start times |
| Render resolution | proxy logs `GetRecommendedRenderTargetSize` + submitted texture size; RenderDoc texture dims |
| Anti-aliasing | RenderDoc: MSAA sample count of render targets for `-msaa 2`; resolve path; `-ssaa` scaling |

The proxy-DLL approach changes only the working copy (one DLL swapped next to the exe);
original files remain untouched. It is optional — RenderDoc/apitrace and the engine's own
switches may be sufficient.

Saves already present (`saves/checkpoint_1..37.dat`, from this user's past sessions) contain
FSM state + camera-rig state per checkpoint, e.g. checkpoint 1 = `Main:seq1_calibrate` at
0.116 s with render graph `sq01s10VRTrig::…spotlightPostEffectsShape`.
