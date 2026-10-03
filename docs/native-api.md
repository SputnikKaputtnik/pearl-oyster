# Native Lua API of moxie.v2 (Steam build 1340090)

Extracted statically from the engine DLL: every `luaL_Reg` table (`tools/lua_reg_scan.py`) and
the `LUtil::bindType_` call that registers it (Ghidra, `DecompileRefsTo.java`). This is the
specification of the native layer the replacement player has to provide.

## Object model (`LUtil::bindType_` @0x18011aea0, `LUtil::pushNativeType_` @0x18011b150)

* Every type is a **global table** `<Type>` holding its functions (`luaL_register`), plus a
  registry metatable `<meta>` with `__index = <Type>`, `__name = "<Type>"`. With a base type,
  `<Type>`'s own metatable has `__index = <Base>` (inheritance, e.g. `Actor` → `Transform`).
* Native objects are pushed as **plain Lua tables** `{__object = lightuserdata(handle)}` with
  metatable `<meta>`, cached per handle in a weak table — so scripts can attach their own fields
  (`ob.def`, `ob.name`). Handles are 32-bit ids (index:16 | generation:16), not pointers.
* Value types are Lua classes from `common/scripts/math`: natives push a fresh table whose
  metatable is the global class table (`Vector3`, `Quaternion`, `SRT` with
  `position/rotation/scale`), after setting `<Class>.__index = <Class>`; natives read vectors
  from fields `x, y, z, w`.
* `Actor.getBoneWorld(i)` = actor transform (SRT) combined with the node's model-space matrix.

## Types and functions

Object: table 18051d2b0 not scanned
### Memory  (luaL_Reg @0x180305c70)
  numBytesAllocated, numBlocksAllocated, peakBlocksAllocated, peakBytesAllocated, totalBlocksAllocated, totalBytesAllocated, getLuaAllocatedBytes, getLuaAllocationCount, getLuaTotalAllocCount
### Stat  (luaL_Reg @0x180305c10)
  create, destroy, set, get, find
### StatLogger  (luaL_Reg @0x180305be0)
  create, destroy
### System  (luaL_Reg @0x180305d10)
  exit, gc, breakIt, getEnv, getPendingResourceCount, flushPendingResources, createResourceGroup, debugDump, exists, deviceString, modelString, patchNumberString, buildNumberString, majorNumberString, minorNumberString, versionString, isOSDSupported, flushPersist, isStatManagerEnabled, getCPUMemory, getMaxCPUMemory, handleToString, resetWaitCounter, fetchWaitCounter, enableOverlayManger, benchmarkMethodCall, benchmarkMethodSend, benchmarkMethodRecv, pauseAllAudio, resumeAllAudio, setNiceness, loadInternalModule, reset
### Log  (luaL_Reg @0x1803060f0)
  fatal, error, warning, message
### FPS  (luaL_Reg @0x180305f30)
  min, max, average, rawMS, rawFPS, string
### Time  (luaL_Reg @0x180305fa0)
  raw, rawElapsedSeconds, rawElapsedMilliseconds, dt, seconds, milliseconds, string, pause, resume, isPaused, step, setTimeScale, getTimeScale, timescale, setFixedTimeStep, getFixedTimeStep, setErrorTime, getErrorTime, setErrorCorrectParams, getErrorCorrectParams
### HitchProfiler  (luaL_Reg @0x180305ba0)
  sampleTime, addSample, resetFrameCounter
### ResourceGroup  (luaL_Reg @0x180305b40)
  addResource, prefetch, getCount, destroy, status
### FileSystem  (luaL_Reg @0x180306160)
  getFilesInDirectory, mkdir
### StoryManager  (luaL_Reg @0x180309fa0)
  getFSM, sendEvent, canPrefetchState
### StoryFSM  (luaL_Reg @0x180309f20)
  getName, getState, setState, prefetchState, downloadState, canPrefetchState, destroy
### Scene  (luaL_Reg @0x180306cf0)
  create, destroy, createTransform, createActor, createCamera, createParticleEmitter, createParentedParticleEmitter, createLight, createProjector, createDepth, createVideoCubePlayer, createAtmospheric, render, update, pick, getRoot, inspect, find, setLocked, setActiveSegment
