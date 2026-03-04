# WAV SFX Production Brief

## Overview

This document specifies all 16 WAV PCM one-shot SFX assets required for Phase 10 exit.
These assets cover placement feedback, service alerts, budget/economy events, UI
interactions, and traffic. All are pre-loaded into static OpenAL Soft buffers via the
short-SFX tier (Tier 1: < 5 s, WAV PCM).

All WAV SFX parameters below are locked. Deviations from format, loudness, channel
count, or duration are hard delivery failures.

This brief covers all WAV SFX **except** the two vehicle SFX one-shots (`sfx_vehicle_horn`
and `sfx_intersection_tick`), which are specified in `vehicle-sfx-production-brief.md`.
Stinger WAV assets (`stinger_crisis.wav`, `stinger_milestone.wav`) are specified in
`stinger-production-brief.md`.

---

## Universal WAV SFX Parameters

| Parameter | Value for all assets in this brief |
|---|---|
| Format | WAV PCM (uncompressed linear PCM — audio format tag 0x0001) |
| Sample rate | 44100 Hz |
| Bit depth | 16-bit signed integer |
| Channels | Mono (1 channel) — see rationale below |
| Loudness measurement | Integrated LUFS (ITU-R BS.1770-3), measured on the authored file |

**Why all WAV SFX are mono**: OpenAL Soft requires mono buffers for positional 3D
sources. Non-positional SFX are also authored mono because: (1) multi-channel
non-positional WAV sources with `AL_SOURCE_RELATIVE = AL_TRUE` produce undefined panning
on some OpenAL Soft implementations; (2) mono SFX consume half the source pool memory of
stereo; (3) for game feedback sounds (button click, budget warning) stereo width provides
no meaningful player information — all spatial character comes from the mix and the game's
3D environment, not from the authored stereo field of a 0.2 s click sound.

---

## Asset Specifications

### Placement and Construction SFX

These four SFX fire at the world-space tile position (`vec3{float(tileX), 0.0f, float(tileZ)}`).
They are positional (`AL_SOURCE_RELATIVE = AL_FALSE`). No EFX bypass — they benefit from
distance-based processing. No per-asset cooldown enforced in `CitySimulation`.

---

#### `sfx_build_place.wav`

| Parameter | Value |
|---|---|
| Duration | < 1 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`) |
| Priority | NORMAL |
| EFX bypass | No |
| Trigger | `CitySimulation::placeZone()` and `placeServiceBuilding()` on success |

**Character**: a brief, satisfying "click-thud" or construction-start sound representing
a building foundation being placed or a zone being designated. Should feel constructive
and positive — not heavy machinery, but the lighter sound of a placement or designation
being confirmed. Think of the sound of setting a piece down on a table, combined with a
short construction creak or material contact. Duration 0.3–0.7 s recommended. The onset
must be crisp (< 50 ms attack time) so it feels responsive to the mouse click.

**Mix context**: plays at the tile position, audible over ambient beds and music. At
−24 LUFS it is a subtle feedback sound — responsive but not intrusive. At 50 m from the
listener it will be nearly inaudible; at 10 m it will be clearly present.

---

#### `sfx_build_demolish.wav`

| Parameter | Value |
|---|---|
| Duration | < 1 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`) |
| Priority | NORMAL |
| EFX bypass | No |
| Trigger | `CitySimulation::demolishTile()` on success |

**Character**: a short deconstruction sound — the opposite emotional valence of
`sfx_build_place`. Should convey removal or demolition without being alarming. A brief
crumbling, tearing, or structural impact. A short low-frequency thud with a dust-and-debris
high-frequency tail is effective. Duration 0.3–0.8 s recommended.

**Differentiation from `sfx_build_place`**: the two sounds must be clearly
distinguishable in blind listening. `sfx_build_demolish` should feel like destruction or
removal, not construction. Recommend verifying differentiation by playing both to a
colleague without context.

---

#### `sfx_road_build.wav`

