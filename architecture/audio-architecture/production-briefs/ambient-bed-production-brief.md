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

Ambient beds use a 200 ms pre-baked crossfade at the loop boundary, not a silence
window (which is used for zone loops). The crossfade is authored in the DAW:

1. Duplicate the first 200 ms of the file and append it after the tail.
2. Apply a 200 ms linear crossfade between the tail and the duplicated head region.
3. Export the crossfaded tail as part of the file.

The engine loops the file by seeking to sample 0. The pre-baked crossfade at the end of
the file covers the transition back to sample 0, producing a seamless loop without
requiring engine-side crossfade logic for the ambient bed.

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

- **Pre-baked 200 ms crossfade**: author the loop crossfade in the DAW before export. Do
  not rely on the engine for crossfade smoothing at the ambient bed loop boundary.
- **Loudness metered per file**: meter each of the four beds individually. Do not use a
  group average — each file must independently hit −20 LUFS / −1 dBTP.
- **Runtime seek-to-0 loop verification**: after export, load the file in a DAW or
  reference player and set the timeline to loop. Listen through at least 5 loop cycles.
  Confirm no click, pop, or level step at the loop point. This must be done before
  delivery.
- **Stereo field**: ambient beds are stereo. They are not positional sources. The left/
  right field should represent a natural wide-image city ambience, not a binaural
  recording. Keep the stereo image moderate — wide panning of prominent elements can
  conflict with HRTF-processed positional sources.

---

## Delivery Verification Checklist

- [ ] `ambient_day.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade.
- [ ] `ambient_night.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade.
- [ ] `ambient_dawn.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade.
- [ ] `ambient_dusk.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade.
- [ ] All four files individually loudness-verified (not group-averaged).
- [ ] Runtime seek-to-0 loop verification completed for each file (minimum 5 cycles, no artifacts).
- [ ] No JSON sidecars submitted for ambient beds (they are not required and would be ignored).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/streaming-architecture.md`
