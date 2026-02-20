# AI Town - 3D City Simulator

## Project Overview
AI Town is a "Sim City" style 3D city simulator built for desktop platforms (Linux/Windows). The project emphasizes realistic graphics, procedurally generated terrain, and immersive audio.

## Technical Stack

### Core Technologies
- **Language**: Object-oriented C++
- **Graphics Engine**: Irrlicht 3D Engine
- **Audio Engine**: OpenSoft AL (OpenAL Soft)
- **Platform**: Cross-platform desktop (Linux/Windows)

### Build System
- CMake (recommended for cross-platform C++ projects)
- Platform-specific build tools (GCC/Clang on Linux, MSVC on Windows)

### ⚠️ Tech Stack Requirements
**The technical stack is fixed and must not be changed.** All development must use:
- C++ (object-oriented)
- Irrlicht 3D Engine for graphics
- OpenSoft AL for audio
- Cross-platform compatibility (Linux/Windows only)

Do not suggest alternative engines, languages, or platforms.

## Project Structure

### Key Components
1. **Terrain Generation**: Procedural terrain generation system (chunked IMeshBuffer approach)
2. **Graphics Rendering**: Irrlicht-based 3D rendering pipeline (OpenGL on all platforms)
3. **Audio System**: OpenAL Soft integration with 3D spatial audio and streaming music
4. **Game Logic**: City simulation mechanics (traffic, economy, zoning, balance)
5. **UI/UX**: User interface for city management

### Planned Development Areas
- **Game Design**: Gameplay balance, traffic systems, economy simulation, zoning
- **Graphics**: 3D models, 2D textures, Irrlicht engine integration
- **Sound**: Game music, sound effects, OpenAL Soft integration
- **Testing**: C++ unit tests (Google Test + GMock), integration tests
- **CI/CD**: GitHub Actions pipelines with vcpkg dependency management

## Team Structure & Specializations

### Game Design
- **Senior Game Designer** (`gamedesign-lookandfeel`): Specialized in gameplay and feel of 3D city simulators - balance, traffic, economy, etc.
- **Senior UI/UX Designer** (`gamedesign-ux`): Specialized in UI/UX of 3D city simulators

### Graphics
- **Senior 3D Model Artist** (`graphics-artist-3d-model`): Specialized in 3D models for 3D city simulators
- **Senior 2D Texture Artist** (`graphics-artist-2d-texture`): Specialized in 2D textures for 3D city simulators
- **Senior C++ Developer** (`graphics-dev-irrlicht`): Specialized in 3D Irrlicht engine

### Sound
- **Senior Sound Artist** (`sound-artist-opensoftal`): Specialized in game music and sounds for city simulators
- **Senior C++ Developer** (`sound-dev-opensoftal`): Specialized in OpenSoft AL and audio

### Testing
- **Senior C++ Test Engineer** (`test-dev-cpp`): Specialized in C++ best practices for testing

### CI/CD
- **Senior GitHub Pipeline Engineer** (`cicd-dev-github`): Specialized in GitHub Actions and continuous integration/deployment

### Product
- **Senior Product Owner** (`prod-owner`): Specialized in implementation planning, phase breakdown, feature prioritization, backlog management, milestone definition, and roadmap creation

### Team Collaboration
**All agents can delegate to each other if necessary.** While each role has specific expertise, cross-functional collaboration is encouraged when tasks require multiple specializations or when expertise from another domain would be beneficial.

## Skills (Slash Commands)

| Skill | Command | Description |
|---|---|---|
| Fix Specs | `/fix-specs` | All agent roles review project specs and iteratively fix all CRITICAL/HIGH issues until a clean pass |
| Design Squad | `/design-squad` | Delegates a task to the full design squad in parallel (Game Designer, UI/UX, 2D Texture, 3D Model, Sound Artist) |
| Tech Squad | `/tech-squad` | Delegates a task to the full technical squad in parallel (GitHub Pipeline, Irrlicht dev, OpenAL Soft dev, Test Engineer) |
| Plan Spec | `/plan-spec` | Product Owner updates the implementation plan from specs, then design + tech squads review it in parallel and iterate until no CRITICAL/HIGH issues remain |
| Plan Fix Spec | `/plan-fix-spec` | Product Owner syncs the implementation plan from specs, then design + tech squads review both the plan AND the spec files, fixing CRITICAL/HIGH issues in specs (via squad agents) and in the plan (via Product Owner) in parallel — repeats until a clean pass is achieved on the targeted phases |

