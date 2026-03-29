# Audio Architecture Specification Review
**Reviewer**: sound-artist-opensoftal
**Date**: 2026-03-29
**Scope**: All files in `/workspace/architecture/audio-architecture/` including `production-briefs/`

---

## Summary

The audio architecture is mature and deeply specified. The majority of issues found are
medium or low severity — primarily authoring gaps, missing CI coverage, and minor internal
inconsistencies. Two HIGH severity issues were identified relating to missing asset QA
tracking documents and an unverified stinger onset/duck timing interaction. No CRITICAL
issues were found.

---

## File-by-File Review

### `audio-asset-formats.md`

**ISSUE-01** — [INCONSISTENCY] — HIGH

**Title**: Ambient bed dual-requirement spec internally contradicts itself on which loop
mechanism the player actually hears.

**Location**: `audio-asset-formats.md`, section "Seamless loop requirement — Ambient beds"
and sub-section "Ambient bed loop authoring — dual-requirement"

**Description**: The spec states in the main bullet: "the runtime seeks to sample 0 (via
`ov_pcm_seek(vf, 0)`) at end-of-file; the pre-baked crossfade in the file eliminates phase
discontinuity." Immediately below it qualifies this with a "Dual-requirement" note: "because
the streaming runtime seeks to sample 0 BEFORE the crossfade tail is reached, bypassing it
entirely. The seamless loop the player hears is produced entirely by the sample-0 boundary,
not the crossfade tail. The 200 ms crossfade tail is a DAW-audition fallback only."

These two statements are in direct conflict. The opening bullet asserts the crossfade
eliminates phase discontinuity as if it participates in the runtime loop; the dual-requirement
clarification correctly states the crossfade tail is never decoded at runtime. An author
reading only the first sentence will misunderstand the runtime loop path.

**Proposed resolution**: Remove or rewrite the first sentence in the main ambient-beds
bullet to not imply the crossfade tail functions at runtime. The opening sentence should
state from the outset that the sample-0 boundary is the only runtime loop mechanism, and
the 200 ms crossfade tail is a DAW-only safeguard.

---

**ISSUE-02** — [GAP] — MEDIUM

**Title**: No CI check specified for ambient bed duration range compliance (90–120 s).

**Location**: `audio-asset-formats.md` / `v1-audio-asset-manifest.md` CI checks table

**Description**: The CI asset validation table covers: music sample rate/channels (check_16),
vehicle engine duration/format (check_17), zone loop duration/format (check_18), zone loop
silence floor (check_21), stinger mono WAV (check_19), and music JSON sidecar (check_14).
There is no CI check that validates ambient bed duration falls within the 90–120 s range
specified in the manifest. An ambient bed authored at 60 s or 140 s would not be caught by
the automated pipeline.

**Proposed resolution**: Add check_22 (or next available number) to `v1-audio-asset-manifest.md`
covering `ambient_*.ogg`: duration must be >= 90.0 s and <= 120.0 s; sample rate must be
44100 Hz; channels must be 2. Assign Phase 10 implementation responsibility.

---

**ISSUE-03** — [GAP] — MEDIUM

**Title**: No CI check for WAV SFX format compliance (mono, PCM, 44100 Hz).

**Location**: `v1-audio-asset-manifest.md` CI checks table, `wav-sfx-production-brief.md`

**Description**: `validate_assets.py` check_19 only covers `stinger_*.wav` mono PCM
verification. None of the 14 non-stinger WAV SFX assets (`sfx_build_place.wav`,
`sfx_fire_alert.wav`, `ui_click.wav`, etc.) are covered by any automated CI check. A
stereo or ADPCM-encoded WAV submitted for any of these would pass all CI gates.

**Proposed resolution**: Add a CI check (e.g., check_20) covering all `*.wav` files in
the audio directory: channels must be 1, audio format tag must be 0x0001 (PCM), sample
rate must be 44100 Hz. This is a superset of check_19 (stingers only) and could replace
it if the stinger check is folded into the broader WAV check. Assign Phase 10.

---

### `audio-occlusion.md`

**ISSUE-04** — [PROBLEM] — MEDIUM

**Title**: `onSourceRecycled()` spec requires acquiring `m_occlusionMutex`, but the caller
is also the pool acquisition path — creating a potential self-deadlock if any future call
site inadvertently holds the mutex before calling `acquireSFXSource()`.

**Location**: `audio-occlusion.md`, "Pool slot recycle — mandatory occlusion state reset" /
"Implementation note" paragraph at bottom

**Description**: The spec states: "The caller must NOT already be holding `m_occlusionMutex`
when calling `onSourceRecycled(i)` — doing so would deadlock." This is a latent danger
because `onSourceRecycled()` is called from pool eviction paths inside `acquireVehicleEnginePair()`
and `acquireSFXSource()`. If any future call site wraps `acquireSFXSource()` with a mutex
that eventually maps to `m_occlusionMutex`, the deadlock fires silently. The spec mentions
the constraint in a single implementation note but does not encode it as a structural
invariant (e.g., a lock-order document or an assert).

**Proposed resolution**: Add a lock-order comment to the `AudioSystem` class definition in
`audio-system.md` that lists `m_streamMutex` and `m_occlusionMutex` as independent mutexes
that must never be held simultaneously (unless explicitly ordered). Document the constraint
in `audio-occlusion.md` as a named invariant: "LOCK ORDER INVARIANT: `m_occlusionMutex`
must never be acquired while holding any other `AudioSystem` mutex."

---

**ISSUE-05** — [GAP] — MEDIUM

**Title**: Occlusion raycast uses simplified `_col.obj` meshes but the spec does not define
how occlusion state is derived when no collision mesh is loaded at a building tile.

**Location**: `audio-occlusion.md`, line 5: "Raycasts performed against simplified
collision-only scene layer (building `_col.obj` meshes + terrain; not full visual geometry)"

