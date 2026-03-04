# AI Town — Architecture Document Index

This index maps every specification section to its corresponding file under `architecture/`.
All files contain the full verbatim content from the master spec (`CLAUDE.md`).

---

## Game Design

| Topic | File |
|---|---|
| Simulation Time System | [game-design/simulation-time.md](game-design/simulation-time.md) |
| Economy Model | [game-design/economy-model.md](game-design/economy-model.md) |
| Traffic System | [game-design/traffic-system.md](game-design/traffic-system.md) |
| Zoning System | [game-design/zoning-system.md](game-design/zoning-system.md) |
| Undo System | [game-design/undo-system.md](game-design/undo-system.md) |
| Service Coverage | [game-design/service-coverage.md](game-design/service-coverage.md) |
| Population Density & Growth | [game-design/population-density-growth.md](game-design/population-density-growth.md) |
| Terrain Interaction | [game-design/terrain-interaction.md](game-design/terrain-interaction.md) |
| Game Progression & Modes | [game-design/game-progression-modes.md](game-design/game-progression-modes.md) |
| Game Over Flow | [game-design/game-over-flow.md](game-design/game-over-flow.md) |
| Save System | [game-design/save-system.md](game-design/save-system.md) |
| Minimum Viable Simulation (V1 Scope) | [game-design/minimum-viable-simulation.md](game-design/minimum-viable-simulation.md) |

---

## UI/UX

| Topic | File |
|---|---|
| Main Menu & New Game Flow | [ui-ux/main-menu-new-game-flow.md](ui-ux/main-menu-new-game-flow.md) |
| HUD Layout | [ui-ux/hud-layout.md](ui-ux/hud-layout.md) |
| Tax Rate Panel | [ui-ux/tax-rate-panel.md](ui-ux/tax-rate-panel.md) |
| Camera Controls | [ui-ux/camera-controls.md](ui-ux/camera-controls.md) |
| Hotkey Scheme | [ui-ux/hotkey-scheme.md](ui-ux/hotkey-scheme.md) |
| Query / Inspector Panel | [ui-ux/query-inspector-panel.md](ui-ux/query-inspector-panel.md) |
| Modal Dialog System | [ui-ux/modal-dialog-system.md](ui-ux/modal-dialog-system.md) |
| Minimap | [ui-ux/minimap.md](ui-ux/minimap.md) |
| Settings / Pause Menu | [ui-ux/settings-pause-menu.md](ui-ux/settings-pause-menu.md) |
| Input Arbitration (Focus Management) | [ui-ux/input-arbitration.md](ui-ux/input-arbitration.md) |
| Notification System | [ui-ux/notification-system.md](ui-ux/notification-system.md) |
| Resolution & UI Scaling | [ui-ux/resolution-ui-scaling.md](ui-ux/resolution-ui-scaling.md) |
| UIManager | [ui-ux/ui-manager.md](ui-ux/ui-manager.md) |

---

## Asset Standards

| Topic | File |
|---|---|
| 3D Model Standards (LOD, Collision, Modular Kit) | [asset-standards/3d-model-standards.md](asset-standards/3d-model-standards.md) |
| 2D Texture Standards (Formats, Resolution, UV/Atlas, Terrain) | [asset-standards/2d-texture-standards.md](asset-standards/2d-texture-standards.md) |
| Building Atlas Layout (Cell Assignments, Road Marking Atlas, Vehicle Atlas Stubs) | [asset-standards/building-atlas-layout.md](asset-standards/building-atlas-layout.md) |

---

## Graphics Architecture

| Topic | File |
|---|---|
| Irrlicht Device Lifecycle, Video Driver & Render Quality | [graphics-architecture/irrlicht-device-lifecycle.md](graphics-architecture/irrlicht-device-lifecycle.md) |
| Procedural Terrain | [graphics-architecture/procedural-terrain.md](graphics-architecture/procedural-terrain.md) |
| Scene Graph Ownership Policy | [graphics-architecture/scene-graph-ownership.md](graphics-architecture/scene-graph-ownership.md) |
| Texture Cache | [graphics-architecture/texture-cache.md](graphics-architecture/texture-cache.md) |
| Shader Loading | [graphics-architecture/shader-loading.md](graphics-architecture/shader-loading.md) |
| Benchmark Tool (VRAM/FPS/Anisotropy Profiler) | [graphics-architecture/benchmark-tool.md](graphics-architecture/benchmark-tool.md) |

