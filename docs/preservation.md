# Preservation record

## Source installation (reference / master)

| Item | Value |
|---|---|
| Product | Google Spotlight Stories: Pearl (Steam AppID **476540**) |
| Install dir | `E:\SteamLibrary\steamapps\common\Google Spotlight Stories - Pearl` |
| Steam build | **1340090** (`LastUpdated` 1560512741 = 2019-06-14) |
| Depots | 476541 manifest `8981992635726712298`, 476542 manifest `7025063224431251032` |
| Files / bytes | **4599 files, 2 209 207 717 bytes** |
| Steam launch (from `appcache/appinfo.vdf`) | `win64/storyplayer.exe -package pearl_vrcam -NOfullscreen -res 1280 720 -msaa 2 -ssaa 1.0` (identical entry for `win32/`) |

Evidence: `steamapps/appmanifest_476540.acf`; launch options extracted from the binary
`appinfo.vdf` app record for 476540 (string values `win64/storyplayer.exe`, `-package pearl_vrcam ...`).

## Preservation copy

* Copy: `C:\Tools\pearl-master\steam-476540-build1340090\` (outside this repository).
* Made with `robocopy /E /COPY:DAT /DCOPY:T` (data, attributes, timestamps), 2026-10-03.
* Verified: `tools/manifest.py diff` source vs copy → **0 differences** in path, size, SHA-256;
  mtimes also identical.
* Every file in the copy has the **read-only** attribute set; a write test is refused.
* The original Steam installation was only ever opened for reading.

**Incident (transparency):** the first attempt to set the read-only flag failed due to shell
path quoting, and the immediately following write-protection test *appended* to
`readme.txt` **in the copy** (never the original). The file was restored from the original
with robocopy; its SHA-256 (`76f602b1…6be39`) matches the manifest, and a full re-verify of
the copy against the manifest reported 0 differences afterwards.

Re-verify at any time:

```
python tools/manifest.py verify "C:/Tools/pearl-master/steam-476540-build1340090" manifests/steam-476540-build1340090.tsv
```

## Manifests in this repo

* `manifests/steam-476540-build1340090.tsv` — path, size, SHA-256, mtime (UTC) for all 4599 files.
* `manifests/steam-476540-build1340090.classified.tsv` — same plus layer/kind classification
  (`tools/classify.py`).

## Volatile (user-state) files inside the install dir

The runtime writes into its own install directory. These files are **not shipped content**;
they reflect this user's play history and must be expected to change whenever Pearl is run
from the Steam folder:

| Path | Observed content |
|---|---|
| `log.txt` (0 B, 2025-03-25), `win32/log.txt`, `win64/log.txt` | runtime log (`[ 10:22:57 ][ storyplayer.exe ][ Sep 16 2016 ]`) |
| `libmoxieclient.tmp`, `win32/…`, `win64/…` (44 B) | unknown binary runtime state |
| `persist.lua` / `persist.chk` | `Persist=loadstring("return {  }")()` + checksum `3434787931` |
| `saves/checkpoint_1..37.dat/.tga` | story checkpoints (serialized Lua tables + 25 % thumbnails), 2019-01-05 and 2025-03-25 |
| `steam_shader_cache/` | empty |

The saves are useful later: they encode FSM state + camera-rig state per checkpoint
(see `docs/reference-capture.md`).

## Repository policy

The repository contains only our own code, manifests/hashes and documentation. No Pearl
asset, binary or bulk extraction is committed (`.gitignore` blocks the asset extensions).
Bulk derived data (JSON dumps of the Lua data, string dumps) is written to
`C:\Tools\pearl-work\` and can be regenerated with the tools at any time. A future build
system must expect the user to supply their own legally obtained installation.