**Description**: The spec states raycasts are performed against `_col.obj` meshes. If a
building tile has no `_col.obj` (e.g., during early-game before any buildings are placed,
or if a building model is authored without a collision mesh), the raycast will simply miss.
The source will be treated as unoccluded (gain = 1.0) — which is correct for empty land
but may be incorrect for a built tile with a missing mesh. There is no spec for the
fallback behaviour when a tile's expected collision mesh is absent.

**Proposed resolution**: Add a fallback rule: "If a tile's `_col.obj` is absent or not
loaded, that tile contributes no occlusion to any raycast. The source is treated as
unoccluded for that tile. Log a warning during tile load if a building model has no
accompanying `_col.obj`." This closes the open question explicitly rather than leaving
it to implementation discretion.

---

**ISSUE-06** — [GAP] — LOW

**Title**: No per-source distance check before spending the raycast budget.

**Location**: `audio-occlusion.md`, "Raycast frequency" specification

**Description**: The spec says "only for sources within 100 m of the listener; max 8
raycasts per frame total." The 8-per-frame budget and 100 m distance gate are specified,
but there is no prioritisation rule for which sources get raycast slots when more than 8
sources are within 100 m. At large city sizes with many simultaneous service-event SFX,
the 8-slot budget could be exhausted by distant 99 m sources before nearby 10 m sources
get occlusion updates.

**Proposed resolution**: Add a prioritisation clause: "When more than 8 sources are within
100 m, allocate raycast slots starting with the nearest sources (closest listener distance
first). Sources that are skipped due to budget exhaustion retain their previous occlusion
state until the next raycast slot becomes available."

---

### `audio-system.md`

**ISSUE-07** — [GAP] — HIGH

**Title**: `assets/audio/ambient_bed_qa.md` and `assets/audio/zone_loop_qa.md` are
referenced as Phase 10 mandatory deliverables in both `ambient-bed-production-brief.md`
and `zone-loop-production-brief.md` respectively, but their required content format is
said to be specified in `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts"
section — and that section does not actually define the format of those two QA documents.

**Location**: `ambient-bed-production-brief.md` line 341–346; `zone-loop-production-brief.md`
lines 256–257; `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts" section

**Description**: The ambient bed production brief states: "See `architecture/audio-architecture/
v1-audio-asset-manifest.md` Phase 10 QA Delivery Artifacts section for the required format."
The zone loop brief states: "Complete the `assets/audio/zone_loop_qa.md` entry for this
file." However, the Phase 10 QA Delivery Artifacts section in `v1-audio-asset-manifest.md`
defines only the music crossfade demo WAV files and `crossfade_demo_qa.md`. The format
for `ambient_bed_qa.md` and `zone_loop_qa.md` is never defined anywhere in the spec files.
An author following the cross-reference will find nothing.

**Proposed resolution**: Add a "Ambient Bed QA Document Format" and "Zone Loop QA Document
Format" subsection to `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts".
Specify the minimum required fields for each document (e.g., for ambient bed: filename,
sample-0 gate result, DAW loop cycles count, crossfade tail present, measured LUFS, measured
dBTP, author sign-off name, date).

---

**ISSUE-08** — [GAP] — MEDIUM

**Title**: `IAudioSystem::setSpeed()` is called to notify the audio system of simulation
speed for dawn/dusk collapse, but there is no spec for what `SimSpeed` values are valid
or how the audio system decides which speed thresholds collapse dawn/dusk.

**Location**: `audio-system.md` lines 92–94 (`setSpeed()` method comment), `dynamic-soundscape.md`
dawn/dusk collapse logic

**Description**: `dynamic-soundscape.md` specifies "at simulation speed >= x3, dawn and
dusk transitions are collapsed." The interface method `setSpeed(SimSpeed speed)` is defined,
and `SimSpeed` is aliased to `SpeedMultiplier`. However, the `SpeedMultiplier` enum values
(x1, x2, x3, x10) are defined in `simulation_types.h`, not the audio architecture specs.
The audio architecture specs reference "speed >= x3" but do not enumerate all legal
`SpeedMultiplier` values, nor define what happens with x10 (which is >x3 and should
also trigger collapse). A reader consulting only the audio specs cannot determine the full
set of speed-collapse thresholds.

**Proposed resolution**: Add a table to `dynamic-soundscape.md` listing all `SpeedMultiplier`
values and their dawn/dusk collapse effect: "x1: dawn/dusk crossfades execute normally;
x2: dawn/dusk crossfades execute normally; x3: dawn and dusk collapsed (pre-emptive skip);
x10: dawn and dusk collapsed (pre-emptive skip)."

---

**ISSUE-09** — [DUPLICATE] — LOW

**Title**: The `StingerType` enum values and their coupling to pool source indices are
documented in detail in both `audio-system.md` (lines 24–33) and `source-pool.md` (lines
79–105), with largely overlapping content.

**Location**: `audio-system.md` lines 24–33 forward-declare comment block; `source-pool.md`
lines 79–96

**Description**: Both files explain: CRISIS=55, MILESTONE=56, the intentional coupling to
pool indices, the `triggerStinger()` API safety rule, the required static_assert, and the
post-V1 promotion sequence. `source-pool.md` has the more complete version. The
`audio-system.md` version is a forward-declare context note but duplicates the semantic
explanation unnecessarily.