---

## Audio Architecture

| Topic | File |
|---|---|
| AudioSystem (RAII) | [audio-architecture/audio-system.md](audio-architecture/audio-system.md) |
| Audio Thread Shutdown Sequence | [audio-architecture/audio-thread-shutdown.md](audio-architecture/audio-thread-shutdown.md) |
| Error Checking — Two Distinct Wrappers | [audio-architecture/error-checking.md](audio-architecture/error-checking.md) |
| HRTF Initialization | [audio-architecture/hrtf-initialization.md](audio-architecture/hrtf-initialization.md) |
| Source Pool with Streaming Partition | [audio-architecture/source-pool.md](audio-architecture/source-pool.md) |
| Audio Asset Formats — Three-Tier Classification | [audio-architecture/audio-asset-formats.md](audio-architecture/audio-asset-formats.md) |
| Streaming Architecture | [audio-architecture/streaming-architecture.md](audio-architecture/streaming-architecture.md) |
| 3D Spatial Audio | [audio-architecture/spatial-audio.md](audio-architecture/spatial-audio.md) |
| Audio Occlusion (V1) | [audio-architecture/audio-occlusion.md](audio-architecture/audio-occlusion.md) |
| Dynamic Soundscape | [audio-architecture/dynamic-soundscape.md](audio-architecture/dynamic-soundscape.md) |
| V1 Audio Asset Manifest | [audio-architecture/v1-audio-asset-manifest.md](audio-architecture/v1-audio-asset-manifest.md) |
| Production Brief — Music Stems (SA-2/SA-3) | [audio-architecture/production-briefs/music-production-brief.md](audio-architecture/production-briefs/music-production-brief.md) |
| Production Brief — Ambient Beds | [audio-architecture/production-briefs/ambient-bed-production-brief.md](audio-architecture/production-briefs/ambient-bed-production-brief.md) |
| Production Brief — Zone Loops | [audio-architecture/production-briefs/zone-loop-production-brief.md](audio-architecture/production-briefs/zone-loop-production-brief.md) |
| Production Brief — Vehicle SFX | [audio-architecture/production-briefs/vehicle-sfx-production-brief.md](audio-architecture/production-briefs/vehicle-sfx-production-brief.md) |
| Production Brief — Stingers | [audio-architecture/production-briefs/stinger-production-brief.md](audio-architecture/production-briefs/stinger-production-brief.md) |
| Production Brief — WAV SFX One-Shots | [audio-architecture/production-briefs/wav-sfx-production-brief.md](audio-architecture/production-briefs/wav-sfx-production-brief.md) |

---

## Testing

| Topic | File |
|---|---|
| Framework (GTest + GMock + RapidCheck) | [testing/framework.md](testing/framework.md) |
| Coverage (Linux, lcov, 80% gate) | [testing/coverage.md](testing/coverage.md) |
| Testability Architecture | [testing/testability-architecture.md](testing/testability-architecture.md) |
| Headless CI Testing | [testing/headless-ci-testing.md](testing/headless-ci-testing.md) |
| Property-Based Test Invariants (RapidCheck) | [testing/property-based-tests.md](testing/property-based-tests.md) |
| Procedural Generation Test Seeds | [testing/procedural-generation-seeds.md](testing/procedural-generation-seeds.md) |

---

## CI/CD

| Topic | File |
|---|---|
| GitHub Actions Workflow | [ci-cd/github-actions-workflow.md](ci-cd/github-actions-workflow.md) |
| Dependency Management (vcpkg, CMake, FetchContent) | [ci-cd/dependency-management.md](ci-cd/dependency-management.md) |
| Caching (vcpkg, FetchContent, compiler version) | [ci-cd/caching.md](ci-cd/caching.md) |
| Branch Protection | [ci-cd/branch-protection.md](ci-cd/branch-protection.md) |
