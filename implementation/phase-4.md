## Phase 4: CI Pipeline Expansion & Asset Tooling Stubs

### Goal

Harden the CI pipeline (GLEW vcpkg, DLL verification, routing verification), deliver all asset-pipeline tooling stubs and schema files, lock artist production briefs and review gates, add the `src/ui/` 25% coverage floor, and lock `simulation_constants.h` Part B — giving Phase 5 and beyond a verified, gate-enforced build environment to ship against.

### Deliverables

#### CI Pipeline Hardening

- [x] **GLEW vcpkg dependency + DLL hard-fail atomicity (CI-2)**: add `glew` to `vcpkg.json`; add `find_package(GLEW REQUIRED)` + `target_link_libraries(aitown_render PRIVATE GLEW::GLEW Irrlicht ...)` in the root `CMakeLists.txt` — **`GLEW::GLEW` MUST appear before `Irrlicht` in the link order** (mitigation-2 per `architecture/graphics-architecture/irrlicht-device-lifecycle.md § GLEW Symbol Duplication Risk`; Phase 2 spike confirmed Irrlicht does not bundle GLEW so the link order is belt-and-suspenders, but the spec-mandated order must be maintained for correctness on all toolchains); add `GLEW32.dll` Windows CI DLL hard-fail check `if (-not (Test-Path "build/GLEW32.dll")) { Write-Error "GLEW32.dll not found"; exit 1 }` as a PowerShell step in `build-windows` (CI-2 PowerShell requirement: `if (-not (Test-Path ...)) { exit 1 }` — `Test-Path ... || exit 1` is PS 7+ only; GitHub Actions Windows runners use PS 5.1). **ATOMICITY BLOCKING**: these four items (vcpkg.json addition, CMakeLists find_package, CMakeLists link, PowerShell hard-fail step) MUST land in the same commit. Any partial state breaks the `build-windows` CI job. (ref: `architecture/ci-cd/dependency-management.md`, `architecture/ci-cd/github-actions-workflow.md`)
- [x] **Linux GLEW artifact verification step (CI-1)**: in `build-linux` CI job, immediately after the Build step, add a step that runs `find build/vcpkg_installed -name "libGLEW.a" | grep -q "libGLEW.a" || exit 1` — hard-fails the job if `libGLEW.a` is absent. **CI-1 globbed path required**: use `find build/vcpkg_installed -name "libGLEW.a"` (NOT a hard-coded triplet subdirectory path, which breaks when vcpkg selects a different triplet) to remain triplet-agnostic. (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [x] **CI-3 compiler-version detect step ordering**: in BOTH `build-linux` AND `coverage-linux` CI jobs, the step that writes `COMPILER_VERSION` to `$GITHUB_ENV` MUST be a SEPARATE step placed BEFORE the `actions/cache` step that uses `${{ env.COMPILER_VERSION }}` in its cache key. `$GITHUB_ENV` writes are not visible within the same step — the cache step must be a subsequent step. Verify this ordering in the CI YAML; if out of order, fix in Phase 4 before any other CI additions. **Status note**: Verified — the Detect GCC/MSVC version step is already a separate named step and precedes the `actions/cache` step in both `build-linux` and `coverage-linux`. No YAML change required — this exit criterion is satisfied by the current `ci.yml`. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/ci-cd/caching.md`)
- [x] **CI-4 markdown lint gate**: verify the `markdownlint` step in `all-checks-pass` is NOT removed or skipped. This is a permanent CI gate. (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [x] **CI-5 `actions/setup-python` SHA audit/re-verification**: Phase 1 required the implementer to resolve and commit a real 40-character `actions/setup-python` SHA. Phase 4 is an **audit and re-verification** step — `cicd-dev-github` must confirm the SHA pinned in Phase 1 is still current, has not been superseded by a security advisory, and that the supply-chain lint step in `build-linux` is passing cleanly. If the Phase 1 SHA has been invalidated or rotated, update it here. Do NOT leave a placeholder — the supply-chain lint step will fail CI on any unresolved placeholder or short SHA. (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [x] **CI routing verification steps** (`cicd-dev-github`): add the following verification steps to `build-linux` CI job to confirm ctest label routing is correct:
  - `ctest --test-dir build -N -L '^unit$' | grep -q "unit"` — verifies at least one test is labelled `unit`
  - `ctest --test-dir build -N -L '^integration$' | grep -q "integration"` — verifies at least one test is labelled `integration`
  - `ctest --test-dir build -N -L '^requires-opengl$' | grep -q "requires-opengl"` — verifies at least one test is labelled `requires-opengl`
  All three steps must pass before Phase 4 is closed. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/testing/headless-ci-testing.md`)