**Proposed resolution**: Trim `audio-system.md`'s forward-declare comment to a single line
("Values are intentionally equal to pool indices. See source-pool.md for full coupling
rationale and static_assert requirement.") and keep the authoritative definition in
`source-pool.md`.

---

### `dynamic-soundscape.md`

**ISSUE-10** — [GAP] — HIGH

**Title**: Stinger onset timing requirement (0.25 s minimum before peak) is specified
only in `stinger-production-brief.md` and is never cross-referenced from the duck state
machine spec in `dynamic-soundscape.md`.

**Location**: `dynamic-soundscape.md` DUCKING state machine; `stinger-production-brief.md`
"Onset Timing Requirement" section

**Description**: `stinger-production-brief.md` specifies: "The most prominent musical
content of each stinger MUST begin no earlier than 0.25 s into the file. Rationale: the
music duck ramp takes 0.2 s. A stinger whose peak lands at t=0 plays over music still at
full gain, undermining the intended ducked-stinger mix balance."

The duck state machine in `dynamic-soundscape.md` specifies the 0.2 s DUCKING ramp
duration but does not mention the 0.25 s onset authoring requirement. An implementation
team member reading only `dynamic-soundscape.md` when designing the duck trigger will
not see the authoring constraint, and a sound artist reading only the production brief
may not realise the 0.25 s onset is derived from the duck timing in the spec. If the
duck ramp duration is ever changed (e.g., to 0.3 s), the 0.25 s onset minimum must also
change — but the link is implicit.

**Proposed resolution**: Add a cross-reference note to the DUCKING state machine in
`dynamic-soundscape.md`: "NOTE: The 0.2 s duck ramp duration is the basis for the 0.25 s
stinger onset minimum specified in `production-briefs/stinger-production-brief.md`.
Any change to the 0.2 s ramp duration requires a simultaneous update to the stinger onset
minimum in that brief." Mirror this with a back-reference in the production brief.

---

**ISSUE-11** — [GAP] — MEDIUM

**Title**: The music state machine has no defined behaviour for what happens when
`setMusicIntensity()` is called while a music crossfade is already in progress AND the
requested tier matches the incoming (not outgoing) stem.

**Location**: `dynamic-soundscape.md`, "Variant selection policy" and "Interrupted crossfade"

**Description**: The interrupted crossfade spec handles the case where a new crossfade
target differs from the current incoming stem (A→B interrupted by C). The spec for the
no-op guard states: "Calling `setMusicIntensity()` with the tier already active is a no-op."
However, "already active" is ambiguous when a crossfade is in progress. If `AudioSystem` is
crossfading from calm→growth, `m_currentMusicIntensity` has presumably been set to GROWTH
(or is it still CALM until the crossfade completes?). If a new `setMusicIntensity(GROWTH)`
arrives mid-crossfade, is it a no-op because GROWTH is the incoming target, or does it
trigger a new interrupted crossfade because GROWTH is not yet the fully-active stem?

**Proposed resolution**: Add a clarifying note: "The no-op guard compares `intensity`
against `m_pendingMusicIntensity` (the target intensity of any in-progress or queued
crossfade), not `m_currentMusicIntensity` (the intensity of the outgoing stem). If a
crossfade to tier X is already in progress or queued, a new `setMusicIntensity(X)` call
is a no-op."

---

**ISSUE-12** — [GAP] — MEDIUM

**Title**: No spec for how the variant selection (01 vs 02) is reset or preserved when
the game transitions to the main menu and back.

**Location**: `dynamic-soundscape.md` "Variant selection policy"; `audio-system.md`
`transitionToGameplay()` / `transitionToMainMenu()`

**Description**: On `transitionToMainMenu()`, gameplay stems are stopped. On `transitionToGameplay()`,
the first gameplay stem begins with a fresh variant selection. The spec says "randomly
select between variant 01 and 02 ... provided the selected variant is not the currently
playing one (no immediate repeat)." After a main menu round-trip, there is no "currently
playing" gameplay stem, so the no-immediate-repeat rule cannot apply. The spec does not
say whether `m_lastPlayedVariant[]` is reset on transition or preserved. If preserved, the
first stem after returning from the main menu could always play the opposite variant from
the last session — which may be desirable or undesirable depending on intent.

**Proposed resolution**: Specify the reset behaviour explicitly: "On `transitionToGameplay()`,
all per-tier variant-last-played tracking is reset. The first stem of each tier is selected
uniformly at random on the first play in the new session, with no prior-session no-repeat
constraint."

---

**ISSUE-13** — [GAP] — LOW

**Title**: "Crisis audio during nighttime disaster events" section specifies that
`sfx_fire_alert` is still played during nighttime crisis events, but `sfx_fire_alert` is
a CRITICAL-priority positional SFX, not gated by time-of-day music intensity — yet this
is stated in the time-of-day music intensity override section, which may confuse readers
into thinking SFX are also governed by the music override.

**Location**: `dynamic-soundscape.md`, "Time-of-Day Music Intensity Override", "Crisis
audio during nighttime disaster events"

**Description**: The note "A city under crisis at night retains the night Calm ambient bed
and night Calm music stem — the crisis is communicated through notification stingers
(`sfx_fire_alert`, `stinger_crisis`) rather than music intensity escalation" correctly
documents that SFX and stingers are NOT affected by the time-of-day music override. However,
placing this note inside the music intensity override section implies these SFX are somehow
related to that gate, which they are not. `sfx_fire_alert` fires unconditionally on service
failure regardless of time-of-day.

**Proposed resolution**: Move this explanatory note to the top-level "Dynamic Soundscape"
section as a standalone clarification: "SFX events (sfx_fire_alert, sfx_police_alert,
stinger_crisis, etc.) are not gated by the time-of-day music intensity override — they fire
on simulation state transitions regardless of the current time-of-day period."

---

### `streaming-architecture.md`

**ISSUE-14** — [INCONSISTENCY] — MEDIUM

**Title**: The `openStreamOGG` spec mandates calling `alSourceStop` before resetting
`m_samplesQueued`, but `alSourceStop` on a streaming source would also trigger the
starvation recovery check on the audio thread — creating a potential double-refill if the
stop and refill occur on different wakes.

**Location**: `streaming-architecture.md`, "MANDATORY — `openStreamOGG` must flush AL buffer
queue when reopening an active slot"

**Description**: The spec states: "When closing the existing stream, the implementation
MUST call `alSourceStop` and `alSourceUnqueueBuffers` on the slot's source before resetting
`m_samplesQueued = 0`." The starvation recovery spec states: "If source has entered
`AL_STOPPED` AND `stream.m_intentionallyStopped == false`, refill buffers then call
`alSourcePlay()`." When `openStreamOGG` calls `alSourceStop` WITHOUT first setting
`m_intentionallyStopped = true`, the audio thread's next wake will see `AL_STOPPED` and
`m_intentionallyStopped == false` and attempt a spurious starvation recovery on the old
stream before the new stream is ready.