## Development Guidelines

### Code Style
- Follow modern C++ best practices (C++11 or later)
- Object-oriented design principles
- Clear separation of concerns (rendering, logic, audio)

### Dependencies
- Irrlicht 3D Engine
- OpenAL Soft (OpenSoft AL)
- libvorbis/libvorbisfile (OGG decoding — vcpkg port: `libvorbis`)
- Google Test + GMock (unit testing)
- RapidCheck (property-based testing)
- vcpkg (dependency management)
- Platform-specific build tools

### Cross-Platform Considerations
- Use CMake for build configuration
- Avoid platform-specific APIs where possible
- Test on both Linux and Windows regularly
- **Video driver**: Always use `EDT_OPENGL` on both platforms for consistent shader behavior

## File Exclusions
The following files should be ignored when analyzing the codebase:
- `epic.txt` - Project planning and team structure document

## Architecture Specification Files

All detailed specifications live in the `architecture/` directory. **The `architecture/` spec files are the canonical source of truth for detailed design decisions.** `CLAUDE.md` contains project overview, guidelines, and summaries; `architecture/` files contain the full detailed spec for each section. Every change to detailed spec content MUST be made in the appropriate `architecture/` file. `CLAUDE.md` sections should remain as summaries that reference the architecture files. The index below MUST be kept up to date whenever:
- A new spec section is added to `CLAUDE.md` (create a new file under `architecture/` and add the entry)
- A spec section is renamed or moved
- A spec file is deleted or merged

See [`architecture/DOCUMENT_INDEX.md`](architecture/DOCUMENT_INDEX.md) for the full mapping of spec sections to files.

### Architecture File Links

#### Game Design
| Section | File |
|---|---|
| Simulation Time System | [architecture/game-design/simulation-time.md](architecture/game-design/simulation-time.md) |
| Economy Model | [architecture/game-design/economy-model.md](architecture/game-design/economy-model.md) |
| Traffic System | [architecture/game-design/traffic-system.md](architecture/game-design/traffic-system.md) |
| Zoning System | [architecture/game-design/zoning-system.md](architecture/game-design/zoning-system.md) |
| Undo System | [architecture/game-design/undo-system.md](architecture/game-design/undo-system.md) |
| Service Coverage | [architecture/game-design/service-coverage.md](architecture/game-design/service-coverage.md) |
| Population Density & Growth | [architecture/game-design/population-density-growth.md](architecture/game-design/population-density-growth.md) |
| Terrain Interaction | [architecture/game-design/terrain-interaction.md](architecture/game-design/terrain-interaction.md) |
| Game Progression & Modes | [architecture/game-design/game-progression-modes.md](architecture/game-design/game-progression-modes.md) |
| Game Over Flow | [architecture/game-design/game-over-flow.md](architecture/game-design/game-over-flow.md) |
| Save System | [architecture/game-design/save-system.md](architecture/game-design/save-system.md) |
| Minimum Viable Simulation (V1 Scope) | [architecture/game-design/minimum-viable-simulation.md](architecture/game-design/minimum-viable-simulation.md) |

