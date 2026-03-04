# Music Bar Count Confirmation (SA-3)

**BPM**: 90 | **Beats per bar**: 4 | **Sample rate**: 44100 Hz

Bar duration: 4 beats × (60 s / 90 BPM) = 2.6667 s
Samples per bar: 44100 × 60.0 / 90.0 × 4 = 117,600 (exact integer — zero drift)

All values are locked per `architecture/audio-architecture/production-briefs/music-production-brief.md` SA-3 section (amended 2026-03-03).

```text
music_main_menu_01: 48 bars at 90 BPM = 128.00 s (5644800 samples at 44100 Hz)
music_main_menu_02: 48 bars at 90 BPM = 128.00 s (5644800 samples at 44100 Hz)
music_calm_01: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
music_calm_02: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
music_growth_01: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
music_growth_02: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
music_crisis_01: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
music_crisis_02: 36 bars at 90 BPM = 96.00 s (4233600 samples at 44100 Hz)
```

## Verification

Sample count arithmetic: `bars × 117,600 samples/bar`

- 48 bars × 117,600 = 5,644,800 samples — exact integer, no fractional residue
- 36 bars × 117,600 = 4,233,600 samples — exact integer, no fractional residue

The 48000 Hz → 44100 Hz resampling ratio (147/160) divides cleanly for both bar
counts — no fractional sample is introduced at any target duration.

## Sign-Off

**Author**: sound-artist-opensoftal
**Date**: 2026-03-04
**Status**: Locked. These values are final for V1. Any change requires a full SA-3
amendment with music-production-brief.md update and a new crossfade demo.

## References

- `architecture/audio-architecture/production-briefs/music-production-brief.md` (SA-3 table)
- `architecture/audio-architecture/v1-audio-asset-manifest.md` (Phase 10 QA Delivery Artifacts)
- `architecture/audio-architecture/dynamic-soundscape.md` (beat-boundary crossfade timing)