### Transform  (luaL_Reg @0x180306f60)
  getName, setTranslation, getTranslation, setPosition, getPosition, setRotation, getRotation, getRollAngle, getPitchAngle, getYawAngle, setScale, getScale, setLocal, getLocal, setWorld, getWorld, getWorldAsArray, getWorldBounds, addComponent, addChild, removeChild, getNumChildren, getParent, setVisibility, isVisible, setActive, isActive, setViewFlags, getViewFlags, destroy, setDrawDebug, setRenderDebug, forceWorldUpdate, record, replay, isRecording, isReplaying, setMarker
### Actor  (luaL_Reg @0x180304f70)
  addFlipbookDriver, addFlipbook, addAnimation, findMaterial, listMaterials, getMaterialCount, getMaterialName, createMaterialInstance, createMaterialInstanceByIndex, listTransforms, listProperties, setBoneLocal, setBoneVisibility, getBoneVisibility, getBoneLocal, getBoneModel, getBoneWorld, getCustomAttribute, getCustomMaterialAttribute, getBoneIndex, setBoneRadius, getBoneRadius, addAttachment, removeAttachment, inspectSkeleton, addPriorityLight, removePriorityLight
### Camera  (luaL_Reg @0x180305860)
  setPerspective, setOrthographic, setProjectionMatrix, getProjectionMatrix, getViewMatrix, setLookAt, getFrustum, reshape
### Animation  (luaL_Reg @0x180305130)
  setRate, getRate, setPaused, isPaused, rewind, ffwd, setTime, getTime, setFrame, getFrame, getStartFrame, getStopFrame, setProgress, getProgress, getDuration, getFramesPerSecond, getNumFrames, getWeight, setWeight, fadeIn, fadeOut, setInterpolate, isInterpolated, isPlaying, isLooping, setMaxLoopCount, isDone, setFixedTimeStep, getFixedTimeStep, setFixedFrameStep, getFixedFrameStep, stepForward, stepBackward, play, stop, crossfade, destroy
### Material  (luaL_Reg @0x180306470)
  reset, setParameterTexture, setParameterColor, setParameterVec4, setParameterVec3, setParameterVec2, setParameterScalar, setRenderGroup, getIndexOfParameter, getParameterColor, getParameterVec4, getParameterVec3, getParameterVec2, getParameterScalar
### Light  (luaL_Reg @0x1803063e0)
  setType, setWrap, setRange, setSpotAngle, setColor, setPriority, inspect, getId
### Projector  (luaL_Reg @0x1803066a0)
  setProjectionPerspective, setOrthographicPerspective, setShadowMapping, setColor, setCookie
Depth: table 180306140 not scanned
### Atmospherics  (luaL_Reg @0x180305390)
  destroy, createFog
### Fog  (luaL_Reg @0x180306270)
  setFogType, setFogColor, setFogStart, setFogEnd, setFogParams, destroy
### TextureAnimationManager  (luaL_Reg @0x180306f20)
  addFlipbook, removeFlipbook, playFlipbook
### VideoCubePlayer  (luaL_Reg @0x180307200)
  setVideo, setSound, close, setCamera, setMaterial, setLooping, setDebugTimeline, setAdaptiveQuality, setQualityLevel, setDebugMode, setColor, isPlaying, isPaused, getFrame, getQualityLevel, getDebugMode, getColor, prefetch, play, pause, stop, destroy
### TrackingCamera  (luaL_Reg @0x1803058f0)
  create, getCamera, getSensorRotation, addTrackingObject, removeTrackingObject, getTrackedObject, startSensorClient, stopSensorClient, startSensorServer, getServerConnectionCount, sendSensorData, enableInput, setBoom, lookAtHeadingDegrees, setProjectiveOffset, setProjectiveRemappingStrength, setPitchDownClamp, enablePitchLimit, setTargetCentering, setIsTCLEnabled, getTCLDebugStrength, setGlobalTrackingStrength, setPrecisionFilterMinMax, getCameraVersion
### ParticleEmitter  (luaL_Reg @0x180306560)
  getMaterial, setEmitRate, getEmitRate, setTimeScale, getTimeScale, setColor, getColor, reset, destroy, setParticleEnable, getParticleEnable
Texture: table 180306f00 not scanned
RenderTarget: table 180306ac0 not scanned
### PostEffect  (luaL_Reg @0x180306620)
  getTexture, setParameters, setEnabled, destroy