| Parameter | Value |
|---|---|
| Duration | < 1 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`) |
| Priority | NORMAL |
| EFX bypass | No |
| Trigger | `CitySimulation::placeRoad()` on success |

**Character**: a short road-laying or asphalt-placement sound. Distinct from
`sfx_build_place` — roads have a different construction character (rolling, laying flat
material) vs. erecting structure. A short mechanical sound with a gravel or asphalt
texture quality. Duration 0.3–0.6 s recommended.

---

#### `sfx_earthworks.wav`

| Parameter | Value |
|---|---|
| Duration | < 1 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`); world-space tile position |
| Priority | NORMAL |
| EFX bypass | YES — `AL_DIRECT_FILTER = AL_FILTER_NULL` (construction on open unoccluded tiles) |
| `AL_SOURCE_RELATIVE` | `AL_FALSE` (world-space positional — NOT `AL_TRUE`) |
| Trigger | `placeZone()`, `placeRoad()`, `placeServiceBuilding()` when `earthworksCostOverride > 0` |

**Character**: terrain levelling — earth moving, ground preparation. A heavier, lower
sound than `sfx_build_place` — conveys soil movement or grading. A brief earth-and-stone
impact or scrape: low-frequency impact (80–200 Hz) with a granular/gritty texture tail.
Duration 0.4–0.8 s recommended.

**EFX bypass clarification**: the EFX bypass is applied because construction always occurs
on open, unoccluded terrain tiles — there are no walls or buildings between the
earthworks tile and the player at the moment of placement. The bypass prevents the
occlusion filter from attenuating or low-passing a sound that should be heard clearly.
This does NOT make the sound non-positional — `AL_SOURCE_RELATIVE` remains `AL_FALSE`
and distance attenuation still applies.

---

### Service Alert SFX

These two SFX fire at the tile position (positional) when tile desirability falls critically
low due to service coverage failure. CRITICAL priority — they must not be evicted under
pool pressure.

---

#### `sfx_fire_alert.wav`

| Parameter | Value |
|---|---|
| Duration | 2–4 s |
| Loudness | −18 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`) |
| Priority | CRITICAL |
| EFX bypass | No (positional, benefits from occlusion) |
| Trigger | `CitySimulation::tick()` on tile desirability ≤ 20 with `!tile.alertFired` |

**Character**: a fire alarm or emergency tone at mid-distance. Should convey fire
emergency clearly. A repeating electronic alarm tone (two-tone yelp pattern or continuous
warble) works well. Duration 2–4 s (the file plays once; it does not loop). The 2–4 s
duration is long enough to register as an emergency alert, short enough not to block the
source pool for extended periods.

At −18 LUFS and CRITICAL priority, this sound will cut through ambient beds and music.
Verify it is clearly audible and recognisable as a fire alarm when played at 30 m from
the listener in the context of ambient beds and music.

**Differentiation from `sfx_police_alert`**: the two alert sounds must be clearly
distinguishable in blind listening. Fire and police emergency audio have distinct
conventional characters — use these conventions.

---

#### `sfx_police_alert.wav`

| Parameter | Value |
|---|---|
| Duration | 2–4 s |
| Loudness | −18 LUFS / −1 dBTP |
| Positional | Yes (`AL_SOURCE_RELATIVE = AL_FALSE`) |
| Priority | CRITICAL |
| EFX bypass | No |
| Trigger | `CitySimulation::tick()` on tile desirability ≤ 20 with `!tile.alertFired` (when no fire station covers the tile) |

**Character**: a police siren or emergency tone, distinct from the fire alarm. A single-
tone or two-tone police siren pattern (wail or yelp) is appropriate. Duration 2–4 s.

**Priority note**: if a tile is covered by both Fire Station and Police Station, only
`sfx_fire_alert` fires (fire takes priority). `sfx_police_alert` fires only when the
tile has police coverage but no fire coverage. This is a code-level decision; the artist
only needs to author the two sounds to be distinguishable from each other.

---

### Service / Infrastructure Outage SFX

These four SFX fire non-positionally (`AL_SOURCE_RELATIVE = AL_TRUE`) with EFX bypass.
They represent infrastructure state changes that affect the whole city, not a specific tile.

---

#### `sfx_power_out.wav`

| Parameter | Value |
|---|---|
| Duration | 1–2 s |
| Loudness | −22 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::tick()` on zone tile losing power coverage (`tile.wasPowered` transition) |