The spec does partially address this with the instruction to set `m_intentionallyStopped = true`
and `alSourceStop()` in the same mutex scope, but the `openStreamOGG` re-open flush code
block does not show `m_intentionallyStopped = true` being set. This is an incomplete
guard.

**Proposed resolution**: Update the `openStreamOGG` re-open flush code block to explicitly
set `stream.m_intentionallyStopped = true` before calling `alSourceStop`, inside the same
`m_streamMutex` lock scope. Add a comment: "Set intentionallyStopped before alSourceStop
to prevent the audio thread from triggering starvation recovery on the old stream."

---

**ISSUE-15** — [GAP] — MEDIUM

**Title**: The spec defines `kSamplesPerBuffer = 64 * 1024 / (2 * 2) = 16384` frames, but
there is no validation check that OGG files decoded for streaming actually produce samples
in multiples of this buffer size. A short OGG (e.g., an ambient bed that is exactly 90.000 s
long) may not fill a buffer exactly at EOF, leaving the last buffer partially filled.
The spec handles EOF via `ov_pcm_seek(vf, 0)` but does not state how partial last-buffer
frames are handled when the file does not align to `kSamplesPerBuffer`.

**Location**: `streaming-architecture.md`, `kSamplesPerBuffer` definition, ambient bed
loop handling

**Description**: `ov_read()` returns a variable number of decoded bytes per call, not
necessarily `kSamplesPerBuffer` frames. The streaming loop presumably reads in a loop until
the buffer is full, but the spec does not explicitly describe this inner decode loop. A
partial buffer at EOF followed by a seek-to-0 and continued reading is implicitly correct
but is never stated — leaving the implementation to guess.

**Proposed resolution**: Add a clarification: "When decoding a buffer slot, call `ov_read()`
in a loop until either `kSamplesPerBuffer` frames have been decoded or EOF is reached. If
EOF is reached mid-buffer (partial fill), call `ov_pcm_seek(vf, 0)` and continue filling
the same buffer from sample 0. The final `alBufferData` call uses only the number of bytes
actually decoded (which may be less than `kSamplesPerBuffer × channels × sizeof(ALshort)`)
for the last buffer before loop wrap."

---

**ISSUE-16** — [INCONSISTENCY] — LOW

**Title**: `streaming-architecture.md` refers to "Pattern A (SPSC queue)" as the primary
pattern with Pattern B (std::vector) as the alternative, but `audio-system.md` Step 1.6
uses `m_preloadQueue.push()` which is SPSC-queue syntax — and `streaming-architecture.md`
notes this inconsistency in a caveat rather than resolving it.

**Location**: `streaming-architecture.md` lines 78–105 "IMPLEMENTER NOTE — `push()` vs
`push_back()` (Pattern B only)"

**Description**: The spec acknowledges the `push()` vs `push_back()` ambiguity between
the two files, but leaves it as an "implementer responsibility to substitute" rather than
aligning the two files. This is a maintainability hazard: future authors editing either
file may not notice the cross-file dependency. The correct approach is to pick one pattern
and use it in both files.

**Proposed resolution**: Choose Pattern A (SPSC queue with `push()`) as canonical, update
`audio-system.md` Step 1.6 to use Pattern A syntax exclusively, and remove the Pattern B
"IMPLEMENTER NOTE" caveat from `streaming-architecture.md` (or keep it only as an
acknowledged alternative with a clear statement that Pattern A is the default).

---

### `spatial-audio.md`

**ISSUE-17** — [GAP] — MEDIUM

**Title**: No reference distance or rolloff factor is specified for `sfx_build_place`,
`sfx_build_demolish`, `sfx_road_build`, and `sfx_earthworks`, which are all positional
3D sources. These assets are not listed in the spatial audio rolloff table.

**Location**: `spatial-audio.md` rolloff table; `wav-sfx-production-brief.md`; `v1-audio-asset-manifest.md`

**Description**: The spatial audio table covers: Traffic/vehicles, Ambient crowd,
Construction/industry, Zone ambient loops, UI/notification sounds, and Service events.
The four placement/construction SFX (`sfx_build_place`, `sfx_build_demolish`, `sfx_road_build`,
`sfx_earthworks`) are positional (`AL_SOURCE_RELATIVE = AL_FALSE`) per the manifest and
WAV SFX brief, yet they appear in none of the rolloff categories. The "Construction /
industry" row in the spatial table is likely intended to cover these (ref=15m, max=120m,
rolloff=1.2), but it is labelled ambiguously — "industry" could be confused with the
industrial zone loop. No source explicitly maps these SFX to a spatial category.

**Proposed resolution**: Rename the "Construction / industry" spatial row to "Placement
and construction SFX (`sfx_build_place`, `sfx_build_demolish`, `sfx_road_build`,
`sfx_earthworks`)" or add an explicit row. Add a row note: "Note: industrial zone ambient
loops use the Zone ambient loops row (ref=30m, max=300m, rolloff=0.6) — not this row."

---

**ISSUE-18** — [GAP] — LOW

**Title**: `spatial-audio.md` does not specify `sfx_intersection_tick` rolloff parameters.

**Location**: `spatial-audio.md` rolloff table; `v1-audio-asset-manifest.md` (intersection_tick)

**Description**: `sfx_intersection_tick` has an "optional distance cull at >80 m" per the
manifest and a cull at >80 m per `vehicle-sfx-production-brief.md`, but it is not in the
spatial audio rolloff table. As a positional WAV source it needs reference distance, max
distance, and rolloff factor defined so the implementation uses consistent values.

