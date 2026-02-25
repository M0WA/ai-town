# Music Production Brief (SA-2) + Bar-Count Confirmation (SA-3)

<!-- APPROVED: sound-artist-opensoftal 2026-02-25 — crossfade demo of music_main_menu_01 into music_calm_01 reviewed; constant-power 3s crossfade at 90 BPM confirms harmonic compatibility at shared root key -->

## Overview

This document is the Phase 4 exit criterion for SA-2 (music production brief) and SA-3
(bar-count confirmation). All parameters in this document are locked. Any deviation is a
hard asset error that will fail `validate_assets.py` Check #14 and block Phase 10 exit.

---

## Locked Global Parameters

| Parameter | Value |
|---|---|
| BPM | 90 (locked — do not change) |
| Beats per bar | 4 |
| Sample rate | 44100 Hz (hard error if any other value — `AudioSystem` rejects non-conformant streams) |
| Bit depth | 16-bit |
| Channels | Stereo (2 channels) |
| Format | OGG Vorbis |
| Root key / mode | Shared across all 8 music files (cross-context harmonic compatibility) |
| Loudness target | −16 LUFS integrated |
| True-peak ceiling | −1 dBTP |

All 8 music files must share the same root key and mode. This is a cross-context harmonic
compatibility requirement: the engine can transition from any music context (main menu,
calm, growth, crisis) to any other on a bar boundary, and the crossfade must not produce
harmonic clashes.

**Ambient beds (`ambient_*.ogg`) are NOT covered by this brief.** They are a separate
deliverable (see `ambient-bed-production-brief.md`). No JSON sidecar is required for
ambient beds.

---

## SA-3: Locked Bar Counts

The following bar counts are locked. Non-integer bar counts cause bar-boundary crossfade
drift over long play sessions. All durations are derived from 90 BPM, 4/4 time
(1 bar = 4 beats = 2.6667 s).

| File | Bars | Duration at 90 BPM |
|---|---|---|
| `music_main_menu_01.ogg` | 48 | 128.00 s |
| `music_main_menu_02.ogg` | 48 | 128.00 s |
| `music_calm_01.ogg` | 32 | 85.33 s |
| `music_calm_02.ogg` | 32 | 85.33 s |
| `music_growth_01.ogg` | 32 | 85.33 s |
| `music_growth_02.ogg` | 32 | 85.33 s |
| `music_crisis_01.ogg` | 24 | 64.00 s |
| `music_crisis_02.ogg` | 24 | 64.00 s |

Duration formula: `bars × 4 beats × (60 s / 90 BPM) = bars × 2.6667 s`.

Verify bar count in the DAW before export. Export to sample-accurate length. A stem that
is even 1 sample long or short will accumulate crossfade drift; this will be caught by
`validate_assets.py` Check #14 duration tolerance check in Phase 5.

---

## JSON Sidecar Requirement

Every music file listed in the table above requires a co-located `.json` sidecar file.
The sidecar must conform to `tools/music_sidecar_schema.json` (Draft-07 JSON Schema).

**Required sidecar content for all 8 music files:**

```json
{"bpm": 90, "beats_per_bar": 4}
```

Sidecar file naming: replace `.ogg` with `.json` (e.g., `music_calm_01.json` beside
`music_calm_01.ogg`).

`validate_assets.py` Check #14 will reject any `music_*.ogg` asset that lacks a valid,
conformant `.json` sidecar. A sidecar with unknown additional fields or missing required
fields is a hard asset error.

Ambient beds (`ambient_*.ogg`) are explicitly exempt from this sidecar requirement.

---

## Asset List

All 8 files below must be delivered before Phase 10 exit.

| File | Bars | Format | Channels | Sidecar |
|---|---|---|---|---|
| `music_main_menu_01.ogg` | 48 | OGG Vorbis | Stereo | `music_main_menu_01.json` |
| `music_main_menu_02.ogg` | 48 | OGG Vorbis | Stereo | `music_main_menu_02.json` |
| `music_calm_01.ogg` | 32 | OGG Vorbis | Stereo | `music_calm_01.json` |
| `music_calm_02.ogg` | 32 | OGG Vorbis | Stereo | `music_calm_02.json` |
| `music_growth_01.ogg` | 32 | OGG Vorbis | Stereo | `music_growth_01.json` |
| `music_growth_02.ogg` | 32 | OGG Vorbis | Stereo | `music_growth_02.json` |
| `music_crisis_01.ogg` | 24 | OGG Vorbis | Stereo | `music_crisis_01.json` |
| `music_crisis_02.ogg` | 24 | OGG Vorbis | Stereo | `music_crisis_02.json` |

---

## Loop and Crossfade Requirements

Stems must loop cleanly on bar boundaries. The engine queues crossfades to the next bar
boundary on a 90 BPM grid. A stem whose tail does not line up to the bar boundary will
produce a click or phase artifact on every crossfade.

Authoring checklist:

- Export starts on beat 1 of bar 1.
- Export ends on the last sample of the final bar — not 1 sample after.
- No DC offset at head or tail.
- No fade-in or fade-out applied by the DAW — the engine handles gain ramping.
- All 8 files rendered at the identical root key and mode before export.

---

## Crossfade Demo Approval (Co-Delivered with SA-3)

Before full stem production begins, a 10–15 s low-fidelity rendered demo must be
delivered demonstrating:

- `music_main_menu_01` mixed into `music_calm_01`
- 3 s constant-power crossfade
- Crossfade triggered on a bar boundary at 90 BPM
- Cross-context harmonic compatibility audible (no key clash, no mode clash)

This demo is reviewed and approved alongside the SA-3 bar-count confirmation. Full stem
production is blocked until both SA-3 and the crossfade demo are approved.

The approval for this demo is recorded in the HTML comment at the top of this document.

---

## Delivery Verification Checklist

Before submitting assets for Phase 10 exit review, confirm each of the following.

- [ ] All 8 OGG files exported at 44100 Hz, 16-bit, stereo.
- [ ] All 8 OGG files loudness-checked: integrated LUFS = −16, true peak ≤ −1 dBTP.
- [ ] All 8 OGG files are integer-bar length at 90 BPM (sample-accurate).
- [ ] All 8 JSON sidecars present and conformant with `tools/music_sidecar_schema.json`.
- [ ] All 8 files share identical root key and mode.
- [ ] Loop tail of each file aligns to bar boundary with no click.
- [ ] Crossfade demo approved (recorded above).

---

## References

- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/streaming-architecture.md`
- `tools/music_sidecar_schema.json`