**Character**: the sound of power going out. A brief electrical snap or buzz followed by
silence — the sudden absence of electrical hum. A short (< 0.5 s) electrical arc or
transformer buzz is the primary element. The file duration is 1–2 s; the sound may have
a short decay tail of electrical residue or silence that fills the remaining time.

**Differentiation from `sfx_water_out`**: power and water outages have different acoustic
signatures. Power out = electrical crackle or buzz. Water out = water rush or pipe
pressure drop. Make these clearly distinct.

---

#### `sfx_water_out.wav`

| Parameter | Value |
|---|---|
| Duration | 1–2 s |
| Loudness | −22 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::tick()` on zone tile losing water coverage (`tile.wasWaterCovered` transition) |

**Character**: the sound of water supply disruption. A brief rushing water sound
followed by a cutoff — as if a pipe pressure has dropped. Alternatively, a gurgling
or draining sound. Duration 1–2 s.

---

#### `sfx_service_degrade.wav`

| Parameter | Value |
|---|---|
| Duration | 1–2 s |
| Loudness | −22 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::tick()` on Fire Station, Police Station, or Water Tower RNG degradation success |

**Character**: a warning tone indicating a service building has entered reduced-coverage
state. Should convey advisory warning — not an emergency (that is `sfx_fire_alert`/
`sfx_police_alert`), but a "service is now diminished" notification. An electronic
alert chime with a descending pitch or a minor-tone notification sound is appropriate.
Should be clearly distinct from both the crisis stinger and the budget warning sounds.

---

### Economy / Budget SFX

These three SFX fire non-positionally in response to budget and economy events.

---

#### `sfx_budget_warn.wav`

| Parameter | Value |
|---|---|
| Duration | 1–2 s (minimum 1 s) |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::tick()` on first crossing of −25% budget surplus threshold in a deficit streak |

**Character**: a budget deficit warning — the city's finances have crossed a significant
negative threshold. Should convey financial concern without the full alarm of the crisis
stinger. A warning chime or descending tone: a brief (1–2 s) electronic or orchestral
alert that reads as "attention, something is going wrong financially." Minimum 1 s is
required — a sub-second budget warning is too brief to register as a distinct financial
alert separate from UI click sounds.

**Co-fire context**: `sfx_budget_warn` fires at the same moment as the budget deficit
toast notification. The crisis stinger (`stinger_crisis`) fires separately if the deficit
streak reaches month 2. The two sounds may overlap — verify that `sfx_budget_warn`
is distinguishable when the `stinger_crisis` follows within 1–2 s.

---

#### `sfx_loan_issued.wav`

| Parameter | Value |
|---|---|
| Duration | < 1 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::tick()` on forced loan issuance |

**Character**: a brief acknowledgement that a loan has been issued — a financial
transaction sound. Not celebratory (the loan is forced by the game, not a player choice),
but neutral — a short register-receipt or coin/currency sound. Duration < 1 s, crisp
onset. Should be distinct from `sfx_budget_warn` in character.

---

#### `sfx_zone_upgrade.wav`

| Parameter | Value |
|---|---|
| Duration | 1–2 s |
| Loudness | −22 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `CitySimulation::doDensityUnlockTick()` on tile density tier upgrade (cap: 3 calls per invocation) |

**Character**: a zone tile has been automatically upgraded to a higher density tier —
a positive, rewarding sound. Should feel like growth and progress, not a crisis. A brief
upward-trending chime, an ascending musical phrase, or a satisfying "level-up" tone.
Duration 1–2 s. At −22 LUFS it sits 2 dB above the build placement feedback sounds,
making it slightly more prominent — appropriate because density upgrades are rarer and
more meaningful events than individual tile placements.

**Per-wave-tick cap**: `AudioSystem` will only fire this SFX a maximum of 3 times per
density unlock wave (`SimulationConstants::sfx_zone_upgrade_per_tick_cap = 3`). The
artist does not need to account for this cap in authoring — it is a code constraint.

---

### UI SFX

