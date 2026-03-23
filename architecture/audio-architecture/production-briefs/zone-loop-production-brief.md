# Zone Loop Production Brief

## Overview

This document is the Phase 4 exit criterion for the Zone Loop Production Brief gate. All
parameters below are locked. Zone loops are positional mono OGG sources placed in 3D
space at zone centroids. They must not compete with music stems in the mix and must loop
without audible artifacts at the loop boundary.

---

## Locked Parameters

| Parameter | Value |
|---|---|
| Format | OGG Vorbis |
| Channels | Mono (1 channel) — positional source, NOT stereo |
| Sample rate | 44100 Hz |
| Duration | 12–18 s (hard cap: 18 s) |
| Loudness target | −26 LUFS integrated |
| True-peak ceiling | −2 dBTP |
| Loop boundary fade-to-silence tail | 100 ms |
| Loop boundary fade-from-silence head | 100 ms |
| Combined silence window at loop point | 200 ms |
| Silence floor at head and tail | ≤ −60 dBFS |
| Pre-load threshold (`kZoneLoopMaxPreloadDurationSeconds`) | 18.0 s |

### Why Mono

Zone loops are emitted from a positional `AL_SOURCE_RELATIVE = AL_FALSE` OpenAL Soft
source placed at the zone centroid. OpenAL Soft requires mono buffers for positional
3D sources. Submitting a stereo OGG as a zone loop is incorrect and will be rejected at
load time.

### Why −26 LUFS / −2 dBTP

Zone loops are subtle background ambience. At −26 LUFS they sit 10 dB below music stems
(−16 LUFS) and 6 dB below ambient beds (−20 LUFS), ensuring they do not mask melodic
content. The −2 dBTP ceiling (rather than the standard −1 dBTP) provides an additional
1 dB headroom margin to accommodate the transient energy that can appear at the
loop-boundary fade junction.

### Hard Cap at 18 s (Not 20 s)

The streaming/pre-load tier boundary in the engine is 20 s: assets under 20 s are
pre-loaded into a static buffer; assets at or above 20 s are streamed. Zone loops must be
pre-loaded (not streamed) to support positional sources without streaming latency. The
hard cap is set at 18 s, not 20 s, to stay safely below the tier boundary and allow for
minor encoder overhead. Do not conflate `kZoneLoopMaxPreloadDurationSeconds = 18.0f` with
the 20 s streaming tier boundary — they are distinct constants.

---

## Asset List

| File | Channels | Format | Duration | Loudness |
|---|---|---|---|---|
| `sfx_zone_residential.ogg` | Mono | OGG Vorbis | 12–18 s | −26 LUFS / −2 dBTP |
| `sfx_zone_commercial.ogg` | Mono | OGG Vorbis | 12–18 s | −26 LUFS / −2 dBTP |
| `sfx_zone_industrial.ogg` | Mono | OGG Vorbis | 12–18 s | −26 LUFS / −2 dBTP |

**Current status (2026-03-04)**: All three files on disk are 15.00 s synthetic SoX
placeholders ("Processed by SoX"). The silence-boundary authoring (100 ms head/tail
fade to −60 dBFS) has not been applied to these files. All three must be replaced with
DAW-authored production content before Phase 10 exit.

---

## Content Specification — Per Asset

These content decisions are locked. Zone loops operate in 3D space at zone centroids.
At 300 m cull distance and −26 LUFS at the reference, a player standing at the zone
edge (~30–50 m from the centroid) will hear the loop at approximately −32 to −36 LUFS
effective — very subtle. At 10 m from the centroid it will be clearly present. The
content must read as ambient texture, not as a foreground sound design element.

All three zone loops are positional mono sources. Author them as if they are sounds
arriving from a single point at the zone centre. Do not design for stereo width —
mono is mandatory. OpenAL Soft's HRTF processing will provide the spatial character at
runtime.

### `sfx_zone_residential.ogg` — Residential Zone

**Acoustic character**: quiet neighbourhood life. Human-scale activity, domestic and
street-level.

**Required elements** (the combination should feel like standing near the centre of a
residential block, hearing the neighbourhood around you):

