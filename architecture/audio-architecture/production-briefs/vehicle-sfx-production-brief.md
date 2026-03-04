# Vehicle SFX Production Brief

## Overview

This document is the Phase 4 exit criterion for the Vehicle SFX Production Brief gate.
All parameters below are locked. Vehicle audio covers engine idle loops, engine move
loops, horn one-shots, and intersection tick one-shots.

Engine loops and zone loops are distinct asset categories managed by separate `SoundId`
ranges in the engine. Do not conflate their specifications — zone loops are covered in
`zone-loop-production-brief.md`.

---

## Engine Loop Locked Parameters

| Parameter | Value |
|---|---|
| Format | OGG Vorbis |
| Channels | Mono (1 channel) — positional source |
| Sample rate | 44100 Hz |
| Minimum duration | 6 s (hard floor — shorter loops will be rejected) |
| Maximum duration | **strictly less than 20 s** — files at exactly 20 s are Tier 3 (streamed) and are prohibited for engine loops; the CI gate uses `duration >= 20.0` as the hard rejection condition |
| Loudness target | −22 LUFS integrated |
| True-peak ceiling | −2 dBTP |
| WAV format | PROHIBITED for engine loops |

### Why 6 s Minimum — Rationale

The engine applies pitch-shift to engine loops to simulate vehicle speed. The lowest
pitch-shift ratio applied is 0.75x (used for stopped or near-stopped vehicles). At this
ratio, the pitch-shifted audio plays at 75% of its authored pitch, which compresses the
perceived loop duration:

```text
perceived_duration = authored_duration × pitch_ratio
```

Human perception detects looping at approximately 4–5 s of identical audio. At 0.75x pitch:

| Authored duration | Perceived loop at 0.75x |
|---|---|
| 4 s | ~3.00 s perceived — audibly mechanical |
| 5 s | ~3.75 s perceived — audibly mechanical |
| 6 s (minimum) | ~4.50 s perceived — below perceptibility threshold |

A 6 s authored loop produces a ~4.5 s perceived loop at the lowest pitch. This is below
the threshold at which most listeners detect repetition. Loops shorter than 6 s will be
rejected by `validate_assets.py`.

**Recommended authoring duration: 8–12 s.** The 6 s minimum is the hard floor required
to pass CI, but a 6 s loop at 0.75× pitch produces only ~4.5 s perceived — just at the
perceptibility boundary. An 8–12 s authored loop gives 6–9 s perceived at the lowest
pitch, providing meaningful headroom above the repetition threshold and richer tonal
variation. Authors should target this range unless content constraints require a shorter
loop. A 6 s loop is acceptable for code-path testing only; a production-quality engine
loop should be 8–12 s.

### Why WAV Is Prohibited for Engine Loops

A 0.4–1 s WAV one-shot is acceptable for horn and tick sounds (single-play, no loop
fatigue). For engine loops, even a 1–2 s WAV loop produces an audibly mechanical repeat
at any realistic distance. OGG Vorbis is required for all looping engine assets.

---

## Engine Loop Asset List

| File | Channels | Format | Duration | Loudness |
|---|---|---|---|---|
| `sfx_vehicle_engine_idle.ogg` | Mono | OGG Vorbis | 6 s ≤ duration < 20 s | −22 LUFS / −2 dBTP |
| `sfx_vehicle_engine_move.ogg` | Mono | OGG Vorbis | 6 s ≤ duration < 20 s | −22 LUFS / −2 dBTP |

Both files are pre-loaded into static OpenAL Soft buffers (not streamed). The idle and
move source slots are acquired and released as an atomic pair — partial acquisition is
prohibited. The engine supports a maximum of 12 simultaneous engine source pairs (24
pool slots / 2 per vehicle). Vehicles beyond 150 m from the listener are culled and
their source pair is released.

---

## Engine Loop Timbral Content Specification

**Current status (2026-03-04)**: Both `sfx_vehicle_engine_idle.ogg` and
`sfx_vehicle_engine_move.ogg` are 6.00 s synthetic SoX-generated sine tones. These
are placeholder assets. Production content must be re-exported at 8–12 s with
DAW-authored engine tone content before Phase 10 exit.

### Vehicle Class Reference

The engine applies a base pitch multiplier per vehicle class:

- **Car**: base pitch 1.0× — the authored loops are the tonal reference.
- **Bus / Truck**: base pitch 0.85× — a lower, heavier register than cars.

The two files (`sfx_vehicle_engine_idle.ogg` and `sfx_vehicle_engine_move.ogg`) are
the single set of loop assets used for ALL vehicle classes. Class differentiation is
achieved at runtime via the base pitch multiplier only. The authored content must be
neutral enough (no strong formants or character that sounds wrong at 0.85×) to work
acceptably for both cars and buses.

**Author the loops as if for a mid-sized car engine** (base pitch 1.0×). The 0.85×
bus/truck pitch-down will shift the tonal register automatically at runtime. Do not
attempt to author a "bus sound" at the loop level.

### Timbral Character — `sfx_vehicle_engine_idle.ogg`

This file represents a vehicle standing still or moving slowly (speed < 3 m/s). The
engine idle should convey:

- A low, steady mechanical rumble centred around 80–120 Hz (fundamental engine frequency
  at idle RPM, approximately 700–900 RPM for a 4-cylinder car: 700 RPM / 60 × 2
  ignition events/rev = ~23 Hz fundamental × 2nd harmonic = ~46 Hz — richer harmonics
  from exhaust resonance place the perceived centre around 80–140 Hz).
- Slow, subtle amplitude modulation at 10–15 Hz (cylinder firing rhythm at idle RPM)
  to give organic texture.
- Minimal high-frequency content above 3 kHz — at 0.75× pitch the upper register shifts
  down, so any prominent high-frequency character will sound dull.
- No strong transients or rhythmic accents that will become audible as a mechanical
  beat at the loop boundary.
- Quiet but present — at −22 LUFS the idle will be barely perceptible beyond 60–80 m
  from the listener at the engine's reference distance (1 m), which is the intended
  behaviour. Do not over-brighten to compensate — the OpenAL distance model handles
  attenuation.

### Timbral Character — `sfx_vehicle_engine_move.ogg`

This file represents a vehicle moving at moderate-to-high speed (speed ≥ 8 m/s). The
move sound should convey:

- A mid-frequency mechanical drone with a higher harmonic density than idle — the higher
  RPM produces a brighter tonal character. Aim for a perceived fundamental of 150–200 Hz
  (corresponding to ~2,000–2,500 RPM on a 4-cylinder).
- Tyre rolling noise as a sub-layer: broadband noise emphasis in the 500 Hz–2 kHz band,
  reflecting road surface contact. This creates perceptual distance differentiation from
  the idle (idle = low mechanical hum; move = mechanical hum + road noise presence).
- Subtle exhaust breath character in the 400–800 Hz region adds realism without strong
  periodicity that would loop-fatigue quickly.
- Amplitude modulation slower than idle — at higher RPM the cylinder rhythm is faster
  and smooths into continuous texture; the overall amplitude variation should be ≤ 3 dB
  peak-to-valley to avoid pumping artifacts at the crossblend boundary.
- The move loop must crossblend with the idle loop without clicks: at the blend threshold
  (3–8 m/s), both sources play simultaneously at partial gain. Audition both files playing
  together at equal gain to verify no comb-filtering or phase cancellation is audible.

### Idle / Move Crossblend Verification

The engine crossblends idle and move gains continuously:

- At speed < 3 m/s: idle gain = 1.0, move gain = 0.0 (idle only)
- At speed 3–8 m/s: both play simultaneously; gains interpolate linearly
- At speed ≥ 8 m/s: idle gain = 0.0, move gain = 1.0 (move only)

**Before delivery, verify the crossblend** by rendering both OGG files simultaneously
in the DAW at equal gain (simulating the 5 m/s mid-blend point). Listen for:

- No audible comb filtering (hollow, frequency-cancelled sound)
- No unexpected resonant peaks when summed
- Tonal continuity — the combined signal should sound like a single richer engine, not
  two separate engines playing at once

If comb filtering is present, shift the fundamental frequency of one file by 5–10 Hz
relative to the other. This breaks the phase coherence without affecting the perceived
tonal character independently.

### Pitch-Shift Verification at Extremes

Both engine loops must be verified at the pitch-shift extremes:

| Pitch ratio | Speed context | Verification requirement |
|---|---|---|
| 0.75× (stopped) | Vehicle at rest / near-stopped | Loop boundary must be click-free; no audible mechanical beat |
| 1.0× (authored) | Reference playback | Loop boundary must be click-free |
| 1.35× (max speed) | Vehicle at full speed | Loop boundary must be click-free at the higher register |

In the DAW, apply a pitch-shift plugin (or resample) at 0.75× and 1.35× to the exported
OGG and listen through at least 5 loop cycles at each pitch. A loop that is click-free at
authored pitch may produce a click at 1.35× if the phase at the loop boundary is
misaligned.

---

## One-Shot SFX Parameters

### `sfx_vehicle_horn`

| Parameter | Value |
|---|---|
| Format | WAV PCM |
| Channels | Mono |
| Sample rate | 44100 Hz |
| Duration | 0.4–1 s |
| Loudness | −18 LUFS / −1 dBTP |

WAV PCM is correct here: at 0.4–1 s this is a one-shot with no loop fatigue concern.
OGG Vorbis would add unnecessary decode latency for a sub-second trigger.

**Timbral character**: a car horn — short honk with a clear fundamental pitch in the
300–500 Hz range. Should be tonally assertive and recognisable as a vehicle horn without
being aggressive or startling. A double-beep pattern (two short blasts 80–100 ms apart)
within the 0.4–1 s window is acceptable. The onset transient must start within the first
50 ms of the file (no pre-silence). The horn will be heard from a positional source —
it must be distinct at distances up to 100 m. The −18 LUFS target ensures it is audible
over engine noise and ambient beds.

**Rate-limiting (enforced in AudioSystem — not an authoring concern)**: per-vehicle 2 s
cooldown and global cap of 3 simultaneous horn sources. These are code constraints; the
authored WAV file has no cooldown embedded.

### `sfx_intersection_tick`

| Parameter | Value |
|---|---|
| Format | WAV PCM |
| Channels | Mono |
| Sample rate | 44100 Hz |
| Duration | < 0.5 s |
| Loudness | −28 LUFS / −2 dBTP |

The intersection tick is a subtle UI-adjacent positional cue. At −28 LUFS it sits well
below engine loops (−22 LUFS) and does not compete with music stems.

**Timbral character**: a very short, dry mechanical click or tick — resembling a
relay contact or a low-gain mechanical switch. Broadband transient energy, duration
< 100 ms of actual content plus a short decay tail (total file < 0.5 s). No reverb
or sustained body — the tick must disappear immediately so it does not stack audibly
across multiple simultaneous intersections. At −28 LUFS it should be inaudible beyond
~50 m under normal city noise; the pre-acquisition distance cull at > 80 m in
`CitySimulation::tick()` handles suppression at further distances.

---

## Engine Loop Authoring Requirements

- **Seamless loop point — bar-aligned content seam, NOT silence-boundary**: engine loops
  use a seamless content loop, not the silence-boundary technique used for zone loops.
  The loop tail must carry engine-tone content that connects directly to the loop head
  with no fade, silence, or abrupt transient. To achieve a click-free seam: (1) author
  the loop so the loop-tail waveform phase and amplitude match the loop-head waveform at
  the join point; (2) optionally apply a 10–20 ms equal-power crossfade at the loop
  boundary in the DAW to remove any residual phase discontinuity — this crossfade must be
  at the content level and must NOT produce a perceivable volume dip. (3) Verify by
  looping the file in the DAW player and listening through the boundary at both the
  lowest pitch-shift (0.75×) and highest pitch-shift (1.35×) settings — a loop that is
  click-free at authored pitch may click at extreme pitch shifts if the phase alignment is
  marginal. Do NOT apply silence at the head or tail — silence boundaries are for zone
  loops only and will produce an audible gap-click when the engine loops a vehicle engine
  file.
- **No fade-in or fade-out**: the engine handles gain ramps via the pitch-shift / distance
  attenuation pipeline. Do not bake gain automation into the file.
- **Mono — no stereo submix**: route all sources to a mono bus before export. A stereo
  OGG submitted as a vehicle engine loop will be rejected at load time.
- **Loudness metered before pitch-shift**: the −22 LUFS / −2 dBTP target is measured on
  the authored file, before the engine applies pitch-shift. The engine does not post-
  process loudness.