These four SFX fire non-positionally in response to UI events. They must be crisp and
immediate — any perceptible latency between player action and sound is unacceptable.

---

#### `ui_click.wav`

| Parameter | Value |
|---|---|
| Duration | < 0.2 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `UIManager::onEvent()` on toolbar and sub-panel button press |

**Character**: a brief, neutral UI button confirmation sound. Not a click in the
literal sense of a mouse switch (that would be too mechanical and annoying at −24 LUFS);
rather a light, professional interface confirmation — a soft tap, gentle chime, or
subtle UI feedback tone. Duration < 0.2 s. The sound must be non-fatiguing — the player
will hear this every time they change tools or select a zone type, potentially dozens of
times per session.

**No reverb tail**: the sound must be dry. Any reverb or tail extending beyond 0.15 s
will stack audibly when the player clicks rapidly. Keep the energy within the first
0.1–0.15 s.

---

#### `ui_toast.wav`

| Parameter | Value |
|---|---|
| Duration | < 0.3 s |
| Loudness | −22 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `NotificationManager::postCritical()` / `postNormal()` when toast becomes visible |

**Character**: a notification arrival sound — a brief, clear chime indicating that
a message or alert has appeared on screen. Should have higher urgency than `ui_click`
(it represents information appearing, not a player action) but should not alarm. A
short bell or glassy chime at 2–4 kHz is appropriate. At −22 LUFS it is 2 dB louder
than `ui_click`, making notifications slightly more prominent than toolbar interactions.

**No reverb tail**: same constraint as `ui_click` — dry, < 0.25 s total duration
including any decay.

---

#### `ui_menu_open.wav`

| Parameter | Value |
|---|---|
| Duration | < 0.3 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `UIManager::updateSubPanelVisibility()` when a sub-panel becomes visible |

**Character**: a panel or menu opening sound — a brief, upward-toned interaction
feedback. Should feel like a drawer or panel opening (not an alert). A subtle whoosh or
upward sweep combined with a soft impact works well. Duration < 0.3 s.

**Pair with `ui_menu_close`**: the open and close sounds form a pair. They must be
clearly distinguishable (open ≠ close) but tonally related — they should feel like two
states of the same design language.

---

#### `ui_menu_close.wav`

| Parameter | Value |
|---|---|
| Duration | < 0.3 s |
| Loudness | −24 LUFS / −1 dBTP |
| Positional | No (`AL_SOURCE_RELATIVE = AL_TRUE`) |
| Priority | NORMAL |
| EFX bypass | Yes (`AL_DIRECT_FILTER = AL_FILTER_NULL`) |
| Trigger | `UIManager::updateSubPanelVisibility()` when a sub-panel becomes hidden |

**Character**: a panel or menu closing sound — a brief, downward-toned interaction
feedback. A subtle downward sweep or soft close impact. Must be clearly distinguishable
from `ui_menu_open`.

---

## Authoring Procedure

All 16 WAV SFX (including the two vehicle one-shots in the vehicle brief) follow the
same authoring procedure:

1. **Author at 44100 Hz, mono** in the DAW. Do not use a stereo session for mono
   one-shots — a stereo session that is later bounced to mono may introduce summing
   artifacts.
2. **Measure loudness** with a BS.1770-3 integrated LUFS meter (Youlean Loudness Meter,
   MeterPlugs LCAST, or `ffmpeg -af ebur128=peak=true`). Apply gain to reach the target
   integrated LUFS. Note: short one-shots (< 0.5 s) may produce unreliable integrated
   LUFS readings from some meters because the integration window (400 ms for BS.1770
   gating) may not have enough content. If the meter reports "ungated" or shows a value
   far from the target, use a peak-based loudness approach: target −24 LUFS approximately
   corresponds to a peak level of approximately −18 dBFS for impulsive content. Adjust by
   listening and comparing level against a calibrated reference asset.
3. **Apply true-peak limiting** with ceiling at the specified dBTP value. Use a true-peak
   limiter (not a sample-peak limiter) — short WAV SFX can have true-peak values
   significantly above the sample peak due to inter-sample peaks.
