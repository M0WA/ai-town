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
level discontinuity at the loop point.

---

## Delivery Verification Checklist

Before submitting assets for Phase 10 exit review, confirm each of the following.

- [ ] `sfx_zone_residential.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] `sfx_zone_commercial.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] `sfx_zone_industrial.ogg` — mono, 44100 Hz, 12–18 s, −26 LUFS, ≤ −2 dBTP.
- [ ] All three files have a 100 ms fade-to-silence tail (last sample ≤ −60 dBFS).
- [ ] All three files have a 100 ms fade-from-silence head (first sample ≤ −60 dBFS).
- [ ] DAW loopback verification completed for each file (minimum 5 cycles, no artifacts).
- [ ] No stereo files submitted (mono check mandatory).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/spatial-audio.md`
