# Audio: the "Maux" mixer of moxie.v2 as Pearl uses it

Re-implemented in `runtime/src/audio/audio_engine.*`, driven by the original Lua through the
`AudioManager` natives (`runtime/src/story/bind_audio.cpp`). Decompiles: `pearl-work/ghidra/`
`audmix.c`, `audfx*.c`, `audk.c`, `hrtf*.c`, `lapiaud.c`, `maux.c`, `audvt_full.txt`.

## Content

19 Ogg Vorbis files at 48 kHz: dialogue mono (on "2.5D" emitters attached to animated bones),
music stereo (direct), ambiences 4-channel first-order ambisonics (W, X, Y, Z; played on the
`GlobalSurroundEmitter`), plus `common:audio/biquad_hrtf_subject_015_order_12_foa.mxhrtf`.

## Engine (AudioSystem defaults, allInitialize)

48 kHz, stereo, int16, blocks of 512 frames. Per block (`audioSystemOutputCallback`):

1. Players in creation order (`AudPlayer::mixdown`): volume and pan ramp linearly per block
   (a change while playing ramps over the requested frames rounded up to whole blocks, at least
   one block; the pan ramp is advanced twice per block - as in the original). Kernels:
   mono -> stereo (`FUN_180044920`), stereo -> stereo (`FUN_180044d40`), 4ch -> surround emitter
   (`FUN_180045520`); integer accumulation saturating at +-32767.
   Pan law (`FUN_1800446c0`, mode 1): balance table, 1.0 up to the centre, then linear to 0
   (steps 1/512); left = t[pan*1023], right = t[1023 - pan*1023]. Lua pan p -> (p+1)/2.
2. 2D emitters (`Emitter2dEffect`, "2D Panning Rolloff Filter"): pan = (right . d + 1) / 2 with
   right = listener forward x up and d = horizontal direction to the source; gain = distance
   rolloff (linear between Distance Min/Max) x angular rolloff (angle between the listener's
   forward vector and the source, linear between Angle Min/Max). Both ramp over the block.
   Pearl's dialogue: 40..180 deg -> 1.0..0.5, no distance rolloff, positional panning on.
3. Surround emitter (`AmbisonicFirstOrderStereoEffect`): int16 -> float (1/32767), dual-band
   shelf per channel (`LP - HP*k`, k = 1.4142 for W, 0.8165 for X/Y/Z, 48 kHz biquads from
   `FUN_180040600`), rotation of X/Y/Z by `Matrix4::setEuler(0, pitch, yaw)` of the listener's
   forward vector (roll ignored), decode to 4 virtual speakers as mid (aW + bX + dZ) and side (cY).
4. Virtual speakers (`BiquadBinauralFilter`, directions (+-45/135 deg, +-45 deg)): nearest .mxhrtf
   record; delay of record[2] ms with first-order allpass fraction; per channel gain + 6 biquads
   (direct form I); L = M+S, R = M-S; added to the output as `out - (int)(v * -32767)`.

`.mxhrtf`: "HRTF", u32 version 2, then 0x104-byte records: azimuth, elevation, delay (ms), then
two cascades of (gain, 6 x (b0 b1 b2 a1 a2)) for mid and side. Pearl's file holds 4 directions
(45/135 deg azimuth, +-42.4 deg elevation), each stored 4 times.

## Lua natives

`addEmitter(type)`: Lua 0 = 2.5D, 2 = surround; `setEmitterEffectParameter(e, fx, p, v)`: p - 8
is the effect parameter (out-of-range values become the minimum; angles in degrees);
`setSoundVolume/Pan(s, v, ms)`; `getSoundPan` returns 0 (stub in the original), `getSoundVolume`
the current ramp value. `syncToSound` is a no-op in the story scripts.

## Differences / open

* Vorbis decoding through libvorbisfile (the original has its own `OggVorbisDecoder`): last-bit
  differences possible.
* Biquad sums use one fixed operand order; the original's order varies per filter (ulp level).
* The output resampler/ring buffer of the original is not reproduced (device runs at 48 kHz).
* No reference recording of the original's audio yet.
