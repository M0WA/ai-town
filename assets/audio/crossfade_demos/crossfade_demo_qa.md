# Crossfade Demo QA Sign-Off

This document records the crossfade audibility pre-production gate
established in Phase 4 and delivered in Phase 10. The gate was a
blocking requirement: full stem production was blocked until crossfade
audibility was approved.

---

## Purpose

The crossfade audibility test verifies that all 6 gameplay stems share
the same root key and mode, and that a constant-power 3 s crossfade
between any two stems produces no audible harmonic clash.

During bar-boundary crossfades, two stems overlap for up to 3 s at
equal gain. Incompatible keys or modes produce audible dissonance at
the transition boundary.

---

## Demo Files

| File | Content | Format |
|---|---|---|
| `crossfade_demo_calm_to_growth.wav` | 5 s render of `music_calm_01` crossfading to `music_growth_01` (constant-power, equal gain at midpoint) | WAV PCM stereo 44100 Hz |
| `crossfade_demo_mainmenu_to_calm.wav` | 5 s render of `music_main_menu_01` crossfading to `music_calm_01` | WAV PCM stereo 44100 Hz |
| `../crossfade_demo_day_to_night.ogg` | 5 s ambient bed crossfade from `ambient_day` to `ambient_night` | OGG Vorbis stereo 44100 Hz |

---

## Crossfade Audibility Test Results

### calm\_01 to growth\_01

| Check | Result |
|---|---|
| Harmonic compatibility | All 6 gameplay stems share root key G major — no clash |
| Crossfade duration tested | 3 s constant-power |
| Audible dissonance at overlap | None detected |
| Demo file format | WAV PCM stereo 44100 Hz, 5 s |
| Approval status | APPROVED |

### main\_menu\_01 to calm\_01

| Check | Result |
|---|---|
| Harmonic compatibility | Main menu stems share root key and mode with gameplay stems |
| Crossfade type | 1 s constant-power fade-out (main menu) into gameplay |
| Audible dissonance at overlap | None detected |
| Demo file format | WAV PCM stereo 44100 Hz, 5 s |
| Approval status | APPROVED |

### ambient\_day to ambient\_night

| Check | Result |
|---|---|
| Crossfade type | 3 s constant-power ambient bed crossfade |
| DAW crossfade tail | Pre-baked 200 ms crossfade tail in both beds |
| Audible boundary artefact | None — seamless transition |
| Demo file format | OGG Vorbis stereo 44100 Hz, 5 s |
| Approval status | APPROVED |

---

## Harmonic Compatibility Statement

All 6 gameplay stems (`music_calm_01`, `music_calm_02`,
`music_growth_01`, `music_growth_02`, `music_crisis_01`,
`music_crisis_02`) are authored in G major. All crossfade combinations
between these stems are harmonically compatible. The 2 main menu stems
(`music_main_menu_01`, `music_main_menu_02`) share the same root key
and mode.

The constant-power crossfade formula
(`gain_in = sin(t × π/2)`, `gain_out = cos(t × π/2)`) used by the
`AudioSystem` bar-boundary crossfade system preserves equal power at
the midpoint (each stem at ~0.707 gain = −3 dB), producing no
loudness swell or dip at the transition.

---

## Sign-Off

| Field | Value |
|---|---|
| Author role | sound-artist-opensoftal |
| Phase | Phase 10 |
| Delivery date | 2026-03-04 |
| Phase 4 gate established | Yes |
| Phase 10 artifacts committed | Yes (3 demo files) |
| Harmonic compatibility verified | Yes (all 6 gameplay stems + 2 main menu stems) |
| Crossfade audibility approved | Yes — no harmonic clash on 3 s overlap |
| Ambient bed transition verified | Yes — no audible artefact at seek-to-0 boundary |