**Proposed resolution**: Add `sfx_intersection_tick` to the spatial audio table:
suggested ref=5m, max=80m, rolloff=2.0. The high rolloff reflects the very subtle nature
of the sound and ensures it is essentially inaudible beyond ~40–50m, consistent with the
"very subtle ambient detail" loudness target of −28 LUFS.

---

### `hrtf-initialization.md`

**ISSUE-19** — [GAP] — LOW

**Title**: No spec for HRTF dataset selection when multiple `.mhr` files are present.

**Location**: `hrtf-initialization.md` — HRTF data file section

**Description**: The spec says "ship `default.mhr` (HRTF data file) alongside the OpenAL
runtime." OpenAL Soft supports multiple HRTF datasets. If the OS also has system-level
HRTF datasets installed (common on Linux), and `default.mhr` conflicts with or is
superseded by a system dataset, the HRTF actually applied may differ from the bundled one.
The spec does not specify how to ensure `default.mhr` takes priority, or whether a specific
named dataset must be selected via `ALC_HRTF_ID_SOFT`.

**Proposed resolution**: Add a note: "On Linux, system HRTF datasets may be discovered by
OpenAL Soft before the bundled `default.mhr`. To ensure the bundled dataset is used,
either set `hrtf_paths` in `alsoft.conf` to point to the binary directory, or use the
`ALC_HRTF_ID_SOFT` attribute to select by dataset name. For V1, the default OpenAL Soft
HRTF selection is acceptable — if `ALC_HRTF_STATUS_SOFT` returns `ALC_HRTF_ENABLED_SOFT`,
the dataset in use is suitable regardless of which file it came from."

---

### `source-pool.md`

**ISSUE-20** — [GAP] — MEDIUM

**Title**: The `acquireVehicleEnginePair()` eviction algorithm targets the pair with
"lowest combined priority," but all vehicle pairs are acquired at NORMAL priority — meaning
all pairs have equal combined priority, and the tiebreak (greatest listener distance) is
always the operative criterion. The spec does not clarify that "combined priority" is
effectively always equal for vehicle pairs.

**Location**: `source-pool.md`, `acquireVehicleEnginePair()` step 3

**Description**: The eviction algorithm step 3 states: "select the eviction candidate:
the pair with the lowest combined priority and, as a tiebreak, the greatest average
listener distance squared." Vehicle engine pairs are always acquired at NORMAL priority
(per the SoundPriority enum note: "Traffic/vehicle engine sounds: NORMAL"). Therefore all
`VehiclePairSlot` entries will have `priority = NORMAL = 1`, making the priority comparison
a tautological tie. The eviction logic in practice will always proceed directly to the
distance tiebreak. This is not wrong but could lead an implementer to write an unnecessary
priority comparison step.

**Proposed resolution**: Add a note: "All vehicle pairs are acquired at NORMAL priority;
the combined priority comparison will always be a tie and eviction is determined purely by
the distance tiebreak. The priority field in `VehiclePairSlot` is retained for forward
compatibility with post-V1 multi-priority vehicle types."

---

**ISSUE-21** — [INCONSISTENCY] — MEDIUM

**Title**: The source budget table row for "Reserve" counts 4 sources (3 evictable burst
slots + sources[57]), but the arithmetic note "(55 total evictable − 16 zone − 24 traffic
− 8 service − 4 UI = 3)" is misleading — it implies those 3 sources are permanently
reserved as burst headroom, but they are in fact fully evictable and can be acquired by
any HIGH/CRITICAL caller.

**Location**: `source-pool.md`, Source Budget Allocation table, "Reserve" row

**Description**: The budget table row for "Reserve" includes 3 "evictable-pool burst slots
beyond the named categories" that total to 3 when the named categories are subtracted. These
3 slots are standard evictable SFX pool sources that happen to be in the transient reserve
range ([51..54] minus the 4th = indices [51], [52], [53] that aren't claimed by the 4 UI
slots). Calling them "Reserve" implies they behave like the 4 UI slots that are
soft-reserved via `kTransientReserveStart`, but these 3 are simply unallocated capacity
within the HIGH/CRITICAL range, not a distinct reservation mechanism.

**Proposed resolution**: Rename the row to "Unallocated evictable headroom" and add a
clarifying note: "These 3 sources are normal evictable pool entries within the
HIGH/CRITICAL-accessible range [51..54]. They are not reserved in any programmatic sense —
they represent the unallocated gap between the 4 explicitly-named categories and
kEvictableSFXCount = 55. Any HIGH/CRITICAL-priority sound may acquire them."

---

### `v1-audio-asset-manifest.md`

**ISSUE-22** — [GAP] — MEDIUM

**Title**: The manifest specifies `sfx_fire_alert` fires at "tile desirability ≤ 20 with
`!tile.alertFired`" but `wav-sfx-production-brief.md` specifies a different trigger
condition: "tile desirability falls critically low due to service coverage failure." These
are subtly different conditions — the spec should use one canonical description.

**Location**: `v1-audio-asset-manifest.md` `sfx_fire_alert` row; `wav-sfx-production-brief.md`
Service Alert SFX intro; `service-coverage.md` (referenced indirectly)

**Description**: The manifest says the trigger is desirability <= 20. The WAV SFX brief
intro says "service coverage failure." The WAV SFX brief per-asset spec says: "CRITICAL priority
— they must not be evicted under pool pressure" and "Trigger: `CitySimulation::tick()` on
tile desirability ≤ 20 with `!tile.alertFired`." This is consistent with the manifest, so
the brief intro's "service coverage failure" is an informal description, not a separate
condition. However, having two different phrasings creates reader confusion about whether
both conditions must be true.

**Proposed resolution**: Align the WAV SFX brief intro to match the manifest's precise
trigger: "fire/police alert SFX fire when a tile's desirability score falls to ≤ 20 for
the first time in its current state (`!tile.alertFired`). The exact desirability model
governing when this occurs is defined in `architecture/game-design/service-coverage.md`."

---

**ISSUE-23** — [INCONSISTENCY] — MEDIUM

