# V1 Audio Asset Manifest

| Asset Name | Category | Format | Duration | Loop | Notes |
|---|---|---|---|---|---|
| `music_main_menu_01` | Main menu music | OGG | 90–180 s | Y | Main menu screen music; **stereo; 2 channels**; streamed; bar-aligned seamless loop (no silence at boundary); `AL_SOURCE_RELATIVE = AL_TRUE` (non-positional); **JSON sidecar mandatory** (`music_main_menu_01.json`: `{"bpm":90,"beats_per_bar":4}`); authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error** |
| `music_main_menu_02` | Main menu music | OGG | 90–180 s | Y | Main menu music variant (same key, BPM, harmonic compatibility); **stereo; 2 channels**; streamed; bar-aligned seamless loop; `AL_SOURCE_RELATIVE = AL_TRUE`; **JSON sidecar mandatory** (`music_main_menu_02.json`); authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error** |
| `ambient_day` | Ambient bed | OGG | 90–120 s | Y | City hum, birds, traffic; **stereo; 2 channels**; streamed; **DAW crossfade loop** (200 ms pre-baked crossfade at loop boundary; no silence floor); authored to **−20 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **No JSON sidecar required** — ambient beds use real-time crossfade duration (constant-power curve), not bar-boundary sample counting. `music_sidecar_schema.json` and validate_assets.py Check #14 do NOT apply to ambient bed OGG files. |
| `ambient_night` | Ambient bed | OGG | 90–120 s | Y | Quiet, insects, distant traffic; **stereo; 2 channels**; streamed; **DAW crossfade loop**; authored to **−20 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **No JSON sidecar required** — ambient beds use real-time crossfade duration (constant-power curve), not bar-boundary sample counting. `music_sidecar_schema.json` and validate_assets.py Check #14 do NOT apply to ambient bed OGG files. |
| `ambient_dawn` | Ambient bed | OGG | 90–120 s | Y | Birds, early traffic; **stereo; 2 channels**; streamed (30–60 s causes loop fatigue); **DAW crossfade loop**; authored to **−20 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **No JSON sidecar required** — ambient beds use real-time crossfade duration (constant-power curve), not bar-boundary sample counting. `music_sidecar_schema.json` and validate_assets.py Check #14 do NOT apply to ambient bed OGG files. |
| `ambient_dusk` | Ambient bed | OGG | 90–120 s | Y | Evening ambient; steady moderate traffic; **stereo; 2 channels**; streamed (30–60 s causes loop fatigue); **DAW crossfade loop**; authored to **−20 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **No JSON sidecar required** — ambient beds use real-time crossfade duration (constant-power curve), not bar-boundary sample counting. `music_sidecar_schema.json` and validate_assets.py Check #14 do NOT apply to ambient bed OGG files. |

