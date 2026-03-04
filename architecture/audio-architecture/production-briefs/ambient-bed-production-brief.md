# Ambient Bed Production Brief

## Overview

This document is the Phase 4 exit criterion for the Ambient Bed Production Brief gate.
All parameters below are locked. Ambient beds are long stereo OGG streams that run
continuously underneath the music stems, providing time-of-day atmosphere. They are
never ducked during stinger playback.

---

## Locked Parameters

| Parameter | Value |
|---|---|
| Format | OGG Vorbis |
| Channels | Stereo (2 channels) |
| Sample rate | 44100 Hz |
| Duration | 90–120 s |
| Loudness target | −20 LUFS integrated |
| True-peak ceiling | −1 dBTP |
| Loop boundary technique | 200 ms pre-baked DAW crossfade (NOT a silence-boundary loop) |
| JSON sidecar | NOT required |
| Stinger ducking | NOT ducked during stinger playback |

### Why 90–120 s

Durations of 30–60 s cause audible loop fatigue — the listener begins to anticipate the
loop point. At 90–120 s the loop period exceeds the perceptibility threshold for casual
play sessions.

### Loop Boundary Technique

Ambient beds use a dual-requirement loop authoring approach. Both requirements must be
satisfied simultaneously — satisfying only one is a delivery failure.

**Requirement 1 — Seamless sample-0 boundary (PRIMARY QUALITY GATE)**: The content at
sample 0 must be clean and loop-friendly with no audible transient or silence. The
`AudioStream` runtime loops the file by seeking to sample 0 (`ov_pcm_seek(vf, 0)`)
BEFORE reaching the pre-baked crossfade tail — the DAW crossfade tail is bypassed
entirely at runtime. The player always hears the sample-0 boundary, never the crossfade
tail. Authors must verify this boundary by setting the DAW timeline to loop the file at
exactly sample 0 and auditioning through the boundary at least 5 times. A click, pop,
or level step at sample 0 is a HARD REJECTION regardless of how well the crossfade tail
sounds. Structure the ambient bed so that the content at sample 0 forms a natural
continuation of the content just before the loop point (consistent harmonic density,
no DC offset, no abrupt transient at sample 0, no silence gap at sample 0).

**Requirement 2 — 200 ms pre-baked DAW crossfade tail (secondary authoring safeguard)**:
The crossfade is authored in the DAW as follows:

1. Duplicate the first 200 ms of the file and append it after the tail.
2. Apply a 200 ms linear crossfade between the tail and the duplicated head region.
3. Export the crossfaded tail as part of the file.

This crossfade tail is a production safeguard and a fallback for non-streaming preview
contexts (e.g. auditioning in the DAW without loop-point sync). It is NOT the loop
mechanism heard in-game. The runtime seek-to-0 mechanism ignores the crossfade tail
entirely.

**Primary quality gate order**: verify sample-0 click-free loop FIRST. Then add the
200 ms crossfade tail as the second step. A file that passes the crossfade tail check
but fails the sample-0 click-free check must be rejected and re-authored.

### No JSON Sidecar Required

Ambient beds do not require bar-boundary synchronisation. The engine uses real-time
crossfade duration (not bar-boundary counting) for ambient bed transitions. There is no
`bpm` or `beats_per_bar` field to specify.

`validate_assets.py` Check #14 applies only to `music_*.ogg` files. Ambient beds
(`ambient_*.ogg`) are explicitly exempt from the sidecar check.

### Not Ducked During Stinger Playback

The music duck (`m_musicDuckGain = 0.4`) applies to music stems only. Ambient beds
continue at full gain when a stinger plays. Stingers are authored to be intelligible over
the full ambient mix (see `stinger-production-brief.md`).

---

## Asset List

| File | Channels | Format | Duration | Loudness |
|---|---|---|---|---|
| `ambient_day.ogg` | Stereo | OGG Vorbis | 90–120 s | −20 LUFS / −1 dBTP |
| `ambient_night.ogg` | Stereo | OGG Vorbis | 90–120 s | −20 LUFS / −1 dBTP |
| `ambient_dawn.ogg` | Stereo | OGG Vorbis | 90–120 s | −20 LUFS / −1 dBTP |
| `ambient_dusk.ogg` | Stereo | OGG Vorbis | 90–120 s | −20 LUFS / −1 dBTP |

---

## Authoring Requirements

- **Sample-0 click-free gate (PRIMARY — verify first)**: before adding the 200 ms
  crossfade tail, set the DAW timeline to loop the file at exactly sample 0 and audition
  through the loop boundary a minimum of 5 times. Confirm: no click, no pop, no level
  step, no silence gap at sample 0. This is the PRIMARY quality gate — if sample 0 is
  not clean, re-author the content before proceeding to the crossfade tail step.
- **Pre-baked 200 ms crossfade tail (SECONDARY — add after sample-0 gate passes)**:
  author the loop crossfade in the DAW before export per the dual-requirement procedure
  above. Do not rely on the engine for crossfade smoothing at the ambient bed loop
  boundary.
- **Loudness metered per file**: meter each of the four beds individually. Do not use a
  group average — each file must independently hit −20 LUFS / −1 dBTP.
- **Dawn and night cross-compatibility**: author the day and night beds so that a direct
  day→night constant-power crossfade (3 s, no dusk intermediate) sounds natural at 3×
  simulation speed. The day bed must have a lower-energy tail character (fewer prominent
  bird calls, lower traffic density in the final 15 s) so it bridges into night without
  needing dusk as a transition. The night bed must open acceptably after a direct day
  crossfade (do not start night with a sudden loud insect burst). Deliver a day→night
  direct crossfade audibility test as part of the ambient bed delivery package — commit
  the test render to `assets/audio/crossfade_demo_day_to_night.ogg`. This test must be
  reviewed and approved before ambient bed asset lock.
- **Stereo field**: ambient beds are stereo. They are not positional sources. The left/
  right field should represent a natural wide-image city ambience, not a binaural
  recording. Keep the stereo image moderate — wide panning of prominent elements can
  conflict with HRTF-processed positional sources.

---

## Delivery Verification Checklist

- [ ] `ambient_day.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_night.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_dawn.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_dusk.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] All four files individually loudness-verified (not group-averaged).
- [ ] **Sample-0 click-free gate passed for each file** — loop auditioned at sample 0 in DAW, minimum 5 cycles, no click or level discontinuity. This is the PRIMARY gate; must be verified before the crossfade tail step and again on the final export.
- [ ] Day bed has lower-energy tail character (suitable for direct day→night crossfade at 3× speed without dusk intermediate).
- [ ] Night bed opens acceptably after a direct day→night crossfade (no jarring onset at start of night content).
- [ ] Day→night direct crossfade audibility test committed to `assets/audio/crossfade_demo_day_to_night.ogg` and approved before asset lock.
- [ ] No JSON sidecars submitted for ambient beds (they are not required and would be ignored).
- [ ] **`assets/audio/ambient_bed_qa.md` committed** — one entry per file documenting
  sample-0 gate result ("pass"), DAW loop cycles count (minimum 5), crossfade tail
  present ("yes"), measured LUFS and dBTP, and author sign-off. See
  `architecture/audio-architecture/v1-audio-asset-manifest.md` Phase 10 QA Delivery
  Artifacts section for the required format. This document is a mandatory Phase 10
  exit deliverable.

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/streaming-architecture.md`
