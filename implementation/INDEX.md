# AI Town — Implementation Plan Index

**Date**: 2026-02-18
**Derived from**: All files under `architecture/` (canonical source of truth) and `CLAUDE.md`
**V1 Scope boundary**: Enforced per `architecture/game-design/minimum-viable-simulation.md`. Post-V1 items are explicitly labelled and excluded from phase deliverables.

---

## Spec Contradictions Flagged

The following contradictions were identified during the spec review. They must be resolved before the affected phase can exit. Items marked **RESOLVED** have been closed based on further spec review.

### Open Contradictions

2. **[OPEN] Source pool slot [57] V1 treatment** — `architecture/audio-architecture/source-pool.md` lists source[57] as "reserved post-V1 (game-over stinger)" but also notes it is "evictable in V1". This is intentional design (not a true contradiction) but teams must confirm the V1 eviction behaviour explicitly during Phase 4 implementation so that the post-V1 promotion of source[57] to non-evictable does not require source-pool restructuring. **Affected phase: Phase 4**.

### Resolved Contradictions

1. **[RESOLVED] IAudioSystem method signature and count mismatch** — `architecture/testing/testability-architecture.md` updated to match the canonical 11-method `IAudioSystem` in `architecture/audio-architecture/audio-system.md`. Both files now agree: `playSound(SoundId id, SoundPriority priority, float gain = 1.0f)`, `playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f)`, `setTimeOfDay(TimeOfDay tod)`, and `transitionToGameplay()` are all present. `MockAudioSystem` MOCK_METHOD block updated to 11 methods. **Closed in Phase 0 plan update (2026-02-18)**.

3. **[RESOLVED] Emergency Municipal Bond interest rate** — Both `architecture/game-design/economy-model.md` and `architecture/ui-ux/modal-dialog-system.md` agree at 5%/year. The `economy-model.md` states "5% per in-game year (the same unified rate as forced loans)"; `modal-dialog-system.md` states "5%/year (same rate as forced loans — the bond's distinguishing cost is the doubled principal and 24-tick repayment period)". An earlier draft flag referenced "8%" which was a tax rate example ("Residential: 8% → 8.8%"), not an interest rate. No spec change required; the contradiction flag is closed. All code and tests must use 5%/year per `economy-model.md`. **Closed in Phase 9**.

4. **[RESOLVED] Zone loop channel count** — Zone loops (`zone_residential`, `zone_commercial`, `zone_industrial`) are mono positional (1 channel), pre-loaded into a single AL buffer from the SFX pool, and are NOT streamed. All three relevant audio specs are consistent: `streaming-architecture.md`, `v1-audio-asset-manifest.md`, and `audio-asset-formats.md` all agree on mono. An earlier draft flag referencing stereo was based on a misread of the `streaming-architecture.md` kSamplesPerBuffer section. No spec change required; the contradiction flag is closed. **Closed in Phase 9**.

---

## Roles

| Role ID | Discipline |
|---|---|
| `graphics-dev-irrlicht` | C++ graphics / Irrlicht engine |
| `sound-dev-opensoftal` | C++ audio / OpenAL Soft |
| `gamedesign-lookandfeel` | Gameplay balance, simulation feel |
| `gamedesign-ux` | UI/UX design |
| `graphics-artist-3d-model` | 3D model authoring |
| `graphics-artist-2d-texture` | 2D texture / atlas authoring |
| `sound-artist-opensoftal` | Audio asset authoring |
| `test-dev-cpp` | C++ testing (GTest + GMock + RapidCheck) |
| `cicd-dev-github` | GitHub Actions CI/CD |

---

## Phase Overview

