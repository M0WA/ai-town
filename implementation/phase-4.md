## Phase 4: CI Pipeline Expansion & Asset Tooling Stubs

### Goal

Harden the CI pipeline (GLEW vcpkg, DLL verification, routing verification), deliver all asset-pipeline tooling stubs and schema files, lock artist production briefs and review gates, add the `src/ui/` 25% coverage floor, and lock `simulation_constants.h` Part B — giving Phase 5 and beyond a verified, gate-enforced build environment to ship against.

### Deliverables

#### CI Pipeline Hardening

- [ ] **GLEW vcpkg dependency + DLL hard-fail atomicity (CI-2)**: add `glew` to `vcpkg.json`; add `find_package(GLEW REQUIRED)` + `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)` in the root `CMakeLists.txt`; add `GLEW32.dll` Windows CI DLL hard-fail check `if (-not (Test-Path "build/Release/GLEW32.dll")) { Write-Error "GLEW32.dll not found"; exit 1 }` as a PowerShell step in `build-windows` (CI-2 PowerShell requirement: `if (-not (Test-Path ...)) { exit 1 }` — `Test-Path ... || exit 1` is PS 7+ only; GitHub Actions Windows runners use PS 5.1). **ATOMICITY BLOCKING**: these four items (vcpkg.json addition, CMakeLists find_package, CMakeLists link, PowerShell hard-fail step) MUST land in the same commit. Any partial state breaks the `build-windows` CI job. (ref: `architecture/ci-cd/dependency-management.md`, `architecture/ci-cd/github-actions-workflow.md`)
- [ ] **Linux GLEW artifact verification step (CI-1)**: in `build-linux` CI job, after `cmake --build`, add a step that runs `find build/vcpkg_installed -name "libGLEW.a" | head -1 | grep -q "libGLEW.a"` or equivalent — hard-fails the job if `libGLEW.a` is absent. **CI-1 hard-coded triplet path removed**: use globbed `find build/vcpkg_installed -name "libGLEW.a"` (not a hard-coded triplet subdirectory path, which breaks when vcpkg selects a different triplet). (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [ ] **CI-3 compiler-version detect step ordering**: in BOTH `build-linux` AND `coverage-linux` CI jobs, the step that writes `COMPILER_VERSION` to `$GITHUB_ENV` MUST be a SEPARATE step placed BEFORE the `actions/cache` step that uses `${{ env.COMPILER_VERSION }}` in its cache key. `$GITHUB_ENV` writes are not visible within the same step — the cache step must be a subsequent step. Verify this ordering in the CI YAML; if out of order, fix in Phase 4 before any other CI additions. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/ci-cd/caching.md`)
- [ ] **CI-4 markdown lint gate**: verify the `markdownlint` step in `all-checks-pass` is NOT removed or skipped. This is a permanent CI gate. (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [ ] **CI-5 `actions/setup-python` SHA audit/re-verification**: Phase 1 required the implementer to resolve and commit a real 40-character `actions/setup-python` SHA. Phase 4 is an **audit and re-verification** step — `cicd-dev-github` must confirm the SHA pinned in Phase 1 is still current, has not been superseded by a security advisory, and that the supply-chain lint step in `build-linux` is passing cleanly. If the Phase 1 SHA has been invalidated or rotated, update it here. Do NOT leave a placeholder — the supply-chain lint step will fail CI on any unresolved placeholder or short SHA. (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [ ] **CI routing verification steps** (`cicd-dev-github`): add the following verification steps to `build-linux` CI job to confirm ctest label routing is correct:
  - `ctest --test-dir build -N -L '^unit$' | grep -q "unit"` — verifies at least one test is labelled `unit`
  - `ctest --test-dir build -N -L '^integration$' | grep -q "integration"` — verifies at least one test is labelled `integration`
  - `ctest --test-dir build -N -L '^requires-opengl$' | grep -q "requires-opengl"` — verifies at least one test is labelled `requires-opengl`
  All three steps must pass before Phase 4 is closed. (ref: `architecture/ci-cd/github-actions-workflow.md`, `architecture/testing/headless-ci-testing.md`)
- [ ] **`validate-assets` CI job four-item atomicity requirement (CI-2 atomicity)**: when the `validate-assets` CI job is first added, ALL FOUR of the following items MUST be committed in the same PR/commit: (1) `tools/validate_assets.py` stub (or update with new check), (2) the `validate-assets` CI job YAML block in `.github/workflows/ci.yml`, (3) the `run: python tools/validate_assets.py` step within that job, (4) `validate-assets` added to `all-checks-pass` gate `needs:` list. A partial commit (e.g., job added but not in `needs:`) will silently not gate PRs. Phase 4 delivers the stub version of this atomicity requirement — the full 13-check implementation is Phase 5. (ref: `architecture/ci-cd/github-actions-workflow.md`)

#### Asset Tooling Stubs

- [ ] **`tools/validate_assets.py` Part B — 4-item atomicity (CI-2 atomicity)**: the Phase 1 stub delivered the script file. Phase 4 ensures the full 4-item atomicity requirement is satisfied: verify the CI job YAML, Run step, and `all-checks-pass` needs entry are all present and wired. If any of the four items were missing from Phase 1, fix them in Phase 4. The Phase 4 stub must include a comment block documenting all 13 required checks (plus the sidecar and road LOD2 color checks added in later phases) so that Phase 5 implementers can add check bodies incrementally without structural CMakeLists changes. Check #1 through Check #13 body stubs (`pass` with a TODO comment) must be present. (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/ci-cd/github-actions-workflow.md`)
- [ ] **`tools/vehicle_atlas_registry.json` schema stub** (`graphics-artist-3d-model`, `graphics-dev-irrlicht`): deliver `tools/vehicle_atlas_registry.json` with the canonical nested schema defined in `architecture/asset-standards/building-atlas-layout.md § Required JSON Schema` (nested atlas objects with `upload_path` fields, `assignments` array keyed by `"vehicle_id"`):

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

  The 5 V1 vehicle types (`car_sedan`, `car_hatchback`, `car_suv`, `bus_standard`, `truck_cargo`) must each have an entry stub. **LOD2 threshold confirmed**: `height_floors >= 4` → `_lod2.b3d`; `height_floors <= 3` → billboard (3D-2). **Normal atlas bicubic mip before DXT5nm**: artist must apply bicubic downsampling to each mip level before DXT5nm encoding to preserve normal vector accuracy at lower mips (3D-3). **Vehicle sprite atlas upload path** (2D-1): `sprite_atlas.upload_path` MUST be `"linear"` — vehicle sprites encode RGBA8 roof-color swatches, not photographic diffuse; sRGB gamma decode is incorrect for palette swatches. See `architecture/asset-standards/2d-texture-standards.md` for the `_d` suffix exception. **`graphics-artist-3d-model` co-sign required**: no `vehicle_atlas_registry.json` schema changes may be committed without explicit `graphics-artist-3d-model` sign-off. (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/building-atlas-layout.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **`tools/music_sidecar_schema.json` stub** (`sound-dev-opensoftal`): deliver `tools/music_sidecar_schema.json` defining the required fields `bpm` (integer, positive) and `beats_per_bar` (integer, positive) for all music stem JSON sidecars. This schema is used by `validate_assets.py` Check #14. (ref: `architecture/audio-architecture/audio-asset-formats.md`)
- [ ] **`shader_constants.h` correctness gate** (`graphics-dev-irrlicht`): verify `src/rendering/shader_constants.h` (delivered in Phase 2) contains the full `kTexUnit*` constant table AND the mandatory `static_assert(kTexUnitBillboard <= 15, ...)` guard. If either is missing, add it in Phase 4. Both must be present before Phase 5 begins. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **`simulation_constants.h` Part B** (`gamedesign-lookandfeel`): lock the remaining simulation constants deferred from Phase 1 Part A. Part B adds constants to `src/simulation/simulation_constants.h` that depend on the service coverage and population specs:
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
  - `SECONDS_PER_BUDGET_TICK = 30.0f` — derived from 30 in-game days × 1.0 s/day at 1× speed; `constexpr float`; no `int` static_assert required (float constant); used in `CitySimulation` tick accumulator (ref: `architecture/game-design/simulation-time.md`)

  **`int` `static_assert` guards required (GD-4)**: for all integer constants, add `static_assert(constant_name > 0, "must be positive")` guards immediately following each constant declaration. **`road_lod2_color` is NOT in `SimulationConstants`** — it is a rendering constant placed in `src/rendering/render_constants.h` (Phase 9 deliverable). (ref: `architecture/game-design/service-coverage.md`, `architecture/game-design/population-density-growth.md`, `architecture/game-design/zoning-system.md`)

#### Production Brief Gates

- [ ] **SA-2 music production brief** (`sound-artist-opensoftal`): deliver the Music Production Brief document to the full team with the following locked parameters: BPM = 90; all 6 gameplay stems (`music_calm_01`, `music_calm_02`, `music_growth_01`, `music_growth_02`, `music_crisis_01`, `music_crisis_02`) in the same root key and mode; each stem must be an integer number of bars at 90 BPM; JSON sidecar mandatory for every music stem file (`{"bpm":90,"beats_per_bar":4}`). **Ambient day/night removed from this brief (SA-1)**: ambient beds are a separate deliverable with different parameters (90–120 s, −20 LUFS, DAW crossfade loop). (ref: `architecture/audio-architecture/dynamic-soundscape.md`, `architecture/audio-architecture/audio-asset-formats.md`)
- [ ] **SA-3 bar-count confirmation requirement**: before full music stem production begins, the `sound-artist-opensoftal` must deliver a written confirmation of the bar count for each of the 6 gameplay stems (e.g., "music_calm_01: 32 bars at 90 BPM = 85.33 s"). This confirmation is required because non-integer bar counts cause the bar-boundary crossfade to drift over long play sessions. (ref: `architecture/audio-architecture/dynamic-soundscape.md`)
- [ ] **Zone Loop Production Brief** (`sound-artist-opensoftal`): deliver the Zone Loop Production Brief locking: 100 ms fade-to-silence tail + 100 ms fade-from-silence head at each loop boundary (totalling a 200 ms combined silence window at the loop point); silence floor ≤ −60 dBFS at both head and tail; DAW loopback verification mandatory before delivery; files encoded as OGG Vorbis mono, 44100 Hz, 12–18 s (hard cap 18 s). (ref: `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [ ] **Vehicle SFX Production Brief** (`sound-artist-opensoftal`): deliver the Vehicle SFX Production Brief locking: engine idle and move loops OGG Vorbis mono, minimum 6 s (prohibition on 1–2 s WAV — audibly mechanical repeat), maximum 20 s; −22 LUFS / −2 dBTP; mono positional (1 channel); 44100 Hz. **Why 6 s minimum**: at the lowest pitch-shift ratio (0.75 for stopped vehicles), a 4–5 s loop produces a ~3–3.75 s perceived loop — audibly mechanical. At 6 s minimum, the lowest-pitch loop is ~4.5 s perceived, below the perceptibility threshold. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] **Stinger Production Brief** (`sound-artist-opensoftal`): deliver the Stinger Production Brief locking: `stinger_crisis` WAV PCM mono 2–4 s; `stinger_milestone` WAV PCM mono 2–3 s; both at −18 LUFS / −1 dBTP; authored to function alongside music ducked to 0.4 gain; `stinger_game_over` is post-V1. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [ ] **Ambient Bed Production Brief** (`sound-artist-opensoftal`): deliver the Ambient Bed Production Brief locking: four beds (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`); OGG Vorbis stereo 44100 Hz; 90–120 s; −20 LUFS / −1 dBTP; DAW crossfade loop 200 ms pre-baked at loop boundary; all four individually loudness-verified; runtime seek-to-0 loop verification required before delivery. (ref: `architecture/audio-architecture/v1-audio-asset-manifest.md`, `architecture/audio-architecture/dynamic-soundscape.md`)
- [ ] **Crossfade audibility pre-production gate (BLOCKING)** (`sound-artist-opensoftal`): before committing any music stem files, deliver a 10–15 s low-fidelity rendered audio demo of `music_calm_01` mixed into `music_growth_01` over a 3 s constant-power crossfade demonstrating harmonic compatibility. This demo must be reviewed and approved by the team before full stem production begins. Full stem production is blocked until this gate passes. (ref: `architecture/audio-architecture/dynamic-soundscape.md`)

#### Artist Sign-Off Gates

- [ ] **Texture artist pre-alignment gate** (`graphics-artist-2d-texture`): before any building UV authoring begins in Phase 5, `graphics-artist-2d-texture` must explicitly sign off on the building atlas layout document (`architecture/asset-standards/building-atlas-layout.md`) confirming: (a) 16 cells in the 4×4 grid are sufficient for V1 building variants if wall module atlas cells are shared across variants within the same zone-tier; (b) the 8-texel per-cell border and 496×496 usable zone are understood; (c) the DXT1 sRGB upload path (raw-GL, not `IVideoDriver::getTexture()`) is understood. Sign-off recorded in `architecture/asset-standards/building-atlas-layout.md`. (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] **3D model artist Phase 4 blocking gate** (`graphics-artist-3d-model`): `graphics-artist-3d-model` must co-sign `tools/vehicle_atlas_registry.json` before any Phase 5 terrain or building mesh production begins. Co-sign records: confirmation that the 4×4 diffuse atlas grid and 8×8 normal atlas grid are understood; LOD2 threshold (`height_floors >= 4` → `_lod2.b3d`) is agreed; billboard bake pitch convention (−45° = 45° below horizontal, camera looking downward) is understood. (ref: `architecture/asset-standards/3d-model-standards.md`)
- [ ] **Bootstrap oscillation design review gate** (`gamedesign-lookandfeel`, `test-dev-cpp`): before Phase 6 simulation begins, conduct a design review of the demand bootstrapping decay formula (ticks 0–5: R=50%, C=25%, I=15%, linear decay to demand floor) to verify that the decay does not produce oscillation at 3× simulation speed. Gate: run the simulation stub for 10 ticks at 3× with a blank map and trace `demand_factor` values per zone type; document the trace result. If oscillation is detected, adjust decay rates before Phase 6 begins. (ref: `architecture/game-design/zoning-system.md`)

#### Coverage Gate

- [ ] **`src/ui/` 25% Phase 4 floor** (`test-dev-cpp`, `cicd-dev-github`): after Phase 3 delivers `UIManagerDrawOrderTest`, 5 `UIScaler` tests (verified passing from Phase 1), and 7 `CameraController` tests, run `coverage-linux` and verify `src/ui/` worst-file line coverage is ≥25%. This gate is a BLOCKING defect, not a medium risk. **Phase 5 raises `--fail-under-percent` from 0 to 80** (the full 80% gate is a Phase 5 Procedural Terrain deliverable, not Phase 4). (ref: `architecture/testing/coverage.md`)
- [ ] **`cicd-dev-github` — upgrade `src/ui/` coverage step to blocking gate**: Replace the Phase 1 informational `Baseline-check src/ui/ coverage entries` step in `coverage-linux` with the blocking 25% float-aware `awk` gate from `architecture/ci-cd/github-actions-workflow.md` (the `src/ui/` coverage gate section). This step uses `lcov --list coverage_filtered.info | grep -E "src/ui/"` piped through the `awk -F'|' '{gsub(/%/,"",$NF); print $NF+0}'` pipeline; enforces the gate with float-aware comparison (`result=$(echo "$pct 25" | awk '{if ($1+0 < $2+0) print "FAIL"; else print "PASS"}')`); fails if `$pct` is empty or non-numeric; and exits non-zero if below 25%. Do NOT use integer comparison (`-lt 25`) — it truncates floats and incorrectly passes 24.8%. (ref: `architecture/ci-cd/github-actions-workflow.md`)

### Exit Criteria

- `build-windows` CI job hard-fails if `GLEW32.dll` absent; `build-linux` CI job hard-fails if `libGLEW.a` absent (globbed path, no hard-coded triplet)
- CI-3 verified: compiler-version detect step is separate from and precedes `actions/cache` step in both `build-linux` and `coverage-linux`
- CI routing verification: at least one `unit`, one `integration`, and one `requires-opengl` test confirmed via `ctest -N -L` listing in CI
- `tools/validate_assets.py` 4-item atomicity satisfied: script stub, CI job YAML, Run step, `all-checks-pass` needs entry all present and wired
- `tools/vehicle_atlas_registry.json` delivered with all 5 V1 vehicle type stubs; `graphics-artist-3d-model` co-sign recorded
- `tools/music_sidecar_schema.json` delivered
- `shader_constants.h` contains full `kTexUnit*` table AND `static_assert(kTexUnitBillboard <= 15, ...)`
- `simulation_constants.h` Part B locked with all 15 constants (12 original service/population/zoning + `kNoUnlockThreshold` + `bond_repayment_ticks` + `SECONDS_PER_BUDGET_TICK`) + `int` static_assert guards for all integer constants
- All 5 production briefs (music SA-2/SA-3, zone loop, vehicle SFX, stinger, ambient bed) delivered and acknowledged
- Crossfade audibility pre-production demo approved
- Texture artist and 3D model artist sign-offs recorded
- `src/ui/` worst-file line coverage ≥ 25% confirmed (awk gate green in `coverage-linux`)
- Phase 1 informational `src/ui/` step in `coverage-linux` replaced with the blocking float-aware 25% `awk` gate from `architecture/ci-cd/github-actions-workflow.md`

### Team

| Role | Responsibility |
|---|---|
| `cicd-dev-github` | GLEW vcpkg + DLL hard-fail, Linux GLEW artifact step, CI-3 fix, CI-4 markdown lint verification, CI-5 SHA resolution, routing verification steps, validate-assets 4-item atomicity, replace Phase 1 informational `src/ui/` step with blocking float-aware 25% `awk` gate in `coverage-linux` |
| `graphics-dev-irrlicht` | `find_package(GLEW REQUIRED)`, `target_link_libraries(aitown_render PRIVATE GLEW::GLEW)`, `shader_constants.h` correctness gate |
| `sound-artist-opensoftal` | SA-2 music brief, SA-3 bar-count confirmation, Zone Loop brief, Vehicle SFX brief, Stinger brief, Ambient Bed brief, Crossfade audibility demo |
| `sound-dev-opensoftal` | `tools/music_sidecar_schema.json` stub |
| `gamedesign-lookandfeel` | `simulation_constants.h` Part B (12 constants + static_assert guards), bootstrap oscillation design review |
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