**Title**: `v1-audio-asset-manifest.md` music duration range specifies "90–180 s" in
the Deep Review Amendment section of `music-production-brief.md` (line 382), but the
manifest itself locks all gameplay stems to exactly 96 s and main menu to exactly 128 s.
There is no "90–180 s range" constraint defined in `v1-audio-asset-manifest.md`.

**Location**: `music-production-brief.md` line 382: "v1-audio-asset-manifest.md requires
all music stems to be 90–180 s"; `v1-audio-asset-manifest.md` locked durations

**Description**: The music production brief's Deep Review Amendment (2026-03-03) states:
"`v1-audio-asset-manifest.md` requires all music stems to be 90–180 s, and 85.33 s is
4.67 s below that floor." This "90–180 s" range constraint does not appear anywhere in
`v1-audio-asset-manifest.md` itself. The manifest specifies exact durations (96.00 s,
128.00 s) with no minimum/maximum range. The "90–180 s" range appears to be an informal
design constraint used to justify the 32→36 bar correction but was never codified in the
authoritative manifest.

**Proposed resolution**: Either add an explicit "Minimum gameplay stem duration: 90 s;
Maximum gameplay stem duration: 180 s" constraint to `v1-audio-asset-manifest.md` (as a
rationale note, since exact durations are already locked), or remove the cross-reference
from the production brief and replace it with: "The SA-3 bar-count lock supersedes any
general duration range; all durations are exact."

---

**ISSUE-24** — [DUPLICATE] — LOW

**Title**: The "Ambient bed JSON sidecar exemption" blockquote in `v1-audio-asset-manifest.md`
is repeated in essentially identical form in three places within the same file and in
`audio-asset-formats.md`, `ambient-bed-production-brief.md`, and the Phase 1 sign-off in
`dynamic-soundscape.md`.

**Location**: `v1-audio-asset-manifest.md` (blockquote above asset table + notes table + after
table); `audio-asset-formats.md` music stem section; `ambient-bed-production-brief.md`
"No JSON Sidecar Required"; `dynamic-soundscape.md` Phase 1 sign-off item 7

**Description**: The exemption rule is stated 5–6 times across the spec files in identical
or near-identical language. While ensuring no author misses it, this level of repetition
creates a maintenance hazard — any future change to the sidecar policy requires updating
all locations simultaneously.

**Proposed resolution**: Retain the full authoritative statement in `audio-asset-formats.md`
(most detailed) and in `v1-audio-asset-manifest.md` (mandatory for the manifest table).
Reduce all other occurrences to a single-sentence cross-reference: "Ambient beds are
exempt from the JSON sidecar requirement — see `audio-asset-formats.md` for the full
exemption specification."

---

### `production-briefs/vehicle-sfx-production-brief.md`

**ISSUE-25** — [GAP] — MEDIUM

**Title**: The vehicle SFX brief specifies OGG encoding at `-q 6` for engine loops, but
`v1-audio-asset-manifest.md` "OGG Vorbis Encoding Quality Minimums" table does not include
a row for vehicle engine loops. The table only covers music stems (-q 8), ambient beds (-q 7),
and zone loops (-q 6).

**Location**: `v1-audio-asset-manifest.md` "OGG Vorbis Encoding Quality Minimums";
`vehicle-sfx-production-brief.md` "Engine Loop Authoring Requirements"

**Description**: Vehicle engine loops (`sfx_vehicle_engine_idle.ogg`,
`sfx_vehicle_engine_move.ogg`) are OGG Vorbis assets with a `-q 6` encoding minimum per
the vehicle SFX brief. This constraint is not reflected in the manifest's OGG encoding
quality table, making the manifest's table incomplete for implementers who rely on it as
the single-source quality reference.

**Proposed resolution**: Add a row to the manifest's OGG encoding quality table:
"Vehicle engine loops (`sfx_vehicle_engine_*.ogg`): `-q 6` minimum (~192 kbps VBR —
same as zone loops; content is mono tonal, -q 6 is sufficient)."

---

**ISSUE-26** — [GAP] — MEDIUM

**Title**: The vehicle SFX brief specifies the idle/move crossblend thresholds as
3 m/s and 8 m/s, but `audio-system.md` `updateVehicleAudio()` comment uses a different
formulation: "idle gain = max(0, 1 − (speedFraction − 0.21) / 0.36)" which implies
thresholds of approximately 2.9 m/s and 7.9 m/s (given max road speed 13.9 m/s). These
round to 3 and 8 m/s but the fractional discrepancy means the formula and the round
numbers are not exactly equivalent.

**Location**: `audio-system.md` `updateVehicleAudio()` comment (0.21 ≈ 3/13.9; 0.36 ≈
5/13.9); `vehicle-sfx-production-brief.md` crossblend table (3 m/s, 8 m/s)
; `dynamic-soundscape.md` Vehicle Engine Audio section (3 m/s, 8 m/s)

**Description**: `dynamic-soundscape.md` Vehicle Engine Audio specifies: "sfx_vehicle_engine_idle
(gain 1.0 at speed < 3 m/s, linearly to 0.0 at speed ≥ 8 m/s)." The `audio-system.md`
formula uses 0.21 and 0.36 which correspond to 3/13.9 ≈ 2.9281... m/s and 5/13.9 ≈
0.3597..., yielding fade-in at ~2.93 m/s not exactly 3.0 m/s and fade-out at ~7.93 m/s not
exactly 8.0 m/s. The vehicle SFX brief uses the round numbers, as does the crossblend
verification table (simulating at "5 m/s mid-blend point"). At the exact 3.0 m/s threshold
boundary the formula and the round numbers diverge slightly.

**Proposed resolution**: Either round the formula constants to exact values (0.2158... → 3/13.9
is fine as a comment; the round numbers are close enough for perceptual purposes), or update
the spec to explicitly note: "The formulas use 0.21 and 0.36 as rounded approximations of
3/13.9 and 5/13.9 respectively. The perceptual crossblend boundary is approximately 3 m/s
and 8 m/s. The formula is authoritative; the round numbers in the production brief and
soundscape spec are approximate documentation for authors."

