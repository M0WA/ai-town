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

## Content Specification — Per Asset

### `stinger_crisis.wav`

**Trigger context**: fires when `consecutive_deficit_months >= 2` is first reached in a
deficit streak (the moment the city's finances have been in deficit for two consecutive
months). This is the sound of financial emergency onset — something is going seriously
wrong. The player has been warned once (via `sfx_budget_warn` at −25% deficit threshold)
and is now in an escalating crisis.

**Character**: urgent, tense, unmistakably negative. Should convey alarm without being
cartoonish or comedic. The stinger must be clearly distinguishable from `stinger_milestone`
in emotional valence — milestone is celebratory, crisis is alarming.

**Suggested approach** (the artist should use professional judgement; this is guidance
not prescription):

- A short, tense orchestral or hybrid-orchestral hit: dense string cluster, low brass
  stab, or distorted electronic impact — any of these is appropriate.
- Low brass (trombone cluster, low horn) with a dissonant interval (minor second,
  tritone) reinforces the "bad event" reading without requiring the player to consciously
  analyse it.
- A rising or falling pitch envelope on the attack transient (not a flat pitch) gives
  urgency.
- Add a short sub-bass impulse in the 60–100 Hz range on the initial transient to make
  the stinger physically felt on speaker systems.
- The body of the stinger (0.25 s to end) should have a short decay — it must disappear
  before the music (ducked to 0.4 gain) re-asserts itself. Target: 80–90% of the sound's
  energy delivered by the 1.5 s mark, with a decay tail to silence by 3–4 s.

**Mix context**: will play simultaneously with:

- Music stems ducked to 0.4 gain (approximately −24 LUFS effective from −16 LUFS stems)
- Ambient beds at full gain (−20 LUFS)
- Potentially `sfx_budget_warn` playing within the previous 1–2 s

At −18 LUFS, `stinger_crisis` sits approximately 6 dB above the ducked music and 2 dB
above the ambient bed. It will cut through clearly. Do not over-compress — a natural
dynamic envelope is more alarming than a heavily limited wall of sound.

---

### `stinger_milestone.wav`

**Trigger context**: fires on every City Rating tier transition:

- Village → Town (1,000 population)
- Town → City (10,000 population)
- City → Metropolis (50,000 population)
- Metropolis → Megalopolis (500,000 population)

This is a reward and achievement moment — the city has reached a new level of status.

**Character**: positive, celebratory, uplifting. Should convey a sense of achievement
and progression — a milestone reached. Clearly distinguishable from `stinger_crisis` in
emotional valence.

**Suggested approach**:

- A short orchestral fanfare or jingle: ascending melody or major-key chord resolution.
  A perfect fourth or fifth upward leap on the main melodic element sounds like progress.
- Bright metallic percussion (glockenspiel, bells, or synth chime) in the 2–4 kHz range
  gives clarity and reads as reward on any speaker system.
- A bright major chord resolution (I chord in any major key — the key does not need to
  match the music stems since stingers are non-harmonic interruptions, not continuations
  of the stem). The short duration means any harmonic content will fade before the music
  re-asserts.
- Sub-bass punch on the attack transient is optional for milestone — a lighter, more
  joyful character may not need the heavy sub impact appropriate for crisis.
- Duration guidance: the main melodic or chordal resolution should complete by 1.5–2 s.
  The decay tail brings the total to 2–3 s. A stinger that lingers past 3 s will keep
  the music ducked longer than necessary for a positive moment.

**Differentiation from crisis**: the two stingers must be clearly differentiable in
blind listening. Recommend verifying this by playing them to a colleague without context
and confirming they can identify which represents a good outcome and which a bad one.
If they cannot, revise.

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

**Blind-mix verification**: before delivery, render a test mix of the stinger playing
over:

- The ambient day bed at full gain (−20 LUFS)
- `music_calm_01` at 0.4 gain (approximately −24 LUFS effective)

Verify that the stinger is clearly audible and its intended emotional character (alarming
for crisis, celebratory for milestone) reads through the mix without ambiguity.

---

## Delivery Verification Checklist

- [ ] `stinger_crisis.wav` — mono WAV PCM, 44100 Hz, 2–4 s (target 3 s), −18 LUFS, ≤ −1 dBTP.
- [ ] `stinger_milestone.wav` — mono WAV PCM, 44100 Hz, 2–3 s (target 2.5 s), −18 LUFS, ≤ −1 dBTP.
- [ ] Both stingers auditioned over ambient bed at full gain + music at 0.4 gain — intelligible.
- [ ] **Onset timing verified for each stinger**: load in DAW, confirm peak impact begins at or
  after t=0.25 s in the file. A stinger whose main body peaks before 0.25 s must be time-shifted
  and re-exported.
- [ ] **Emotional differentiation verified**: both stingers played to a second listener without
  context; listener correctly identifies crisis (negative/alarming) vs. milestone (positive/celebratory).
- [ ] `stinger_crisis.wav` tonal character verified: dissonant interval, low brass or sub-bass
  impact, tense character.
- [ ] `stinger_milestone.wav` tonal character verified: ascending melodic element or major
  chord resolution, bright metallic percussion, celebratory character.
- [ ] No stereo WAV files submitted (mono check mandatory).
- [ ] `stinger_game_over.wav` confirmed deferred (not submitted for V1).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/source-pool.md`
