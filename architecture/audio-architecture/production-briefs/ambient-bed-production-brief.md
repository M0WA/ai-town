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

**Current status (2026-03-04)**: All four files on disk are 90.00 s synthetic SoX
placeholders ("Processed by SoX"). None satisfy the dual-requirement (sample-0
click-free gate and 200 ms pre-baked crossfade tail). All four must be replaced with
DAW-authored production content before Phase 10 exit.

---

## Content Specification — Per Asset

These content decisions are locked. Deviating from the character guidance below
will cause the required day→night direct crossfade demo (`crossfade_demo_day_to_night.ogg`)
to fail the transition review.

### `ambient_day.ogg` — Day (06:00–20:00)

**Acoustic character**: mid-distance city ambience with moderate human activity.

**Required elements** (combine at appropriate levels):

- Distant traffic drone — a broadband bed of traffic movement 3–5 km away; this is the
  loudest consistent layer; should dominate the low-mid frequency range (150–600 Hz)
  and provide the bed's sense of distance and scale.
- Occasional mid-distance birdsong (2–3 species, non-tropical — sparrows, pigeons,
  starlings are appropriate); bird calls must be sparse and positioned in the stereo
  field, not panned hard left or right (keep within ±30% of centre to avoid conflict
  with HRTF-processed positional sources).
- Faint pedestrian activity: distant footsteps, occasional voice murmur — barely
  perceptible, adding organic texture below the traffic bed.
- Light breeze or air movement: a soft broadband noise below 800 Hz to fill the
  bottom of the stereo image.

**Stereo image**: wide but not extreme. Side content (bird calls, specific traffic
events) should occupy ±20–40% L/R. The main traffic drone should be close to centre
(≤ ±15%). Maximum correlation between L and R channels when measured over the full
file: do not use decorrelated noise that produces a hollow sound on mono summing.

**Tail character (final 15 s) — mandatory for day→night crossfade compatibility**:
The final 15 s of `ambient_day.ogg` must gradually reduce in energy and prominence:

- Reduce bird call activity to near-silence in the final 15 s (no prominent bird calls
  after 80 s if the file is 95 s, for example).
- Reduce traffic movement sense in the final 10 s by lowering the 2–4 kHz presence
  of the traffic drone, simulating late evening traffic thinning.
- No abrupt transient events (car horn, dog bark, loud voice) in the final 20 s.
- The final 5 s should feel like a gradual settling — lower energy, quieter. This
  allows a direct day→night 3 s constant-power crossfade at x3 speed to sound
  natural without a dusk intermediate.

**Head character (first 5 s — sample-0 loop quality)**:

- No abrupt transient at sample 0.
- Begin with the traffic drone already present at 70–80% of its peak level; fade to
  full over the first 5 s (a gentle 5 s fade-in from 70% to 100% is acceptable and
  provides a clean sample-0 boundary).
- Do NOT start with complete silence — silence at sample 0 will produce an audible gap
  at each loop boundary (the runtime seek-to-0 will produce a brief silence before
  audio re-enters).

---

### `ambient_night.ogg` — Night (23:00–05:00)

**Acoustic character**: quiet late-night urban environment. Traffic nearly absent.

**Required elements**:

- Very low-level distant traffic — barely perceptible constant hum below 300 Hz. The
  night traffic presence should be approximately 12–15 dB lower in perceived level than
  the day traffic drone when both files are metered at −20 LUFS.
- Insect ambient: distant cricket or cicada bed — a broadband tonal texture in the
  1–4 kHz range. Should be continuous and steady (not rhythmically accented). Level
  should be 3–5 dB below the night traffic hum in the mix so it reads as background
  rather than foreground.
- Occasional distant owl call or night bird — maximum 1–2 events per 90–120 s file.
  Sparse and unobtrusive.
- Light wind or air movement: soft broadband noise below 600 Hz; slightly higher
  perceived level than the day bed's air movement to give the night a subtle outdoor
  quality.

**Head character (first 5 s — sample-0 loop quality AND day→night crossfade compatibility)**:

- The opening of `ambient_night.ogg` is what the player hears immediately after a direct
  day→night crossfade at x3 speed. The night bed must NOT open with:
  - A sudden loud insect burst or accent.
  - Complete silence (which would produce an audible step from the day bed's residual energy).
  - Any prominent mid-frequency transient.
- The night bed should open with its full steady-state character already established
  (insect bed and distant traffic hum present from sample 0). If the insect bed has
  a slight fade-in over the first 3–5 s, that is acceptable and supports a clean
  sample-0 boundary; a hard start with full insect energy at sample 0 is also
  acceptable if the level is gentle (−25 LUFS or below for the insect layer alone).

**Stereo image**: narrower than day — night feels more interior and enclosed. Keep
side content within ±20% L/R. The insect bed may be slightly wider than the traffic
hum but should not be panned beyond ±35%.

---

### `ambient_dawn.ogg` — Dawn (05:00–06:00)

**Acoustic character**: pre-sunrise transition — quiet night transitioning toward early
day activity. Heard only at x1 and x2 simulation speed (at x3 the dawn window is collapsed
and this bed is never played). Author for quality at x1; compatibility with the night→day
direct crossfade is NOT required for this bed.

**Required elements**:

- Early birdsong: more prominent than night but sparser than day. A distinct dawn chorus
  character with 2–4 bird species, some of which are more prominent than the day bed's
  birds. Dawn birds may be panned slightly wider (±40% L/R) to give the chorus a spatial
  spread.
- Very light traffic — the very first cars of the morning; faint, intermittent. Less
  continuous than the day traffic drone.
- Crickets fading: the insect bed from night should still be present but at reduced level,
  fading toward inaudibility by the middle of the file, simulating morning silence of
  insects.
- Gentle air movement and natural ambience: wind, rustling — more nature-forward than
  the day bed's more urbanised character.

**Head character**: the dawn bed opens after the night bed at x1 speed. The transition
from night to dawn uses a 3 s constant-power crossfade. The dawn opening should accept
a night→dawn crossfade gracefully: open with birdsong already present at partial gain
(a gentle 3–5 s fade-in from 30% to full is acceptable) and retain faint insect texture
at the head.

---

### `ambient_dusk.ogg` — Dusk (20:00–23:00)

**Acoustic character**: late-evening transition — the energy and activity of day winding
down toward night. Heard only at x1 and x2 simulation speed. Author for quality at x1.

**Required elements**:

- Reduced traffic bed: the traffic drone from day should be present but lower in level
  and energy — 6–8 dB below the day traffic drone at equal LUFS. Less mid-frequency
  presence (the 2–4 kHz traffic detail should be reduced).
- Evening birds: fewer and less active than day. Occasional call, not a continuous chorus.
- Early insect onset: towards the end of the dusk file (final 30–40 s), introduce the
  first hint of the insect bed that will be prominent in the night bed — at low level
  (−35 LUFS or below for the insect layer alone) as a very subtle texture.
- Quiet domestic sounds at distance: very faint — TV audio at long distance, brief voice,
  door closing. These should be barely perceptible and occur no more than 2–3 times per
  90–120 s file.

**Head character**: the dusk bed opens after the day bed at x1 speed (20:00 crossfade).
The dusk opening should feel like a natural continuation of the day's energy at reduced
level — no abrupt silence, no strong transient.

---

## Stereo Field Guidance

Across all four beds:

- Keep the M (mid/sum) signal stronger than the S (side/difference) signal. A natural
  city ambience has good mono compatibility — vehicles, birds, and ambience radiate in
  all directions, not from specific stereo positions.
- Avoid hard-panning (> ±50% L/R) for any continuous element.
- The M/S width ratio (expressed as S-to-M dB level) should be approximately −6 to
  −10 dB (S is 6–10 dB quieter than M). This produces a moderately wide image that
  does not collapse badly to mono and does not compete with HRTF-processed positional
  sources.
- Verify stereo compatibility: bounce to mono and confirm the bed sounds natural (no
  hollow cancellation artifacts, no loss of important frequency content in the
  mono fold).

---

## Day→Night Direct Crossfade Compatibility (Mandatory Verification)

At the default simulation speed of x3, dawn and dusk ambient beds are collapsed —
transitions go directly day→night and night→day. This means players at the default speed
ALWAYS hear a direct day→night crossfade. This crossfade must sound natural.

**Required demo**: commit `assets/audio/crossfade_demo_day_to_night.ogg` before
`ambient_day.ogg` and `ambient_night.ogg` are declared production-final. This is a
mandatory Phase 10 exit deliverable.

**Demo content**: render a 10–15 s OGG at 44100 Hz stereo (-q 7) consisting of:

- ~4 s from the **tail** of `ambient_day.ogg` (the final 4 s of the file, not the head)
- A 3 s constant-power crossfade: `gain_out = cos(t × π/2)`, `gain_in = sin(t × π/2)`
  where t runs 0→1 over the 3 s window; both beds simultaneously audible
- ~3 s from the **head** of `ambient_night.ogg`

Apply a −0.1 dBTP true-peak limiter before OGG encoding. Do NOT loudness-normalise the
demo file.

**Acceptance criterion**: the 3 s overlap region must not produce a jarring transient,
tonal clash, or sudden silence. The fade from the day bed's reduced-energy tail into the
night bed's quiet opening must feel gradual. If the demo reveals a clash, revise the day
bed tail character (reduce bird call prominence, reduce traffic density) or the night bed
head character (reduce insect burst, soften opening) before committing the demo.

**When to produce**: produce this demo early in ambient bed production, before mastering
the final full-length beds. Structural tail/head content decisions are cheap to change
before the full bed is completed; they are expensive to change after.

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
- **Dawn and night cross-compatibility**: see Day→Night Direct Crossfade Compatibility
  section above. The day bed must have a lower-energy tail character so it bridges into
  night without needing dusk as a transition. The night bed must open acceptably after a
  direct day crossfade.
- **Stereo field**: ambient beds are stereo. They are not positional sources. Keep the
  stereo image moderate — wide panning of prominent elements can conflict with
  HRTF-processed positional sources. See Stereo Field Guidance section.
- **OGG encoding**: encode at **libvorbis -q 7** (minimum). Verify with `soxi` that the
  encoded file is stereo (2 channels) and 44100 Hz.
- **Re-export procedure for synthetic placeholders**: all four current `ambient_*.ogg`
  files are 90.00 s SoX-generated pink noise placeholders. They must be replaced with
  DAW-authored production content. Recommended DAW workflow:
  1. Build the bed in the DAW at 44100 Hz stereo using the content specification above.
  2. Verify sample-0 click-free loop (PRIMARY gate).
  3. Add 200 ms pre-baked DAW crossfade tail (SECONDARY step).
  4. Measure loudness with a BS.1770-3 meter and normalise to −20 LUFS.
  5. Apply true-peak limiter at −1 dBTP ceiling.
  6. Export to WAV at 44100 Hz 16-bit stereo, then encode to OGG at -q 7.
  7. Verify with `soxi`: channels = 2, rate = 44100, duration 90–120 s.
  8. Complete `assets/audio/ambient_bed_qa.md` entry for this file.

---

## Delivery Verification Checklist

- [ ] `ambient_day.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_night.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_dawn.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] `ambient_dusk.ogg` — stereo, 44100 Hz, 90–120 s, −20 LUFS, ≤ −1 dBTP, pre-baked 200 ms loop crossfade tail present.
- [ ] All four files individually loudness-verified (not group-averaged).
- [ ] **Sample-0 click-free gate passed for each file** — loop auditioned at sample 0 in DAW, minimum 5 cycles, no click or level discontinuity. This is the PRIMARY gate; must be verified before the crossfade tail step and again on the final export.
- [ ] Day bed tail character verified: reduced bird call activity in final 15 s; reduced traffic presence in final 10 s; no prominent transient events in final 20 s.
- [ ] Night bed head character verified: no sudden loud insect burst at opening; insect bed and distant traffic hum present from sample 0 at gentle level.
- [ ] Day→night direct crossfade audibility test committed to `assets/audio/crossfade_demo_day_to_night.ogg` and approved before asset lock.
- [ ] Stereo field verified: M/S width ratio approximately −6 to −10 dB; mono sum sounds natural; no hard-panned continuous element.
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