| Phase | Name | Primary Deliverables | Team | Status |
|---|---|---|---|---|
| [0](phase-0.md) | Foundations & CI Skeleton | CMake scaffold, `vcpkg.json` (libvorbisfile chosen), FetchContent (GTest v1.14.0, RapidCheck SHA-pinned), `aitown_add_tests()` macro, `src/interfaces/` with `IClock.h`/`ISimulationRNG.h`/`ISimulationPauser.h`, GitHub Actions CI YAML (build-linux/build-windows/coverage-linux/all-checks-pass), supply-chain lint FIRST step, `--fail-under-percent 0` gate, branch protection + bootstrap procedure | `cicd-dev-github`, `graphics-dev-irrlicht`, `test-dev-cpp` | Planned |
| [1](phase-1.md) | Render Skeleton & Camera | `RenderSystem` RAII (`EDT_OPENGL`, `Bits=32`, `Vsync=false`), render loop order, GL capability queries (consolidated EDT_NULL guard), `IAudioSystem.h` (11 methods, `SoundPriority`), `audio_types.h`, `ICitySimulation.h`, `IUIBackend` (14 methods in `src/ui/`), `UIManager` shell (NotificationManager-first invariant), `UIScaler` (6-param), `CameraController` in `src/ui/`, `TextureCache` skeleton, GLSL stubs, `ManualClock`/`ManualRNG`/`WallClock`/`NullSimulationPauser`/`MockSimulationPauser`, `ui_tests` + `integration_tests` targets | `graphics-dev-irrlicht`, `gamedesign-ux`, `sound-dev-opensoftal`, `test-dev-cpp`, `graphics-artist-3d-model`, `graphics-artist-2d-texture`, `cicd-dev-github` | Planned |
| [2](phase-2.md) | Procedural Terrain | Chunked `IMeshBuffer` terrain (LOD0/1/2), `TerrainSystem` rebuild deque, `SceneEntityManager`, `TextureCache` (3 pools: linear/sRGB raw-GL/splat map raw-GL), sRGB shader + gamma fallback, splat-map shader, terrain textures (DXT1 sRGB 2048×2048), terrain normal maps (DXT5nm), `validate_assets.py` stub, atlas layout sign-off, **`--fail-under-percent` raised from 0 to 80 (BLOCKING)** | `graphics-dev-irrlicht`, `graphics-artist-2d-texture`, `graphics-artist-3d-model`, `test-dev-cpp`, `cicd-dev-github` | Planned |
| [3](phase-3.md) | Simulation Core | `CitySimulation` (4-arg constructor), `SimulationConstants`, economy model, zoning (R/C/I demand + bootstrapping + density unlock), population growth, traffic (A* agent, smoothstep demand coupling), service coverage (fire 800 m/police 600 m/power BFS/water 700 m), undo system, game progression (Sandbox), game-over flow (Scenario skeleton), `simulation_tests` target (5 source files upfront) | `gamedesign-lookandfeel`, `test-dev-cpp` | Planned |
| [4](phase-4.md) | Audio Foundation | `AudioSystem` RAII (`ALC_EXT_thread_local_context` hard-failure, partial-construction safety), 62-source pool (`kEvictableSFXCount=55`, stingers[55..56] V1, streams[58..61]), streaming (OGG/libvorbisfile, 8×64 KB buffers, 10 ms wake), HRTF (`default.mhr`), EFX lowpass occlusion (2 boolean guards), duck state machine (full impl), `AudioSourcePool` vehicle pairs (`kMaxVehiclePairs=12`), DLL + `default.mhr` CI verification, placeholder OGG assets | `sound-dev-opensoftal`, `sound-artist-opensoftal` | Planned |
| [5](phase-5.md) | HUD & UI Panels | Full HUD (resource bar, speed selector, toolbar x:8–72 px, undo button y:608–656 px, grace period countdown, demand bars y:664–744 px, active tool indicator y:752–784 px, notification bell x:1820–1868 px), Budget Detail Panel (320×200 px), Tax Rate Panel (300×200 px), Query/Inspector Panel (240×160 px), Minimap (200×200 px, Service Coverage overlay), Modal Dialog system (forced loan 2-screen, demolish confirm, game-over, WASD preset), Notification System (CRITICAL/Normal queues, auto-pause, log 400×500 px), Settings/Pause Menu, Main Menu + New Game flow, full input arbitration (6-priority chain), all UI tests | `gamedesign-ux`, `graphics-dev-irrlicht`, `graphics-artist-2d-texture`, `test-dev-cpp` | Planned |
| [6](phase-6.md) | City Assets (Buildings & Vehicles) | V1 building models (min 18 sets: 2 per zone×tier; LOD0/1/2 `.b3d`; billboard 1024×128 DXT5 sRGB at −45° pitch), vehicle models (5 types, car ≤1500 tris LOD0), collision meshes, building facade atlas (2048×2048 DXT1 sRGB raw-GL), vehicle atlas (2048×2048 DXT1), road textures (`road_asphalt_tileable.dds`, `road_markings_atlas.dds`), UI sprite sheet (2048×2048 RGBA8 UNORM), LOD swap integration test (negative bounding-box case), `validate_assets.py` full 13-check + sidecar check #14, `validate-assets` CI job added to `all-checks-pass` gate | `graphics-artist-3d-model`, `graphics-artist-2d-texture`, `graphics-dev-irrlicht`, `cicd-dev-github` | Planned |
| [7](phase-7.md) | Dynamic Soundscape | All V1 audio assets per manifest (2 menu music + 4 ambient beds + 6 gameplay stems + 3 zone loops + all WAV SFX + 2 stingers), adaptive music (constant-power crossfade, beat-boundary via `m_samplesQueued`, min 2 s default 3 s), ambient beds (sources[60..61], time-of-day forced Calm), zone loops (mono positional, up to 16 simultaneous, ≤300 m cull), vehicle engine audio (idle/move crossblend, `AL_PITCH` 0.75–1.35, ≤150 m cull, 12 pairs max), stingers (music duck to 0.4, `stinger_milestone` at City Rating only), all UI/SFX wiring | `sound-artist-opensoftal`, `sound-dev-opensoftal` | Planned |
| [8](phase-8.md) | Save System & Game Flow | JSON save V1 (1 auto-save + 3 manual slots; auto-save 120 s or 5 ticks; auto-save on forced loan dialog + on Pause Menu open), save paths (`~/.config/aitown/saves/` Linux / `%APPDATA%\aitown\saves\` Windows), game-over flow integration (`transitionToGameOver()` Scenario-only, progressive warnings, auto-slow to 1×), main menu save-state integration, `stinger_milestone` wired to City Rating transitions, loading screen `flushPendingRebuilds()` integration, all save system unit tests (round-trip, corrupt JSON, schema version, auto-save timing) | `gamedesign-lookandfeel`, `gamedesign-ux`, `graphics-dev-irrlicht`, `test-dev-cpp` | Planned |
| [9](phase-9.md) | Polish, Performance & V1 Hardening | 60 FPS @ 10,000 agents verified, map size 256×256–1024×1024 confirmed, ≤2,000 draw calls, VRAM ≤170 MB scene / ≤1.0 GB total, colorblind mode QA verification pass only (Phase 5 delivers all implementation; Phase 9 verifies correctness at 1280×720 and 1920×1080), coverage gate ≥80% verified (not raised — gate established Phase 2), all spec contradictions resolved, fixed-seed regression pinning, full QA pass, `all-checks-pass` consistently green | All roles | Planned |

---

## Phase Files

- [Phase 0: Foundations & CI Skeleton](phase-0.md)
- [Phase 1: Render Skeleton & Camera](phase-1.md)
- [Phase 2: Procedural Terrain](phase-2.md)
- [Phase 3: Simulation Core](phase-3.md)
- [Phase 4: Audio Foundation](phase-4.md)
- [Phase 5: HUD & UI Panels](phase-5.md)
- [Phase 6: City Assets (Buildings & Vehicles)](phase-6.md)
- [Phase 7: Dynamic Soundscape](phase-7.md)
- [Phase 8: Save System & Game Flow](phase-8.md)
- [Phase 9: Polish, Performance & V1 Hardening](phase-9.md)
- [Post-V1 Backlog](post-v1-backlog.md)
