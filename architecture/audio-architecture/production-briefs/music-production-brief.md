# Music Production Brief (SA-2) + Bar-Count Confirmation (SA-3)

<!-- APPROVED: sound-artist-opensoftal 2026-02-25 — (1) SA-2 cross-context demo: music_main_menu_01 into music_calm_01, constant-power 3s crossfade at 90 BPM, no key/mode clash; (2) gameplay crossfade demo: music_calm_01 into music_growth_01, constant-power 3s crossfade at 90 BPM, no harmonic clash at shared root key. Both demos reviewed and approved. Full stem production unblocked. -->

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
| Root key / mode | **A natural minor (Aeolian mode), root A** — locked for all 8 V1 music files. All intensity tiers including crisis use A natural minor. See `architecture/audio-architecture/dynamic-soundscape.md` Cross-tier harmonic compatibility requirement and the Mode constraint section below. |
| Loudness target | −16 LUFS integrated |
| True-peak ceiling | −1 dBTP |

All 8 music files must share the same root key. This is a cross-context harmonic
compatibility requirement: the engine can transition from any music context (main menu,
calm, growth, crisis) to any other on a bar boundary, and the crossfade must not produce
harmonic clashes.

**Root key and mode (V1 final decision — 2026-03-03):** All 8 music files are authored in
**A natural minor (Aeolian mode), root A**. This applies to all intensity tiers including
crisis. Rationale: A natural minor shares the key signature of C major (no sharps or flats)
while its neutral-to-melancholic Aeolian character suits the city-builder aesthetic; root A
at 440 Hz aligns to concert pitch (A440); and A natural minor provides a clear harmonic
vocabulary for differentiating calm, growth, and crisis intensity tiers through orchestration
weight, rhythmic density, and dissonant cluster voicings within the same scale. Crisis stems
differentiate from calm and growth via heavier percussion, denser layering, tighter rhythmic
syncopation, dissonant cluster voicings (e.g. diminished 7ths, tritone extensions, dense
minor-mode stacking), and increased orchestral density — all within A natural minor. A mode
change (e.g. parallel A major or any other mode) during a live crossfade would produce
harmonic dissonance between the simultaneous mode-mismatched chord material; this is not
acceptable because the engine does not control which specific chords are sounding at
the crossfade point. **Crisis stems in any mode other than A natural minor are prohibited in V1.**
If the sound artist later proposes parallel-major or other-mode crisis stems, a new 3 s
constant-power crossfade demo (A natural minor calm stem into the proposed mode crisis stem,
bar-aligned at 90 BPM) must be submitted and approved by the full team first. That
demo has not been requested for V1.

**Ambient beds (`ambient_*.ogg`) are NOT covered by this brief.** They are a separate
deliverable (see `ambient-bed-production-brief.md`). No JSON sidecar is required for
ambient beds.

---

## SA-3: Locked Bar Counts

The following bar counts are locked. Non-integer bar counts cause bar-boundary crossfade
drift over long play sessions. All durations are derived from 90 BPM, 4/4 time
(1 bar = 4 beats = 2.6667 s).

| File | Bars | Duration at 90 BPM | Sample count at 44100 Hz |
|---|---|---|---|
| `music_main_menu_01.ogg` | 48 | 128.00 s | 5,644,800 |
| `music_main_menu_02.ogg` | 48 | 128.00 s | 5,644,800 |
| `music_calm_01.ogg` | 36 | 96.00 s | 4,233,600 |
| `music_calm_02.ogg` | 36 | 96.00 s | 4,233,600 |
| `music_growth_01.ogg` | 36 | 96.00 s | 4,233,600 |
| `music_growth_02.ogg` | 36 | 96.00 s | 4,233,600 |
| `music_crisis_01.ogg` | 36 | 96.00 s | 4,233,600 |
| `music_crisis_02.ogg` | 36 | 96.00 s | 4,233,600 |

Duration formula: `bars × 4 beats × (60 s / 90 BPM) = bars × 2.6667 s`.

**Sample-count arithmetic (SA-3 verified):** At 44100 Hz, 90 BPM, 4 beats/bar:
`spb = (44100 × 60.0 / 90.0) × 4 = 117,600 samples/bar` (exact integer — zero drift).
All bar counts above are exact multiples of 117,600. The 48000→44100 Hz resampling ratio
(147/160) divides cleanly for every integer bar count: no fractional sample introduced.

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
| `music_calm_01.ogg` | 36 | OGG Vorbis | Stereo | `music_calm_01.json` |
| `music_calm_02.ogg` | 36 | OGG Vorbis | Stereo | `music_calm_02.json` |
| `music_growth_01.ogg` | 36 | OGG Vorbis | Stereo | `music_growth_01.json` |
| `music_growth_02.ogg` | 36 | OGG Vorbis | Stereo | `music_growth_02.json` |
| `music_crisis_01.ogg` | 36 | OGG Vorbis | Stereo | `music_crisis_01.json` |
| `music_crisis_02.ogg` | 36 | OGG Vorbis | Stereo | `music_crisis_02.json` |

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
- All 8 files rendered in **A natural minor (Aeolian mode), root A**. This applies to all
  intensity tiers including crisis. Crisis stems in any other key or mode are NOT permitted
  in V1 — a separate crossfade demo approval for any non-Aeolian mode is required first
  and has not been obtained. See `architecture/audio-architecture/dynamic-soundscape.md`
  and the Mode constraint section of this document.

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