- Distant lawn mower or garden activity: a gentle mid-frequency mechanical hum (800–
  1,500 Hz), intermittent — not continuous for the full 12–18 s. Should appear for
  4–6 s, disappear, return later in the loop.
- Children playing in the distance: very faint voices, laughter — 2–3 brief events per
  loop cycle, not a sustained murmur. Level should be approximately 8–12 dB below the
  full-scale −26 LUFS integrated level of the file.
- Occasional distant dog bark: 1–2 per loop cycle, short (< 0.5 s each), no reverb tail.
- Light traffic — single or occasional car pass in the distance: a brief (1–2 s)
  broadband noise event with low-pass character to simulate distance (roll off above
  2 kHz). 1–2 events per loop cycle.
- Ambient air/breeze: soft broadband noise below 500 Hz as the continuous bed. This is
  the quietest and most consistent layer, filling the loop between events.

**Loop body structure for silence-boundary compatibility**: the 200 ms combined silence
window (100 ms tail + 100 ms head) must fall in a natural rhythmic gap. Structure the
loop so that there is a quiet passage (only the ambient air layer present) near both
the tail and the head. Avoid ending or beginning the loop on a dog bark, lawn mower
onset, or voice burst. The quiet passage at both boundaries is what allows the silence-
boundary technique to be inaudible.

---

### `sfx_zone_commercial.ogg` — Commercial Zone

**Acoustic character**: busy street-level commerce. Shopfront activity, pedestrian
movement, and light commercial traffic.

**Required elements**:

- Pedestrian murmur: a continuous low-level voice texture (not individual words) at
  the sub-threshold of intelligibility — the acoustic character of a shopping street
  heard from 50 m. Broadband noise emphasis in the 300–3,000 Hz range with speech
  formant peaks.
- Occasional door chime or shop door mechanism: a brief metallic or bell-like sound
  (0.2–0.5 s), 2–3 per loop cycle. Should feel like a shop door opening or closing at
  medium distance.
- Light vehicle activity: more consistent than residential — brief passing car or
  delivery vehicle 2–3 times per loop. Higher frequency content than residential traffic
  (the commercial zone has closer, more direct traffic proximity).
- HVAC or mechanical hum: a subtle continuous mid-frequency hum (200–400 Hz) representing
  air conditioning units or commercial equipment. Should be barely perceptible — 15 dB
  below the integrated level.

**Loop body structure**: the HVAC hum provides a natural continuous bed that makes
silence-boundary transitions less abrupt than a purely event-based loop. The 200 ms
silence window should fall between a door chime or vehicle event and the next onset. If
the pedestrian murmur is continuous, apply a very short (100–150 ms) level reduction near
both boundaries to create the natural gap.

---

### `sfx_zone_industrial.ogg` — Industrial Zone

**Acoustic character**: factory and light industrial ambience. Machinery and mechanical
processes.

**Required elements**:

- Low-frequency mechanical drone: a continuous 50–150 Hz fundamental representing large
  machinery or industrial equipment. This is the dominant and most continuous layer —
  it provides the character that differentiates industrial from residential and commercial.
  The drone should be steady with very slow amplitude modulation (0.3–1 Hz) to avoid
  feeling static but not rhythmically prominent.
- Metal impact or industrial process event: a brief (0.1–0.3 s) metallic transient
  representing a press, clamp, or impact — 2–4 per loop cycle, irregular spacing (not
  evenly spaced, which would create a mechanical beat at the loop boundary). Keep
  individual event peaks below −30 dBFS (measured at the unprocessed output) so they
  do not create hot transients that violate the −2 dBTP ceiling after the loop-boundary
  fade.
- Ventilation or fan noise: a broadband mid-frequency noise (800–2,000 Hz) from large
  industrial fans, lower level than the drone.
- Occasional brief machinery rev or activity burst: 1–2 per loop cycle, 0.5–1 s duration
  each.

**Loop body structure**: the continuous low-frequency drone provides a natural ambient
bed, making the silence-boundary technique work well — the 200 ms silence window simply
pauses the drone momentarily. Schedule the metal impacts and activity bursts to avoid
the tail and head regions (no impact within the final 200 ms or first 200 ms of content
before the fade regions). The drone will resume smoothly after the silence window.

---

## Loop Boundary Requirements

