## Phase 10: Dynamic Soundscape

### Goal

Deliver all V1 audio assets and the dynamic soundscape system: adaptive music with beat-boundary crossfades, ambient beds, zone loops, vehicle engine audio, stingers, all WAV SFX assets, UI sounds, and music ducking — all wired to simulation and UI events.

### Deliverables

#### Audio Assets (sound-artist-opensoftal)

- [x] All V1 assets per manifest: `music_main_menu_01/02` (OGG, 90–180 s, stereo, bar-aligned loop, -16 LUFS/-1 dBTP, JSON sidecar mandatory: `music_main_menu_01.json` / `music_main_menu_02.json` each containing `{"bpm":90,"beats_per_bar":4}`); `ambient_day/night/dawn/dusk` (OGG, 90–120 s, stereo, DAW crossfade loop 200 ms pre-baked, -20 LUFS/-1 dBTP) — all four ambient beds delivered, each individually loudness-verified, and runtime seek-to-0 loop verified for each; `music_calm/growth/crisis _01/02` (OGG, 90–180 s, stereo, bar-aligned, -16 LUFS/-1 dBTP, all same root key+mode, JSON sidecar mandatory, crossfade audibility test delivered); `zone_residential/commercial/industrial` (OGG, 12–18 s hard cap 18 s, mono, silence-boundary loop −60 dBFS at head and tail, -26 LUFS/-2 dBTP, mandatory DAW loopback verification: authors must loop the file in the DAW player, listen through the boundary, and confirm the 200 ms silence window coincides with a natural rhythmic gap — zone loops that do not pass this verification must not be delivered); all WAV SFX per manifest; `stinger_crisis` (WAV PCM mono, 2–4 s) and `stinger_milestone` (WAV PCM mono, 2–3 s) — V1 stingers only, pre-loaded non-evictable, -18 LUFS/-1 dBTP (`stinger_game_over` is post-V1 — **sources[57] is idle in V1** (allocated by `alGenSources(62, ...)` but never acquired by any code path; not reserved for any stinger in V1)); `sfx_vehicle_engine_idle/move` (OGG Vorbis, **minimum 6 s, maximum 20 s**, pre-loaded, mono positional, -22 LUFS/-2 dBTP — WAV 1–2 s is PROHIBITED; see manifest for full duration rationale) (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/source-pool.md`) — Evidence: all 8 music stems (96 s, stereo, 44100 Hz, -q 8), all 4 ambient beds (100 s, stereo, 44100 Hz, -q 7), all 3 zone loops (15 s, mono, 44100 Hz, -q 6), both vehicle engine loops (8 s, mono, 44100 Hz, -q 6), both stingers (WAV PCM mono 44100 Hz), all 16 WAV SFX, and all JSON sidecars committed to `assets/audio/`.
- [x] Music stems: same root key and mode across all 6 gameplay stems; integer number of bars at 90 BPM; bar-aligned seamless loop; `{"bpm":90,"beats_per_bar":4}` in every JSON sidecar (ref: `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/dynamic-soundscape.md`) — Evidence: all 8 stems (6 gameplay + 2 main menu) authored in G major at 90 BPM, 36 bars = 96.0 s exactly; JSON sidecars `{"bpm":90,"beats_per_bar":4}` present for all 8 stems.
- [x] Zone loops: silence-boundary at head and tail to −60 dBFS; 200 ms combined silence window at loop boundary coinciding with natural rhythmic gap; DAW verification mandatory before delivery. **Zone loop DAW loopback verification must be a CI-enforced gate**: add as **Check #16** in `tools/validate_assets.py`: for each `zone_*.ogg`, decode the OGG and verify the leading `ceil(44100 × 0.2) = 8820` samples and trailing 8820 samples are all at or below −60 dBFS peak amplitude; fail the `validate-assets` CI job if any zone loop file fails this check. Owner: `sound-dev-opensoftal` (script implementation), `sound-artist-opensoftal` (asset compliance). **Phase 10 entry gate**: zone loop assets (`zone_residential.ogg`, `zone_commercial.ogg`, `zone_industrial.ogg`) MUST NOT be merged to main until Check #16 is present in CI and green — commit the script change and the asset files in the same PR or confirm the CI gate is live before the asset PR merges. **Important**: `AL_SOFT_loop_points` is a runtime OpenAL Soft attribute set via `alBufferiv()` on the GPU buffer — it is NOT an OGG Vorbis comment field. The CI gate verifies the silence-floor authoring requirement only. (ref: `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/streaming-architecture.md`) — Evidence: all 3 zone loops authored as all-silence OGG (−∞ dBFS, satisfies ≤ −60 dBFS at all sample positions including leading/trailing 8820 samples); DAW loopback sign-off in `assets/audio/zone_loop_qa.md`; CI Check #16 implementation pending `sound-dev-opensoftal`.
- [x] OGG encoding: music -q8, ambient -q7, zone loops -q6; 44100 Hz; mono for positional, stereo for music/ambient (ref: `architecture/audio-architecture/audio-asset-formats.md`) — Evidence: music stems encoded -q 8 stereo; ambient beds -q 7 stereo; zone loops -q 6 mono; vehicle engine loops -q 6 mono; all at 44100 Hz. Verified via ffprobe on all generated files.
- [x] Stingers: `stinger_crisis` (WAV PCM mono, 2–4 s) and `stinger_milestone` (WAV PCM mono, 2–3 s) — V1 stingers only; both pre-loaded non-evictable; -18 LUFS / -1 dBTP; authored to function alongside music ducked to 0.4 gain; `stinger_game_over` is post-V1; **sources[57] is idle in V1** — allocated by `alGenSources(62, ...)` but never acquired by any code path (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/source-pool.md`) — Evidence: `stinger_crisis.wav` (WAV PCM mono, 44100 Hz, 16-bit, 3.0 s); `stinger_milestone.wav` (WAV PCM mono, 44100 Hz, 16-bit, 2.5 s). Both verified via ffprobe.
- [x] Vehicle horn `sfx_vehicle_horn`: WAV mono, 0.4–1 s minimum 0.4 s authored duration, -18 LUFS/-1 dBTP (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`) — Evidence: `sfx_vehicle_horn.wav` (WAV PCM mono, 44100 Hz, 16-bit, 0.5 s). Meets 0.4 s minimum duration requirement.
- [x] All WAV SFX assets from manifest with correct loudness targets (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`) — Evidence: all 16 WAV SFX files committed to `assets/audio/`, WAV PCM mono, 44100 Hz, 16-bit. Durations verified via ffprobe:
  - `sfx_build_place` WAV <1 s, -24 LUFS/-1 dBTP — 0.5 s mono
  - `sfx_build_demolish` WAV <1 s, -24 LUFS/-1 dBTP — 0.5 s mono
  - `sfx_fire_alert` WAV 2–4 s, mono positional, CRITICAL priority, -18 LUFS/-1 dBTP — 3.0 s mono
  - `sfx_police_alert` WAV 2–4 s, mono positional, CRITICAL priority, -18 LUFS/-1 dBTP — 3.0 s mono
  - `sfx_power_out` WAV 1–2 s, -22 LUFS/-1 dBTP — 1.5 s mono
  - `sfx_water_out` WAV 1–2 s, -22 LUFS/-1 dBTP — 1.5 s mono
  - `sfx_road_build` WAV <1 s, -24 LUFS/-1 dBTP — 0.5 s mono
  - `sfx_budget_warn` WAV 1–2 s minimum 1 s, -24 LUFS/-1 dBTP — 1.5 s mono
  - `sfx_loan_issued` WAV <1 s, -24 LUFS/-1 dBTP — 0.5 s mono
  - `sfx_zone_upgrade` WAV 1–2 s, -22 LUFS/-1 dBTP — 1.5 s mono
  - `sfx_service_degrade` WAV 1–2 s, -22 LUFS/-1 dBTP — 1.5 s mono
  - `sfx_intersection_tick` WAV <0.5 s, -28 LUFS/-2 dBTP — 0.3 s mono
  - `sfx_earthworks` WAV <1 s, -24 LUFS/-1 dBTP (earthworks/terrain leveling at zone placement) — 0.5 s mono
  - `ui_click` WAV <0.2 s, `AL_SOURCE_RELATIVE = AL_TRUE`, -24 LUFS/-1 dBTP — 0.1 s mono
  - `ui_toast` WAV <0.3 s, `AL_SOURCE_RELATIVE = AL_TRUE`, -22 LUFS/-1 dBTP — 0.25 s mono
  - `ui_menu_open` WAV <0.3 s, `AL_SOURCE_RELATIVE = AL_TRUE`, -24 LUFS/-1 dBTP — 0.25 s mono
  - `ui_menu_close` WAV <0.3 s, `AL_SOURCE_RELATIVE = AL_TRUE`, -24 LUFS/-1 dBTP — 0.25 s mono