---

### `production-briefs/stinger-production-brief.md`

**ISSUE-27** — [INCONSISTENCY] — MEDIUM

**Title**: The stinger production brief describes `stinger_crisis` trigger as "fires when a
crisis event is triggered by the simulation (e.g., budget collapse, service failure cascade)"
in the trigger rules section, but the authoritative trigger spec in `dynamic-soundscape.md`
explicitly states it fires only when `consecutive_deficit_months >= 2` is first reached,
NOT on service failure.

**Location**: `stinger-production-brief.md`, "Stinger Trigger Rules — stinger_crisis";
`dynamic-soundscape.md`, "Stinger_crisis trigger specification"

**Description**: The stinger brief's trigger rule says: "Fires when a crisis event is
triggered by the simulation (e.g., budget collapse, service failure cascade)." This is
incorrect — `dynamic-soundscape.md` explicitly states: "`sfx_service_degrade` does NOT
trigger `stinger_crisis` (service degradation is an advisory event; crisis is reserved for
imminent bankruptcy)." And: "stinger_crisis fires in Phase 8 UIManager::update() by direct
polling of `getConsecutiveDeficitMonths()` — NOT on `BudgetDeficitWarn` receipt."

An artist reading only the production brief could conclude that service failure events
trigger `stinger_crisis`, which is incorrect.

**Proposed resolution**: Update the stinger production brief trigger rule to: "Fires when
`consecutive_deficit_months >= 2` is first reached in a deficit streak (the city has been
in deficit for two consecutive months). Trigger is in `UIManager::update()` via direct
polling — NOT on service failure events. Service degradation fires `sfx_service_degrade`
only. See `dynamic-soundscape.md §Stinger_crisis trigger specification` for the full
trigger contract."

---

### `production-briefs/zone-loop-production-brief.md`

**ISSUE-28** — [GAP] — LOW

**Title**: The zone loop brief specifies a minimum duration of 12 s but `validate_assets.py`
check_18 only verifies a maximum cap of 18 s — there is no lower bound check in the CI gate.

**Location**: `v1-audio-asset-manifest.md` check_18 row; `zone-loop-production-brief.md`
locked parameters

**Description**: check_18 tests: "Duration must be <= `kZoneLoopMaxPreloadDurationSeconds`
(18.0 s); file must be mono (1 channel); sample rate must be 44100 Hz." No lower bound is
checked. A zone loop authored at 10 s (below the 12 s minimum) would pass all CI gates.
The 12 s minimum is specified in both the brief and the manifest notes table but is not
automated.

**Proposed resolution**: Update check_18 to also require duration >= 12.0 s. Add
`kZoneLoopMinPreloadDurationSeconds = 12.0f` to `audio_types.h` alongside the existing
max constant, and reference it in check_18.

---

### `production-briefs/ambient-bed-production-brief.md`

**ISSUE-29** — [GAP] — LOW

**Title**: The ambient bed brief specifies `ambient_dawn.ogg` content requires "crickets
fading" from the night bed — but there is no cross-compatibility authoring requirement
between `ambient_night.ogg` insect texture and `ambient_dawn.ogg` opening, analogous to
the day→night direct crossfade compatibility requirement.

**Location**: `ambient-bed-production-brief.md`, `ambient_dawn.ogg` content spec

**Description**: The day→night compatibility requirement is formally documented: "must be
compatible with a direct day→night crossfade at x3 speed; deliver `crossfade_demo_day_to_night.ogg`."
The night→dawn transition also uses a 3 s constant-power crossfade at x1 speed, and the
spec notes the dawn bed should "accept a night→dawn crossfade gracefully." However, no
analogous demo or formal gate is required for the night→dawn crossfade — it is described
only in narrative guidance, without a delivery artifact.

**Proposed resolution**: Either explicitly note that night→dawn crossfade has no demo gate
requirement (as a conscious scoping decision), or add a brief note specifying that the
night→dawn crossfade is informally verified by the author (no committed demo required) and
the acceptance criterion is the author's judgment.

---

## Cross-File Issues

**ISSUE-30** — [MISSING] — MEDIUM

**Title**: No audio specification exists for what happens when `AudioSystem` is constructed
in "silent mode" (alcOpenDevice returns null) and game-level code calls `IAudioSystem` methods.

**Location**: `audio-system.md` constructor note; all files referencing `IAudioSystem` methods

**Description**: `audio-system.md` states: "alcOpenDevice failure: logs warning, sets
`m_deviceLost=true`, returns early (silent mode — all IAudioSystem calls become no-ops)."
This is correctly specified as a "no-op" fallback, but no file details the implementation
contract for each method in silent mode: does `playSound()` return a valid `SoundHandle`
or 0? Does `acquireVehicleEnginePair()` return `{-1, -1}`? If callers do not check for
{-1, -1} (the documented valid failure return), they may log spurious warnings ("Releasing
unknown source pair" from `releaseVehicleEnginePair(-1, -1)` which is specified as a
no-op — but the SoundHandle return from `playSound()` in silent mode is unspecified).

**Proposed resolution**: Add a "Silent mode fallback contract" section to `audio-system.md`:
list each IAudioSystem method and its silent-mode return value:
`playSound()` → returns 0 (invalid handle);
`playPositionalSound()` → returns 0;
`acquireVehicleEnginePair()` → returns {-1, -1};
All void methods → return immediately without action.

---

**ISSUE-31** — [MISSING] — LOW

**Title**: No spec for audio validation on save-game load — if a saved game resumes at
NIGHT time-of-day, the audio system must initialise with the night ambient bed and a Calm
music stem without playing a crossfade from DAY first.

**Location**: `audio-system.md` `transitionToGameplay()` spec; `dynamic-soundscape.md`
time-of-day schedule