### RenderView  (luaL_Reg @0x180306ae0)
  setFlags, setClearBits, setClearColor, setCamera, isActiveCamera, setSuperSampleRate, setRenderTargetScale, setRenderTarget, getRenderTarget, getPostOutputTexture, addPostEffect, clearPostEffects, setClipRect, getAlphaRange, setAlphaRange, setMaterialOverrides, project, unproject, hitTest, saveScreenshot, resize, destroy, toggleDisplayMode, setInterpupillaryDistance, setFocalLength, setLensCorrection, setInterpupillaryDistanceScalar, setLensSeparation, setFovScalar
### Renderer  (luaL_Reg @0x1803068c0)
  setClearColor, clear, createView, display, drawText, setRenderDebug, getRenderDebug
RenderGraph: table 180306940 not scanned
RenderGraphInstance: table 180306960 not scanned
RGSceneNode: table 180306aa0 not scanned
### RenderManager  (luaL_Reg @0x180306980)
  setupRenderGraphDisplay, resize, getRenderGraphByIndex, getRenderGraphByName, getMainRenderview, createRenderGraph, createRenderGraphInstance, destroyRenderGraph, setActiveRenderGraph, configureDisplay, draw, enableRenderToDisk, saveScreenshot, captureThumbnail, getDisplayType, setDisplayType, reset
### RenderDebug  (luaL_Reg @0x180306700)
  setEnabled, isEnabled, text, label, point, line, axes, plane, sphere, circle, reticle, solidSphere, solidPlane, solidCube, solidTorus, solidCone, solidCylinder, solidCapsule, aabb, ray, frustum, billboard, curve, setRenderGroupFilter, setMaxRenderModels, enableDrawSortOrder, disableDrawSortOrder
### FlipbookSampler  (luaL_Reg @0x180306190)
  create, destroy, setFlipbook, setFrame, setTime, getTexture, getNumTextures, getExposureFrame, getNumFrames, setLooping, getAlphaCropping, startPlayingFrom, setMaterial
### DeprecatedSound  (luaL_Reg @0x180305420)
  setPlayState, setSeekPosition, setVolume, setVolumeOverTime, setPanBalance, setAmbBinaural, setAmbPan, setRate, setMute, setLooping, getDuration, getNumChannels, getSeekPosition, getPlaybackTime, getPlayState, getLooping, getVolume, getPanBalance, getRate, getMute, play, pause, stop, mute, unmute, isPlaying, isPaused, isMuted, setPan, getPan
### DeprecatedAudioEmitter  (luaL_Reg @0x1803053c0)
  create, destroy, addSound, removeSound, setHRTF
### AudioManager  (luaL_Reg @0x180305610)
  pause, resume, setHRTFUri, setHRTF, setListenerPosition, setListenerRotation, setListenerVelocity, setPlayerResetMode, setResourceResidentThreshold, setMasterVolume, addEmitter, setEmitterTag, destroyEmitter, setEmitterPosition, setEmitterRotation, setEmitterVelocity, setEmitterEffectParameter, setEffectParameter, addSound, setSoundTag, getSoundTag, removeSound, playSound, stopSound, setSoundVolume, setSoundPan, setSoundTimePosition, getSoundTimePosition, getSoundIsPlaying, setSoundLoops, getSoundVolume, getSoundPan, getSoundTimePosition, setSoundQuantization, prerollSound, getEmitterObject
### Input  (luaL_Reg @0x1803062e0)
  isKeyDown, isKeyUp, anyKeyDown, anyKeyUp, isModifierDown, isModifierUp, isMouseMoving, getMouseButton, getMouseDblClick, getMousePosition, getMouseDelta, getMouseWheelDelta, getDeviceRotation, getTouchCount, getTouch
### Sensor  (luaL_Reg @0x180306e40)
  isActive, setActive, getStatus, setSensorFusionEnabled, getSensorFusionEnabled, getSensorFusionOrientation, setSensorFusionLowpassFilterEnabled, setSensorFusionLowpassFilterEnable, setSensorFusionLowpassFilterCurve, pause, resume
### DisplayDevice  (luaL_Reg @0x180305a80)
  getType, hasSensorRotation, getSensorRotation, hasSensorFusionOrientation, getSensorFusionOrientation, isRoomBasedVR, hasSensorFusionTranslation, getSensorFusionTranslation, setInterpupillaryDistanceScalar, setFovScalar, setSplashPath
### Rpc  (luaL_Reg @0x180306cc0)
  sendCommand, sendStateChanged