Each zone loop must exhibit a clean silence window at the loop boundary so that the
transition from the loop tail back to the loop head is inaudible.

**Tail (end of file):**

- Apply a 100 ms linear fade-to-silence at the very end of the file.
- The last sample of the exported file must be at or below −60 dBFS.

**Head (start of file):**

- Apply a 100 ms linear fade-from-silence at the very start of the file.
- The first sample of the exported file must be at or below −60 dBFS.

This produces a 200 ms combined silence window across the loop boundary (100 ms from the
tail + 100 ms from the head). The engine loops the file by re-queuing from the beginning;
the silence window masks any buffer-swap glitch.

**DAW loopback verification** is mandatory before delivery. Set the DAW timeline to loop
the exported file and listen through at least 5 loop cycles to confirm no click, pop, or
level discontinuity at the loop point. Specifically verify that the 200 ms silence window
falls during a natural rhythmic gap in the content — not across active content.

---

## Re-Export Procedure for Placeholder Replacement

The current placeholder files are 15.00 s SoX pink-noise tones. Replace them as follows:

1. Open the DAW and create a new mono session at 44100 Hz.
2. Build the zone loop content using the per-asset specification above. Target duration:
   13–16 s of primary content (before adding the 100 ms fade regions at head and tail).
3. Apply a 100 ms linear fade-in at the head: the first sample must be at or below
   −60 dBFS.
4. Apply a 100 ms linear fade-out at the tail: the last sample must be at or below
   −60 dBFS.
5. Render to WAV at 44100 Hz, 16-bit, mono. Verify total duration is 12–18 s.
6. Measure integrated LUFS with a BS.1770-3 meter. Apply gain to reach −26 LUFS.
   Apply a true-peak limiter with ceiling −2 dBTP.
7. Check that the −60 dBFS silence floor requirement is still met at head and tail after
   loudness normalisation (the limiter should not affect the silence regions, but verify).
8. Export to OGG Vorbis at -q 6 using libvorbis.
9. Verify with `soxi`: channels = 1, rate = 44100, duration 12–18 s.
10. Import back into the DAW and perform DAW loopback verification: loop the OGG file
    and listen through at least 5 cycles. Confirm the silence window (200 ms total)
    falls in a natural rhythmic gap. If not, revise the loop trim points and repeat.
11. Complete the `assets/audio/zone_loop_qa.md` entry for this file.

**CI gate verification**: `validate_assets.py` Check #21 will decode the OGG and verify
that the leading 4410 samples (100 ms at 44100 Hz) and trailing 4410 samples are each
at or below −60 dBFS peak amplitude. Both windows are independent pass/fail checks. Run
the validation script locally before committing the asset.

---

## OGG Encoding Quality

Encode at **libvorbis -q 6** (minimum). Zone loops are mono with simpler content than
music or ambient beds; -q 6 is sufficient and produces approximately 192 kbps VBR.

Verification command:

```bash
oggenc -q 6 -o sfx_zone_residential.ogg sfx_zone_residential.wav
soxi sfx_zone_residential.ogg
# Expected: Channels = 1, Sample Rate = 44100, duration 12–18 s
```

---

## Delivery Verification Checklist

Before submitting assets for Phase 10 exit review, confirm each of the following.

- [ ] `sfx_zone_residential.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] `sfx_zone_commercial.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] `sfx_zone_industrial.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] All three files have a 100 ms fade-to-silence tail (last sample ≤ −60 dBFS).
- [ ] All three files have a 100 ms fade-from-silence head (first sample ≤ −60 dBFS).
- [ ] DAW loopback verification completed for each file (minimum 5 cycles, no artifacts).
- [ ] 200 ms silence window confirmed to fall during a natural rhythmic gap in each file.
- [ ] Content specification verified per asset: residential (neighbourhood sounds), commercial (pedestrian/commerce), industrial (mechanical drone/impacts).
- [ ] CI Check #21 (`validate_assets.py`) passes locally for all three files before committing.
- [ ] No stereo files submitted (mono check mandatory).
- [ ] `assets/audio/zone_loop_qa.md` committed with one entry per file (DAW tool, loop boundary confirmed natural, author sign-off).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/spatial-audio.md`