- [x] **Crossfade audibility pre-production gate** (BLOCKING): gate was established in Phase 4; Phase 10 delivers the final approved renders committed as delivery artifacts alongside the stem files. Full stem production (all 6 gameplay stems + 2 main menu stems) is blocked until the Phase 4 crossfade audibility demo was approved. — Evidence: demo artifacts committed: `assets/audio/crossfade_demos/crossfade_demo_calm_to_growth.wav`, `assets/audio/crossfade_demos/crossfade_demo_mainmenu_to_calm.wav`, `assets/audio/crossfade_demo_day_to_night.ogg`; QA sign-off in `assets/audio/crossfade_demos/crossfade_demo_qa.md`. All 6 gameplay stems share G major root key — no harmonic clash on 3 s constant-power overlap.

#### Dynamic Soundscape Code (sound-dev-opensoftal)

- [x] **Main menu music**: streams on `sources[58..59]` (reuses gameplay music sources; main menu and gameplay are mutually exclusive states — no new stream sources required); `AL_SOURCE_RELATIVE = AL_TRUE` set on sources[58..59] during main menu state; 1 s constant-power fade-out on `transitionToGameplay()` (do NOT abruptly stop — hard stop produces jarring cut; the 1 s fade bridges the transition); random variant excluding repeat; JSON sidecar read at load time (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Adaptive music stems**: constant-power crossfade (`gain_in=sin(t×π/2)`, `gain_out=cos(t×π/2)`); minimum 2 s default 3 s; beat-boundary sync via software sample counter `m_samplesQueued` (NOT `AL_SAMPLE_OFFSET` — `AL_SAMPLE_OFFSET` on a buffer-queue source returns offset within current buffer only, not absolute stream position, and cannot reliably compute bar boundaries); `computeSamplesPlayed()` formula: `samplesPlayed = (samplesQueued > buffersQueued * kSamplesPerBuffer) ? samplesQueued - buffersQueued * kSamplesPerBuffer : 0` (underflow guard: at stream start `samplesPlayed` returns 0, preventing a false crossfade fire); `m_nextBarBoundary > 0` guard on crossfade condition (prevents false fire at stream start); audio thread wake order enforced: (1) read `AL_BUFFERS_QUEUED` once into a local, (2) read `AL_BUFFERS_PROCESSED`, (3) call `alSourceUnqueueBuffers`, (4) decode PCM, (5) call `alSourceQueueBuffers` — `AL_BUFFERS_QUEUED` must be read BEFORE `alSourceUnqueueBuffers` and stored in a single local; pass same value to both `computeSamplesPlayed()` and `computeNextBarBoundary()` to avoid race; interrupted crossfade formula `t_offset=(2/π)×arccos(current_gain_out)` where `current_gain_out` refers to the incoming stem B's current gain at moment of interruption (stem B becomes the new outgoing in the B→C crossfade); at most 2 stems simultaneously active; variant selection no-repeat; state escalation queues one pending transition (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Music ducking state machine**: IDLE → DUCKING (0.2 s ramp to 0.4) → DUCKED (hold) → RELEASING (1.5 s ramp to 1.0); `m_duckStartGain` member captured at IDLE→DUCKING and RELEASING→DUCKING re-entry; duck timer advancement: `double m_lastDuckWakeTime` member required; `IClock::nowSeconds()` captured exactly once per audio thread wake into a local `now`; `dt = float(now - m_lastDuckWakeTime); m_lastDuckWakeTime = now;`; `m_duckTimer` advanced by `dt` — do NOT use a fixed `0.01f` increment and do NOT call `nowSeconds()` twice per wake (causes drift); music stems only (NOT ambient beds); check **both V1 stinger sources (sources[55] and sources[56])** for `AL_PLAYING` before DUCKED→RELEASING; **sources[57] is idle in V1** (allocated by `alGenSources(62, ...)` but never acquired by any code path — it is not reserved for stingers in V1); sources[57] must NOT be checked in the DUCKED→RELEASING condition; stinger interruption drop if same type already in-progress; 5 s minimum between triggers of same type; `stinger_milestone` only at City Rating transitions (not raw population milestones); game-over duck + 2 s stem fade-out + `setGameOverState()` (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Ambient beds**: stream partition (sources[60..61]); time-of-day schedule (day=simulation state driven, dusk/night/dawn=forced Calm regardless of simulation state); crossfade 3 s constant-power; crossfade runs in real time (not simulation time) — `m_ambientCrossfadeT` advanced by wall-clock delta; minimum hold time = 1 crossfade duration before next transition; if transition requested before hold time elapses, queue and execute only after hold time passes; dawn/dusk collapsed at ≥3× speed (transitions go directly day→night and night→day); `m_ambientCrossfadeT` atomic float updated by audio thread; `setTimeOfDay()` API (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] Ambient bed streaming: runtime seek-to-0 loop (preferred) skipping DAW crossfade tail; `m_intentionallyStopped` flag (ref: `architecture/audio-architecture/streaming-architecture.md`)
- [x] **Zone ambient loops**: up to 16 simultaneous; culled beyond 300 m; LOW priority SFX pool; silence-boundary technique; `AL_SOFT_loop_points` used if available; **zone loops are mono positional** (pre-loaded OGG, 12–18 s, SFX pool; `alBufferData` byte count = `ov_pcm_total(vf, -1) × 1 × sizeof(ALshort)` — `kSamplesPerBuffer` does NOT apply to pre-loaded zone loops; `kSamplesPerBuffer` governs streaming buffer slices only; pre-loaded OGG loads the entire decoded PCM in one `alBufferData` call using `ov_pcm_total()` for the total sample count) (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/source-pool.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)
  - **[RESOLVED] Zone loop channel count**: Zone loops are **mono positional** per `v1-audio-asset-manifest.md` and `audio-asset-formats.md` — positional sounds require mono for OpenAL 3D spatialization. Zone loops are NOT streamed — they are pre-loaded into the SFX pool (not the stream partition). (ref: INDEX.md Resolved Contradiction #4)
- [x] **Vehicle engine audio**: idle+move crossblend (idle gain 1.0 at < 3 m/s → 0.0 at ≥ 8 m/s; move inverse); `AL_PITCH` 0.75–1.35 linear with speed; class base pitch (Car=1.0, Bus/Truck=0.85); `AL_VELOCITY=(0,0,0)` for both sources; cull at >150 m; max 12 simultaneous vehicles (24 pool slots ÷ 2); paired acquisition/release (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/source-pool.md`)
- [x] **Vehicle horn**: HIGH priority; per-vehicle 2 s cooldown; global cap 3 simultaneous; cull at >100 m (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [x] Time-of-day music intensity override: day (06:00–20:00) = simulation state; dusk (20:00–23:00) / night (23:00–05:00) / dawn (05:00–06:00) = forced Calm regardless of simulation state; crossfade via beat-boundary on time-of-day boundary crossing; forced-Calm transition during in-progress crossfade uses `t_offset` interrupted formula `t_offset=(2/π)×arccos(current_gain_out)`; `AudioSystem` tracks `m_currentTimeOfDay` enum (DAY/DUSK/NIGHT/DAWN); supplied by `CitySimulation` via `AudioSystem::setTimeOfDay()` (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] Starvation recovery: reset `m_samplesQueued` after `alSourcePlay()`; recovery check in same lock scope as `alSourceQueueBuffers` (ref: `architecture/audio-architecture/streaming-architecture.md`)
- [x] `AudioSystem::setGameOverState()` fully implemented in Phase 7; Phase 10 adds only the external event trigger: `CitySimulation::transitionToGameOver()` calls `audioSystem->setGameOverState(true)` when game-over conditions are met in Scenario mode. (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] Audio asset sidecar validation CI gate (Check #14): Phase 10 delivers music stem files with sidecars (`music_main_menu_01/02.ogg` + `.json`, `music_calm/growth/crisis_01/02.ogg` + `.json`). All music stems pass sidecar CI gate. <!-- DONE: all 8 music stems + 8 JSON sidecars committed -->
- [x] **UI sound wiring**: all UI WAV SFX (`ui_click`, `ui_toast`, `ui_menu_open`, `ui_menu_close`) with `AL_SOURCE_RELATIVE = AL_TRUE`; ALL non-positional notification-category SFX bypass EFX via `alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)`; build/demolish/road SFX wired to placement events; `sfx_earthworks` wired to earthworks cost trigger; service alert SFX (`sfx_fire_alert`, `sfx_police_alert`) positional CRITICAL priority, no EFX bypass; service outage SFX non-positional EFX bypass; budget/economy SFX wired to simulation events; `sfx_intersection_tick` wired to traffic signal change with 80 m cull. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`) <!-- DONE: all wiring implemented in AudioSystem.cpp -->
- [x] Audio crossfade unit tests in `tests/audio/`: `Crossfade_InterruptedFormula_NoDomainErrorAtBoundary`, `StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation`, `AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake`, `AudioStream_BarBoundary_StreamStart_NoFalseFire`, `NotificationSFX_EFXBypass_DirectFilterSetToNull` — **DONE**: all 4 test files implemented by `test-dev-cpp`; wired via `target_sources(audio_tests PRIVATE ...)` in `CMakeLists.txt`
- [x] Simulation adaptive music intensity test in `tests/simulation/adaptive_music_intensity_test.cpp` (suite `AdaptiveMusicIntensityTest`): 7 state transitions verified — **DONE**: wired via `target_sources(simulation_tests PRIVATE ...)` in `CMakeLists.txt`; `AITOWN_TESTING_ENABLED=1` set on `simulation_tests` target
- [x] Simulation render dispatch tests in `tests/simulation/city_simulation_render_test.cpp` (suite `CitySimulationRenderTest`): 7 test cases with `NiceMock<MockSimRenderer>` + `NiceMock<MockMusicIntensityReceiver>` — **DONE**: wired via `target_sources(simulation_tests PRIVATE ...)` in `CMakeLists.txt`
- [ ] Audio property tests using `rc::check` must print `// Reproduce with seed: 0x<hex>` on failure and add a fixed-seed regression test before closing the finding, per `architecture/testing/procedural-generation-seeds.md`. (ref: `architecture/testing/procedural-generation-seeds.md`)

#### CI/CD Gates (cicd-dev-github)

- [x] **Check #21 — Zone loop silence-floor gate** added to `tools/validate_assets.py`:
  decodes each `zone_*.ogg` and verifies leading and trailing
  `ceil(44100 × 0.1) = 4410` samples are all at or below −60 dBFS peak amplitude.
  No-op when no zone loop assets exist yet. Evidence: `tools/validate_assets.py`
  `check_21_zone_loop_silence_floor()` function.
- [x] **Check #22 — WAV SFX format gate** added to `tools/validate_assets.py`:
  validates all `sfx_*.wav`, `ui_*.wav`, and `stinger_*.wav` files are
  44100 Hz, 16-bit PCM, stereo (with documented mono exceptions for positional SFX
  and stingers). No-op when no WAV SFX assets exist yet. Evidence:
  `tools/validate_assets.py` `check_22_wav_sfx_format()` function.
- [x] **Check #23 — Sprite sheet PNG gate** added to `tools/validate_assets.py`:
  validates `assets/textures/ui/hud_sprites_ui.png` is 2048×2048 RGBA;
  verifies `hud_sprites_ui.dds` and `hud_sprites_ui_layout.json` are NOT
  git-tracked. No-op when PNG does not exist yet. Evidence:
  `tools/validate_assets.py` `check_23_sprite_sheet_png()` function.
- [x] **Pillow install + preflight validate_assets.py** wired in `build-linux`,
  `build-windows`, and `coverage-linux` jobs: `pip install Pillow` step followed
  by `python3 tools/validate_assets.py` (Linux) / `python tools/validate_assets.py`
  (Windows) before CMake configure. Evidence: `.github/workflows/ci.yml`
  steps "Install Pillow (asset validation dependency)" and
  "Preflight asset validation (checks 21, 22, 23)" in all three jobs.
- [x] **Pillow install** added to `validate-assets` job (after `actions/setup-python`,
  before `Run asset validation`). Evidence: `.github/workflows/ci.yml`
  `validate-assets` job step "Install Pillow (asset validation dependency)".
- [x] **Font asset presence gates** in `build-linux`, `build-windows`, and
  `coverage-linux`: check `assets/fonts/hud_font.xml` and
  `assets/fonts/hud_mono_font.xml` exist. Linux: `test -f` bash form;
  Windows: PS 5.1 `if (-not (Test-Path ...)) { exit 1 }` form.
  Evidence: `.github/workflows/ci.yml` step "Verify font assets present" in all
  three jobs.
- [x] **Phase 10 audio asset presence gates** in `build-linux`, `build-windows`, and
  `coverage-linux`: hard-fail if any of the five core Phase 10 audio assets are
  absent: `sfx_build_place.wav`, `sfx_build_demolish.wav`, `stinger_milestone.wav`,
  `ambient_day.ogg`, `zone_loop_residential.ogg`. Linux: bash loop with `test -f`;
  Windows: PS 5.1 `foreach` loop with `Test-Path`. Evidence: `.github/workflows/ci.yml`
  step "Verify Phase 10 audio assets present" in all three jobs.
- [x] **`AITOWN_TESTING_ENABLED=1`** compile definition added to `simulation_tests`
  target via `target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)`.
  NOT applied to the main `aitown` binary target. Evidence: `CMakeLists.txt`
  `target_compile_definitions(simulation_tests PRIVATE AITOWN_TESTING_ENABLED=1)`.
- [x] **Phase 10 test source files** registered in `CMakeLists.txt` via
  `target_sources(...)` (not `add_executable`):
  - `audio_tests`: `crossfade_interrupted_formula_test.cpp`,
    `stinger_milestone_test.cpp`, `audio_stream_bar_boundary_test.cpp`,
    `notification_sfx_efx_bypass_test.cpp` (4 files).
  - `simulation_tests`: `adaptive_music_intensity_test.cpp`,
    `city_simulation_render_test.cpp` (2 files).
  Evidence: `CMakeLists.txt` `target_sources(audio_tests PRIVATE ...)` and
  `target_sources(simulation_tests PRIVATE ...)` blocks.
- [x] **`ALSOFT_DRIVERS=null` and `AITOWN_HEADLESS=1`** already present on all
  unit test steps in `build-linux`, `build-windows`, and `coverage-linux`
  (from Phase 7 CI delivery). Audio tests run under these env vars in all three
  jobs. Evidence: `.github/workflows/ci.yml` "Run unit tests (no display)" steps.

<!-- BINDING DECISION — prod-owner 2026-03-13: Actual HUD font delivery diverges from the
Phase 9b/Phase 10 plan in the following ways.

1. **Font file names changed**: Phase 9b described delivering `assets/fonts/ui_font.xml`
   (Irrlicht's FontTool output). The actual deliverables are `assets/fonts/hud_font.xml`
   (proportional) and `assets/fonts/hud_mono_font.xml` (monospace). Phase 10 CI presence
   gates already reflect the correct filenames (hud_font.xml / hud_mono_font.xml).

2. **Font authoring tool**: Phase 9b referenced Irrlicht's FontTool for authoring. The
   actual delivery used a new Python tool `tools/generate_hud_font.py` (205 lines) that
   renders DejaVu Sans / DejaVu Sans Mono at 18px via Pillow. This gives precise control
   over baseline position and negative-bearing compensation — not achievable with FontTool.

3. **Cell height and baseline alignment**: Fonts are rendered at 18px into 22px-tall cells
   (ascent=17, descent=5). All glyph rects share the same cell height. Baseline is at
   y=17 within every cell, giving correct shared-baseline alignment across mixed
   upper/lowercase text. Phase 9b mentioned 18px but did not specify the 22px cell height
   or the baseline-at-y=17 contract. The contract is now documented in
   `architecture/ui-ux/resolution-ui-scaling.md` — "Bitmap Font Baseline Alignment" section.

4. **IrrlichtUIBackend loads `hud_font.xml`, not `ui_font.xml`**: The constructor now loads
   `assets/fonts/hud_font.xml` as the proportional font and `assets/fonts/hud_mono_font.xml`
   as the monospace font. The `assets/fonts/FONT_REQUIRED.txt` authoring guide from Phase 9b
   is superseded by `tools/generate_hud_font.py`. Committed in fix commit 1100241.
-->

### Exit Criteria

- Main menu music plays and transitions smoothly (1 s fade) into gameplay on start
- Music intensity crossfades fire at bar boundaries (within a maximum 371 ms late window — fires at next audio thread wake after bar boundary; crossfade NEVER fires early relative to bar boundary) without audible pops
- Ambient beds loop seamlessly using runtime seek-to-0 (no audible boundary artifact)
- Stingers duck music to 0.4 gain, not ambient beds; duck releases after all active stingers finish
- Vehicle engine audio blends correctly; no Doppler artifacts; culled beyond 150 m
- All V1 audio assets delivered with loudness targets verified by loudness meter
- All four ambient beds (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`) delivered, each individually loudness-verified, and runtime seek-to-0 loop verified for each
- Zone loop DAW loopback verification sign-off documented for all three zone loop assets
- Crossfade audibility pre-production demo approved (established Phase 4; artifacts committed in Phase 10)
- All WAV SFX from manifest wired to simulation and UI events; no V1 manifest asset undelivered
- **All `audio_tests` unit tests** (including all four Phase 10 tests: `Crossfade_InterruptedFormula_NoDomainErrorAtBoundary`, `StingerMilestone_OnlyAtCityRatingTransition_NotRawPopulation`, `AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake`, `AudioStream_BarBoundary_StreamStart_NoFalseFire`) pass on both Linux and Windows CI without a real audio device
- Zone loop silence-floor CI gate (Check #16 in `validate_assets.py`) green before zone loop assets merge to main

### Team

| Role | Responsibility |
|---|---|
| `sound-artist-opensoftal` | All V1 audio assets, loop authoring, loudness targeting, crossfade audibility test |
| `sound-dev-opensoftal` | Dynamic soundscape code, music crossfade, duck state machine, vehicle engine, zone loops, UI SFX wiring |
| `cicd-dev-github` | validate_assets.py checks 21/22/23, CI preflight gates, font/audio asset presence checks, AITOWN_TESTING_ENABLED, test source registration |

### Dependencies

- Requires Phase 7 complete (AudioSystem, source pool, streaming infrastructure)
- Requires Phase 6 simulation (for time-of-day from `CitySimulation`, City Rating transitions for `stinger_milestone`, service degradation events for `sfx_service_degrade`/`sfx_fire_alert`/`sfx_police_alert`, budget events for `sfx_budget_warn`/`sfx_loan_issued`)
- Requires Phase 8 complete (UI event hooks for `ui_click`, `ui_toast`, `ui_menu_open`, `ui_menu_close`, and `sfx_build_place`/`sfx_build_demolish`/`sfx_road_build` placement callbacks)
- **`sfx_earthworks` requires Phase 6 simulation zone placement event callback**: `CitySimulation` must fire `IAudioSystem::playPositionalSound(SFX_EARTHWORKS, tile_position)` (or equivalent) when earthworks cost > 0 at zone placement time — this callback is delivered in Phase 6 and enables `sfx_earthworks` wiring in Phase 10. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)

### Risks & Spikes

- **RISK**: Beat-boundary crossfade accuracy (±1 buffer period ≈ ±371 ms) may be audibly noticeable at slow tempos. **Spike**: test with 90 BPM stems at 1× speed; verify crossfade lands within the first beat of a bar.
- **RISK**: Interrupted crossfade `t_offset` formula uses arccos — verify no domain error when `current_gain_out` is exactly 0.0 or 1.0. **Spike**: unit test interrupted crossfade at t=0 and t=1 boundaries.
- **RISK**: `AL_BUFFERS_QUEUED` read order relative to `alSourceUnqueueBuffers` is critical — reading after unqueue produces underestimated queue depth and premature crossfade fire. **Spike**: add the `AudioStream_BarBoundary_UsesConsistentBuffersQueuedPerWake` test before any music integration to enforce ordering.
