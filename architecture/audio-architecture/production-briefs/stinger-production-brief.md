# Stinger Production Brief

## Overview

This document is the Phase 4 exit criterion for the Stinger Production Brief gate. All
parameters below are locked. Stingers are short non-positional WAV one-shots that
communicate game state transitions. They are played over the full ambient mix but trigger
a music duck.

---

## Locked Parameters

| Parameter | Value |
|---|---|
| Format | WAV PCM |
| Channels | Mono (1 channel) — see rationale below |
| Sample rate | 44100 Hz |
| `AL_SOURCE_RELATIVE` | `AL_TRUE` (non-positional) |
| Music duck gain on stinger playback | 0.4 (music stems only) |
| Ambient beds during stinger | NOT ducked — stingers play over the full ambient mix |
| Minimum trigger gap (same type) | 5 s |

### Why Mono WAV

Stingers use `AL_SOURCE_RELATIVE = AL_TRUE` (non-positional — they represent game state,
not a 3D position in the world). On some OpenAL Soft implementations, multi-channel WAV
sources with `AL_SOURCE_RELATIVE` produce undefined panning behavior. Mono is mandatory
to guarantee consistent non-positional playback across all target platforms.

WAV PCM is used (not OGG Vorbis) because stingers are short one-shots pre-loaded into
reserved non-evictable SFX source slots. The decode latency of OGG Vorbis is not
acceptable for a time-critical trigger. WAV PCM decodes instantly from the static buffer.

---

## Asset List

| File | Target duration | Duration range | Loudness | Trigger Condition |
|---|---|---|---|---|
| `stinger_crisis.wav` | 3 s | 2–4 s | −18 LUFS / −1 dBTP | Crisis event onset |
| `stinger_milestone.wav` | 2.5 s | 2–3 s | −18 LUFS / −1 dBTP | City Rating tier transition only |
| `stinger_game_over.wav` | Deferred | — | — | Post-V1 (Scenario mode) |

`stinger_game_over` is not a V1 deliverable. Scenario mode is not in V1 scope.

---

## Onset Timing Requirement

The most prominent musical content of each stinger (the main impact or peak transient)
MUST begin no earlier than **0.25 s** into the file. This is a hard authoring requirement.

**Rationale**: The music duck ramp takes 0.2 s (IDLE → DUCKING state, ramp to 0.4 gain).
A stinger whose peak lands at t=0 plays over music still at full gain, undermining the
intended ducked-stinger mix balance. A 0.25 s onset ensures the 0.2 s duck ramp completes
before the stinger body hits, with 50 ms of margin. A brief attack transient or build
envelope before the main body is acceptable and encouraged — this 0.25 s window can
contain a fade-in, a soft pre-transient, or silence. The main body must not arrive before
t=0.25 s.

**Verification**: load the stinger WAV in the DAW and check the waveform peak position.
The highest-amplitude region must begin at or after the 0.25 s mark. Stingers with an
earlier peak must be time-shifted (add 0.25 s of near-silence or gentle pre-attack at the
head) and re-exported before delivery.

---

## Stinger Trigger Rules

### `stinger_crisis`

Fires when a crisis event is triggered by the simulation (e.g., budget collapse, service
failure cascade). Drop (do not queue) if a `stinger_crisis` is already in progress.
Minimum 5 s between triggers of the same type.

### `stinger_milestone`

Fires only on City Rating tier transitions:

- Village → Town
- Town → City
- City → Metropolis
- Metropolis → Megalopolis

`stinger_milestone` does NOT fire for raw population-count milestones (e.g., reaching
100,000 population). The population milestone toast notification is shown on screen, but
no stinger fires for population count alone. The stinger fires only when the City Rating
tier threshold is crossed.

Drop (do not queue) if a `stinger_milestone` is already in progress. Minimum 5 s between
triggers of the same type.

---

## Mix Context

Stingers are authored to function in the following mix context:

- Music stems playing at 0.4 gain (ducked) during stinger playback.
- Ambient beds playing at full gain — ambient beds are NOT ducked.
- The stinger must be audible and intelligible over the full ambient mix at 0.4 music
  gain. Author and test at this mix ratio.

At −18 LUFS the stinger sits 2 dB above music stems at −16 LUFS (which are further
reduced to −16 + 20×log10(0.4) ≈ −24 LUFS effective during ducking). The stinger is
clearly audible in this context.

---

## Delivery Verification Checklist

- [ ] `stinger_crisis.wav` — mono WAV PCM, 44100 Hz, 2–4 s (target 3 s), −18 LUFS, ≤ −1 dBTP.
- [ ] `stinger_milestone.wav` — mono WAV PCM, 44100 Hz, 2–3 s (target 2.5 s), −18 LUFS, ≤ −1 dBTP.
- [ ] Both stingers auditioned over ambient bed at full gain + music at 0.4 gain — intelligible.
- [ ] **Onset timing verified for each stinger**: load in DAW, confirm peak impact begins at or
  after t=0.25 s in the file. A stinger whose main body peaks before 0.25 s must be time-shifted
  and re-exported.
- [ ] No stereo WAV files submitted (mono check mandatory).
- [ ] `stinger_game_over.wav` confirmed deferred (not submitted for V1).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/source-pool.md`
