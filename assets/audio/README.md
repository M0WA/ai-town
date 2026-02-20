# AI Town Audio Assets — Production Brief

This file is reviewed and approved by `sound-artist-opensoftal` before Phase 0 is declared complete.

Reference: `architecture/audio-architecture/v1-audio-asset-manifest.md`

---

## 1. JSON Sidecar Requirement

OGG music stems (`music_*` assets only) require a `.json` sidecar file with BPM data:

```json
{"bpm": 90, "beats_per_bar": 4}
```

Stems requiring sidecars: `music_main_menu_01`, `music_main_menu_02`, `music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`, `music_crisis_02`.

**All 6 gameplay stems (`music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`, `music_crisis_02`) MUST be authored at exactly 90 BPM, 4/4 time signature.** This is a functional requirement. The `AudioSystem` bar-boundary crossfade system uses `"bpm": 90` from the sidecar to compute bar boundaries. Submitting a stem at 120 BPM with a sidecar still reading `{"bpm": 90}` causes crossfades to fire at wrong positions (33% timing error). The sidecar `"bpm"` value must exactly match the authored BPM of the specific file.

The 4 ambient beds (`ambient_*` files) do **NOT** require sidecars — they use DAW crossfade loops and the `AudioSystem` never parses sidecars for them. SFX and zone loops also do NOT require sidecars.

JSON sidecars follow the flat `assets/audio/` directory layout.

---

## 2. Tiered Loudness Targets

All targets are integrated LUFS (ITU-R BS.1770-3) measured on the authored file before runtime gain or pitch shift:

| Asset category | Integrated loudness | True peak ceiling |
|---|---|---|
| Music stems (music_calm, music_growth, music_crisis, main menu music) | -16 LUFS | -1 dBTP |
| Stingers (crisis/milestone; game-over post-V1) | -18 LUFS | -1 dBTP |
| Ambient beds (ambient_day/night/dawn/dusk) | -20 LUFS | -1 dBTP |
| CRITICAL service alerts (fire/police), vehicle horn | -18 LUFS | -1 dBTP |
| Budget deficit warning | -20 LUFS | -1 dBTP |
| Important gameplay notifications (zone_upgrade, service_degrade, power_out, water_out) | -22 LUFS | -1 dBTP |
| General feedback SFX (build, demolish, road, loan, earthworks) | -24 LUFS | -1 dBTP |
| Very subtle ambient detail (intersection_tick) | -28 LUFS | -2 dBTP |
| Vehicle engine (idle/move) | -22 LUFS | -2 dBTP |
| Zone loops | -26 LUFS | -2 dBTP |
| UI sounds (click, toast, menu) | -22 to -24 LUFS | -1 dBTP |

---

## 3. V1 Asset Manifest

The following table is the complete list of V1 audio assets. All assets must be delivered before Phase 0 is declared complete. Use this as the authoritative delivery checklist.

**`stinger_game_over` is NOT a V1 asset — it is deferred to post-V1 (requires Scenario mode). Do not produce it for V1 delivery.**