- [x] All 8 OGG files exported at 44100 Hz, 16-bit, stereo.
- [ ] All 8 OGG files loudness-checked: integrated LUFS = −16, true peak ≤ −1 dBTP.
- [ ] All 8 OGG files are integer-bar length at 90 BPM (sample-accurate).
- [x] All 8 JSON sidecars present and conformant with `tools/music_sidecar_schema.json`.
- [ ] All 8 files confirmed in **A natural minor (Aeolian mode), root A** — verify by loading each OGG in the DAW and confirming root pitch is A and mode is Aeolian (natural minor); confirm no parallel-major or parallel-mode crisis stems are present.
- [ ] Loop tail of each file aligns to bar boundary with no click.
- [x] Crossfade demo approved (recorded above).

---

## SA-2 / SA-3 Sign-Off — 2026-03-02

**Reviewer:** sound-artist-opensoftal (acting as SA-2 Music Production Brief + SA-3 Bar-Count
Confirmation)

### Format and Sidecar — PASS

All 8 OGG files confirmed at 44100 Hz, stereo (2 channels), OGG Vorbis. All 8 JSON sidecars
present, parse as `{"bpm":90,"beats_per_bar":4}`, and fully conform to
`tools/music_sidecar_schema.json` (Draft-07): `bpm` integer ≥ 1, `beats_per_bar` integer ≥ 1,
no additional properties.

### Loudness — FAIL

Target: −16 LUFS integrated ±1 LU (−17 to −15 acceptable), true peak ≤ −1 dBTP.

| File | Integrated LUFS | True Peak dBFS | LUFS result | TP result |
|---|---|---|---|---|
| `music_main_menu_01.ogg` | −15.9 | −1.6 | PASS | PASS |
| `music_main_menu_02.ogg` | −15.9 | −2.3 | PASS | PASS |
| `music_calm_01.ogg` | −13.9 | −3.1 | FAIL (+2.1 LU hot) | PASS |
| `music_calm_02.ogg` | −13.9 | −3.8 | FAIL (+2.1 LU hot) | PASS |
| `music_growth_01.ogg` | −13.9 | −0.8 | FAIL (+2.1 LU hot) | FAIL (exceeds −1 dBTP) |
| `music_growth_02.ogg` | −13.9 | −1.2 | FAIL (+2.1 LU hot) | PASS |
| `music_crisis_01.ogg` | −16.0 | −3.2 | PASS | PASS |
| `music_crisis_02.ogg` | −15.9 | −4.3 | PASS | PASS |

4 files (`music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`) read −13.9 LUFS,
which is 2.9 LU above the −16 LUFS target and 0.9 LU outside the +1 LU tolerance window.
`music_growth_01` additionally clips the −1 dBTP ceiling at −0.8 dBFS. These assets are
synthetic/placeholder tones or early production exports and will require normalisation and
true-peak limiting before Phase 10 exit.

### Bar-Count Conformance (SA-3) — FAIL

Tolerance: ±0.05 s. Bar duration at 90 BPM 4/4: 2.6667 s.

| File | Expected bars | Expected dur (s) | Actual dur (s) | Delta (s) | Within tol | Actual bars |
|---|---|---|---|---|---|---|
| `music_main_menu_01.ogg` | 48 | 128.00 | 122.17 | −5.83 | FAIL | ~45.8 |
| `music_main_menu_02.ogg` | 48 | 128.00 | 124.94 | −3.06 | FAIL | ~46.9 |
| `music_calm_01.ogg` | 36 | 96.00 | 90.77 | −5.23 | FAIL | ~34.0 |
| `music_calm_02.ogg` | 36 | 96.00 | 93.89 | −2.11 | FAIL | ~35.2 |
| `music_growth_01.ogg` | 36 | 96.00 | 92.20 | −3.80 | FAIL | ~34.6 |
| `music_growth_02.ogg` | 36 | 96.00 | 93.38 | −2.62 | FAIL | ~35.0 |
| `music_crisis_01.ogg` | 36 | 96.00 | 93.66 | −2.34 | FAIL | ~35.1 |
| `music_crisis_02.ogg` | 36 | 96.00 | 90.41 | −5.59 | FAIL | ~33.9 |