- [x] **Unit-label routing verification step (CI-7)** (`cicd-dev-github`): add a unit-label routing verification step to BOTH `build-linux` AND `coverage-linux` jobs (in addition to the existing `integration` and `requires-opengl` checks). The step must exit 1 if \`ctest --test-dir build -N -L '^unit$' 2>/dev/null | grep -c 'Test #'\` returns zero. Uses \`-N\` dry-run + \`grep -c 'Test #'\` pattern (identical to existing integration/requires-opengl routing checks; \`--collect-labels\` does NOT exist in CTest). This ensures the `unit` label is correctly wired in both jobs, not just `build-linux`. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/testing/headless-ci-testing.md`)
- [x] **`validate-assets` CI job four-item atomicity requirement (CI-2 atomicity)**: when the `validate-assets` CI job is first added, ALL FOUR of the following items MUST be committed in the same PR/commit: (1) `tools/validate_assets.py` stub (or update with new check), (2) the `validate-assets` CI job YAML block in `.github/workflows/ci.yml`, (3) the `run: python tools/validate_assets.py` step within that job, (4) `validate-assets` added to `all-checks-pass` gate `needs:` list. A partial commit (e.g., job added but not in `needs:`) will silently not gate PRs. Phase 4 delivers the stub version of this atomicity requirement — the full 14-check implementation (checks #1–#14) is Phase 5. (ref: `architecture/ci-cd/github-actions-workflow.md`)

#### Asset Tooling Stubs

- [x] **`tools/validate_assets.py` Part B — 4-item atomicity (CI-2 atomicity)**: the Phase 1 stub delivered the script file. Phase 4 ensures the full 4-item atomicity requirement is satisfied: verify the CI job YAML, Run step, and `all-checks-pass` needs entry are all present and wired. If any of the four items were missing from Phase 1, fix them in Phase 4. The Phase 4 stub must include a comment block documenting all 14 required checks (including Check #14 music JSON sidecar, a Phase 5 deliverable, and Check #15 .meta sidecar file presence, a Phase 9 stub) so that Phase 5 implementers can add check bodies incrementally without structural CMakeLists changes. Check #1 through Check #14 body stubs (`pass` with a TODO comment) must be present; Check #15 also stubbed. (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/ci-cd/github-actions-workflow.md`)
- [x] **`tools/vehicle_atlas_registry.json` schema stub** (`graphics-artist-3d-model`, `graphics-dev-irrlicht`): deliver `tools/vehicle_atlas_registry.json` with the canonical nested schema defined in `architecture/asset-standards/building-atlas-layout.md § Required JSON Schema` (nested atlas objects with `upload_path` fields, `assignments` array keyed by `"vehicle_id"`):

  ```json
  {
    "diffuse_atlas": {
      "atlas_file": "vehicles_diffuse_atlas_d.dds",
      "grid": { "cols": 4, "rows": 4, "cell_size_px": 512 },
      "mip_levels": 4,
      "upload_path": "srgb"
    },
    "normal_atlas": {
      "atlas_file": "vehicles_normal_atlas_n.dds",
      "grid": { "cols": 8, "rows": 8, "cell_size_px": 256 },
      "mip_levels": 4,
      "upload_path": "linear",
      "_comment_normal_atlas": "same row/col assignments as diffuse but 8x8 grid; U=[C/8,(C+1)/8], V=[R/8,(R+1)/8]"
    },
    "sprite_atlas": {
      "atlas_file": "vehicles_sprite_atlas_d.dds",
      "grid": { "cols": 16, "rows": 16, "cell_size_px": 16 },
      "mip_levels": 1,
      "upload_path": "linear"
    },
    "assignments": [
      { "vehicle_id": "car_sedan",     "row": 0, "col": 0 },
      { "vehicle_id": "car_hatchback", "row": 0, "col": 1 },
      { "vehicle_id": "car_suv",       "row": 0, "col": 2 },
      { "vehicle_id": "bus_standard",  "row": 1, "col": 0 },
      { "vehicle_id": "truck_cargo",   "row": 1, "col": 1 }
    ]
  }
  ```

  The 5 V1 vehicle types (`car_sedan`, `car_hatchback`, `car_suv`, `bus_standard`, `truck_cargo`) must each have an entry stub. **LOD2 threshold confirmed**: `height_floors >= 4` → `_lod2.b3d`; `height_floors <= 3` → billboard (3D-2). **Normal atlas bicubic mip before DXT5nm**: artist must apply bicubic downsampling to each mip level before DXT5nm encoding to preserve normal vector accuracy at lower mips (3D-3). **Vehicle sprite atlas upload path** (2D-1): `sprite_atlas.upload_path` is `"linear"` — the vehicle sprite atlas encodes synthetic palette-swatch roof colors (not photographic diffuse data) and is uploaded via the linear pool (`IVideoDriver::getTexture()`), per `architecture/asset-standards/building-atlas-layout.md § Vehicle Sprite Atlas` (LINEAR UPLOAD EXCEPTION). **`graphics-artist-3d-model` co-sign required**: no `vehicle_atlas_registry.json` schema changes may be committed without explicit `graphics-artist-3d-model` sign-off. The sign-off must confirm: (1) the V-flip UV convention is correctly documented in the atlas layout spec; (2) the atlas cell boundary UV formulas are correct for both the 4×4 diffuse grid and the 8×8 normal grid; (3) all five V1 vehicle type assignments (`car_sedan`, `car_hatchback`, `car_suv`, `bus_standard`, `truck_cargo`) are in the correct cell positions. Unsigned sign-offs or sign-offs that omit these three conditions do not satisfy this gate. (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/building-atlas-layout.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [x] **`tools/music_sidecar_schema.json` stub** (`sound-dev-opensoftal`): deliver `tools/music_sidecar_schema.json` defining the required fields `bpm` (integer, positive) and `beats_per_bar` (integer, positive) for all music stem JSON sidecars. The file MUST contain exactly the following JSON Schema structure:

  ```json
  {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "type": "object",
    "properties": {
      "bpm":           { "type": "integer", "minimum": 1 },
      "beats_per_bar": { "type": "integer", "minimum": 1 }
    },
    "required": ["bpm", "beats_per_bar"],
    "additionalProperties": false
  }
  ```

  The CI validation step in `validate_assets.py` Check #14 MUST use this schema to reject any music OGG without a valid co-located `.json` sidecar that conforms to this schema — a sidecar with unknown additional fields or missing required fields is a hard asset error. (ref: `architecture/audio-architecture/audio-asset-formats.md`)
- [x] **`shader_constants.h` correctness gate** (`graphics-dev-irrlicht`): verify `src/rendering/shader_constants.h` contains the following constants at minimum: `kTexUnitDiffuse = 0`, `kTexUnitNormal = 1`, `kTexUnitBillboard = 9` (and any others defined in Phase 2), **plus** the mandatory `static_assert(kTexUnitBillboard <= 15, "billboard tex unit must be in GL guaranteed range")`. If any of these are missing, add them in Phase 4. All must be present before Phase 5 begins. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [x] **`simulation_constants.h` Part B** (`gamedesign-lookandfeel`): lock the remaining simulation constants deferred from Phase 1 Part A. Part B adds constants to `src/simulation/simulation_constants.h` that depend on the service coverage and population specs:
  - `fire_station_coverage_radius_m = 800` (meters)
  - `police_station_coverage_radius_m = 600` (meters)
  - `water_tower_coverage_radius_m = 700` (meters)
  - `service_deficit_radius_halving_threshold = -0.10f` (−10% surplus triggers radius halving)
  - `demand_bootstrapping_ticks = 6` // bootstrap subsidies apply during ticks 0 through demand_bootstrapping_ticks−1 (i.e., ticks 0–5); the correct conditional is `if (currentTick < demand_bootstrapping_ticks)`. The divisor of 6 in the zoning-system.md decay formula (`1 − tick/6`) is consistent with this value — the bootstrap period spans exactly 6 ticks (0..5).
  - `demand_floor_residential = 0.20f`
  - `demand_floor_commercial = 0.10f`
  - `demand_floor_industrial = 0.10f`
  - `density_upgrade_wave_demand_threshold = 0.75f`
  - `density_max_upgrade_rate_per_tick = 0.20f`
  - `population_growth_cap_fraction = 0.10f`
  - `population_decay_cap_fraction = 0.15f`
  - `kNoUnlockThreshold = -1.0f` — sentinel returned by `ICitySimulation::getNextUnlockThreshold()` when all six density tiers are unlocked; compare via `threshold < 0.0f`; `static constexpr float`; no `int` static_assert (float constant) (ref: `architecture/game-design/economy-model.md`)
  - `bond_repayment_ticks = 24` — Emergency Municipal Bond repayment period (24 budget ticks = 2 in-game years); distinct from `loan_repayment_ticks = 12`; `int` static_assert guard required: `static_assert(bond_repayment_ticks > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `loan_repayment_ticks = 12` — forced-loan repayment period (12 budget ticks = 1 in-game year); distinct from `bond_repayment_ticks = 24`; `int` static_assert guard required: `static_assert(loan_repayment_ticks > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `grace_period_real_seconds = 120.0f` — forced-loan and road-maintenance grace period in real seconds (not ticks); the 120 s floor ensures the grace window is a meaningful new-player protection window at any simulation speed; `constexpr float`; no `int` static_assert required (float constant); gate measurement begins at first `tick()` call (ref: `architecture/game-design/economy-model.md`)
  - `road_maintenance_cost_per_tile = 10` — road upkeep per road tile per budget tick, dollars; `int`; static_assert guard required: `static_assert(road_maintenance_cost_per_tile > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `service_upkeep_fire_station_per_tick = 500` — fire station upkeep per budget tick, dollars; `int`; static_assert guard required: `static_assert(service_upkeep_fire_station_per_tick > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `service_upkeep_police_station_per_tick = 400` — police station upkeep per budget tick, dollars; `int`; static_assert guard required: `static_assert(service_upkeep_police_station_per_tick > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `service_upkeep_power_plant_per_tick = 1000` — power plant upkeep per budget tick, dollars; `int`; static_assert guard required: `static_assert(service_upkeep_power_plant_per_tick > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `service_upkeep_water_tower_per_tick = 300` — water tower upkeep per budget tick, dollars; `int`; static_assert guard required: `static_assert(service_upkeep_water_tower_per_tick > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `wage_fraction_of_revenue = 0.20f` — fraction of total C/I tax revenue deducted as wages per budget tick; `constexpr float`; no `int` static_assert required (float constant) (ref: `architecture/game-design/economy-model.md`)
  - `density_unlock_scale_easy = 0.70f` — density unlock threshold multiplier at Easy difficulty (70% of Normal); `constexpr float`; no `int` static_assert required (ref: `architecture/game-design/economy-model.md`)
  - `density_unlock_scale_normal = 1.00f` — density unlock threshold multiplier at Normal difficulty (baseline); `constexpr float`; no `int` static_assert required (ref: `architecture/game-design/economy-model.md`)
  - `density_unlock_scale_hard = 1.50f` — density unlock threshold multiplier at Hard difficulty (50% harder); `constexpr float`; no `int` static_assert required (ref: `architecture/game-design/economy-model.md`)
  - `bond_max_uses_easy = 3` — maximum Emergency Municipal Bond uses at Easy difficulty; `int`; static_assert guard required: `static_assert(bond_max_uses_easy > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `bond_max_uses_normal = 2` — maximum Emergency Municipal Bond uses at Normal difficulty; `int`; static_assert guard required: `static_assert(bond_max_uses_normal > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `bond_max_uses_hard = 1` — maximum Emergency Municipal Bond uses at Hard difficulty; `int`; static_assert guard required: `static_assert(bond_max_uses_hard > 0, "must be positive")` (ref: `architecture/game-design/economy-model.md`)
  - `SECONDS_PER_BUDGET_TICK = 30.0f` — derived from 30 in-game days × 1.0 s/day at 1× speed; `constexpr float`; no `int` static_assert required (float constant); used in `CitySimulation` tick accumulator (ref: `architecture/game-design/simulation-time.md`)
  - `desirability_base_value = 50` — starting desirability for a newly zoned tile (neutral mid-point of the [0, 100] range); `int`; static_assert guard required (ref: `architecture/game-design/zoning-system.md`)
  - `adjacency_commercial_residential_bonus = 10` — desirability bonus applied to a Residential tile when a Commercial tile is at Chebyshev distance d=1; no falloff beyond d=1; `int`; static_assert guard required (ref: `architecture/game-design/zoning-system.md`)
  - `adjacency_industrial_residential_base_penalty = 20` — desirability penalty applied to a Residential tile when an Industrial tile is at Chebyshev distance d=1; linear falloff to 0 at d=5; `int`; static_assert guard required (ref: `architecture/game-design/zoning-system.md`)
  - `service_uncovered_desirability_penalty_per_tick = 5` — desirability lost per budget tick for an uncovered residential zone; `int`; static_assert guard required (ref: `architecture/game-design/service-coverage.md`)
  - `service_recovery_desirability_per_tick = 8` — desirability recovered per budget tick when a previously uncovered zone regains service coverage (60% faster than penalty rate); `int`; static_assert guard required (ref: `architecture/game-design/service-coverage.md`)
  - `R_raw_material_rate = 0.05f` — per-resident raw-material demand contribution — used in formula `R_raw_material_demand = current_R_population × R_raw_material_rate`; each resident contributes 0.05 units of industrial raw-material demand per tick; `constexpr float`; no static_assert required (float constant); ref: `architecture/game-design/zoning-system.md`
  - `C_goods_consumption_rate = 0.25f` — fraction of Residential population that generates goods consumption demand per tick; `constexpr float`; no `int` static_assert required (float constant); referenced as `SimulationConstants::C_goods_consumption_rate` in the Commercial demand formula (ref: `architecture/game-design/zoning-system.md`)

  **`int` `static_assert` guards required (GD-4)**: for all integer constants, add `static_assert(constant_name > 0, "must be positive")` guards immediately following each constant declaration. **`road_lod2_color` is NOT in `SimulationConstants`** — it is a rendering constant placed in `src/rendering/render_constants.h` (Phase 9 deliverable). (ref: `architecture/game-design/service-coverage.md`, `architecture/game-design/population-density-growth.md`, `architecture/game-design/zoning-system.md`)

#### Production Brief Gates

- [x] **SA-2 music production brief** (`sound-artist-opensoftal`): deliver the Music Production Brief document to the full team with the following locked parameters: BPM = 90; all 6 gameplay stems (`music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`, `music_crisis_02`) in the same root key and mode; each stem must be an integer number of bars at 90 BPM; JSON sidecar mandatory for every music stem file (`{"bpm":90,"beats_per_bar":4}`). **Both main menu variants** (`music_main_menu_01`, `music_main_menu_02`) MUST share the same root key and mode as all 6 gameplay stems (cross-context harmonic compatibility). **All 8 music files** (both `music_main_menu_*.ogg` variants + 6 `music_calm/growth/crisis_*.ogg` gameplay stems — **ambient beds are NOT included in this count and do NOT require JSON sidecars**) must be authored at **44100 Hz, 16-bit stereo** — authoring at any other sample rate is a hard asset error; `AudioSystem` validates `vi->rate == 44100 && vi->channels == 2` at load time and silences non-conformant streams. **JSON sidecar mandatory for all 8 music files** (the 2 main menu + 6 gameplay stems listed above; ambient bed OGG files are explicitly exempt from the sidecar requirement per `architecture/audio-architecture/v1-audio-asset-manifest.md`) per `music_sidecar_schema.json` (i.e., both `music_main_menu_*.json` and all 6 gameplay `music_*.json` sidecars). **Ambient day/night removed from this brief (SA-1)**: ambient beds are a separate deliverable with different parameters (90–120 s, −20 LUFS, DAW crossfade loop). **Cross-context crossfade demo (BLOCKING, co-delivered with SA-3)**: before full stem production begins, the `sound-artist-opensoftal` must also deliver a 10–15 s low-fidelity rendered demo of `music_main_menu_01` mixed into `music_calm_01` over a 3 s constant-power crossfade at 90 BPM, demonstrating cross-context harmonic compatibility. This demo is reviewed alongside the gameplay crossfade demo (SA-3). Both demos must be approved before full stem production begins. (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [x] **SA-3 bar-count confirmation requirement**: before full music stem production begins, the `sound-artist-opensoftal` must deliver a written confirmation of the bar count for each of the 6 gameplay stems (e.g., "music_calm_01: 32 bars at 90 BPM = 85.33 s"). This confirmation is required because non-integer bar counts cause the bar-boundary crossfade to drift over long play sessions. (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Zone Loop Production Brief** (`sound-artist-opensoftal`): deliver the Zone Loop Production Brief locking: 100 ms fade-to-silence tail + 100 ms fade-from-silence head at each loop boundary (totalling a 200 ms combined silence window at the loop point); silence floor ≤ −60 dBFS at both head and tail; DAW loopback verification mandatory before delivery; files encoded as OGG Vorbis mono, 44100 Hz, 12–18 s (hard cap 18 s); authored to **−26 LUFS / −2 dBTP** (subtle background positional — must not compete with music stems; −2 dBTP true-peak margin required to accommodate loop-boundary transient). (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Vehicle SFX Production Brief** (`sound-artist-opensoftal`): deliver the Vehicle SFX Production Brief locking: engine idle and move loops OGG Vorbis mono, minimum 6 s (prohibition on 1–2 s WAV — audibly mechanical repeat), maximum 20 s; −22 LUFS / −2 dBTP; mono positional (1 channel); 44100 Hz. **Why 6 s minimum**: at the lowest pitch-shift ratio (0.75 for stopped vehicles), a 4–5 s loop produces a ~3–3.75 s perceived loop — audibly mechanical. At 6 s minimum, the lowest-pitch loop is ~4.5 s perceived, below the perceptibility threshold. **Note**: vehicle engine loops (6–20 s OGG Vorbis) and ambient zone loops (12–18 s OGG Vorbis) are distinct asset categories managed by separate SoundId ranges; they must not be conflated. See Zone Loop Production Brief for zone loop specifications. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [x] **Stinger Production Brief** (`sound-artist-opensoftal`): deliver the Stinger Production Brief locking: `stinger_crisis` WAV PCM mono 2–4 s; `stinger_milestone` WAV PCM mono 2–3 s; both at −18 LUFS / −1 dBTP; authored to function alongside music ducked to 0.4 gain; `stinger_game_over` is post-V1. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [x] **Ambient Bed Production Brief** (`sound-artist-opensoftal`): deliver the Ambient Bed Production Brief locking: four beds (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`); OGG Vorbis stereo 44100 Hz; 90–120 s; −20 LUFS / −1 dBTP; DAW crossfade loop 200 ms pre-baked at loop boundary; all four individually loudness-verified; runtime seek-to-0 loop verification required before delivery. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [x] **Crossfade audibility pre-production gate (BLOCKING)** (`sound-artist-opensoftal`): before committing any music stem files, deliver a 10–15 s low-fidelity rendered audio demo of `music_calm_01` mixed into `music_growth_01` over a 3 s constant-power crossfade demonstrating harmonic compatibility. This demo must be reviewed and approved by the team before full stem production begins. Full stem production is blocked until this gate passes. (ref: `architecture/audio-architecture/dynamic-soundscape.md`)

#### Artist Sign-Off Gates

- [x] **Texture artist pre-alignment gate** (`graphics-artist-2d-texture`): before any building UV authoring begins in Phase 5, `graphics-artist-2d-texture` must explicitly sign off on the building atlas layout document (`architecture/asset-standards/building-atlas-layout.md`) confirming: (a) all V1 cell assignments are within the 496×496 px usable area per cell; (b) all required road marking cell types for V1 decals are covered; (c) all V1 vehicle atlas stub rows are finalized and consistent with the diffuse/sprite/normal atlas split. Sign-off recorded in `architecture/asset-standards/building-atlas-layout.md`. (ref: `architecture/asset-standards/building-atlas-layout.md`)

  **Developer texture-unit alignment confirmation** (`graphics-dev-irrlicht`): before Phase 5 terrain shader authoring begins, `graphics-dev-irrlicht` must confirm in a code review comment that the texture unit assignments in `src/rendering/shader_constants.h` (`kTexUnitDiffuse`, `kTexUnitNormal`, terrain layer units 5–8) are finalized and will not change during Phase 5 texture production. This confirmation prevents artist rework caused by late texture-unit renumbering.
- [x] **3D model artist Phase 4 blocking gate** (`graphics-artist-3d-model`): `graphics-artist-3d-model` must co-sign `tools/vehicle_atlas_registry.json` before any Phase 5 terrain or building mesh production begins. Co-sign records: confirmation that the 4×4 diffuse atlas grid and 8×8 normal atlas grid are understood; LOD2 threshold (`height_floors >= 4` → `_lod2.b3d`) is agreed; billboard bake pitch convention (−45° = 45° below horizontal, camera looking downward) is understood. (ref: `architecture/asset-standards/3d-model-standards.md`)
- [x] **Bootstrap oscillation design review gate** (`gamedesign-lookandfeel`, `test-dev-cpp`): before Phase 6 simulation begins, conduct a design review of the demand bootstrapping decay formula (ticks 0–5: R=50%, C=25%, I=15%, linear decay to demand floor) to verify that the decay does not produce oscillation at 3× simulation speed. Gate: run the simulation stub for 10 ticks at 3× with a blank map and trace `demand_factor` values per zone type; document the trace result. If oscillation is detected, adjust decay rates before Phase 6 begins. (ref: `architecture/game-design/zoning-system.md`)

#### Coverage Gate

- [x] **`src/ui/` 25% Phase 4 floor** (`test-dev-cpp`, `cicd-dev-github`): after Phase 3 delivers `UIManagerDrawOrderTest`, 5 `UIScaler` tests (verified passing from Phase 1), and 7 `CameraController` tests, run `coverage-linux` and verify `src/ui/` worst-file line coverage is ≥25%. This gate is a BLOCKING defect, not a medium risk. **Phase 5 raises `--fail-under-percent` from 0 to 80** (the full 80% gate is a Phase 5 Procedural Terrain deliverable, not Phase 4). (ref: `architecture/testing/coverage.md`)
- [x] **`cicd-dev-github` — upgrade `src/ui/` coverage step to blocking gate**: Replace the Phase 1 informational `Baseline-check src/ui/ coverage entries` step in `coverage-linux` with the blocking 25% float-aware worst-file `awk` gate from `architecture/testing/coverage.md` (the `src/ui/` coverage gate section). Use `lcov --list coverage_filtered.info | grep -E 'src/ui/'` piped through `awk -F'|' '{gsub(/%/,"",$NF); print $NF+0}' | sort -n | head -1` to extract the **minimum** (worst-file) coverage value for `src/ui/`; then `awk '{if ($1+0 < 25.0) {print "FAIL: worst src/ui/ file at " $1 "% < 25%"; exit 1} else {print "PASS: " $1 "%"}}'`. This enforces that even the least-covered `src/ui/` file meets the threshold. Do NOT use average coverage — it masks under-covered files. Do NOT use integer comparison (`-lt 25`) — it truncates floats and incorrectly passes 24.8%. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/testing/coverage.md`)

### Exit Criteria

- `build-windows` CI job hard-fails if `GLEW32.dll` absent; `build-linux` CI job hard-fails if `libGLEW.a` absent (globbed path, no hard-coded triplet)
- CI-3 verified: compiler-version detect step is separate from and precedes `actions/cache` step in both `build-linux` and `coverage-linux`
- CI routing verification: at least one `unit`, one `integration`, and one `requires-opengl` test confirmed via `ctest -N -L` listing in `build-linux` CI
- Unit-label routing verification (CI-7) present in BOTH `build-linux` AND `coverage-linux`; step exits 1 if zero `unit`-labelled tests found
- `tools/validate_assets.py` 4-item atomicity satisfied: script stub, CI job YAML, Run step, `all-checks-pass` needs entry all present and wired
- `tools/vehicle_atlas_registry.json` delivered with all 5 V1 vehicle type stubs; `graphics-artist-3d-model` co-sign recorded confirming: (1) V-flip convention, (2) UV formula correctness for 4×4 diffuse and 8×8 normal grids, (3) all 5 V1 vehicle type cell assignments
- `tools/music_sidecar_schema.json` delivered with full JSON Schema structure (Draft-07, `bpm` and `beats_per_bar` integer fields, `required`, `additionalProperties: false`); CI `validate_assets.py` Check #14 uses this schema to reject non-conformant sidecars
- `shader_constants.h` contains full `kTexUnit*` table AND `static_assert(kTexUnitBillboard <= 15, ...)`
- `simulation_constants.h` Part B locked with all 36 constants (12 original service/population/zoning radius/demand/density constants + `kNoUnlockThreshold` + `bond_repayment_ticks` + `SECONDS_PER_BUDGET_TICK` + `desirability_base_value` + `adjacency_commercial_residential_bonus` + `adjacency_industrial_residential_base_penalty` + `service_uncovered_desirability_penalty_per_tick` + `service_recovery_desirability_per_tick` + `grace_period_real_seconds` + `loan_repayment_ticks` + `road_maintenance_cost_per_tile` + `service_upkeep_fire_station_per_tick` + `service_upkeep_police_station_per_tick` + `service_upkeep_power_plant_per_tick` + `service_upkeep_water_tower_per_tick` + `wage_fraction_of_revenue` + `density_unlock_scale_easy` + `density_unlock_scale_normal` + `density_unlock_scale_hard` + `bond_max_uses_easy` + `bond_max_uses_normal` + `bond_max_uses_hard` + `R_raw_material_rate` + `C_goods_consumption_rate`) + `int` static_assert guards for all integer constants
- All 5 production briefs (music SA-2/SA-3, zone loop, vehicle SFX, stinger, ambient bed) delivered and acknowledged
- Crossfade audibility pre-production demo approved
- Texture artist and 3D model artist sign-offs recorded
- `src/ui/` worst-file line coverage ≥ 25% confirmed (awk gate green in `coverage-linux`)
- Phase 1 informational `src/ui/` step in `coverage-linux` replaced with the blocking worst-file 25% `awk` gate from `architecture/testing/coverage.md` (sort -n | head -1 pipeline — not average)

### Team

| Role | Responsibility |
|---|---|
| `cicd-dev-github` | GLEW vcpkg + DLL hard-fail, Linux GLEW artifact step (CI-1, globbed path, immediately after Build step), CI-3 ordering verified (no YAML change needed), CI-4 markdown lint verification, CI-5 SHA resolution, routing verification steps, unit-label routing verification in both `build-linux` and `coverage-linux` (CI-7), validate-assets 4-item atomicity, replace Phase 1 informational `src/ui/` step with blocking float-aware 25% `awk` gate in `coverage-linux` |
| `graphics-dev-irrlicht` | `find_package(GLEW REQUIRED)`, `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)`, `shader_constants.h` correctness gate |
| `sound-artist-opensoftal` | SA-2 music brief, SA-3 bar-count confirmation, Zone Loop brief, Vehicle SFX brief, Stinger brief, Ambient Bed brief, Crossfade audibility demo |
| `sound-dev-opensoftal` | `tools/music_sidecar_schema.json` stub |
| `gamedesign-lookandfeel` | `simulation_constants.h` Part B (36 constants including all economy/grace-period/service-upkeep/difficulty-scaling/bond-limit constants + `R_raw_material_rate` + `C_goods_consumption_rate` + static_assert guards), bootstrap oscillation design review |
| `graphics-artist-3d-model` | `vehicle_atlas_registry.json` co-sign, 3D model artist Phase 4 blocking gate sign-off |
| `graphics-artist-2d-texture` | Texture artist pre-alignment gate sign-off on `building-atlas-layout.md` |
| `test-dev-cpp` | `src/ui/` 25% coverage floor verification (awk gate), bootstrap oscillation design review |

### Dependencies

- Requires Phase 3 complete (UIManager shell and test targets must exist for coverage measurement)
- Requires Phase 2 complete (`aitown_render` CMake target, `shader_constants.h`, `validate_assets.py` stub)
- Requires Phase 1 complete (`RenderSystem`, Irrlicht device lifecycle)

### Risks & Spikes

- **RISK**: `actions/setup-python` SHA may change between Phase 4 authoring and implementation. **Spike**: resolve SHA at implementation time via `gh release view`; do NOT hard-code a value from the plan document.
- **RISK**: `src/ui/` coverage below 25% after Phase 3 indicates test registration or stub-body errors. **Spike**: run `coverage-linux` locally after Phase 3 before declaring Phase 3 done; check for no-op `draw()` bodies in panel stubs.