> **Ambient bed JSON sidecar exemption**: Phase 5 validate_assets.py Check #14 ("Music sidecar .json file present and valid for each .ogg music stem file") applies ONLY to music stem files (`music_main_menu_*.ogg`, `music_calm_*.ogg`, `music_growth_*.ogg`, `music_crisis_*.ogg`). Ambient bed OGG files (`ambient_day.ogg`, `ambient_night.ogg`, `ambient_dawn.ogg`, `ambient_dusk.ogg`) are explicitly exempted — they require no JSON sidecar. Ambient bed crossfades use a real-time constant-power curve driven by wall-clock duration; the `AudioSystem` does not read BPM or beats-per-bar data for ambient beds. The `music_sidecar_schema.json` schema and the sidecar validation step in the asset pipeline must not flag missing sidecars for files matching the `ambient_*.ogg` pattern.
>
> **Ambient bed OGG header validation**: `AudioSystem` validates the OGG Vorbis header of each ambient bed file at load time using the same check applied to music stems. Specifically, `AudioSystem` reads the `vorbis_info` struct (via `ov_info()`) immediately after `ov_open_callbacks()` and refuses to play the asset if `vi->rate != 44100` or `vi->channels != 2`. A mismatched ambient bed produces a logged error ("ambient bed `<filename>` has wrong sample rate or channel count — expected 44100 Hz stereo") and the stream is silenced for that bed slot for the lifetime of the session. This ensures the streaming infrastructure (sources[58..61], shared with music stems) receives only conformant PCM data.
| `music_calm_01` | Music stem | OGG | 90–180 s | Y | Calm exploration music; **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE` (non-positional); bar-aligned seamless loop; no fade/silence at boundary; **JSON sidecar `music_calm_01.json` mandatory** (`{"bpm":90,"beats_per_bar":4}`); authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** (cross-tier harmonic compatibility — see Dynamic Soundscape spec) |
| `music_calm_02` | Music stem | OGG | 90–180 s | Y | Calm exploration music (variant); **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE`; bar-aligned seamless loop; **JSON sidecar mandatory**; authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** |
| `music_growth_01` | Music stem | OGG | 90–180 s | Y | City growing, energetic; **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE`; bar-aligned seamless loop; **JSON sidecar mandatory**; authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** |
| `music_growth_02` | Music stem | OGG | 90–180 s | Y | City growing (variant); **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE`; bar-aligned seamless loop; **JSON sidecar mandatory**; authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** |
| `music_crisis_01` | Music stem | OGG | 90–180 s | Y | Crisis / disaster theme; **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE`; bar-aligned seamless loop; **JSON sidecar mandatory**; authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** |
| `music_crisis_02` | Music stem | OGG | 90–180 s | Y | Crisis variant; **stereo; 2 channels**; `AL_SOURCE_RELATIVE = AL_TRUE`; bar-aligned seamless loop; **JSON sidecar mandatory**; authored to **−16 LUFS / −1 dBTP**; **44100 Hz, 16-bit stereo — authoring at any other sample rate is a hard asset error**; **MUST share root key and mode with all 6 gameplay stems** |
| `sfx_zone_residential` | Zone loop | OGG | 12–18 s | Y | Residential ambience; mono positional; **pre-loaded; hard cap 18 s** (pre-load tier boundary is 20 s; stay safely below); **silence-boundary loop** (−60 dBFS at head and tail; **silence window: at least 100 ms at head AND at least 100 ms at tail** per `audio-asset-formats.md`; CI gate Check #21 verifies leading 4410 samples AND trailing 4410 samples ≤ −60 dBFS, two independent checks); authored to **−26 LUFS / −2 dBTP** (subtle background positional — should not compete with music stems) |
| `sfx_zone_commercial` | Zone loop | OGG | 12–18 s | Y | Commercial activity; mono positional; pre-loaded; hard cap 18 s; **silence-boundary loop** (−60 dBFS at head and tail; same 100 ms silence requirement and Check #21 CI gate as `sfx_zone_residential`); authored to **−26 LUFS / −2 dBTP** |
| `sfx_zone_industrial` | Zone loop | OGG | 12–18 s | Y | Factory, industrial; mono positional; pre-loaded; hard cap 18 s; **silence-boundary loop** (−60 dBFS at head and tail; same 100 ms silence requirement and Check #21 CI gate as `sfx_zone_residential`); authored to **−26 LUFS / −2 dBTP** |

**Zone loop true peak margin**: Zone loops use −2 dBTP (vs. −1 dBTP for most one-shots) because the loop boundary transient plus continuous looping content requires additional peak headroom.

| `sfx_build_place` | SFX | WAV | <1 s | N | Building/zone placed; **positional (3D, played at tile center — `AL_SOURCE_RELATIVE = AL_FALSE`; source position set to the world-space centroid of the placed tile)**; consistent with `sfx_road_build`; authored to **−24 LUFS / −1 dBTP** (subtle placement feedback) |
| `sfx_build_demolish` | SFX | WAV | <1 s | N | Building demolished; **positional (3D, played at tile center — `AL_SOURCE_RELATIVE = AL_FALSE`; source position set to the world-space centroid of the demolished tile)**; authored to **−24 LUFS / −1 dBTP** |
| `sfx_fire_alert` | SFX | WAV | 2–4 s | N | Fire service event; **mono positional** (3D spatial at building location); CRITICAL priority; authored to **−18 LUFS / −1 dBTP** (must cut through ambient/music at a crisis moment) |
| `sfx_police_alert` | SFX | WAV | 2–4 s | N | Police service event; **mono positional** (3D spatial at building location); CRITICAL priority; authored to **−18 LUFS / −1 dBTP** |
| `sfx_power_out` | SFX | WAV | 1–2 s | N | Power outage notification; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−22 LUFS / −1 dBTP** |
| `sfx_water_out` | SFX | WAV | 1–2 s | N | Water outage notification; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−22 LUFS / −1 dBTP** |
| `sfx_road_build` | SFX | WAV | <1 s | N | Road construction feedback; **positional (3D, played at tile center — `AL_SOURCE_RELATIVE = AL_FALSE`; source position set to the world-space centroid of the road tile being built)**; authored to **−24 LUFS / −1 dBTP** |
| `sfx_budget_warn` | SFX | WAV | 1–2 s | N | Budget deficit warning chime; fires when the city crosses the −25% deficit threshold; non-looping one-shot event alert. A sub-second chime is not long enough to register as a distinct alert separate from UI click sounds — minimum 1 s ensures perceptibility. **Confirmed WAV 1–2 s non-looping one-shot** (event chime for deficit threshold crossing, not a persistent loop). The 6 s minimum rule applies to engine loops only — non-looping one-shots have no loop fatigue concern. Resolution: 2026-02-20. **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−24 LUFS / −1 dBTP** (general feedback SFX tier; recurring alert must not dominate the mix during extended budget crises). |
| `sfx_loan_issued` | SFX | WAV | <1 s | N | Loan auto-issued; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−24 LUFS / −1 dBTP** |
| `stinger_crisis` | Music stinger | **WAV PCM** | 2–4 s | N | Crisis start; **mono; 1 channel**; one-shot; pre-loaded; reserved non-evictable SFX pool source **(index 55 — `StingerType::CRISIS`)**; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — stingers represent game state, not 3D position; must not be distance-attenuated); music ducks to 0.4 gain on playback; authored to −18 LUFS / −1 dBTP |
| `stinger_milestone` | Music stinger | **WAV PCM** | 2–3 s | N | Fires for **City Rating tier transitions** (Village→Town at 1K population; Town→City at 10K; City→Metropolis at 50K; Metropolis→Megalopolis at 500K). The 100K population milestone does NOT trigger this stinger — 100K is a population toast milestone only, not a City Rating transition. Population milestone toasts (1K/10K/50K/100K/500K) always appear regardless of whether the stinger fires. At thresholds that coincide with a City Rating transition (1K/10K/50K/500K), only the City Rating stinger fires — the population milestone toast still appears but no second stinger triggers; **mono; 1 channel**; one-shot; pre-loaded; reserved non-evictable SFX pool source (index 56); **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional); music ducks to 0.4 gain on playback; authored to −18 LUFS / −1 dBTP. If a lightweight non-ducked sound for population-count-only milestones (e.g. 100K) is desired in future, a separate `sfx_population_notification` asset (WAV, <0.5 s, HIGH priority, no ducking) should be added post-V1. |
| `sfx_vehicle_engine_idle` | Vehicle SFX | **OGG Vorbis** | **6 s &le; duration &lt; 20 s** | Y | Mono positional; **pre-loaded** (full decode at load into AL buffer); pitch-shifted per vehicle class. **WAV is prohibited** — a 1–2 s WAV loop at 44100 Hz / mono is audibly mechanical; OGG with a loop in the 6–19 s range sounds natural. **Minimum 6 s** (not 4 s): at the lowest pitch-shift ratio (0.75 for stopped vehicles), a 4 s loop at 44100 Hz is compressed to 4 / 0.75 ≈ 3 s perceived loop length — audible repetition. At minimum 6 s, the lowest-pitch loop is ~4.5 s perceived, which is below the perceptibility threshold for engine loops. **Maximum strictly less than 20 s** (Tier 2/Tier 3 boundary; the CI gate uses `>= 20.0` as the hard failure condition — a file at exactly 20 s is rejected). Authored to **−22 LUFS / −2 dBTP** (before OpenAL distance attenuation and pitch-shift — these affect perceived level at runtime but not authoring loudness). |
| `sfx_vehicle_engine_move` | Vehicle SFX | **OGG Vorbis** | **6 s &le; duration &lt; 20 s** | Y | Mono positional; pre-loaded; pitch varies with speed. Same duration rationale as `sfx_vehicle_engine_idle` (minimum 6 s, not 4 s; maximum strictly less than 20 s — files at exactly 20 s are rejected by the CI gate). Authored to **−22 LUFS / −2 dBTP**. |
| `sfx_vehicle_horn` | Vehicle SFX | WAV | **0.4–1 s** | N | Mono positional; **HIGH priority** (accesses the transient reserve sources[51..54]; NORMAL would be starved by engine sources in large cities); authored to **−18 LUFS / −1 dBTP**; minimum authored duration **0.4 s** (shorter sounds audibly like a click, not a horn). **Rate-limiting and source caps (enforced in AudioSystem)**: (1) **per-vehicle re-trigger cooldown**: 2 s minimum between horn triggers from the same vehicle entity; (2) **global simultaneous cap**: max 3 horn sources playing at any time across all vehicles — if already at 3 active sources, new horn requests are dropped silently; (3) **cull distance**: 100 m from listener (beyond which the horn is inaudible at `AL_INVERSE_DISTANCE_CLAMPED` max distance for vehicles). |
| `sfx_intersection_tick` | SFX | WAV | <0.5 s | N | Traffic signal change sound; optional distance cull at >80 m; authored to **−28 LUFS / −2 dBTP** (very subtle ambient detail) |
| `sfx_zone_upgrade` | SFX | WAV | 1–2 s | N | Zone tile auto-upgraded to higher density tier; positive/rewarding tone; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−22 LUFS / −1 dBTP** |
| `sfx_service_degrade` | SFX | WAV | 1–2 s | N | Service building entered reduced-coverage state (budget deficit degradation); warning tone distinct from `sfx_budget_warn`; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional — plays at listener position regardless of world coordinates); **EFX bypass: yes (non-positional — no EFX send, no reverb insert, no lowpass occlusion applied)**; authored to **−22 LUFS / −1 dBTP** |
| `sfx_earthworks` | SFX | WAV | <1 s | N | Earthworks/terrain leveling applied at zone placement; short percussive impact; **mono positional**; **`AL_SOURCE_RELATIVE = AL_FALSE`** (world-space — source position set to tile world-space centroid at trigger time); **`AL_DIRECT_FILTER: AL_FILTER_NULL` — EFX bypass because construction occurs on open, unoccluded tiles (design choice). This does NOT make the sound non-positional — `sfx_earthworks` remains a world-space positional source with `AL_SOURCE_RELATIVE = AL_FALSE`. Do NOT set `AL_SOURCE_RELATIVE = AL_TRUE`.** No lowpass occlusion applied; authored to **−24 LUFS / −1 dBTP** |
| `ui_click` | UI | WAV | <0.2 s | N | Button click; **`AL_SOURCE_RELATIVE = AL_TRUE`** (non-positional); authored to **−24 LUFS / −1 dBTP** |
| `ui_toast` | UI | WAV | <0.3 s | N | Toast notification chime; **`AL_SOURCE_RELATIVE = AL_TRUE`**; authored to **−22 LUFS / −1 dBTP** (must be clearly audible as a notification) |
| `ui_menu_open` | UI | WAV | <0.3 s | N | Menu opened; **`AL_SOURCE_RELATIVE = AL_TRUE`**; authored to **−24 LUFS / −1 dBTP** |
| `ui_menu_close` | UI | WAV | <0.3 s | N | Menu closed; **`AL_SOURCE_RELATIVE = AL_TRUE`**; authored to **−24 LUFS / −1 dBTP** |

**Implementation rule for `stinger_milestone`**: `stinger_milestone` fires on every City Rating tier transition event (Village→Town, Town→City, City→Metropolis, Metropolis→Megalopolis), regardless of whether the transition threshold also coincides with a population milestone count. The population milestone toast is orthogonal and always fires for 1K/10K/50K/100K/500K population counts. The "no second stinger at overlapping thresholds" rule means: if a City Rating transition and a population count milestone occur at the same tick, only ONE stinger fires (not two stingers of different types). It does NOT mean the stinger is suppressed when the transition threshold happens to match a population count.

**`stinger_game_over` is deferred to post-V1.** Scenario mode (the only gameplay context in which a game-over stinger fires) is a post-V1 feature. `stinger_game_over` will be added to the asset manifest when Scenario mode is implemented. At that point sources[57] will be reserved as the non-evictable GAME_OVER stinger slot and `kStingerCount` will increase from 2 to 3. See dynamic-soundscape.md for the post-V1 game-over duck interaction spec.

## SoundId Assignment Table

> **These integer values are locked. Do not reassign. `sound_ids.h` must use exactly these values.**

`SoundId` 0 is reserved as the invalid/null identifier. All V1 SFX and UI assets are assigned stable integer values starting at 1. Zone loop assets are positional sound buffers loaded at startup and share the same `SoundId` namespace as other pre-loaded SFX.

> **Zone loop SoundId constants use the `SFX_` prefix (not `ZONE_` alone) to distinguish them from the `ZoneType` enum values defined in `simulation_types.h`. Zone loops are positional pre-loaded sounds, not streaming audio.**

| SoundId | Asset Name | Category |
|---|---|---|
| 0 | *(invalid/reserved)* | — |
| 1 | `sfx_build_place` | SFX |
| 2 | `sfx_build_demolish` | SFX |
| 3 | `sfx_road_build` | SFX |
| 4 | `sfx_earthworks` | SFX |
| 5 | `sfx_zone_upgrade` | SFX |
| 6 | `sfx_service_degrade` | SFX |
| 7 | `sfx_budget_warn` | SFX |
| 8 | `sfx_loan_issued` | SFX |
| 9 | `sfx_power_out` | SFX |
| 10 | `sfx_water_out` | SFX |
| 11 | `sfx_fire_alert` | SFX |
| 12 | `sfx_police_alert` | SFX |
| 13 | `sfx_vehicle_engine_idle` | Vehicle SFX |
| 14 | `sfx_vehicle_engine_move` | Vehicle SFX |
| 15 | `sfx_vehicle_horn` | Vehicle SFX |
| 16 | `sfx_intersection_tick` | SFX |
| 17 | `sfx_zone_residential` | Zone loop |
| 18 | `sfx_zone_commercial` | Zone loop |
| 19 | `sfx_zone_industrial` | Zone loop |
| 20 | `stinger_crisis` | Music stinger |
| 21 | `stinger_milestone` | Music stinger |
| 22 | `ui_click` | UI |
| 23 | `ui_toast` | UI |
| 24 | `ui_menu_open` | UI |
| 25 | `ui_menu_close` | UI |

**Stinger identifier spaces**: Stinger SoundIds (e.g. SoundId 20 = `stinger_crisis`, SoundId 21 = `stinger_milestone`) identify pre-loaded WAV buffers and are entirely separate from stinger source pool indices (sources[55] for CRISIS, sources[56] for MILESTONE, as defined in `source-pool.md`). SoundId is passed to `loadSound()`; the pool index is returned by `acquireStingerSource(StingerType)` and is fixed by the `StingerType` enum. Do NOT conflate these two identifier spaces.

Stingers (IDs 20–21) are included in this table because they are loaded as static WAV PCM buffers via the same `AudioSystem::loadSound()` path as other pre-loaded SFX. Their reserved non-evictable pool source indices (55 and 56) are an orthogonal concern — the `SoundId` identifies the buffer, the pool source index identifies the playback slot.

Post-V1 additions (e.g., `stinger_game_over`, `sfx_population_notification`) MUST be assigned the next available integer (26, 27, …) and appended to this table. Never reuse a retired ID.

## MusicTrackId Assignment Table

> **These integer values are locked. Do not reassign. `sound_ids.h` must use exactly these values.**

`MusicTrackId` 0 is reserved as the invalid/null identifier. All V1 streamed music assets (main menu stems and gameplay stems) are assigned stable integer values starting at 1. Ambient bed streams are selected internally by `AudioSystem` using the `TimeOfDay` enum — they do NOT have `SoundId` constants in `sound_ids.h` and are not passed to `playSound()` or `playPositionalSound()`. Their filenames are resolved by `AudioSystem`'s `TimeOfDay`-to-filename mapping table. Only assets managed exclusively by the music crossfade state machine use `MusicTrackId`.

| MusicTrackId | Asset Name | Category |
|---|---|---|
| 0 | *(invalid/reserved)* | — |
| 1 | `music_main_menu_01` | Main menu music |
| 2 | `music_main_menu_02` | Main menu music |
| 3 | `music_calm_01` | Music stem |
| 4 | `music_calm_02` | Music stem |
| 5 | `music_growth_01` | Music stem |
| 6 | `music_growth_02` | Music stem |
| 7 | `music_crisis_01` | Music stem |
| 8 | `music_crisis_02` | Music stem |

Post-V1 music stems MUST be assigned the next available integer (9, 10, …) and appended to this table. Never reuse a retired ID.

## Asset Path Convention

All V1 audio assets reside at `${AITOWN_ASSETS_DIR}/audio/<filename>` (flat layout, no subdirectory), where `<filename>` is the Asset Name from the table above plus its format extension (e.g., `ambient_day.ogg`, `sfx_build_place.wav`, `stinger_crisis.wav`). JSON sidecar files for music stems follow the same flat layout (e.g., `${AITOWN_ASSETS_DIR}/audio/music_calm_01.json`). `AudioSystem` must resolve all asset paths using this convention; no asset category uses a subdirectory under `audio/`.

### Loudness Target Summary

| Asset category | Integrated loudness | True peak ceiling |
|---|---|---|
| Music stems | −16 LUFS | −1 dBTP |
| Stingers (crisis/milestone; game-over post-V1) | −18 LUFS | −1 dBTP |
| Ambient beds | −20 LUFS | −1 dBTP |
| CRITICAL service alerts (fire/police), vehicle horn | −18 LUFS | −1 dBTP |
| Important gameplay notifications (zone_upgrade, service_degrade, power_out, water_out) | −22 LUFS | −1 dBTP |
| General feedback SFX (build, demolish, road, loan, earthworks, budget_warn) | −24 LUFS | −1 dBTP |
| Very subtle ambient detail (intersection_tick) | −28 LUFS | −2 dBTP |
| Vehicle engine (idle/move) | −22 LUFS | −2 dBTP |
| Zone loops | −26 LUFS | −2 dBTP |
| UI notification sound (ui_toast) | −22 LUFS | −1 dBTP |
| UI action sounds (ui_click, ui_menu_open, ui_menu_close) | −24 LUFS | −1 dBTP |

All loudness targets are **integrated LUFS** (ITU-R BS.1770-3) measured on the authored file before runtime gain, distance attenuation, or pitch shift. Runtime processing (OpenAL distance model, gain crossfades) affects perceived level but not the authoring target.

## OGG Vorbis Encoding Quality Minimums

| Asset category | Minimum OGG quality flag | Approximate bitrate |
|---|---|---|
| Music stems (gameplay + main menu) | `-q 8` | ~256 kbps VBR |
| Ambient beds | `-q 7` | ~224 kbps VBR |
| Zone loops | `-q 6` | ~192 kbps VBR |

Lower quality flags are not acceptable for V1 — audible compression artifacts on
sustained or ambient passages undermine the production quality goal.
These floors apply to the authored OGG files; they are a delivery requirement, not a runtime concern.

## Vehicle Engine Loop Duration Constant

```cpp
constexpr float kVehicleEngineLoopMinDurationSeconds = 6.0f;
constexpr float kVehicleEngineLoopMaxDurationSeconds = 20.0f;
```

Both constants must be referenced by the Phase 5 `validate_assets.py` vehicle engine CI check — do NOT inline the literals 6.0 or 20.0. The rationale for 6.0 s minimum: at the lowest pitch-shift ratio (0.75×), a 6 s loop produces a ~4.5 s perceived loop — below the audible repetition threshold. A 5 s loop would produce ~3.75 s perceived, which is perceptibly mechanical. The rationale for 20.0 s maximum: files at exactly 20 s or longer are Tier 3 (streamed via the music/ambient streaming path); only files strictly under 20 s qualify for Tier 2 pre-loading. The CI gate uses `>= 20.0` as the hard failure condition — a file at exactly 20 s is rejected.

Both constants are declared in `audio_types.h` alongside `kZoneLoopMaxPreloadDurationSeconds`. The Phase 5 `validate_assets.py` check for `sfx_vehicle_engine_*.ogg` files MUST import or reference both constants (or the equivalent Python literals derived from them) rather than hardcoding `6.0` or `20.0` directly in the check script. This ensures that any future change to either threshold requires a single-source update in `audio_types.h` and `v1-audio-asset-manifest.md` simultaneously, rather than a scattered literal search.

## CI Asset Validation Requirements

The following checks MUST be enforced by `tools/validate_assets.py` before Phase 10 audio integration. Any failure is a hard build error — the asset pipeline must not proceed to integration if any of these checks fail.

All four format checks (check_16–check_19, Phase 5) require the `mutagen` Python library for OGG duration and format inspection and the standard `wave` stdlib module for WAV inspection. Check #14 (music JSON sidecar) and Check #15 (`.meta` sidecar, Phase 9 stub) were assigned in Phase 4 and already exist in `tools/validate_assets.py`.

| # | File pattern | Rule | Hard error condition |
|---|---|---|---|
| check_16 | `music_*.ogg`, `ambient_*.ogg` | Phase 5. Sample rate must be 44100 Hz; channel count must be stereo (2 channels) for all music stems (`music_*.ogg`) and all ambient beds (`ambient_*.ogg`). **This stereo check covers ONLY files matching `music_*.ogg` and `ambient_*.ogg` — it does NOT extend to zone loops (`sfx_zone_*.ogg`). Zone loops are mono (1 channel) and are validated separately in the check_18 row below. Phase 5 implementers MUST NOT apply this stereo check to any file matching `sfx_zone_*.ogg`.** Graceful no-op if no files exist. | Any file where `vi->rate != 44100` or `vi->channels != 2`; authoring at any other sample rate is a hard asset error |
| check_17 | `sfx_vehicle_engine_*.ogg` | Phase 5. Duration must be >= `kVehicleEngineLoopMinDurationSeconds` (6.0 s) AND strictly < `kVehicleEngineLoopMaxDurationSeconds` (20.0 s); file must be mono (1 channel); sample rate must be 44100 Hz. Files at exactly 20 s are Tier 3 (streamed) and must NOT be placed in the vehicle engine SFX pool. Graceful no-op if no files exist. | Hard error if duration < `kVehicleEngineLoopMinDurationSeconds` (6.0 s) OR duration >= `kVehicleEngineLoopMaxDurationSeconds` (20.0 s) OR `vi->channels != 1` OR `vi->rate != 44100`. The check script MUST reference both constants — do NOT inline the literals 6.0 or 20.0 (see Vehicle Engine Loop Duration Constant section above for full rationale) |
| check_18 | `sfx_zone_*.ogg` | Phase 5. Duration must be <= `kZoneLoopMaxPreloadDurationSeconds` (18.0 s); file must be mono (1 channel); sample rate must be 44100 Hz. Graceful no-op if no files exist. | Any zone loop file with duration > 18.0 s or `vi->channels != 1` or `vi->rate != 44100` |
| check_21 | `sfx_zone_*.ogg` | Phase 10 — reserved. Zone loop silence-floor amplitude gate. For each `sfx_zone_*.ogg`, decode the full OGG to PCM and apply **two independent region checks** — both must pass: **(1) Leading silence check**: the first `ceil(44100 × 0.1) = 4410` samples (samples at index 0–4409 inclusive) must ALL be at or below −60 dBFS peak amplitude. This region is evaluated independently; a failure here is a hard error regardless of the trailing region result. **(2) Trailing silence check**: the last 4410 samples (the final 4410 samples of the decoded PCM stream) must ALL be at or below −60 dBFS peak amplitude. This region is evaluated independently; a failure here is a hard error regardless of the leading region result. The two 4410-sample windows are NOT combined into a single 8820-sample or 200 ms aggregate check — each is a separate pass/fail gate. A file that passes the leading check but fails the trailing check (or vice versa) is still a hard asset error. For multi-channel files (which should not exist for zone loops — see check_18), apply the −60 dBFS threshold per channel; fail if any channel in either window exceeds the threshold. Graceful no-op if no files exist. | Hard error if ANY sample in the leading 4410-sample window (indices 0–4409) exceeds −60 dBFS peak amplitude, OR if ANY sample in the trailing 4410-sample window (last 4410 samples) exceeds −60 dBFS peak amplitude. The two windows are checked independently — failure of either window alone is sufficient to reject the file |
| check_19 | `stinger_*.wav` | Phase 5. Must be mono WAV PCM (1 channel, uncompressed). Graceful no-op if no files exist. | Any stinger file that is not a PCM WAV or has `channels != 1` (stereo or non-PCM compressed WAV is a hard error) |
| check_14 | `music_*.ogg` | Phase 4. A co-located `.json` sidecar file must be present alongside the OGG file. All `music_*.ogg` files are covered — including main menu variants (`music_main_menu_*.ogg`) AND all gameplay stems (`music_calm_*.ogg`, `music_growth_*.ogg`, `music_crisis_*.ogg`). Files matching `ambient_*.ogg` are explicitly excluded. The pattern `music_*.ogg` covers both main menu and gameplay stem files. | Any `music_*.ogg` file with no matching `<basename>.json` sidecar in `${AITOWN_ASSETS_DIR}/audio/`; build error if absent (per audio-asset-formats.md) |

**Check number reservation for Phase 10**: The zone-loop silence-floor amplitude gate (`sfx_zone_*.ogg` leading/trailing silence <= -60 dBFS) is Phase 10's new validate_assets.py check. It must be assigned Check #21 (next available slot after any existing checks), NOT Check #17. Phase 10 implementers must not reuse check numbers assigned to prior phases. Update this table when Phase 10 is implemented to add the check_21 row. (check_21 is already added as a placeholder row above with status "Planned".)

**Notes on the table above:**

**Ambient bed sidecar exemption**: the sidecar check (last row above) applies to all files matching `music_*.ogg` — this includes main menu variants (`music_main_menu_*.ogg`) as well as all gameplay stems (`music_calm_*.ogg`, `music_growth_*.ogg`, `music_crisis_*.ogg`). Files matching `ambient_*.ogg` are explicitly excluded from the sidecar check — consistent with the ambient bed JSON sidecar exemption documented in the asset table and blockquote above.

**Zone loop mono requirement rationale**: zone loop SFX (`sfx_zone_residential`, `sfx_zone_commercial`, `sfx_zone_industrial`) are mono positional sources played via `alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE)` with full 3D spatial positioning. OpenAL renders mono sources with HRTF and distance attenuation. Stereo files cannot be used as positional sources — OpenAL Soft will either reject the buffer or collapse the channels, producing incorrect spatialization. The 18 s hard cap keeps zone loops in the pre-load tier (boundary is 20 s); staying below 20 s avoids promotion to the streaming tier, which is reserved for music stems and ambient beds (sources[58..61]).

**Check number summary**: Check #14 (music JSON sidecar) and Check #15 (`.meta` sidecar, Phase 9 stub) were assigned in Phase 4. Check #16 (`music_*.ogg`/`ambient_*.ogg` sample rate and channel count), Check #17 (`sfx_vehicle_engine_*.ogg` duration and format), Check #18 (`sfx_zone_*.ogg` duration and format), and Check #19 (`stinger_*.wav` mono PCM) are assigned above and implemented in Phase 5. Check #20 is reserved. Check #21 (`sfx_zone_*.ogg` silence-floor amplitude gate — Zone loop silence floor, Phase 10 — reserved/Planned) is reserved for Phase 10. References in other spec files to specific check numbers (e.g., "validate_assets.py Check #14" in the ambient bed sidecar exemption blockquote above) remain valid. Phase 10 implementers MUST assign the zone-loop silence-floor gate to Check #21 and MUST NOT reuse Check #17 or any other check number already assigned to a prior phase.

## Phase 7 Placeholder Asset Sign-Off

The following synthetic placeholder OGG files were generated in Phase 7 using SoX v14.4.2
to unblock streaming smoke tests, and subsequently upgraded to functional synthetic tones
to enable meaningful signal-path testing. These files contain real audio content (synthetic
tones at approximately −18 to −28 LUFS depending on category) and are NOT production-quality
deliverables — they exist to satisfy header validation checks (`ov_fopen`, `ov_info`,
`vi->rate`, `vi->channels`), enable code-path smoke testing of the streaming and pre-load
subsystems, and exercise the audio mixing, spatialization, and loudness code paths with
real signal before final DAW-authored assets are available.

**Note on named Phase 4 assets**: The full set of named placeholder assets in
`implementation/phase-4.md` (music stems, ambient beds, zone loops, vehicle engine loops,
WAV SFX, and stingers) also uses this functional synthetic tone approach. See the Phase 4
"Missing Named Asset Placeholders" section for per-file tone design details and loudness
levels.

### Files Created

| File | Path | Rate | Channels | Duration | Format | Tone Content | soxi Verified |
|---|---|---|---|---|---|---|---|
| `music_placeholder.ogg` | `assets/audio/music_placeholder.ogg` | 44100 Hz | 2 (stereo) | 30.00 s | OGG Vorbis | Sine drone 110 Hz + 220 Hz, −18 LUFS approx., 2 s fade-in/out | Yes |
| `ambient_bed_placeholder.ogg` | `assets/audio/ambient_bed_placeholder.ogg` | 44100 Hz | 2 (stereo) | 90.00 s | OGG Vorbis | Pink noise, −20 LUFS approx., 3 s fade-in/out | Yes |
| `placeholder_zone_loop.ogg` | `assets/audio/placeholder_zone_loop.ogg` | 44100 Hz | 1 (mono) | 15.00 s | OGG Vorbis | Pink noise, −26 LUFS approx., 0.5 s fade-in/out | Yes |
| `placeholder_vehicle_engine.ogg` | `assets/audio/placeholder_vehicle_engine.ogg` | 44100 Hz | 1 (mono) | 6.00 s | OGG Vorbis | Sine harmonics 55 Hz + 110 Hz, −22 LUFS approx. | Yes |

### Music Sidecar JSON

`assets/audio/music_placeholder.json` exists alongside `music_placeholder.ogg` and
contains valid `bpm` and `beats_per_bar` fields (`{"bpm": 90, "beats_per_bar": 4}`).
The `AudioSystem` sidecar check at music stem load time will not throw `std::runtime_error`
for this placeholder file.

### Production Authoring Notes

The 200 ms pre-baked DAW crossfade tail required for production ambient beds (see
Section 5, "DAW Crossfade Loop") is NOT present in `ambient_bed_placeholder.ogg`.
The fade-in/fade-out envelope present on this placeholder exercises the level-ramping
code path, but is not a substitute for the DAW-authored loop-point crossfade required
in production. Production `ambient_day.ogg`, `ambient_night.ogg`, `ambient_dawn.ogg`,
and `ambient_dusk.ogg` must each carry the pre-baked 200 ms crossfade tail authored
in the DAW before Phase 10 exit.

Zone loop placeholder `placeholder_zone_loop.ogg` (15.00 s) satisfies the check_18
duration constraint (12–18 s) and the mono positional source requirement. The 0.5 s
fade-in/fade-out exercises the loop-boundary handling code path. The production
silence-boundary fade (−60 dBFS head/tail) will be authored in the DAW for final
production zone loop assets before Phase 10 exit.

Vehicle engine placeholder `placeholder_vehicle_engine.ogg` (6.00 s) meets the
`kVehicleEngineLoopMinDurationSeconds = 6.0f` minimum. The sine harmonic content
(55 Hz + 110 Hz) exercises the pitch-shift and distance-attenuation paths. At the
lowest pitch-shift ratio (0.75×) the perceived loop duration is ~4.5 s, which is at
the lower bound of the perceptibility threshold — acceptable for code-path testing,
not for final delivery where a longer authored loop (8–12 s) is recommended.

### Starvation Risk Mitigation

The streaming queue uses 8 OpenAL buffers per stream slot. At 44100 Hz stereo
(`music_placeholder.ogg`) that is 8 × 4096 samples × 2 channels × 2 bytes = 131,072 bytes
(~640 ms headroom). Synthetic tone content (sine waves and pink noise) produces minimal
decode stalls — the OGG bitstream decoder returns frames immediately, keeping the queue
full. Buffer starvation risk from placeholder files is negligible.

### Sign-Off Checklist

- [x] All four placeholder OGG files are valid OGG Vorbis (`ov_fopen` returns 0)
- [x] All files are exactly 44100 Hz sample rate
- [x] `music_placeholder.ogg` and `ambient_bed_placeholder.ogg` are stereo (2 channels)
- [x] `placeholder_zone_loop.ogg` is mono (1 channel), duration 15.00 s (within 12–18 s)
- [x] `placeholder_vehicle_engine.ogg` is mono (1 channel), duration 6.00 s (meets 6 s minimum)
- [x] `music_placeholder.json` sidecar present with `{"bpm": 90, "beats_per_bar": 4}`
- [x] All four placeholder files contain functional synthetic tones (−18 to −26 LUFS depending on category) — NOT silent
- [x] Production 200 ms pre-baked DAW crossfade NOT required for placeholders (noted above)
- [x] Starvation risk mitigated by 8-buffer streaming queue (~640 ms headroom at 44100 Hz stereo)
- [x] Named Phase 4 placeholder assets (music stems, ambient beds, zone loops, vehicle engines, WAV SFX, stingers) also upgraded to functional synthetic tones — see `implementation/phase-4.md`
- [ ] Replace all placeholder files with production DAW-authored assets before Phase 10 exit