#### UI/UX
| Section | File |
|---|---|
| Main Menu & New Game Flow | [architecture/ui-ux/main-menu-new-game-flow.md](architecture/ui-ux/main-menu-new-game-flow.md) |
| HUD Layout | [architecture/ui-ux/hud-layout.md](architecture/ui-ux/hud-layout.md) |
| Tax Rate Panel | [architecture/ui-ux/tax-rate-panel.md](architecture/ui-ux/tax-rate-panel.md) |
| Camera Controls | [architecture/ui-ux/camera-controls.md](architecture/ui-ux/camera-controls.md) |
| Hotkey Scheme | [architecture/ui-ux/hotkey-scheme.md](architecture/ui-ux/hotkey-scheme.md) |
| Query / Inspector Panel | [architecture/ui-ux/query-inspector-panel.md](architecture/ui-ux/query-inspector-panel.md) |
| Modal Dialog System | [architecture/ui-ux/modal-dialog-system.md](architecture/ui-ux/modal-dialog-system.md) |
| Minimap | [architecture/ui-ux/minimap.md](architecture/ui-ux/minimap.md) |
| Settings / Pause Menu | [architecture/ui-ux/settings-pause-menu.md](architecture/ui-ux/settings-pause-menu.md) |
| Input Arbitration (Focus Management) | [architecture/ui-ux/input-arbitration.md](architecture/ui-ux/input-arbitration.md) |
| Notification System | [architecture/ui-ux/notification-system.md](architecture/ui-ux/notification-system.md) |
| Resolution & UI Scaling | [architecture/ui-ux/resolution-ui-scaling.md](architecture/ui-ux/resolution-ui-scaling.md) |
| UIManager | [architecture/ui-ux/ui-manager.md](architecture/ui-ux/ui-manager.md) |

#### Asset Standards
| Section | File |
|---|---|
| 3D Model Standards | [architecture/asset-standards/3d-model-standards.md](architecture/asset-standards/3d-model-standards.md) |
| 2D Texture Standards | [architecture/asset-standards/2d-texture-standards.md](architecture/asset-standards/2d-texture-standards.md) |
| Building Atlas Layout | [architecture/asset-standards/building-atlas-layout.md](architecture/asset-standards/building-atlas-layout.md) |

#### Graphics Architecture
| Section | File |
|---|---|
| Irrlicht Device Lifecycle | [architecture/graphics-architecture/irrlicht-device-lifecycle.md](architecture/graphics-architecture/irrlicht-device-lifecycle.md) |
| Procedural Terrain | [architecture/graphics-architecture/procedural-terrain.md](architecture/graphics-architecture/procedural-terrain.md) |
| Scene Graph Ownership | [architecture/graphics-architecture/scene-graph-ownership.md](architecture/graphics-architecture/scene-graph-ownership.md) |
| Texture Cache | [architecture/graphics-architecture/texture-cache.md](architecture/graphics-architecture/texture-cache.md) |
| Shader Loading | [architecture/graphics-architecture/shader-loading.md](architecture/graphics-architecture/shader-loading.md) |

#### Audio Architecture
| Section | File |
|---|---|
| AudioSystem (RAII) | [architecture/audio-architecture/audio-system.md](architecture/audio-architecture/audio-system.md) |
| Audio Thread Shutdown | [architecture/audio-architecture/audio-thread-shutdown.md](architecture/audio-architecture/audio-thread-shutdown.md) |
| Error Checking | [architecture/audio-architecture/error-checking.md](architecture/audio-architecture/error-checking.md) |
| HRTF Initialization | [architecture/audio-architecture/hrtf-initialization.md](architecture/audio-architecture/hrtf-initialization.md) |
| Source Pool | [architecture/audio-architecture/source-pool.md](architecture/audio-architecture/source-pool.md) |
| Audio Asset Formats | [architecture/audio-architecture/audio-asset-formats.md](architecture/audio-architecture/audio-asset-formats.md) |
| Streaming Architecture | [architecture/audio-architecture/streaming-architecture.md](architecture/audio-architecture/streaming-architecture.md) |
| 3D Spatial Audio | [architecture/audio-architecture/spatial-audio.md](architecture/audio-architecture/spatial-audio.md) |
| Audio Occlusion | [architecture/audio-architecture/audio-occlusion.md](architecture/audio-architecture/audio-occlusion.md) |
| Dynamic Soundscape | [architecture/audio-architecture/dynamic-soundscape.md](architecture/audio-architecture/dynamic-soundscape.md) |
| V1 Audio Asset Manifest | [architecture/audio-architecture/v1-audio-asset-manifest.md](architecture/audio-architecture/v1-audio-asset-manifest.md) |

#### Testing
| Section | File |
|---|---|
| Framework | [architecture/testing/framework.md](architecture/testing/framework.md) |
| Coverage | [architecture/testing/coverage.md](architecture/testing/coverage.md) |
| Testability Architecture | [architecture/testing/testability-architecture.md](architecture/testing/testability-architecture.md) |
| Headless CI Testing | [architecture/testing/headless-ci-testing.md](architecture/testing/headless-ci-testing.md) |
| Property-Based Tests | [architecture/testing/property-based-tests.md](architecture/testing/property-based-tests.md) |
| Procedural Generation Seeds | [architecture/testing/procedural-generation-seeds.md](architecture/testing/procedural-generation-seeds.md) |

