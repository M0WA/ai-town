## Phase 9: Polish, Performance & V1 Hardening

### Goal

Reach V1 release quality: performance targets verified, coverage gate green, accessibility complete, all spec contradictions resolved, and full QA pass.

### Deliverables

- [ ] **Performance**: 60 FPS on mid-range desktop GPU with 10,000 vehicle agents; ≤2,000 draw calls per frame; simultaneous scene texture VRAM ≤170 MB (per Phase 6 budget table — this is the working scene budget); total resident texture VRAM ≤1.0 GB ceiling (GPU headroom on 4 GB card). Both constraints must hold simultaneously. (ref: `architecture/game-design/minimum-viable-simulation.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **Map dimensions**: minimum 256×256 tiles, maximum 1024×1024 tiles playable without performance regression (ref: `architecture/game-design/minimum-viable-simulation.md`)
- [ ] **V1 systems confirmed in-scope and complete**: economy, traffic, population growth, zoning, basic service coverage (fire, police, power, water); **post-V1 confirmed excluded**: education, health, environment, disasters, tourism income, Scenario mode (grayed), full undo stack, binary save format, atlased lightmaps, BC5 normal maps, EFX reverb zones, additional road decals (ref: `architecture/game-design/minimum-viable-simulation.md`, `architecture/game-design/economy-model.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **Coverage gate**: Verify the `coverage-linux` lcov gate (established at Phase 2 and enforced at ≥80% from Phase 2 onward) still passes after all Phase 9 changes. This is NOT a new gate — it is a verification that Phase 9 optimizations, refactors, and QA fixes have not regressed coverage below 80% on `src/simulation/`, `src/terrain/`, `src/ui/`; `src/rendering/`, `src/audio/`, `src/platform/` remain excluded. (ref: `architecture/testing/coverage.md`)
- [ ] **Colorblind mode QA verification pass** — no new functional implementation in Phase 9; Phase 5 delivers all colorblind mode code. This deliverable is a joint `gamedesign-ux` + `graphics-dev-irrlicht` QA review confirming the Phase 5 implementation is correct and complete: pattern-supplemented minimap palette renders correctly at both 1280×720 and 1920×1080; R/C/I demand bar labels always present (color supplemental, not color-only); service overlay geometric patterns visible and distinguishable; zone cursor zone-type label overlay present. Any implementation gaps discovered during QA are fixed before Phase 9 exit. (ref: `architecture/ui-ux/resolution-ui-scaling.md`)
- [ ] **Spec contradiction resolutions**: R demand formula aligned to `zoning-system.md`; Commercial demand T=30–65 used consistently; **Industrial demand T=40–80 s confirmed as simulation seconds per both `traffic-system.md` and `simulation-time.md` — the `simulation-time.md` reconciliation statement is the canonical resolution; do NOT remove the `simulation-time.md` reconciliation paragraph**; `input-arbitration.md` priority order used in `UIManager::onEvent()`; TaxRatePanel outside-click pass-through behavior confirmed per `input-arbitration.md`. **Colorblind Mode tab placement: confirmed resolved — both `architecture/ui-ux/resolution-ui-scaling.md` and `architecture/ui-ux/settings-pause-menu.md` agree on Graphics tab > Accessibility subsection placement. Verify implementation matches spec (no spec change needed).** (ref: `architecture/game-design/traffic-system.md`, `architecture/game-design/simulation-time.md`, `architecture/ui-ux/resolution-ui-scaling.md`, `architecture/ui-ux/settings-pause-menu.md`)
- [x] **RESOLVED — Emergency Municipal Bond interest rate**: Both specs agree at **5%/year**. `economy-model.md` states "5% per in-game year (the same unified rate as forced loans)"; `modal-dialog-system.md` line 12 states "interest rate (5%/year)" for the forced loan and line 23 states "5%/year (same rate as forced loans — the bond's distinguishing cost is the doubled principal and 24-tick repayment period)". The 8% figure referenced in the stale flag was a tax rate example ("Residential: 8% → 8.8%"), not an interest rate. No spec change was required — the stale contradiction flag is closed. `BondRepayment_NonDivisiblePrincipal_LastTickAbsorbsRemainder_24Tick` test must assert 5%/year interest calculation as specified.
- [x] **RESOLVED — Zone loop channel count**: zone loops (`zone_residential`, `zone_commercial`, `zone_industrial`) are **mono positional (1 channel)**, pre-loaded into a single AL buffer from the SFX pool, and are NOT streamed. All three audio specs are consistent: `streaming-architecture.md` (lines 10 and 13), `v1-audio-asset-manifest.md`, and `audio-asset-formats.md` all agree. `alBufferData` byte count for zone loops uses `channels = 1`. No spec change was required — the stale contradiction flag is closed.
- [ ] **Regression seed pinning**: seed `0xDEADBEEF00000001` pinned in terrain generator tests; any RapidCheck failures during QA have fixed-seed regressions added before close (ref: `architecture/testing/procedural-generation-seeds.md`)
- [ ] Full QA pass: all simulation systems, audio, UI, save/load, game-over flow, keyboard navigation, edge-scroll on/off, UI scaling at 1280×720 and 1920×1080
- [ ] `all-checks-pass` gate consistently green on `main`; all required `needs:` jobs listed including `validate-assets` (ref: `architecture/ci-cd/github-actions-workflow.md`)
- [ ] Confirm `workflow_dispatch` trigger works for re-running stale CI on `main` (ref: `architecture/ci-cd/github-actions-workflow.md`)

### Exit Criteria

- 60 FPS benchmark at 10,000 agents on reference mid-range GPU
- `coverage-linux` gate green at ≥80%
- All property-based tests and fixed-seed regressions pass
- Full QA sign-off on all V1 systems
- No CRITICAL or HIGH spec contradictions remaining unresolved

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Performance profiling, draw call optimization, LOD tuning |
| `gamedesign-lookandfeel` | Economy/balance QA, simulation tuning |
| `gamedesign-ux` | UI/UX QA, keyboard navigation, accessibility, colorblind mode sign-off |
| `graphics-artist-3d-model` | Asset performance audit, export validation clean pass |
| `graphics-artist-2d-texture` | Texture VRAM audit, atlas utilization check |
| `sound-dev-opensoftal` | Audio QA, streaming stability, starvation monitoring |
| `sound-artist-opensoftal` | Audio asset final loudness and loop verification; mandatory delivery of audio QA loudness measurement report: for every V1 asset in the manifest, measure and record integrated LUFS, true peak dBTP, and LRA using a reference loudness meter; report must confirm each asset meets its spec target; submitted alongside full QA sign-off |
| `test-dev-cpp` | Coverage gate, RapidCheck regression pinning, final test suite green pass |
| `cicd-dev-github` | `all-checks-pass` gate verification, `workflow_dispatch` confirmation |

### Dependencies

- Requires all previous phases complete

### Risks & Spikes

- **RISK**: 10,000-agent simulation performance may require traffic pathfinding optimization. **Spike**: profile A* at 10,000 agents on a 1024×1024 map with standard grid road spacing early in Phase 9.
- **RISK**: `coverage-linux` gate may fall below 80% if `src/ui/` UIManager methods are incompletely tested via `MockUIBackend`. **Spike**: run lcov locally after Phase 5 to check `src/ui/` line coverage before declaring Phase 5 done.