4. **Export as WAV PCM**: 44100 Hz, 16-bit, mono, uncompressed. Use "PCM (*.wav)" or
   "Microsoft WAV" with no compression, not ADPCM or any other compressed WAV variant.
5. **Verify with the `wave` Python stdlib module** or a DAW inspector: channels = 1,
   sample rate = 44100, audio format = PCM (0x0001). The `validate_assets.py` Check #19
   (`stinger_*.wav`) verifies mono WAV PCM for stingers; a similar check should be applied
   manually to all WAV SFX before delivery.

---

## Loudness Reference Summary

| Asset | Target LUFS | True-peak ceiling | Duration range |
|---|---|---|---|
| `sfx_build_place.wav` | −24 LUFS | −1 dBTP | < 1 s |
| `sfx_build_demolish.wav` | −24 LUFS | −1 dBTP | < 1 s |
| `sfx_road_build.wav` | −24 LUFS | −1 dBTP | < 1 s |
| `sfx_earthworks.wav` | −24 LUFS | −1 dBTP | < 1 s |
| `sfx_fire_alert.wav` | −18 LUFS | −1 dBTP | 2–4 s |
| `sfx_police_alert.wav` | −18 LUFS | −1 dBTP | 2–4 s |
| `sfx_power_out.wav` | −22 LUFS | −1 dBTP | 1–2 s |
| `sfx_water_out.wav` | −22 LUFS | −1 dBTP | 1–2 s |
| `sfx_service_degrade.wav` | −22 LUFS | −1 dBTP | 1–2 s |
| `sfx_budget_warn.wav` | −24 LUFS | −1 dBTP | 1–2 s (min 1 s) |
| `sfx_loan_issued.wav` | −24 LUFS | −1 dBTP | < 1 s |
| `sfx_zone_upgrade.wav` | −22 LUFS | −1 dBTP | 1–2 s |
| `ui_click.wav` | −24 LUFS | −1 dBTP | < 0.2 s |
| `ui_toast.wav` | −22 LUFS | −1 dBTP | < 0.3 s |
| `ui_menu_open.wav` | −24 LUFS | −1 dBTP | < 0.3 s |
| `ui_menu_close.wav` | −24 LUFS | −1 dBTP | < 0.3 s |

---

## Delivery Verification Checklist

- [ ] All 16 WAV files: mono (1 channel), 44100 Hz, 16-bit PCM (uncompressed).
- [ ] Placement SFX (`sfx_build_place`, `sfx_build_demolish`, `sfx_road_build`, `sfx_earthworks`): each < 1 s, −24 LUFS, ≤ −1 dBTP; `sfx_build_place` and `sfx_build_demolish` clearly distinguishable from each other.
- [ ] Alert SFX (`sfx_fire_alert`, `sfx_police_alert`): each 2–4 s, −18 LUFS, ≤ −1 dBTP; clearly distinguishable from each other in blind listening.
- [ ] Outage SFX (`sfx_power_out`, `sfx_water_out`): each 1–2 s, −22 LUFS, ≤ −1 dBTP; `sfx_power_out` (electrical snap) clearly distinguishable from `sfx_water_out` (water pressure/rush).
- [ ] Service/economy SFX (`sfx_service_degrade`, `sfx_budget_warn`, `sfx_loan_issued`, `sfx_zone_upgrade`): loudness targets per table; tonal character as specified.
- [ ] UI SFX (`ui_click`, `ui_toast`, `ui_menu_open`, `ui_menu_close`): loudness targets per table; each < max duration; no reverb tail; `ui_menu_open` and `ui_menu_close` tonally paired but distinguishable.
- [ ] Blind-mix test: `sfx_fire_alert` and `sfx_police_alert` distinguishable without context.
- [ ] All files exported as uncompressed linear PCM WAV (not ADPCM or other compressed WAV variants).

---

## References

- `architecture/audio-architecture/v1-audio-asset-manifest.md`
- `architecture/audio-architecture/audio-asset-formats.md`
- `architecture/audio-architecture/dynamic-soundscape.md`
- `architecture/audio-architecture/source-pool.md`
- `architecture/audio-architecture/spatial-audio.md`
