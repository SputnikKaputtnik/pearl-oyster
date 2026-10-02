# Pearl preservation ("Project Oyster")

Preservation and native-porting research for *Google Spotlight Stories: Pearl* (2016,
real-time VR animated short). Long-term goal: run Pearl faithfully and natively on modern
standalone headsets (Meta Quest 3, OpenXR), without remaking it.

This repository contains **only our own tools, manifests (hashes) and documentation** — no
Pearl assets or binaries. Bring your own legally obtained installation (Steam AppID 476540).

## Status

Phase 1 (forensics) — complete for the Steam build 1340090.

| Document | Contents |
|---|---|
| [docs/preservation.md](docs/preservation.md) | reference installation, preservation copy, verification, user-state files |
| [docs/architecture.md](docs/architecture.md) | runtime vs content layers, native engine services, how Lua drives the story, camera path |
| [docs/launch-chain.md](docs/launch-chain.md) | Steam → exe → engine → Lua → scene loading |
| [docs/runtime-observations.md](docs/runtime-observations.md) | observed runs: modules, file access, writes, RenderDoc result |
| [docs/runtime-analysis.md](docs/runtime-analysis.md) | PE analysis: graphics, VR, audio, Lua, CLI switches |
| [docs/file-formats.md](docs/file-formats.md) | `.mxm`, `.mxa`, `.mxb`, `.shd`, `.pfb`, DDS, OGG, HRTF — first characterization |
| [docs/reference-capture.md](docs/reference-capture.md) | how to record ground truth from the original |
| [docs/quest-port-plan.md](docs/quest-port-plan.md) | route assessment and Phase 2 plan |

## Layout

```
docs/        findings (facts / inferences / unknowns marked)
manifests/   SHA-256 manifests of the reference installation (+ classified inventory)
tools/       small, read-only analysis tools (Python 3.11; pefile, lupa)
research/    curated analysis outputs (symbol tables, probes, checks)
```

Bulk derived data (string dumps, JSON dumps of the Lua data) is regenerated into
`C:\Tools\pearl-work\` and never committed.

## Tools

| Tool | Purpose |
|---|---|
| `tools/manifest.py create/verify/diff` | path/size/SHA-256 manifest of an installation |
| `tools/classify.py` | layer/kind classification of a manifest |
| `tools/pe_inspect.py` | PE headers, imports, exports, version info |
| `tools/demangle_exports.py` | demangled C++ export table (Windows dbghelp) |
| `tools/source_paths.py` | build source paths embedded in binaries |
| `tools/format_probe.py` | header survey per extension (Moxie container ids, DDS FourCC, Ogg) |
| `tools/lua_data_dump.py` | evaluate the story's Lua data files in a sandbox → JSON |
| `tools/lua_requires.py` | Lua `require` dependency tree / Mermaid graph |
| `tools/reference_check.py` | do all `package:path` references resolve? |
| `tools/package_manifest_check.py` | Spotlight build manifests vs files on disk |
| `tools/observe_run.py` | launch + observe a run from outside (modules, open files, clean WM_CLOSE) |
| `tools/procmon_filter.py` | reduce a Procmon CSV to one process, per-file access summary |
| `tools/rdoc_capture.py` | RenderDoc inject + triggered captures (qrenderdoc --python); not usable for Moxie |

Requirements: `pip install pefile lupa`.

## Ground rules

Preservation, not remake: no changes to animation timing, cadence, choreography, lighting,
materials, shaders (except technically equivalent ports), audio, story logic or triggers.
Allowed improvements: render resolution, anti-aliasing, texture filtering, display output.
No AI upscaling or asset replacement.
