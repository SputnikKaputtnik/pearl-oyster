Oyster - Pearl for Meta Quest
=============================

A native Quest 3 port of the runtime of "Google Spotlight Stories: Pearl" (2016). It plays the
original film from YOUR OWN copy: the original story scripts, models, animation, shaders and
sound, unchanged. This package contains no part of Pearl.

You need
  - Pearl from Steam (AppID 476540, build 1340090), installed on this PC. The store page is
    region-restricted (not shown in some countries, e.g. Germany, but e.g. in the US).
  - A Meta Quest 3 (Quest 2/Pro may work, untested) in developer mode, connected by USB:
    Meta Horizon app on the phone > Devices > Developer Mode on.

Install
  1. Unpack this folder.
  2. Double-click "Install Pearl on Quest.cmd".
     It finds Pearl in your Steam libraries (or asks for the folder), checks the files,
     copies about 2.2 GB to the headset (/sdcard/Oyster/pearl), installs the app and grants it
     access to that folder. If adb is missing it offers to download Google's platform-tools.
     Put on the headset when asked and allow USB debugging.
  3. In the headset: Library > Unknown Sources > "Oyster - Pearl". Sit down; the story starts
     when you are seated, as in the original.

Options (PowerShell): install.ps1 -PearlPath <folder> -VerifyHashes -SkipData -Serial <serial>

Controls while the film plays
  A / X                  animation: original (stepped, as authored) or interpolated
  thumbstick up / down   eye height in 5 cm steps (not in the original; default +30 cm)

Settings: /sdcard/Oyster/oyster.cfg on the headset (created on the first start)
  resolution_scale = 1.30   eye image = the headset's recommended size x this
  animation = original      or interpolated
  msaa = 2                  1, 2 (as the original) or 4
  eye_height_offset = 0.30  metres; 0 = the original's eye height

The first start builds all shaders ("COMPILING SHADERS"); later starts load them in a moment.

Source, documentation and issues: https://github.com/SputnikKaputtnik/pearl-oyster
License: MIT (see LICENSE); third-party components: THIRD_PARTY.md.
Pearl (c) Google (Google Spotlight Stories). Not affiliated with Google or Meta.