### Profiler  (luaL_Reg @0x180306670)
  enableTimedSampling, enableResourceTiming
### Trigger  (luaL_Reg @0x1803071d0)
  evaluateLookAtTriggerWithParent, evaluateLookAtTriggerWithoutParent
Query: table 180309d70 not scanned
### Calendar  (luaL_Reg @0x180309a50)
  getTimeZone, getTimeZoneExact, getDate, getLocalTime, getGMTTime, getDateDifferenceInDays, getWallClockTime
### Scheduler  (luaL_Reg @0x180309d90)
  schedule, requestFrame, cancel
### PlatformUtils  (luaL_Reg @0x180309cd0)
  viewUrl, getLocale, collapseNotificationsPanel, isKeyguardLocked, vibrate
### Avatar  (luaL_Reg @0x180309b60)
  restart, getUniqueId, getByeByeDontShowAgainPreference, setOverlayFrame, setOverlayAlpha, setOverlayTouchable, setWindowOrientation, setWindowKeepScreenOn, setWindowLockOrientation, setEnableActivity, exitDoodle, showLibrary, showNotification, showNotificationForPackage, cancelNotification, setCanPreempt, savePreferences, showSplashScreen, updateActivityParams, refreshWidget, getUserViewedOptOut, setWindowVisibility
### (unnamed table @0x18022fce0): tobit, bnot, band, bor, bxor, lshift, rshift, arshift, rol, ror, bswap, tohex
### (unnamed table @0x180304880): onStart, onDestroy
### (unnamed table @0x1803048e0): updateOffscreenTimer, getLineSegmentToTarget, updateCameraFrustum, onUpdateNative, onStartNative
### (unnamed table @0x180304970): setGlowSettings, update, hide, show, dummy
### (unnamed table @0x180304a20): setWorldT, updateLayersNative
### (unnamed table @0x180304a60): init, destroy, updateBegin, updateEnd, setCamera, setScene, hiresTimerGet, unitTest_Vector, unitTest_Matrix, unitTest_Quaternion, unitTest_SRT
### (unnamed table @0x180304b50): Start, Update, Destroy
### (unnamed table @0x180304b90): Start, Update, Destroy
### (unnamed table @0x180304bd8): onUpdateNative, setSensorRotation
### (unnamed table @0x180304c18): setMaterial, updateMaterial
### (unnamed table @0x180304c50): onStartNative, onDestroyNative, onGlobalShutdown, onUpdateNative
### (unnamed table @0x180304cd0): Start, Update, SegmentPlaying, Destroy
### (unnamed table @0x180304dd0): onStart, onDestroy, onGlobalShutdown, segmentPlayingNative
### (unnamed table @0x180304e50): onUpdateNative, onScreenMetric, followMetric
### (unnamed table @0x180304e90): setCamData, computeAngularDist, setNearestActorData, correctRotation
### (unnamed table @0x180304ef0): onStartNative, onDestroyNative, onGlobalShutdown, segmentPlayingNative
### (unnamed table @0x180307370): init, destroy, setScene, hiresTimerGet, setLuaProfilerEnable
### (unnamed table @0x180309ad0): getStreamVolume, getHeadsetState, isMicrophoneMute, isMusicActive, isSpeakerphoneOn, getRingerMode, getMode, requestFocus
### (unnamed table @0x180309d30): getDeviceChargeStatus, getBatteryLevel, isScreenOn
### (unnamed table @0x180309dd0): getWifiState, getAccessPoints
### (unnamed table @0x180309e00): setVersionString, getDefaultUID, initialize, dispatch, sendSessionComplete, setEnabled, setAnonymizeMode, setCustomDimension, setCustomMetric, sendEvent, sendView, markCrash, convertIntToZeroPaddedInt, start, stop, setState, setFsmState
### Depth  (luaL_Reg @0x180306140)
  setDepth
### Texture  (luaL_Reg @0x180306f00)
  destroy
### RenderTarget  (luaL_Reg @0x180306ac0)
  destroy
### RenderGraph  (luaL_Reg @0x180306940)
  findNodeByName
### RenderGraphInstance  (luaL_Reg @0x180306960)
  addAnimation
### RGSceneNode  (luaL_Reg @0x180306aa0)
  getRenderView
### Query  (luaL_Reg @0x180309d70)
  getPss
### Object: table in .bss, filled at runtime (not readable statically)
