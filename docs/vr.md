# VR: the original's HMD path and how Oyster reproduces it

Decompiled from moxie.v2 (Ghidra project, outputs in `pearl-work/ghidra/dd.c`, `vt_hmd.c`,
`vrd.c`, `camvr.c`, `rview.c`).

## Display device (Lua `DisplayDevice`)

The natives forward to the active display device (`locator + 0x30`). With SteamVR the engine
uses `DisplayDeviceHMD` (vtable @0x18024f008) with its internal OpenVR backend:

| native | OpenVR behaviour |
|---|---|
| `getType` | 1 = `DT_HMD` |
| `hasSensorFusionOrientation` / `getSensorFusionOrientation` | true / HMD rotation as a normalised quaternion |
| `isRoomBasedVR`, `hasSensorFusionTranslation` | true |
| `getSensorFusionTranslation` | HMD position (m, tracking space) x the IPD scalar |
| `hasSensorRotation` | false |
| `setInterpupillaryDistanceScalar` / `setFovScalar` | store display +0x24 / +0x20 |

The pose is read once per frame after the eye textures are submitted (`FUN_18018d070`:
`WaitGetPoses`, quaternion from the 3x3, translation x display +0x24). Pearl's
`story.lua` sets `interpupillaryDistanceScalar = 83`: one metre of head motion = 83 world units.
`isRoomBasedVR` makes the camera rig subtract 100 units in Y (floor-origin tracking).
The story starts only when the viewer is seated: `userIsSeated` wants the camera height between
80 and 130 units (state `you_are_standing` loops otherwise).

## Eye cameras (CameraVR)

`RenderView::setupForVR` creates a `CameraVR` with a left and a right camera derived from the
story's main camera (`FUN_18018a5f0`, `FUN_18018a740`):

* eye view = translate(+-half, 0, 0) x centre view, half = IPD scalar x user IPD (m,
  `Prop_UserIpdMeters`) x 0.5; the left eye (index 0) gets +half;
* projection = the runtime's per-eye frustum at the camera's near/far planes
  (`IVRSystem::GetProjectionMatrix`), the FOV scalar is not applied on this path;
* every eye renders the whole render graph into its own targets (`RenderView::beginEye`);
  per eye the shaders get `u_viewIndex` (the colour shaders flip the film grain per eye) and
  `u_viewMid` = the eye's optical centre in NDC (lens flare).

## Oyster

`story::HmdState` (engine.h) is filled by the platform layer: OpenXR on the Quest, an emulation
on the desktop (`oyster_player --vr`: seated head at 1.2 m, IPD 63 mm, Quest-3-like asymmetric
field of view, head turned with the mouse, both eyes side by side; `--eye WxH`;
`OYSTER_DEBUG_HEAD=yaw,pitch,height` for headless runs). Differences to the original:
the pose is the one predicted for the frame's display time instead of the previous frame's,
and the optical centre is derived from the frustum (OpenVR offered lens-centre properties).