#### CI/CD
| Section | File |
|---|---|
| GitHub Actions Workflow | [architecture/ci-cd/github-actions-workflow.md](architecture/ci-cd/github-actions-workflow.md) |
| Dependency Management | [architecture/ci-cd/dependency-management.md](architecture/ci-cd/dependency-management.md) |
| Caching | [architecture/ci-cd/caching.md](architecture/ci-cd/caching.md) |
| Branch Protection | [architecture/ci-cd/branch-protection.md](architecture/ci-cd/branch-protection.md) |

## Key Design Goals
1. **Realistic Graphics**: High-quality 3D rendering with detailed textures
2. **Generated Terrain**: Procedural generation for varied landscapes
3. **Performance**: Smooth gameplay on desktop hardware
4. **Simulation Depth**: Meaningful city management mechanics
5. **Cross-Platform**: Consistent experience on Linux and Windows

---

## Getting Started
(To be populated as project structure develops)

### Building
```bash
# Linux
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build

# Windows
cmake -B build -S . -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

### Running
```bash
./build/aitown
```

### Running Tests
```bash
# Linux — unit tests (no display needed; -LE excludes integration and OpenGL tests)
ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure

# Linux — integration tests (no display required)
ctest --test-dir build -L "^integration$" --output-on-failure

# Linux — OpenGL tests (requires virtual display)
xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure

# Linux with coverage
cmake -B build -S . -DENABLE_COVERAGE=ON -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure
ctest --test-dir build -L "^integration$" --output-on-failure
xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure
lcov --capture --directory build --base-directory . --ignore-errors mismatch --output-file coverage.info
BUILD_DIR=build
lcov --remove coverage.info \
  --ignore-errors unused \
  '/usr/*' \
  "*/.fetchcontent_cache/*" \
  '*/tests/*' \
  '*/mock_*.h' '*/mock_*.cpp' \
  '*/manual_*.h' '*/manual_*.cpp' \
  '*/src/rendering/*' '*/src/audio/*' '*/src/platform/*' \
  --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html/
lcov --summary coverage_filtered.info
# NOTE: --fail-under-percent does not exist in lcov 2.0 (ubuntu-latest ships 2.0).
# Phase 0 gate is informational only. Phase 2 adds a real 80% gate via awk or lcov 2.1+.