- **OGG encoding quality**: encode at **libvorbis -q 6** (minimum) for vehicle engine
  loops — they are mono and the content is tonal rather than complex harmonic, so -q 6
  is sufficient. -q 8 is not required. Verify with `ogginfo` or `soxi` that the encoded
  file is mono and 44100 Hz after export.

---

## Re-Export Procedure for Placeholder Replacement

The current placeholder files (`sfx_vehicle_engine_idle.ogg`, `sfx_vehicle_engine_move.ogg`)
both measure 6.00 s and are synthetic SoX tones. The following procedure replaces them:

1. Open the DAW and create a new mono session at 44100 Hz.
2. Build the engine idle sound using the timbral guidance above (fundamental 80–120 Hz,
   slow AM at 10–15 Hz). Target duration: 8–12 s of loop-able content.
3. Trim the content to a loop-friendly length (8–12 s). Set a DAW loop point covering
   the file and audition through at least 10 cycles to verify no click at the loop
   boundary.
4. Render to WAV at 44100 Hz, 16-bit, mono. Measure integrated LUFS with a BS.1770-3
   loudness meter (e.g. Youlean Loudness Meter, MeterPlugs LCAST, or ffmpeg with
   `ebur128`). Apply makeup gain or attenuation to reach −22 LUFS. Apply a true-peak
   limiter with ceiling −2 dBTP.
5. Export to OGG Vorbis at -q 6 using libvorbis (e.g.
   `oggenc -q 6 -o sfx_vehicle_engine_idle.ogg sfx_vehicle_engine_idle.wav`).
6. Verify with `soxi sfx_vehicle_engine_idle.ogg`: channels = 1, sample rate = 44100,
   duration ≥ 8.00 s and < 20.00 s.
7. Apply pitch-shift verification at 0.75× and 1.35× in the DAW.
8. Repeat steps 1–7 for `sfx_vehicle_engine_move.ogg` using the move timbral guidance.
9. Verify crossblend by auditioning both files simultaneously at equal gain.

---

## Delivery Verification Checklist

- [ ] `sfx_vehicle_engine_idle.ogg` — mono, 44100 Hz, **8–12 s (production target)**, −22 LUFS, ≤ −2 dBTP, seamless loop.
- [ ] `sfx_vehicle_engine_move.ogg` — mono, 44100 Hz, **8–12 s (production target)**, −22 LUFS, ≤ −2 dBTP, seamless loop.
- [ ] **Engine loop duration target met**: both files are 8–12 s. A 6 s loop is permitted
  by the CI gate but is only acceptable for placeholder and code-path testing — it sits
  at the perceptibility boundary (4.5 s perceived at 0.75× pitch). Any file currently
  at exactly 6.00 s that was generated as a synthetic placeholder must be re-exported at
  8–12 s with production-quality DAW-authored engine tone content before Phase 10 exit.
- [ ] Idle timbral verification: fundamental centred 80–120 Hz; slow AM at 10–15 Hz;
  no prominent high-frequency content above 3 kHz.
- [ ] Move timbral verification: higher harmonic density than idle; road noise presence
  in 500 Hz–2 kHz band; amplitude variation ≤ 3 dB peak-to-valley.
- [ ] Crossblend verification: both files auditioning simultaneously at equal gain in
  the DAW produces no comb filtering or phase cancellation.
- [ ] `sfx_vehicle_horn.wav` — mono WAV PCM, 44100 Hz, 0.4–1 s, −18 LUFS, ≤ −1 dBTP;
  onset transient within first 50 ms.
- [ ] `sfx_intersection_tick.wav` — mono WAV PCM, 44100 Hz, < 0.5 s, −28 LUFS, ≤ −2 dBTP;
  broadband transient, no reverb tail.
- [ ] No engine loop file is below 6 s (duration check mandatory).
- [ ] No engine loop file is at exactly 20 s or longer.
- [ ] No engine loop file is in WAV format.
- [ ] DAW loop verification completed for both engine loops at both pitch extremes (0.75× and 1.35×) — no click at loop boundary.
- [ ] Loop pitch verification: loopback audition performed at both 0.75× and 1.35× pitch
  in the DAW. A loop that is click-free at authored pitch may click at extreme pitch shifts
  if phase alignment is marginal. Both extremes must be verified before delivery.

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/source-pool.md`
- `architecture/audio-architecture/spatial-audio.md`