All 8 files fail bar-count conformance. No file lands within ±0.05 s of its SA-3 locked
duration. Expected values for calm and growth updated from 32 bars (85.33 s) to 36 bars
(96.00 s) per the SA-2/SA-3 deep review amendment (2026-03-03) — see amendment note below.

### Outstanding items before Phase 10 exit

- **Loudness — 4 files hot:** `music_calm_01`, `music_calm_02`, `music_growth_01`,
  `music_growth_02` measure −13.9 LUFS integrated. Apply gain reduction of approximately
  −2.1 LU and re-export to reach −16 LUFS target (±1 LU).
- **True peak — 1 file over ceiling:** `music_growth_01` measures −0.8 dBFS true peak.
  Apply a true-peak limiter (ceiling −1 dBTP) before delivery.
- **Bar-count — all 8 files non-conformant:** Every stem deviates from its SA-3 locked
  duration by more than ±0.05 s. Re-export each file from the DAW at the corrected
  sample-accurate targets: calm/growth/crisis at **36 bars = 96.00 s = 4,233,600 samples**;
  main-menu at **48 bars = 128.00 s = 5,644,800 samples**. Verify with `validate_assets.py`
  Check #14 before Phase 10 exit review.
- **Root key / mode:** Requires DAW or audio-engineer review to confirm all 8 files share
  the same root key and mode. Cannot be confirmed by automated loudness measurement.
- **Loop tail click-check:** Requires DAW or audio-engineer review to confirm each file's
  tail aligns to the bar boundary with no click or phase artifact.

---

## SA-2 / SA-3 Deep Review Amendment — 2026-03-03

**Reviewers:** sound-artist-opensoftal (SA-2 musical feasibility) + sound-artist-opensoftal
(SA-3 bar-count technical analysis), independent parallel reviews.

### Bar-count correction — calm and growth: 32 → 36 bars

The original SA-3 locked value of 32 bars (85.33 s) for calm and growth stems was a spec
defect: `v1-audio-asset-manifest.md` requires all music stems to be 90–180 s, and
85.33 s is 4.67 s below that floor. The SA-3 table has been corrected to **36 bars
(96.00 s)**.

Rationale for 36 over the minimum-compliant 34 bars (90.67 s):

- 36 bars yields exactly 96.000 s — integer seconds, no repeating decimal.
- Aligns calm/growth/crisis to a single gameplay-tier bar count; only two values in the
  full table (36 and 48).
- Provides a 6 s margin above the 90 s manifest floor (vs. 0.67 s at 34 bars).
- Sample arithmetic confirmed exact: 36 × 117,600 = 4,233,600 samples with zero
  fractional residue at 44100 Hz, and 48000→44100 Hz resampling (ratio 147/160) is
  arithmetically exact for this value.

The previously-delivered calm/growth files (90–94 s, ~34–35 bars) are below the corrected
target and must be re-exported at 36 bars = 96.00 s before Phase 10 exit.

### Mode constraint — V1 final decision: A natural minor (Aeolian) locked for all stems

The Deep Review (2026-03-03) locked the root key and mode as **A natural minor (Aeolian),
root A** for all 8 V1 music files. Any alternative mode (parallel major, relative major,
or otherwise) is **permanently rejected for V1** (decision recorded in
`implementation/phase-10.md` Music stems deliverable and
`architecture/audio-architecture/dynamic-soundscape.md`).

Rationale for rejection:

- No crossfade audibility demo for A natural minor→parallel-major has been produced or approved.
- Producing the demo mid-Phase 10 adds schedule risk.
- Crisis intensity is fully achievable within A natural minor through orchestration, rhythmic
  density, and dissonant chord extensions without a mode change.
- The engine crossfade point is non-deterministic at the chord level — a simultaneous
  A minor calm chord and A major crisis chord at the crossfade moment would produce
  an audible harmonic clash, not "modal mixture."

**V1 final constraint**: All 8 music files (calm, growth, crisis, main-menu) are in
**A natural minor (Aeolian mode), root A**. No exceptions. Crisis stems in any other mode will
be rejected at the musical QA step (`crossfade_demo_qa.md`).

Post-V1: parallel-major crisis stems (`music_crisis_major_01/02`) may be added as
separate assets once a complete minor→parallel-major crossfade demo has been produced and
approved by the full team. The demo requirement from the original Deep Review text remains
in place for any post-V1 mode-change delivery.

### Variant compatibility authoring guidance

Both variants within a tier must share: (a) the same bar count; (b) tonic-chord arrival
at bar 1; (c) comparable textural density and dynamic envelope at bar 1; (d) compatible
bass voice-leading at the loop seam (bass interval ≤ a fifth between end of variant A and
bar 1 of variant B). Verify by setting up both variants on parallel DAW tracks aligned to
bar 1 and auditioning a manual crossfade at the final bar boundary before export.

---

## References

- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/streaming-architecture.md`
- `tools/music_sidecar_schema.json`
