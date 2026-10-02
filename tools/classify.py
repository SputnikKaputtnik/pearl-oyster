#!/usr/bin/env python3
"""Classify every file of a preservation manifest by layer and asset type.

Usage: classify.py <manifest.tsv> <out.tsv>
Adds two columns to each manifest row:
  layer : runtime-moxie | runtime-thirdparty | steam-redist | engine-common | story-framework |
          pearl-shared | pearl-content | user-state | build-metadata
  kind  : executable, dll, lua, model, animation, texture, audio, shader, posteffect,
          particle, font, hrtf, manifest, save, ...
Rules are explicit and evidence-based (see docs/architecture.md); unknowns stay 'unknown'.
"""
import collections
import sys

KIND_BY_EXT = {
    "exe": "executable", "dll": "dll", "lua": "lua-script", "mxm": "model (Moxie .mxm)",
    "mxa": "animation (Moxie .mxa)", "mxb": "particle-system (Moxie .mxb)", "dds": "texture (DDS)",
    "ogg": "audio (Ogg Vorbis)", "shd": "shader (GLSL ES permutation)", "pfb": "material/post-effect (.pfb)",
    "fnt": "bitmap-font", "mxhrtf": "hrtf-filters", "manifest": "build-manifest", "dat": "save-state",
    "tga": "save-thumbnail", "chk": "persist-checksum", "tmp": "runtime-temp", "vdf": "steam-installscript",
    "cmd": "steam-installscript", "txt": "text", "lck": "build-lockfile", "ds_store": "macos-junk",
}
MOXIE_DLLS = {"storyplayer.exe", "moxie.v1.shared.windows.dll", "moxie.v2.shared.windows.dll"}
THIRD_PARTY = {"lua51.dll": "LuaJIT", "openvr_api.dll": "Valve OpenVR", "freeimage.dll": "FreeImage 3.17.0",
               "libcurl.dll": "curl 7.39.0", "libeay32.dll": "OpenSSL 1.0.1g", "ssleay32.dll": "OpenSSL 1.0.1g",
               "libssh2.dll": "libssh2 1.4.3", "pthreadvc2.dll": "pthreads-win32 2.9.1",
               "pvrtexlib.dll": "PowerVR PVRTexLib 4.14.6", "msvcp140.dll": "MSVC 2015 CRT",
               "vcruntime140.dll": "MSVC 2015 CRT", "concrt140.dll": "MSVC 2015 CRT"}


def classify(path):
    low = path.lower()
    name = low.rsplit("/", 1)[-1]
    ext = name.rsplit(".", 1)[-1] if "." in name else ""
    kind = KIND_BY_EXT.get(ext, "unknown")
    top = low.split("/", 1)[0] if "/" in low else ""
    note = ""
    if top in ("win32", "win64"):
        if name in MOXIE_DLLS:
            layer = "runtime-moxie"
        elif name in THIRD_PARTY:
            layer, note = "runtime-thirdparty", THIRD_PARTY[name]
        else:
            layer = "user-state"
    elif top == "_commonredist":
        layer = "steam-redist"
    elif top == "common":
        layer = "engine-common"
    elif top == "story":
        layer = "story-framework"
    elif top == "pearlpackage":
        layer = "pearl-shared"
    elif top == "pearl_vrcam":
        layer = "pearl-content"
    elif top in ("saves", "steam_shader_cache") or name in ("log.txt", "persist.lua", "persist.chk",
                                                            "libmoxieclient.tmp"):
        layer = "user-state"
    else:
        layer = "install-root"
    if name in ("log.txt", "libmoxieclient.tmp"):
        layer, kind = "user-state", "runtime-log/temp"
    if name == "persist.lua":
        kind = "persist-table"
    if ext in ("manifest", "lck") or name == "modelmapping.txt":
        layer = layer + "/build-metadata"
    if kind == "lua-script" and "/scripts/data/" in low:
        kind = "lua-data (Spotlight Tools export)"
    if kind == "lua-script" and "/rendergraphs/" in low:
        kind = "lua-data (render graph)"
    if kind == "lua-script" and "/particles/" in low:
        kind = "lua-data (particle material)"
    return layer, kind, note


def main(argv):
    rows, stats = [], collections.Counter()
    sizes = collections.Counter()
    for line in open(argv[1], encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        path, size, digest, mtime = line.rstrip("\n").split("\t")
        layer, kind, note = classify(path)
        rows.append(f"{path}\t{size}\t{digest}\t{layer}\t{kind}\t{note}")
        stats[(layer, kind)] += 1
        sizes[(layer, kind)] += int(size)
    with open(argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write("# path\tsize\tsha256\tlayer\tkind\tnote\n" + "\n".join(rows) + "\n")
    for (layer, kind), n in sorted(stats.items()):
        print(f"{layer:32s} {kind:38s} {n:5d} {sizes[(layer, kind)] / 1e6:10.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