# Windows
ctest --test-dir build -C Release --output-on-failure
```

## Notes for AI Assistants
- C++ project using Irrlicht (EDT_OPENGL only) and OpenAL Soft
- Focus on cross-platform compatibility (Linux + Windows)
- Prioritize performance and code quality; follow OO design patterns
- Terrain uses chunked `IMeshBuffer` — never `ITerrainSceneNode`
- `SMesh*` must be released via `->drop()`, never `delete`
- **SMesh bounding boxes**: call `recalculateBoundingBox()` on every `SMeshBuffer` AND the `SMesh` itself before `addMeshSceneNode()` — omitting this breaks frustum culling
- **LOD swap (buildings/vehicles)**: use `node->setMesh(newLODMesh)` to swap mesh reference in-place — preserves scene node transform and materials, avoids scene graph mutation. Only destroy and recreate the node on entity death or chunk unload. **Before calling `setMesh()`**, call `recalculateBoundingBox()` on each mesh buffer and then the `SMesh` itself — identical to the terrain mesh attachment sequence. Omitting this leaves a stale bounding box, causing incorrect frustum culling at the new LOD.
- **LOD rebuild (terrain chunks)**: full node rebuild required (vertex count changes); always call `node->remove()` on the old node (via `SceneEntityManager::destroy()`) before creating the new one; store chunk IDs (not raw node pointers) in rebuild deque
- **LOD hysteresis**: use separate swap-in / swap-out distances (5–10 m band); never bare threshold comparisons
- **Eviction sequence**: iterate node's material slots to clear all texture pointers, THEN `driver->setMaterial(SMaterial{})`, THEN `textureCache->evictUnreferenced()`, THEN `node->remove()`
- **sRGB textures**: upload diffuse via **fully raw GL path only** — `glGenTextures` + `glBindTexture` + `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` / `_DXT5_EXT`. **Never** use `addTexture(ECF_A8R8G8B8)` + `glCompressedTexImage2D` (linear internal format already committed — produces `GL_INVALID_OPERATION`) or `addTexture` + `COpenGLTexture::getOpenGLTextureName()` (same root cause). `ITexture::lock()` returns **null** for DXT compressed formats (no lockable CPU buffer) — always null-check before dereference. `TextureCache` tracks sRGB textures as raw `GLuint` in `m_srgbTextures` (separate from the `ITexture*` linear pool).
- **GL_MAX_TEXTURE_SIZE**: query immediately after `createDevice()` in `RenderSystem`; stored as `m_maxTextureSize`; never at static init
- **Shader callbacks**: Irrlicht **DOES** call `grab()` on `IShaderConstantSetCallBack` (Tutorial 10 pattern). Use **raw heap + `->drop()` after passing** to `addHighLevelShaderMaterialFromFiles`. Do NOT use `std::unique_ptr` or `std::vector<unique_ptr<>>` — `unique_ptr` calls `delete` directly, causing double-free when Irrlicht also drops the callback. No `m_shaderCallbacks` member needed in `RenderSystem`.
- **Blender export axis**: use **"-Z Forward, Y Up"** (not "Y Forward, Z Up") — wrong setting produces Z-up assets in Irrlicht
- **Asset formats**: `.b3d` mandatory for all building assets (UV2/lightmap support); `.obj` only for simple props with no lightmap
- **Billboard imposters**: 8-direction bakes at **45° below horizontal (camera pitch = −45°)** (midpoint of [−70°, −20°] camera pitch range); **1024×128 DDS DXT5 atlas** (8 × 128×128 frames in 1×8 horizontal strip); flat ambient lighting only; 4-level mip chain mandatory
- Scene node ownership via `SceneEntityManager`; destroy() nulls the pointer before remove()
- All AL calls → `alCheckError()`; all ALC calls → `alcCheckError(device, op)`
- **Audio thread**: must call `alcSetThreadContext(m_context)` at startup before any AL calls. **`alcSetThreadContext` requires `ALC_EXT_thread_local_context` extension** — check extension presence and load via `alcGetProcAddress` at `AudioSystem` construction; never call directly (null dereference if absent)
- **Audio shutdown**: streaming sources — call `alSourceStop` then query `AL_BUFFERS_QUEUED` and `alSourceUnqueueBuffers` (never hardcode buffer count) before `alDeleteBuffers`; SFX pool sources — call `alSourceStop` then `alSourcei(src, AL_BUFFER, 0)` to detach static buffer before `alDeleteBuffers`; all must be done after `m_audioThread.join()` and before `alcDestroyContext`
- Streaming music + ambient beds run on dedicated audio thread; both use non-evictable stream partition (4 sources total: 2 music + 2 ambient, sources[58..61]); 3 additional reserved SFX slots for stingers (crisis, milestone, game-over); main menu music reuses music stream sources[58..59] (main menu and gameplay are mutually exclusive states)
- **Music crossfade**: queue to next bar boundary (90 BPM); constant-power curve; real-time delta (not simulation delta); minimum hold = 1 crossfade duration. Bar boundary tracked via **software sample counter** `m_samplesQueued` — never `AL_SAMPLE_OFFSET` (unreliable on buffer-queue sources; returns offset within current buffer only, not absolute stream position)
- **Stingers**: WAV PCM; reserved non-evictable SFX sources; music ducks to 0.4 gain on stinger playback (ambient beds NOT ducked — `m_musicDuckGain` applies to music stems only); authored to −18 LUFS / −1 dBTP; drop if same type already in-progress; min 5 s between triggers of same type; `stinger_milestone` fires for City Rating transitions only at overlapping thresholds — population milestone toast still shown but no second stinger fires
- **Vehicle engine SFX**: OGG Vorbis 5–20 s (min 6 s), pre-loaded, mono positional; WAV 1–2 s is prohibited (audibly mechanical repeat); max 12 simultaneous engine source pairs (24 pool slots ÷ 2 per vehicle); cull at > 150 m; idle + move sources must be acquired and released as an atomic pair — partial acquisition prohibited. **Why 6 s minimum**: at the lowest pitch-shift ratio (0.75 for stopped vehicles), a 4–5 s loop produces a ~3–3.75 s perceived loop — audibly mechanical. At 6 s minimum the lowest-pitch loop is ~4.5 s perceived, below the perceptibility threshold. See v1-audio-asset-manifest.md for full rationale.
- **Simulation RNG**: inject `ISimulationRNG*` at `CitySimulation` construction; never use `std::rand()` or global RNG in simulation logic — tests use `ManualRNG` for deterministic service-degradation scenarios
- **IClock**: inject `IClock*` at `AudioSystem` and `CitySimulation` construction for deterministic timing in tests (crossfade timing and forced-loan 120 s gate); production uses `WallClock` (`std::chrono::steady_clock`); tests use `ManualClock`
- **IUIBackend methods**: interface requires `setElementAlpha`, `isElementVisible`, `setElementImage`, `setElementEnabled`, `isElementEnabled` in addition to base methods — `setElementEnabled`/`isElementEnabled` distinguish disabled (grayed-out, non-interactive) from hidden (`setElementVisible`); required for ModalDialog speed-selector and undo-button disable tests; without these, grayed-out-vs-hidden semantics cannot be tested headlessly. `NotificationManager::dismissCriticalToast(UIElementHandle)` is the production API for player-dismissal of CRITICAL toasts (not a test backdoor)
- **CI build-linux**: uses `-DENABLE_COVERAGE=OFF`; only `coverage-linux` uses `-DENABLE_COVERAGE=ON`
- **CI PowerShell**: use `if (-not (Test-Path ...)) { exit 1 }` — `Test-Path ... || exit 1` is PS 7+ only; GitHub Actions Windows runners use PS 5.1
- **FetchContent cache**: set `FETCHCONTENT_BASE_DIR` to `.fetchcontent_cache` (outside build tree); cache that path, not `build/_deps`
- **CI step order**: compiler-version detect step must write to `$GITHUB_ENV` in a **separate step before** the `actions/cache` step — `$GITHUB_ENV` writes are not visible within the same step
- Tests use Google Test + GMock (pinned v1.14.0) + RapidCheck (SHA-pinned); simulation injected via IRenderer/IAudioSystem; UIManager via IUIBackend (opaque UIElementHandle — no raw Irrlicht pointers in interface)
- Mock policy: StrictMock for unit tests, NiceMock for property/integration tests; add TearDown() to explicitly reset sim_ and document destructor-path contract
- Coverage gate (lcov 80%) Linux only; use `"${{ github.workspace }}/.fetchcontent_cache/*"` in CI YAML (or `"*/.fetchcontent_cache/*"` in local scripts) to exclude googletest/RapidCheck sources; do NOT include `${BUILD_DIR}/_deps/*` — with `FETCHCONTENT_BASE_DIR=.fetchcontent_cache` that path never exists and newer lcov (2.x) treats unused patterns as errors (exit 25); also pass `--ignore-errors mismatch` to `lcov --capture` for GCC 13 compatibility; building atlas mip chain clamped at 4 levels
- CI: permissions block needed (`checks: write`); `coverage-linux` is self-contained job (builds+tests+lcov with ENABLE_COVERAGE=ON); use `GTEST_OUTPUT=xml:test_results/` (directory form); `all-checks-pass` gate MUST have `if: always()` + strict branch protection; vcpkg baseline enforcement step required; cache FetchContent `_deps/` in CI; DLL verification before upload; `actions/upload-artifact` steps must be explicit
- Linux CI: run unit tests with `ctest -LE "integration|requires-opengl"`; integration tests (no display) with `ctest -L "^integration$"`; OpenGL tests under `xvfb-run` with `ctest -L "^requires-opengl$"`
- Windows CI: use `vswhere.exe` for MSVC version in cache key; include `AITOWN_HEADLESS=1` and `ALSOFT_DRIVERS=null` in test step `env:`