**Description**: The `transitionToGameplay()` spec states: "setTimeOfDay() must be called
at least once before transitionToGameplay() is invoked. transitionToGameplay() reads
`m_currentTimeOfDay` to determine which ambient bed to start." This covers the
initialisation contract, but does not address whether the initial ambient bed start uses
a crossfade (from silence/nothing to the ambient bed) or plays immediately at full gain.
If a crossfade is used, the 3 s minimum hold time applies but there is nothing to cross
from (silence → bed), which is a degenerate case. The spec does not clarify whether this
initial "start from silence" is a real crossfade or an immediate gain-set-to-1.0 play.

**Proposed resolution**: Add a note to `transitionToGameplay()`: "The initial ambient bed
start (from silence) is not a crossfade — it is an immediate `alSourcePlay()` with gain
set to 1.0. No crossfade outgoing source exists at start time. The 3 s minimum hold time
begins counting from `alSourcePlay()`, so the first time-of-day transition will queue until
3 s of real wall-clock time has elapsed from session start."

---

## Issues Summary Table

| ID | File(s) | Type | Severity | Title |
|---|---|---|---|---|
| ISSUE-01 | audio-asset-formats.md | INCONSISTENCY | HIGH | Ambient bed loop mechanism self-contradicting |
| ISSUE-02 | audio-asset-formats.md, v1-audio-asset-manifest.md | GAP | MEDIUM | No CI check for ambient bed duration range |
| ISSUE-03 | v1-audio-asset-manifest.md, wav-sfx-production-brief.md | GAP | MEDIUM | No CI check for non-stinger WAV SFX format |
| ISSUE-04 | audio-occlusion.md | PROBLEM | MEDIUM | Mutex lock-order undocumented; latent deadlock risk |
| ISSUE-05 | audio-occlusion.md | GAP | MEDIUM | Undefined behaviour when building has no _col.obj |
| ISSUE-06 | audio-occlusion.md | GAP | LOW | No prioritisation rule for raycast budget allocation |
| ISSUE-07 | v1-audio-asset-manifest.md, ambient-bed-production-brief.md, zone-loop-production-brief.md | GAP | HIGH | ambient_bed_qa.md and zone_loop_qa.md format undefined |
| ISSUE-08 | audio-system.md, dynamic-soundscape.md | GAP | MEDIUM | Speed collapse thresholds not enumerated for x10 |
| ISSUE-09 | audio-system.md, source-pool.md | DUPLICATE | LOW | StingerType coupling rationale duplicated |
| ISSUE-10 | dynamic-soundscape.md, stinger-production-brief.md | GAP | HIGH | Duck ramp / stinger onset timing not cross-referenced |
| ISSUE-11 | dynamic-soundscape.md | GAP | MEDIUM | No-op guard ambiguous during in-progress crossfade |
| ISSUE-12 | dynamic-soundscape.md, audio-system.md | GAP | MEDIUM | Variant selection state on main menu round-trip undefined |
| ISSUE-13 | dynamic-soundscape.md | GAP | LOW | SFX/stinger independence from music override not clearly scoped |
| ISSUE-14 | streaming-architecture.md | INCONSISTENCY | MEDIUM | openStreamOGG flush path missing m_intentionallyStopped guard |
| ISSUE-15 | streaming-architecture.md | GAP | MEDIUM | Partial-buffer decode loop at EOF not specified |
| ISSUE-16 | streaming-architecture.md, audio-system.md | INCONSISTENCY | LOW | push() vs push_back() left as implementer responsibility |
| ISSUE-17 | spatial-audio.md, wav-sfx-production-brief.md | GAP | MEDIUM | Placement SFX rolloff parameters not in spatial table |
| ISSUE-18 | spatial-audio.md, v1-audio-asset-manifest.md | GAP | LOW | sfx_intersection_tick rolloff parameters missing |
| ISSUE-19 | hrtf-initialization.md | GAP | LOW | HRTF dataset selection priority on Linux not specified |
| ISSUE-20 | source-pool.md | GAP | MEDIUM | Vehicle pair eviction priority tie always occurs; undocumented |
| ISSUE-21 | source-pool.md | INCONSISTENCY | MEDIUM | "Reserve" row misleadingly implies reserved mechanism |
| ISSUE-22 | v1-audio-asset-manifest.md, wav-sfx-production-brief.md | GAP | MEDIUM | sfx_fire_alert trigger description inconsistency |
| ISSUE-23 | v1-audio-asset-manifest.md, music-production-brief.md | INCONSISTENCY | MEDIUM | 90–180 s range referenced but not defined in manifest |
| ISSUE-24 | Multiple files | DUPLICATE | LOW | Ambient bed sidecar exemption stated 5–6 times |
| ISSUE-25 | v1-audio-asset-manifest.md, vehicle-sfx-production-brief.md | GAP | MEDIUM | Vehicle engine OGG quality floor missing from manifest table |
| ISSUE-26 | audio-system.md, vehicle-sfx-production-brief.md, dynamic-soundscape.md | INCONSISTENCY | MEDIUM | Idle/move crossblend thresholds: formula vs round numbers |
| ISSUE-27 | stinger-production-brief.md, dynamic-soundscape.md | INCONSISTENCY | MEDIUM | stinger_crisis trigger wrongly includes service failure |
| ISSUE-28 | v1-audio-asset-manifest.md, zone-loop-production-brief.md | GAP | LOW | check_18 missing lower bound (12 s minimum not verified) |
| ISSUE-29 | ambient-bed-production-brief.md | GAP | LOW | Night→dawn crossfade compatibility gate undefined |
| ISSUE-30 | audio-system.md, multiple | MISSING | MEDIUM | Silent mode return value contract not specified per-method |
| ISSUE-31 | audio-system.md, dynamic-soundscape.md | MISSING | LOW | Initial ambient bed start at session load: no crossfade spec |

---

## High/Critical Issue Count: 3 HIGH, 0 CRITICAL
## Medium Issue Count: 15 MEDIUM
## Low Issue Count: 13 LOW

---

*End of review.*