| Asset name | Category | Format | Duration | Loop | Channels | Loudness | Notes |
|---|---|---|---|---|---|---|---|
| `music_main_menu_01` | Main menu music | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Bar-aligned seamless loop; JSON sidecar mandatory; 90 BPM 4/4 |
| `music_main_menu_02` | Main menu music | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Variant; same key, BPM, harmonic compatibility as all gameplay stems; JSON sidecar mandatory |
| `ambient_day` | Ambient bed | OGG | 90–120 s | Y | Stereo | -20 LUFS / -1 dBTP | DAW crossfade loop; city hum, birds, traffic |
| `ambient_night` | Ambient bed | OGG | 90–120 s | Y | Stereo | -20 LUFS / -1 dBTP | DAW crossfade loop; quiet, insects, distant traffic |
| `ambient_dawn` | Ambient bed | OGG | 90–120 s | Y | Stereo | -20 LUFS / -1 dBTP | DAW crossfade loop; birds, early traffic (05:00–06:00 window) |
| `ambient_dusk` | Ambient bed | OGG | 90–120 s | Y | Stereo | -20 LUFS / -1 dBTP | DAW crossfade loop; evening ambient, steady moderate traffic (20:00–23:00 window) |
| `music_calm_01` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Bar-aligned seamless loop; JSON sidecar mandatory; 90 BPM 4/4; shared root key/mode |
| `music_calm_02` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Calm variant; JSON sidecar mandatory; shared root key/mode |
| `music_growth_01` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | City growing, energetic; JSON sidecar mandatory; shared root key/mode |
| `music_growth_02` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Growth variant; JSON sidecar mandatory; shared root key/mode |
| `music_crisis_01` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Crisis/disaster theme; JSON sidecar mandatory; shared root key/mode |
| `music_crisis_02` | Music stem | OGG | 90–180 s | Y | Stereo | -16 LUFS / -1 dBTP | Crisis variant; JSON sidecar mandatory; shared root key/mode |
| `zone_residential` | Zone loop | OGG | 12–18 s | Y | Mono | -26 LUFS / -2 dBTP | Silence-boundary loop; pre-loaded; hard cap 18 s |
| `zone_commercial` | Zone loop | OGG | 12–18 s | Y | Mono | -26 LUFS / -2 dBTP | Silence-boundary loop; pre-loaded; hard cap 18 s |
| `zone_industrial` | Zone loop | OGG | 12–18 s | Y | Mono | -26 LUFS / -2 dBTP | Silence-boundary loop; pre-loaded; hard cap 18 s |
| `sfx_build_place` | SFX | WAV PCM | < 1 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Building placed; subtle placement feedback |
| `sfx_build_demolish` | SFX | WAV PCM | < 1 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Building demolished |
| `sfx_road_build` | SFX | WAV PCM | < 1 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Road construction feedback |
| `sfx_earthworks` | SFX | WAV PCM | < 1 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Terrain leveling; short percussive impact |
| `sfx_loan_issued` | SFX | WAV PCM | < 1 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Loan auto-issued |
| `sfx_budget_warn` | SFX | WAV PCM | 1–2 s | N | Mono or stereo | -20 LUFS / -1 dBTP | Budget deficit warning; minimum 1 s (sub-second is not perceptibly distinct from UI clicks) |
| `sfx_power_out` | SFX | WAV PCM | 1–2 s | N | Mono or stereo | -22 LUFS / -1 dBTP | Power outage notification |
| `sfx_water_out` | SFX | WAV PCM | 1–2 s | N | Mono or stereo | -22 LUFS / -1 dBTP | Water outage notification |
| `sfx_zone_upgrade` | SFX | WAV PCM | 1–2 s | N | Mono or stereo | -22 LUFS / -1 dBTP | Zone tile auto-upgraded; positive/rewarding tone |
| `sfx_service_degrade` | SFX | WAV PCM | 1–2 s | N | Mono or stereo | -22 LUFS / -1 dBTP | Service building reduced-coverage; warning tone distinct from sfx_budget_warn |
| `sfx_fire_alert` | SFX | WAV PCM | 2–4 s | N | **Mono** | -18 LUFS / -1 dBTP | **Positional** (3D spatial at building location); CRITICAL priority |
| `sfx_police_alert` | SFX | WAV PCM | 2–4 s | N | **Mono** | -18 LUFS / -1 dBTP | **Positional** (3D spatial at building location); CRITICAL priority |
| `sfx_intersection_tick` | SFX | WAV PCM | < 0.5 s | N | **Mono** | -28 LUFS / -2 dBTP | Traffic signal change; very subtle ambient detail; optional distance cull at > 80 m |
| `sfx_vehicle_engine_idle` | Vehicle SFX | **OGG Vorbis** | **6–20 s** | Y | **Mono** | -22 LUFS / -2 dBTP | Positional; pre-loaded; minimum 6 s (see Section 6); WAV prohibited |
| `sfx_vehicle_engine_move` | Vehicle SFX | **OGG Vorbis** | **6–20 s** | Y | **Mono** | -22 LUFS / -2 dBTP | Positional; pre-loaded; minimum 6 s; WAV prohibited |
| `sfx_vehicle_horn` | Vehicle SFX | WAV PCM | **0.4–1 s** | N | **Mono** | -18 LUFS / -1 dBTP | Positional; minimum 0.4 s authored duration |
| `stinger_crisis` | Music stinger | **WAV PCM** | 2–4 s | N | **Mono** | -18 LUFS / -1 dBTP | Non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`); music ducks to 0.4 gain on playback |
| `stinger_milestone` | Music stinger | **WAV PCM** | 2–3 s | N | **Mono** | -18 LUFS / -1 dBTP | Fires at City Rating tier transitions only (1K/10K/50K/500K pop); non-positional; music ducks to 0.4 gain |
| `ui_click` | UI | WAV PCM | < 0.2 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| `ui_toast` | UI | WAV PCM | < 0.3 s | N | Mono or stereo | -22 LUFS / -1 dBTP | Non-positional; must be clearly audible as a notification |
| `ui_menu_open` | UI | WAV PCM | < 0.3 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Non-positional |
| `ui_menu_close` | UI | WAV PCM | < 0.3 s | N | Mono or stereo | -24 LUFS / -1 dBTP | Non-positional |

JSON sidecar files (e.g. `music_calm_01.json`) are also part of the delivery and must accompany every `music_*` OGG file.

---

## 4. Channel Count Rules

| Asset type | Channels | Notes |
|---|---|---|
| Music stems | Stereo (2 channels) | `AudioSystem` validates OGG header at load time; mismatched sample rate or channel count is a hard asset error (playback refused) |
| Ambient beds | Stereo (2 channels) | Same validation applies |
| Zone loops | Mono (1 channel) | Mandatory for `alSource3f(AL_POSITION)` 3D spatialization (OpenAL ignores 3D position on stereo sources) |
| Stingers (`stinger_crisis`, `stinger_milestone`) | Mono (1 channel) | Non-positional (`AL_SOURCE_RELATIVE = AL_TRUE`) but must be mono; multi-channel WAV sources with `AL_SOURCE_RELATIVE` produce undefined panning on some OpenAL implementations |
| Positional SFX (`sfx_fire_alert`, `sfx_police_alert`, `sfx_vehicle_horn`, `sfx_intersection_tick`, zone loops) | Mono (1 channel) | OpenAL ignores `AL_POSITION` on stereo sources entirely; stereo positional SFX is a silent correctness failure |
| Vehicle engine SFX (`sfx_vehicle_engine_idle`, `sfx_vehicle_engine_move`) | Mono (1 channel) OGG Vorbis | WAV format is prohibited for vehicle engine loops (a 1-2 s WAV loop is audibly mechanical; OGG minimum 6 s required) |
| UI sounds | Stereo or mono | `AL_SOURCE_RELATIVE = AL_TRUE` |

---

## 5. Loop Authoring Rules

Three distinct methods depending on asset type:

### Bar-Aligned Seamless Loop (Music Stems)

- No silence or crossfade at loop boundary
- Loop point must align to a bar boundary at the authored BPM (at 90 BPM, 4/4 time: N bars = N * 2.667 s)
- BPM coded in the JSON sidecar
- `AudioSystem` implements bar-boundary crossfade in software

### DAW Crossfade Loop (Ambient Beds)

The ambient bed must satisfy BOTH:
1. **200 ms pre-baked crossfade tail** at end of file authored in the DAW (not handled at runtime). The `.ogg` file sounds seamless at the embedded loop boundary. No silence floor permitted.
2. **Seamless sample-0 boundary** — the streaming runtime seeks to sample 0 (`ov_pcm_seek(vf, 0)`) BEFORE the crossfade tail, bypassing it entirely. Authors must verify the sample-0 boundary is click-free and transient-free using DAW loopback-audition.

### Silence-Boundary Loop (Zone Loops)

- -60 dBFS or lower at both head and tail of the file
- The final 100 ms (tail) and the first 100 ms (head) must fade to and from silence (floor: -60 dBFS or lower)
- Both boundaries must individually reach -60 dBFS or below
- The resulting ~200 ms silence window at every loop boundary must coincide with a natural rhythmic gap in the content
- **Zone loop hard duration cap: 18 s** — zone loops must not exceed 18 s authored duration (20 s pre-load tier boundary; 18 s provides a 2 s safety margin). Zone loops exceeding 18 s are incompatible with the pre-loaded AL buffer loading strategy.

---

## 6. Vehicle Engine Duration Requirements

- `sfx_vehicle_engine_idle` and `sfx_vehicle_engine_move` must be **minimum 6 s** (NOT 4 s or 5 s).
- Rationale: at the lowest pitch-shift ratio (0.75 for stopped vehicles), a 4 s loop produces a ~3 s perceived loop — audibly mechanical. At 6 s minimum, the perceived loop is ~4.5 s, below the perceptibility threshold.
- **Vehicle horn minimum authored duration**: `sfx_vehicle_horn` must be a minimum of **0.4 s** authored duration. Sub-0.4 s sounds are perceptually indistinguishable from UI click feedback.
- Rate-limiting rules (2 s per-vehicle cooldown, 3 simultaneous horn sources max, 100 m cull distance) are enforced by `AudioSystem` at runtime — these are not authoring constraints.

---

## 7. Flat Asset Path Convention

All V1 audio assets are placed directly in `assets/audio/` with no subdirectories:

- `assets/audio/ambient_day.ogg`
- `assets/audio/sfx_build_place.wav`
- `assets/audio/music_calm_01.ogg`
- `assets/audio/music_calm_01.json` (JSON sidecar)

---

## 8. OGG Encoding Quality Settings

All OGG files must be encoded at 44100 Hz:

| Asset category | libvorbis quality | Approx. bitrate |
|---|---|---|
| Music stems | `-q 8` | ~256 kbps VBR stereo |
| Ambient beds | `-q 7` | ~224 kbps VBR stereo |
| Zone loops | `-q 6` | ~192 kbps VBR mono |
| Vehicle engine loops | `-q 6` | ~192 kbps VBR mono |

Inconsistent encoder quality settings cause inter-file loudness variation. Never use `-q 10` (unnecessarily large files) or `-q 4` or lower (audible compression artifacts).

---

## 9. WAV Technical Specification

All WAV PCM files must be authored and delivered at:

- **Sample rate**: 44100 Hz
- **Bit depth**: 16-bit signed PCM
- **Channel count**: as specified in the asset manifest (mono for positional SFX; see Sections 3 and 4)

Do not deliver 24-bit or 32-bit float WAV files. The OpenAL Soft `AL_FORMAT_MONO16` / `AL_FORMAT_STEREO16` format expects 16-bit signed PCM. Mismatched bit depth causes incorrect playback levels or silence.

---

## 10. Time-of-Day Audio Schedule

The `AudioSystem` selects ambient beds and music intensity based on in-game time. Authors must design each ambient bed for the stated time-of-day context:

| In-game hours | Active ambient bed | Music intensity |
|---|---|---|
| 06:00–20:00 (day) | `ambient_day` | Calm or Growth (driven by simulation state) |
| 20:00–23:00 (dusk) | `ambient_dusk` | Forced Calm (regardless of simulation state) |
| 23:00–05:00 (night) | `ambient_night` | Forced Calm |
| 05:00–06:00 (dawn) | `ambient_dawn` | Forced Calm |

**Crossfade duration**: 3 s (default), constant-power curve (`gain_in = sin(t * pi/2)`, `gain_out = cos(t * pi/2)`). Never a linear ramp.

**Dawn and dusk collapse at high simulation speeds**: At simulation speeds >= 3x, the dawn (05:00–06:00) and dusk (20:00–23:00) windows are collapsed — transitions go directly day→night and night→day. `ambient_dawn` and `ambient_dusk` may never reach full volume in these cases. Author them to sound acceptable even when faded in briefly before crossfading out. Do not rely on either bed sustaining for a full 90–120 s play-through at high simulation speeds.

**Crisis music at night**: If a city crisis occurs during nighttime or dusk/dawn hours, the game does NOT escalate to crisis music stems. Crisis music applies during day hours only. Crisis notifications (`sfx_fire_alert`, `stinger_crisis`) still fire at night — the stinger system operates independently of the time-of-day music lock.

---

## 11. Spatial Audio Distance Parameters

All positional SFX are spatialized using the `AL_INVERSE_DISTANCE_CLAMPED` distance model. The following parameters are set by `AudioSystem` at source creation — authors must calibrate authored loudness targets against these runtime attenuation characteristics:

| Source category | Reference distance | Max distance | Rolloff factor |
|---|---|---|---|
| Traffic / vehicles | 10 m | 150 m | 1.0 |
| Service events (fire, police) | 15 m | 150 m | 1.5 |
| Zone ambient loops | 30 m | 300 m | 0.6 |
| Construction / industry | 15 m | 120 m | 1.2 |
| Ambient crowd | 20 m | 200 m | 0.8 |
| UI / notification sounds | N/A | N/A | 0.0 (non-positional) |

**Authoring implication**: At the reference distance, the source plays at full authored gain. Beyond the reference distance, gain attenuates at the rolloff rate until the max distance, beyond which gain is clamped to the attenuated value at max distance (effectively inaudible). Zone loops are culled by `AudioSystem` beyond 300 m. Vehicle engine sources are culled beyond 150 m. The loudness targets in Sections 2 and 3 are measured on the authored file — they do not account for runtime distance attenuation.

---

## 12. Occlusion System — Authoring Implications

Positional SFX sources in the evictable SFX pool (vehicle engines, zone loops, fire/police alerts, intersection ticks) are subject to the V1 EFX lowpass occlusion system. When a raycast from the listener to a source is blocked by a building or terrain:

- **Gain floor**: The source is attenuated to **0.1 gain (-20 dB)** — not silenced entirely
- **HF cut**: `AL_LOWPASS_GAINHF` is set to **0.3 (-10 dB HF cut)**, simulating high-frequency absorption through building walls

**Authoring implication**: Do NOT pre-bake muffling or low-frequency rolloff into positional SFX assets to simulate "indoors" occlusion. The `AudioSystem` applies its own EFX lowpass filter at runtime. Pre-baked muffling on top of the runtime filter produces over-attenuated, muddy sounds when occluded. Deliver positional SFX with full high-frequency content at the authored loudness target — the occlusion filter handles the acoustic effect at runtime.

Non-positional sources (music stems, ambient beds, stingers, UI sounds) are **not** subject to the occlusion filter.

---

## 13. Harmonic Compatibility and Crossfade Audibility Requirement

All 6 gameplay stems (`music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`, `music_crisis_02`) **MUST share the same root key and mode**. During bar-boundary crossfades, two stems are simultaneously audible for 3 s — different keys or modes produce audible harmonic clashes at the crossfade transition.

**Mandatory delivery gate**: The audio artist must submit a crossfade audibility test (manually mix `music_calm_01` with `music_growth_01` for 3 seconds at equal gain, verify no harmonic clash) as part of the Phase 4 asset delivery package.

Main menu variants (`music_main_menu_01`, `music_main_menu_02`) must share the same root key and mode as gameplay stems.

---

## AITOWN_ASSETS_DIR Compile Definition

The asset root path is defined in `CMakeLists.txt` as a compile-time definition so `AudioSystem` resolves paths consistently on Linux and Windows:

```cmake
target_compile_definitions(aitown_audio PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
```

`${CMAKE_SOURCE_DIR}/assets` is an absolute path expanded at configure time.
