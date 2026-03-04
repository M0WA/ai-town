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

---

## Delivery Verification Checklist

- [ ] `sfx_vehicle_engine_idle.ogg` — mono, 44100 Hz, 6 s ≤ duration < 20 s, −22 LUFS, ≤ −2 dBTP, seamless loop.
- [ ] `sfx_vehicle_engine_move.ogg` — mono, 44100 Hz, 6 s ≤ duration < 20 s, −22 LUFS, ≤ −2 dBTP, seamless loop.
- [ ] `sfx_vehicle_horn.wav` — mono WAV PCM, 44100 Hz, 0.4–1 s, −18 LUFS, ≤ −1 dBTP.
- [ ] `sfx_intersection_tick.wav` — mono WAV PCM, 44100 Hz, < 0.5 s, −28 LUFS, ≤ −2 dBTP.
- [ ] No engine loop file is below 6 s (duration check mandatory).
- [ ] No engine loop file is at exactly 20 s or longer — the Tier 2/Tier 3 boundary is exclusive; a file at exactly 20 s is Tier 3 (streamed) and will be rejected by `validate_assets.py`.
- [ ] No engine loop file is in WAV format.
- [ ] DAW loop verification completed for both engine loops (no click at loop boundary).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/source-pool.md`
- `architecture/audio-architecture/spatial-audio.md`
