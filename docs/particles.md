# Particles: SGParticleEmitter as Pearl uses it

Re-implemented in `runtime/src/assets/particles.*` (loader), `runtime/src/scene/particles.*`
(simulation) and `Renderer::drawParticles` (drawing). Decompiles: `pearl-work/ghidra/sgpe.c`
(SGParticleEmitter), `part.c`/`part2.c` (fields, collider, ParticleBuffer, resource),
`noise.c` (FastNoise, random helpers).

## Content

`pearl_vrcam/scripts/data/particles.lua` attaches particle shapes to bones of the trigger actors
(`ParticleTemplate` -> `Scene.createParticleEmitter(name, uri)`, `setViewFlags(3)`). 9 systems are
used: DustAFX (car interior, seq1_shot05..S01_10_animB and seq6_shot30) and the snow systems
SnowA..D + SnowPlugA..D (seq2_shot40..seq3_shot10); `steamafx.mxb` is not referenced. All use one
ForceField, one TurbulenceField3D and one PlaneCollider, the same shader
(`common:shaders/particle_5d3ea85d.shd`) and a pre-simulated particle map.

## Update (SGParticleEmitter::internalUpdate)

* dt = global frame dt (us) x 1e-6 x emitter time scale; run after the actors pose their bones.
* First update (emitter age 0) with dt > 0 and definition +0x1c set: the particles become a copy
  of the file's pre-simulated map (warm-up); otherwise an initial burst of min(+0x10, +0x14).
* While age < duration (+0x0c, 0 = forever): accumulator += dt x rate (+0x08, or setEmitRate);
  whole particles are emitted up to the maximum (+0x14).
* `integrateParticles` runs backwards over the array: age += rate x dt with the life mode
  (+0x1fc: 0 die, 1 immortal, 2 loop, 3 ping-pong), dead particles (age = FLT_MAX) are replaced by
  the last one; force = sum of fields; a = (F + drag x v) x invMass x dt; p += (a/2 + v) dt;
  v += a; then the plane collider (definition +0x280 set: particles that cross the plane die).
* ForceField: constant force (optionally through a matrix, unused). TurbulenceField3D:
  `FastNoise::noise` value noise on Perlin's permutation (gradient p/255*2-1, smoothstep),
  sampled at freq x position with phase x age, scaled by a height falloff and dt.

## Emission (FUN_18011ec40)

Every random number comes from the CRT `rand()` with its default seed (moxie never calls
`srand`), one sequence shared by all emitters; the call order per particle is reproduced:
age, rotation, colour (lerp of two RGBA8 colours), size, lifetime, flipbook frame, 1/mass, drag,
random value, speed, direction (+0x200: random unit / outward / box), position by shape (+0x1f0:
0 box volume or surface, 4 cylinder surface or volume, 5 disc with inner ratio +0x1f8), then the
position through the emitter's world matrix and the direction through its rotation.
Shapes 1-3 and 6 (sphere, hemisphere, line, ParticleMap grid) are not used by Pearl and not
implemented (warning).

## Drawing (ParticleBuffer::draw)

Four vertices per particle (0x30 bytes): position + corner code, half size + rotation + size
life, RGBA8 colour, flipbook frame bytes, colour life, rotation life; indices 0,1,2 0,2,3. The
life values are age/lifetime (capped at 0.999, optional fade ramps; modes 1/2 unused). The
shader interpolates size, rotation and colour curves (`u_particleSizeScaleX/Y`,
`u_particleRotation`, `u_particleColors` = channels of the four colour keys at +0x34) and
billboards in view space. Sort depth = (view z of the particle bounds' centre - largest half
extent) / far.

## Validation

Dust stretch (frames 470..1270 of run07a, every 20th): in the pixels the particles cover the mean
difference to the original is 9.2 with particles vs 31.0 without; most frames 3-6 (grain floor),
i.e. the specks sit where the original's are - the random sequence and the integration run in
step. Around frames 970..1110 single particles drift by a few pixels (turbulence amplifies
last-bit differences of the emitter position) and re-converge as particles are replaced.
No reference frames exist for the snow shots yet.
