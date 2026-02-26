## Phase 5: Procedural Terrain

### Goal

Deliver the fully functional procedural terrain system: chunked `IMeshBuffer` generation, `TerrainSystem` with LOD rebuild deque, `SceneEntityManager`, `TextureCache` full 3-pool implementation with sRGB and splat map upload paths, terrain GLSL shaders, terrain textures, and the full `validate_assets.py` 19-check implementation (checks #1–#19; Check #20 is Phase 9 scope) — plus raising the coverage gate to 80%.

### Deliverables

#### Terrain Generation

- [x] `TerrainChunk` class: accepts float heightmap buffer, `gridSize`, and `cellSize`; builds an `SMesh` with `IMeshBuffer` grids; correct bounding-box recalculation sequence (`recalculateBoundingBox()` on each `SMeshBuffer`, then on the `SMesh`, BEFORE `addMeshSceneNode()`); `smesh->drop()` after `addMeshSceneNode()` (safe because `addMeshSceneNode()` calls `grab()` internally). **NEVER use `ITerrainSceneNode`** — terrain is chunked `IMeshBuffer` only. (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [x] `TerrainChunk` heightmap query API (`graphics-dev-irrlicht`): expose `float getHeightAt(int tileX, int tileZ) const` and `float getSlopeDegrees(int tileX, int tileZ) const` using the stored heightmap buffer. Required by Phase 6 buildability check and earthworks cost formula. Tests: `TerrainChunk_GetHeightAt_ReturnsCorrectValue` and `TerrainChunk_GetSlopeDegrees_FlatTerrain_ReturnsZero`.
- [x] LOD grid sizes: LOD0 = 32×32, LOD1 = 16×16, LOD2 = 8×8 per chunk. Each `ChunkRebuildRequest` struct stores `uint64_t chunkId` (not raw pointer) and `targetLOD`. (ref: `architecture/graphics-architecture/procedural-terrain.md`)

  **LOD hysteresis exact distances** (source: `architecture/asset-standards/3d-model-standards.md` terrain LOD threshold table): LOD0→LOD1: switch-out >100 m, switch-in <92 m (8 m hysteresis band); LOD1→LOD2: switch-out >300 m, switch-in <285 m (15 m hysteresis band). Test: `TerrainSystem_LOD_HysteresisGap_At_Least8mClose_At_Least15mFar`.
- [x] `TerrainSystem` class — constructor signature: `TerrainSystem(IRenderer* renderer, IClock* clock)` — `IClock*` is injected for the `flushPendingRebuilds()` 100 ms wall-clock budget. Production code passes `WallClock`; tests pass `ManualClock` to drive deterministic budget exhaustion without real-time delays. This follows the same injection pattern as `CitySimulation` and `AudioSystem` per CLAUDE.md. The `TerrainSystem_FlushPendingRebuilds_BudgetExhausted_StopsAfterBudget` test uses `ManualClock` to advance time 101 ms mid-flush and verifies that not all pending rebuilds were processed.
  - `update(float dt)` processes a `std::deque<ChunkRebuildRequest>` (distance-weighted, nearest-first priority); pops **at most 2 entries per call** to amortize GPU upload cost
  - Per-frame deduplication: `std::unordered_set<uint64_t> processedThisFrame` in `update()`; skip if chunk ID already processed this frame; also skip if `currentLOD == req.targetLOD`
  - `flushPendingRebuilds()`: processes the entire deque until empty or 100 ms CPU budget exhausted (whichever comes first); bypasses the 2-per-frame cap; called once at map load time. Phase 5 does NOT implement a loading screen or spinner — that is Phase 11 scope (see `implementation/phase-11.md`, "loading screen `flushPendingRebuilds()` integration" deliverable). In Phase 5, `flushPendingRebuilds()` is called synchronously from EDT_NULL integration test context. A `TerrainSystem_FlushPendingRebuilds_EDT_NULL_DoesNotCrash` test verifies the flush completes without crash at `m_driverType == EDT_NULL`. Loading screen animation integration is tracked as a Phase 11 exit criterion.
  - Chunk load/unload based on camera distance; `m_activeChunks` map keyed by `uint64_t` chunk ID
  - LOD rebuild calls `SceneEntityManager::destroy()` on the old node (via chunk ID lookup) BEFORE creating the new node — prevents orphaned node accumulation
  (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [x] `ITerrainLoadProgress` stub interface (`graphics-dev-irrlicht`): declare `struct ITerrainLoadProgress { virtual void onChunkRebuilt(int done, int total) {} virtual ~ITerrainLoadProgress() = default; }` in `src/terrain/terrain_types.h`; `TerrainSystem::flushPendingRebuilds(ITerrainLoadProgress* cb = nullptr)` calls `cb->onChunkRebuilt()` if non-null. No-op in EDT_NULL test context. Phase 11 wires the loading screen spinner to this callback.
- [x] Map playability guarantee (`graphics-dev-irrlicht`): `TerrainSystem::generate()` verifies (1) flat tile percentage ≥ 20% of total tiles (slope < 15°) and (2) at least one contiguous flat region ≥ 50×50 tiles exists; re-seeds up to 10 attempts if either constraint fails (per `architecture/game-design/terrain-interaction.md`). Tests: `TerrainSystem_FlatTilePercentage_MeetsMinimum` and `TerrainSystem_ContiguousFlatRegion_MeetsMinimum`.
- [x] `SceneEntityManager` class: authoritative entity list; sole caller of `addXxxSceneNode()` and `node->remove()`; `destroy(entity)` sequence:
  - Step 1: iterate all material slots on the scene node, call `textureCache->releaseLinear(tex)` for each `ITexture*`, clear the slot via `mat.setTexture(t, nullptr)` — `getMaterial()` called ONCE per loop iteration, result cached as `SMaterial&`

    > **MANDATORY IMPLEMENTATION CONSTRAINT**: `getMaterial(m)` MUST be called **exactly once per outer `m` loop iteration** and cached as `SMaterial& mat = node->getMaterial(m)`. The inner `t` loop then calls `mat.setTexture(t, nullptr)` and `mat.TextureLayer[t].Texture = nullptr` WITHOUT re-calling `getMaterial()`. Calling `getMaterial(m)` inside the inner `t` loop introduces a temporary-object hazard per `architecture/graphics-architecture/texture-cache.md` line 139: the return value is a reference to an internal array element; a second call may return a different temporary, silently leaving texture slots populated. The `SceneEntityManager_Destroy_FullSequence_ReleasesAllPools` test must verify that all texture slots are null after destroy() — this is the observable proxy for correct loop structure.

  - Step 1b: for each filename in `entity->getSRGBTextureFilenames()`, call `textureCache->releaseSRGB(filename)`
  - Step 1c: for each filename in `entity->getSplatMapFilenames()`, call `textureCache->releaseSplatMap(filename)`
  - Step 2: `driver->setMaterial(SMaterial{})` — flushes driver's last-bound state
  - Step 3: `textureCache->evictUnreferenced()` — covers all three pools
  - Step 4: set entity's node pointer to `nullptr` BEFORE `node->remove()`; do NOT access node pointer after this line
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/texture-cache.md`)

#### TextureCache Full Implementation

- [x] **Phase 2 stub signature audit** (`graphics-dev-irrlicht`): before implementing any Phase 5 `TextureCache` method body, read `src/rendering/TextureCache.h` and verify the Phase 2 stub method signatures for `loadSRGB()`, `loadSplatMap()`, `releaseSRGB()`, `releaseSplatMap()`, `evictUnreferenced()`, `getSRGBGLuint()`, `getSplatMapGLuint()` exactly match the signatures specified in `architecture/graphics-architecture/texture-cache.md`. If any signature differs (e.g., `releaseSRGB(const std::string&)` vs `releaseSRGB(std::string)`), update the header declaration first. All Phase 3–4 callers must continue to compile without modification after the Phase 5 implementation is added.
- [x] `TextureCache` full 3-pool implementation in `src/rendering/TextureCache.h` and `TextureCache.cpp`:
  - **Linear pool** (`m_linearTextures`): `std::unordered_map<std::string, CacheEntry>` where `CacheEntry = { ITexture*, ref_count, last_access_timestamp, estimated_vram_bytes }`; loaded via `IVideoDriver::getTexture()`; `releaseLinear(ITexture*)` decrements ref_count (does NOT immediately delete); `releaseLinear(const std::string& key)` by string key; `evictUnreferenced()` calls `IVideoDriver::removeTexture()` for zero-ref linear entries
  - **sRGB pool** (`m_srgbTextures`): `std::unordered_map<std::string, SRGBEntry>` where `SRGBEntry = { GLuint texId, ref_count, last_access_timestamp, estimated_vram_bytes }`; loaded via fully raw-GL path only (`glGenTextures` + `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` or `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`); `releaseSRGB(filename)` decrements ref_count; `evictUnreferenced()` calls `glDeleteTextures(1, &texId)` for zero-ref sRGB entries — NO pre-delete `glBindTexture(0)` (OpenGL 3.0+ auto-unbind guarantee); `getSRGBGLuint(path) const` accessor — canonical name `getSRGBGLuint` (NOT `getGLuint`)

    **Anisotropy for terrain detail layer textures** (apply after `glCompressedTexImage2D` in `loadSRGB()`): `glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(m_maxAnisotropy, 8.0f))` (requires `GL_EXT_texture_filter_anisotropic` presence check — skip if unsupported). Splat maps: anisotropy disabled (already specified). Normal maps: anisotropy ≥ 4× via `SMaterial::AnisotropicFilter = 4u` after `driver->getTexture()`.

  - **Splat map pool** (`m_splatMaps`): `std::unordered_map<std::string, SplatEntry>` where `SplatEntry = { GLuint texId, ref_count, last_access_timestamp, estimated_vram_bytes }`; canonical member name `m_splatMaps` (NOT `m_splatMapTextures`); loaded via `glTexImage2D(GL_RGBA8)` (NEVER via `glCompressedTexImage2D` — DXT compression destroys smooth blend weight gradients); `releaseSplatMap(filename)` decrements ref_count; `evictUnreferenced()` evicts zero-ref splat map entries in the SAME call as sRGB eviction; **`getSplatMapGLuint(const std::string& path) const`** — returns raw `GLuint` for terrain shader binding (0 if not loaded); documented in `architecture/graphics-architecture/texture-cache.md`
  - **sRGB tracking atomicity**: every `textureCache->loadSRGB(filename)` call in an entity constructor or `load()` method MUST be immediately followed by `m_srgbFilenames.push_back(filename)` (or equivalent tracking container) in the same scope, with no intervening early-returns or exception throws. Conversely, every `push_back` to the tracking container must correspond to a preceding `loadSRGB()` call in the same code path. Violating this pairing creates unbalanced ref counts that cause `evictUnreferenced()` to either leak VRAM (under-eviction) or double-delete (over-eviction). Consider a helper: `void trackSRGB(TextureCache* tc, std::vector<std::string>& tracked, const std::string& path)` that atomically calls `loadSRGB` and `push_back`.
  - **EDT_NULL guard**: `evictUnreferenced()` MUST check `m_driverType == EDT_NULL` at top and return early — no raw GL deletion under EDT_NULL
  - **VRAM estimation**: DXT1/BC1 = `ceil(w/4) × ceil(h/4) × 8 × 1.33`; DXT5/BC3 = `ceil(w/4) × ceil(h/4) × 16 × 1.33`; RGBA8 splat = `w × h × 4` (NO ×1.33 — single mip)
  - **Eviction policy**: LRU zero-reference entries evicted first; error-log and skip if no zero-reference entries available
  - **`evictUnreferenced()` must NOT be called from within `OnSetConstants()`** — eviction during draw call is UB; call only from game logic update phase (strictly between `beginScene()`/`endScene()` but NOT inside `drawAll()`)
  (ref: `architecture/graphics-architecture/texture-cache.md`)

- [x] **sRGB upload dispatch — `_d` suffix exception (2D-1)**: `loadSRGB()` routes `_d`-suffixed textures to `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` by default, EXCEPT `vehicles_sprite_atlas_d.dds` → LINEAR pool via `loadLinear()`. Vehicle sprites encode RGBA8 color data, not sRGB diffuse. This exception must be documented as a dispatch comment in `TextureCache::loadSRGB()`. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [x] **Billboard atlas wrap mode**: filename suffix `_billboard` must set `GL_TEXTURE_WRAP_S/T = GL_CLAMP_TO_EDGE` during upload — default `GL_REPEAT` causes ghost-frame artifacts at the 1×8 strip boundary. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [x] **sRGB texture entity lifetime tracking**: each entity class loading sRGB textures maintains `std::vector<std::string> m_srgbTextureFilenames`; push filename after each `textureCache->loadSRGB()` call; Step 1b in `SceneEntityManager::destroy()` releases all tracked filenames. LOD swap does NOT call `releaseSRGB()` at swap time — only at entity destruction. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [x] **`GL_TEXTURE_MAX_LEVEL` dispatch** (set via `glTexParameteri` immediately after `glGenTextures`/`glBindTexture` in the raw-GL path):
  - Diffuse atlas (`_d`): `GL_TEXTURE_MAX_LEVEL = 3` (4-level mip chain mandatory)
  - Billboard imposter atlas (`_billboard`): `GL_TEXTURE_MAX_LEVEL = 3`
  - Splat map (`_splat` category): `GL_TEXTURE_MAX_LEVEL = 0` (single mip only)
  - Normal map (`_n`) and specular (`_s`): loaded via `loadLinear()` — `GL_TEXTURE_MAX_LEVEL` cannot be set via the `IVideoDriver` path; use driver default
  (ref: `architecture/graphics-architecture/texture-cache.md`)

#### GLSL Shaders (Terrain)

- [x] Terrain GLSL shaders: sRGB terrain shader (vertex + fragment) and splat-map terrain shader (vertex + fragment) — delivered in `assets/shaders/`; loaded via `getGPUProgrammingServices()->addHighLevelShaderMaterialFromFiles()` with raw heap `new` callback + `->drop()` after passing to Irrlicht; shader error fallback to `EMT_SOLID` magenta in release. **`GL_ACTIVE_TEXTURE` save/restore in `OnSetConstants()`** for all multi-texture shaders. (ref: `architecture/graphics-architecture/shader-loading.md`)

  **GLSL version**: all terrain shader files must begin with `#version 130` — required for `texture()` (not `texture2D()`), `in`/`out` qualifiers, and multiple `sampler2D` uniforms per `architecture/graphics-architecture/shader-loading.md` line 83.

  **sRGB gamma fallback mechanism**: use a **uniform bool `u_srgbLinear`** (NOT two shader variants). In `OnSetConstants()`:

  ```cpp
  bool srgbLinear = !renderer->isSRGBTextureSupported();
  // Must pass int*, not bool* — Irrlicht reads 4 bytes; bool is 1 byte (UB).
  int srgbLinearInt = srgbLinear ? 1 : 0;
  services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1);
  ```

  In the fragment shader: `if (u_srgbLinear) { color.rgb = pow(color.rgb, vec3(2.2)); }`. This avoids the complexity of selecting between shader variants at runtime while correctly gamma-correcting on drivers without `GL_EXT_texture_sRGB`.

- [x] **Gamma fallback**: if `GL_EXT_texture_sRGB` absent (`isSRGBTextureSupported()` returns false), terrain fragment shader applies `pow(color.rgb, vec3(2.2))` gamma correction on the diffuse sample via the `u_srgbLinear` uniform mechanism above. This is the same fallback as the billboard imposter shader; both shaders must check the same `RenderSystem::isSRGBTextureSupported()` flag. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [x] **sRGB texture binding to shader units**: sRGB diffuse textures are raw `GLuint` (not `ITexture*`) and cannot be set via `SMaterial::setTexture()`; binding MUST occur **inside `IShaderConstantSetCallBack::OnSetConstants()`** — NOT before/after the draw call (there is no post-draw callback). Pattern: save `GL_ACTIVE_TEXTURE` via `glGetIntegerv()`, bind via `glActiveTexture(GL_TEXTURE0 + kTexUnitDiffuse); glBindTexture(GL_TEXTURE_2D, texId);`, unbind via `glBindTexture(GL_TEXTURE_2D, 0);` before restoring the saved active unit; shader sampler uniform assigned via `glUniform1i(diffuseLocation, kTexUnitDiffuse)`. (ref: `architecture/graphics-architecture/shader-loading.md`, `architecture/graphics-architecture/texture-cache.md`)

#### Terrain Textures

- [x] Terrain diffuse textures (`graphics-artist-2d-texture`): at least 2 biome variants; 2048×2048 DDS DXT1 sRGB (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`); 4-level mip chain; anisotropy ≥ 8× (terrain requirement per `architecture/asset-standards/2d-texture-standards.md`). **Biome variant definition**: each biome variant is a set of 4 tileable textures (one per splat channel: grass/desert base, asphalt, soil, concrete) sharing the identical splat channel assignment (R=layer0, G=layer1, B=layer2, A=layer3). Different biome variants swap texture content only — the splat map architecture does NOT change per biome. A single splat map PNG per chunk serves all biome variants. **Per-biome splat maps are Phase 9+ scope** and are not required in Phase 5.

  **Splat channel assignment lock** (`graphics-artist-2d-texture` confirmation required before terrain texture production begins): `graphics-artist-2d-texture` must confirm the 4-channel terrain material assignment (R=grass/base, G=asphalt, B=soil, A=concrete) is understood and locked for the entire V1 project. Once texture production begins, splat channel reassignment requires full re-authoring of all splat maps and is prohibited without Product Owner approval. Sign-off must be recorded using the standard block format appended to `architecture/asset-standards/building-atlas-layout.md`:

  ```html
  <!-- SIGN-OFF: graphics-artist-2d-texture [YYYY-MM-DD] — confirmed splat channel lock:
       R=base (biome-specific: grassland=grass, desert=sand), G=asphalt, B=soil, A=concrete;
       content-only swap rule confirmed; per-chunk splat map 16×16 px for 64×64 m chunk confirmed -->
  ```

  This sign-off is a **blocking exit criterion** — `TextureCache::loadSplatMap()` must not be implemented until it is recorded.

  **Splat channel lock sequencing enforcement**: The `graphics-artist-2d-texture` splat channel sign-off (recorded as the standard block in `architecture/asset-standards/building-atlas-layout.md`) MUST appear in the git log BEFORE any commit implementing `TextureCache::loadSplatMap()`. This is a PHASE ENTRY GATE for the `loadSplatMap()` implementation — it must be verified at the PR review stage, not just as an exit criterion.

- [x] Terrain normal maps (`graphics-artist-2d-texture`): DXT5nm packed (X→alpha, Y→green, Z discarded; Y-flip before swizzle for OpenGL convention); uploaded via `loadLinear()`; anisotropy ≥ 4×. **Mip pre-baking required**: terrain normal map mip levels MUST be pre-baked via bicubic downsampling before DXT5nm compression to preserve normal vector normalisation; driver-generated mips are prohibited for normal maps.
- [x] Splat map PNG (`graphics-artist-2d-texture`): RGBA8 PNG (blend weights, NOT a DDS/DXT compressed file); uploaded via `loadSplatMap()` → `glTexImage2D(GL_RGBA8)`; `GL_TEXTURE_MAX_LEVEL = 0`; anisotropy disabled for splat maps

  **Per-chunk splat map pixel dimensions**: 1 texel per terrain tile. For V1: 64 m chunk / 4 m tile = **16×16 px per chunk**. Author and commit one 16×16 RGBA PNG splat map as the terrain test fixture for `TextureCache::loadSplatMap()` unit tests. Full-map splat texture (Phase 9+ scope): 256×256 px for a 256×256-tile map.
- [x] **Atlas layout sign-off** (`graphics-artist-2d-texture`, `graphics-dev-irrlicht`, `graphics-artist-3d-model`): joint approval of `architecture/asset-standards/building-atlas-layout.md` confirming building variants within the same zone-tier share wall module atlas cells; 16-cell atlas is sufficient if this sharing is enforced; sign-off must explicitly confirm the sharing rule. Required before any Phase 9 building mesh UV channel 0 authoring proceeds. (ref: `architecture/asset-standards/building-atlas-layout.md`)

  **`graphics-artist-3d-model` building atlas sign-off gate** (Phase 5 exit criterion): `graphics-artist-3d-model` MUST review `architecture/asset-standards/building-atlas-layout.md` and confirm: (a) the shared atlas cell variant approach (multiple mesh variants referencing one module-type cell) is compatible with modular kit UV authoring workflows; (b) per-module UV islands can be fully authored within the 496×496 px usable area per 512×512 cell without requiring bleed into the 8 px border; (c) the 4×4 cell grid and 16-cell capacity correctly covers the V1 minimum building module set. Sign off before Phase 9 UV authoring begins.

#### C++ Class File Naming (CamelCase)

- [x] **Rename snake_case C++ class files to CamelCase** (`graphics-dev-irrlicht`): all C++ class implementation files (`.cpp`) and their paired class header files (`.h`) created in Phase 5 that used snake_case naming must be renamed to CamelCase per the project code style convention (see `CLAUDE.md § Code Style`). Non-class C-style headers (e.g., `terrain_types.h`, `simulation_constants.h`) are exempt.
  - `src/rendering/texture_cache.h` → `src/rendering/TextureCache.h`
  - `src/rendering/texture_cache.cpp` → `src/rendering/TextureCache.cpp`
  - `src/rendering/scene_entity_manager.h` → `src/rendering/SceneEntityManager.h` (replaces existing alias wrapper)
  - `src/rendering/scene_entity_manager.cpp` → `src/rendering/SceneEntityManager.cpp`
  - Update all `#include` directives project-wide to use the new CamelCase filenames
  - Update `CMakeLists.txt` `target_sources()` calls to reference renamed files
  - **Non-class headers that are exempt** (keep as-is): `terrain_types.h`, `simulation_constants.h`, `shader_constants.h`, and any other header containing only constants/enums/POD structs with no class definition

#### LOD Smoke Test Promotion

- [x] **`lod_swap_smoke_test.cpp` promoted from Phase 2 stub to real test** (`graphics-dev-irrlicht`): fill in the `SetMeshGrabDropContract` test body (which has `GTEST_SKIP()` in Phase 2). The Phase 2 spike already confirmed the LOD swap contract (Checkbox A + B in `architecture/graphics-architecture/scene-graph-ownership.md` lines 53–68): `SMesh::addMeshBuffer()` calls `grab()` and `CMeshSceneNode::setMesh()` calls `drop()` on the old mesh and `grab()` on the new mesh. The spike result is SETTLED. Phase 5 fills in the `SetMeshGrabDropContract` test body to assert this confirmed contract. The test body MUST NOT contain `GTEST_SKIP()`. It must include at least one of: (a) verify the scene node's mesh reference count is correct after setMesh(), (b) verify the old mesh ref count decrements, or (c) ASAN/valgrind clean run confirming no double-free. This test is labelled `requires-opengl` and run under `xvfb-run` in CI. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

#### Terrain Tests

- [x] `TerrainChunk` unit tests in `tests/terrain/terrain_chunk_test.cpp` (`test-dev-cpp`): `TerrainChunk_BuildsMesh_WithCorrectVertexCount` (32×32 grid → (33×33) vertices); `TerrainChunk_BoundingBox_NotDegenerate` (verify extent > 0 in all axes after `recalculateBoundingBox()`); property-based test `TerrainChunk_ArbitraryGrid_NeverDegenerateBoundingBox` using `rc::gen::inRange(4, 64)` for gridSize

  **Quad cells clarification**: LOD0 = 32×32 QUAD CELLS (not vertices); vertex count per LOD = (gridSize+1)²; LOD0 = 33×33 = 1089 vertices, LOD1 = 17×17 = 289 vertices, LOD2 = 9×9 = 81 vertices.
- [x] `TerrainSystem` rebuild deque tests in `tests/terrain/terrain_system_test.cpp` (`test-dev-cpp`): `TerrainSystem_RebuildDeque_ProcessesAtMostTwoPerFrame`; `TerrainSystem_RebuildDeque_DeduplicatesWithinFrame`; `TerrainSystem_RebuildDeque_SkipsIfAlreadyAtTargetLOD`; `TerrainSystem_FlushPendingRebuilds_EDT_NULL_DoesNotCrash` (label `integration`; runs in CI without GPU)

> **Test label routing — MANDATORY**: `terrain_tests` target is labelled `unit` (no display, no GL context). Tests that call Irrlicht or raw-GL APIs MUST be placed in `tests/integration/` under `integration_tests` (label `integration`):
>
> - `SceneEntityManager_Destroy_FullSequence_ReleasesAllPools` — uses EDT_NULL device → `integration_tests`
> - `TextureCache_EvictUnreferenced_ZeroRefSRGB_DeletesGLTexture` — calls `glDeleteTextures` → `integration_tests`
>
> **Decision locked: TerrainSystem holds `IRenderer*` and never calls Irrlicht API directly. All four rebuild-deque tests belong in `terrain_tests` (label `unit`).**
>
> `terrain_tests` (label `unit`) may only contain tests that use no Irrlicht or GL APIs directly (pure math, data structure, algorithm tests with EDT_NULL guard in `TestBody`).

- [x] `TextureCache` tests in `tests/terrain/texture_cache_test.cpp` (`test-dev-cpp`): `TextureCache_EvictUnreferenced_EDT_NULL_DoesNotCallGL` (verify no GL calls under EDT_NULL driver); `TextureCache_ReleaseSRGB_DecrementsRefCount`; `TextureCache_EvictUnreferenced_ZeroRefSRGB_DeletesGLTexture`; `TextureCache_SplatMap_ReleaseThenEvict_NoLeak`
- [x] `SceneEntityManager::destroy()` integration test in `tests/terrain/texture_cache_test.cpp` (`test-dev-cpp`): `SceneEntityManager_Destroy_FullSequence_ReleasesAllPools` (label `integration`; uses EDT_NULL driver; loads a mock entity with one linear texture, one sRGB filename, and one splat map filename; calls `destroy(entity)`; verifies: Step 1 decrements linear ref count to 0, Step 1b decrements sRGB ref count to 0, Step 1c decrements splat map ref count to 0, Step 2 sets driver material to default, Step 3 calls `evictUnreferenced()` without GL errors, Step 4 sets entity node pointer to nullptr before remove). This test confirms the four-step destroy sequence does not skip any pool under EDT_NULL. (ref: `architecture/graphics-architecture/texture-cache.md`, `architecture/graphics-architecture/scene-graph-ownership.md`)
- [x] `terrain_tests` CMake target: promoted from Phase 3 INTERFACE skeleton to a real STATIC or OBJECT library `aitown_terrain`; add all terrain test source files; maintain `DISCOVERY_TIMEOUT 60` per `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)` (ref: `architecture/testing/framework.md`). The promotion involves three concrete CMake steps: (1) change `add_library(aitown_terrain INTERFACE)` to `add_library(aitown_terrain STATIC)` (or OBJECT) in `src/terrain/CMakeLists.txt` and add source files; (2) ensure `target_link_libraries(terrain_tests PRIVATE aitown_terrain)` is present — if not added in Phase 3, add it now; (3) after the build, verify `./build/tests/terrain_tests --gtest_list_tests` discovers all expected test cases (non-zero count) before closing Phase 5. A STATIC target that has no test discovery is a CI failure.

  **Prerequisite — `aitown_add_tests()` DISCOVERY_TIMEOUT extension (CI-8)**: The `aitown_add_tests()` macro in `cmake/AitownTestHelpers.cmake` must be extended to accept an optional `DISCOVERY_TIMEOUT` parameter and forward it to `gtest_discover_tests()`. The current macro silently ignores `DISCOVERY_TIMEOUT` (it is not in `cmake_parse_arguments`). This extension must land in the same commit as the `terrain_tests` CMake changes. See `cicd-dev-github` responsibility column.

  **Step 4 — Update include directories**: After converting `aitown_terrain` from INTERFACE to STATIC, update the `target_include_directories` call from `INTERFACE` to `PUBLIC` to maintain transitive include visibility:

  ```cmake
  target_include_directories(aitown_terrain PUBLIC src/terrain/ src/simulation/)
  ```

  **Step 5 — Circular dependency resolution**: `SceneEntityManager` calls Irrlicht APIs (`node->remove()`, `driver->setMaterial()`, material slot iteration) and **MUST** reside in `aitown_render` (NOT `aitown_terrain`) to avoid a circular `aitown_terrain ↔ aitown_render` CMake dependency. `TerrainSystem` holds a forward-declared `SceneEntityManager*` pointer; only `aitown_render` links Irrlicht directly. `aitown_terrain` links `aitown_render` (one direction only). Verify link order: `aitown_terrain` → `aitown_render` → `Irrlicht`; never `aitown_render` → `aitown_terrain`.

- [x] Move `SceneEntityManager` into `aitown_render` CMake target (`graphics-dev-irrlicht`): add `src/rendering/SceneEntityManager.cpp` to `aitown_render` in `CMakeLists.txt`; verify no `aitown_render → aitown_terrain` dependency is introduced. Exit criterion: `cmake --build build` succeeds with no CMake cycle error; `cmake --graphviz` confirms `aitown_terrain → aitown_render → Irrlicht` direction.

#### Validate Assets — Full 19-Check Implementation (Checks #1–#19)

- [x] `tools/validate_assets.py` checks #1–#19 implemented (checks #1–#13 per 3D model standards; check #14 music JSON sidecar; check #15 as stub with `# TODO Phase 9` comment; checks #16–#19 audio format/duration checks; Check #20 is Phase 9 scope) (building on Phase 4 stub; body stubs filled in). **Phase 5 implements checks #1–#14 and checks #16–#19 per `architecture/asset-standards/3d-model-standards.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`, and this plan. Check #14 (music JSON sidecar presence) IS a Phase 5 deliverable — it must be implemented in `validate_assets.py` in Phase 5.** Check #15 remains a stub in Phase 5; the `.meta` sidecar system is Phase 9 scope.
  - Check #1: `.b3d` format for building `_lod0`, `_lod1` files
  - Check #2: Small building/prop `_lod2.b3d` absent when `height_floors <= 3` (billboard path)
  - Check #3 (large building): `_lod2.b3d` present, within 300–500 tri budget, `_lod2_lm.dds` uses DXT5/BC3 (not DXT1) — read DDS fourCC to determine format
  - Check #4: UV channel 0 coordinates on all LOD levels in [0, 1] (LOD0, LOD1, LOD2 shell)
  - Check #5: UV channel 1 non-degenerate on all building `.b3d` files
  - Check #6: Assembled LOD0 total ≤ 5,000 tris (large) or ≤ 1,500 tris (small)
  - Check #7: Facade detail piece count ≤ 10 per assembled stack
  - Check #8: Pivot/extent tolerance 5 mm
  - Check #9: LOD hysteresis ≥ 5 m (close), ≥ 10 m (far)
  - Check #10: Vehicle atlas UV
  - Check #11: buildings with `height_floors >= 4` MUST have `_lod2.b3d`; buildings with `height_floors <= 3` MUST NOT have `_lod2.b3d` (billboard only). The boundary is inclusive at 4 — not `> 3`. This two-sided check catches both a missing LOD2 shell on tall buildings AND a spurious LOD2 mesh on short buildings that should use billboard-only LOD2. (NOT `height_floors > 3` — `>= 4` is the canonical form per spec, 3D-1)
  - Check #12: Vehicle normal atlas UV (8×8 grid of 256×256 px cells)
  - Check #13: Facade atlas cell pixels — all non-transparent pixel content within [8, 504] texel range on both U and V axes per cell; DDS fourCC determines transparency interpretation (DXT1: 1-bit alpha; DXT5/BC3: alpha > 0)
  (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [x] **Check #14** (`validate_assets.py`): validate all `music_*.ogg` files have co-located JSON sidecars matching `music_sidecar_schema.json`. Pattern `music_*.ogg` covers both main menu variants (`music_main_menu_*.ogg`) and all gameplay stems (`music_calm_*.ogg`, `music_growth_*.ogg`, `music_crisis_*.ogg`). Files matching `ambient_*.ogg` are explicitly excluded (no sidecar required for ambient beds per `architecture/audio-architecture/v1-audio-asset-manifest.md`). A sidecar that fails schema validation (missing `bpm`, missing `beats_per_bar`, or additional properties) is a hard asset error. This check IS implemented in Phase 5 — it is NOT a Phase 9 stub. Check #15 (`.meta` sidecar file presence) remains a commented stub for Phase 9. Road LOD2 color validation is Check #20 — a Phase 9 addition implemented alongside the road tile LOD2 deliverable. (ref: `architecture/audio-architecture/audio-asset-formats.md`, `architecture/audio-architecture/v1-audio-asset-manifest.md`)
- [x] **Check #15 stub** (Phase 9 scope): verify `pass` stub with `# TODO Phase 9: .meta sidecar file presence` comment is present in `tools/validate_assets.py`. This stub was committed in Phase 4; Phase 5 must confirm it is present and NOT accidentally removed. **Phase 5 exit criterion: check_15 stub present.**

Road LOD2 color validation is Check #20 (phase-9.md and 3d-model-standards.md updated).

- [x] **Check #16** (`sound-dev-opensoftal`, `cicd-dev-github`): `music_*.ogg` must be stereo (channels == 2), 44100 Hz sample rate; `ambient_*.ogg` must be stereo, 44100 Hz. Hard error on any mismatch. Graceful no-op if no matching files exist.
- [x] **Check #17** (`sound-dev-opensoftal`, `cicd-dev-github`): `sfx_vehicle_engine_*.ogg` must have duration ≥ 6.0 s, mono (channels == 1), 44100 Hz. Hard error if duration < 6.0 s (audibly mechanical loop). Graceful no-op if no matching files exist.
- [x] **Check #18** (`sound-dev-opensoftal`, `cicd-dev-github`): `sfx_zone_*.ogg` must have duration ≤ `kZoneLoopMaxPreloadDurationSeconds` (18.0 s), mono, 44100 Hz. Hard error if duration > 18 s. Graceful no-op if no matching files exist.
- [x] **Check #19** (`sound-dev-opensoftal`, `cicd-dev-github`): `stinger_*.wav` must be mono WAV PCM (1 channel, uncompressed). Hard error on stereo or compressed WAV. Graceful no-op if no matching files exist.

> **Note**: Audio checks (#16–#19) require the `mutagen` Python library (`pip install mutagen`) for OGG/WAV duration and format inspection. The `validate-assets` CI job must install `mutagen` before running the script.

#### Coverage Gate Raise

- [x] **`--fail-under-percent` raised from 0 to 80 (BLOCKING)**: update the `coverage-linux` CI job. Replace the `lcov --summary` informational step with the **Phase 5 total-line-coverage awk pipeline** from `architecture/testing/coverage.md § Phase 5 — Total Line Coverage 80% Gate` (NOT the Phase 4 worst-file `src/ui/` 25% pipeline). The gate measures aggregate LH/LF across all SF entries in `coverage_filtered.info`. The gate covers `src/simulation/`, `src/terrain/`, `src/ui/`; `src/rendering/`, `src/audio/`, `src/platform/` remain excluded. This gate is BLOCKING — no subsequent phase is considered clean until this gate is green. (ref: `architecture/testing/coverage.md`, `architecture/ci-cd/github-actions-workflow.md`)

### Exit Criteria

- All Phase 5 C++ class files use CamelCase naming (`TextureCache.h/.cpp`, `SceneEntityManager.h/.cpp`); no snake_case `.cpp` files for class implementations remain; all `#include` directives and `CMakeLists.txt` references updated
- Terrain generates and renders chunked `IMeshBuffer` LOD0/1/2 correctly in Irrlicht; no `ITerrainSceneNode` used
- `TerrainSystem` deque processes ≤ 2 rebuilds per frame; `flushPendingRebuilds()` empties deque within 100 ms budget
- `TextureCache` all three pools operational: linear, sRGB (raw-GL), splat map (raw-GL); `evictUnreferenced()` passes EDT_NULL guard test
- Terrain GLSL shaders compile and render without GL errors; gamma fallback engaged when `isSRGBTextureSupported()` is false
- `lod_swap_smoke_test.cpp` `SetMeshGrabDropContract` test body filled in with spike result recorded; test passes under `xvfb-run`
- `validate_assets.py` checks #1–#14 implemented (checks #1–#13 per 3D model standards + Check #14 music JSON sidecar validation); runs cleanly in `validate-assets` CI job
- `tools/validate_assets.py` contains check #15 stub (`pass` with `# TODO Phase 9` comment)
- checks #16–#19 implemented in `tools/validate_assets.py`; script exits 0 with informational no-op message when no matching audio assets are found
- Atlas layout sign-off document updated in `building-atlas-layout.md` with explicit building variant sharing confirmation; `graphics-artist-3d-model` sign-off recorded confirming: (a) shared atlas cell variant approach compatible with modular kit UV workflows; (b) per-module UV islands fit within 496×496 px usable area per 512×512 cell; (c) 4×4 grid and 16-cell capacity covers V1 minimum building module set
- `graphics-artist-2d-texture` splat channel lock sign-off recorded in `architecture/asset-standards/building-atlas-layout.md` using the standard block format before terrain texture production begins; `TextureCache::loadSplatMap()` not implemented until sign-off is recorded
- `coverage-linux` gate green at ≥80% on `src/simulation/`, `src/terrain/`, `src/ui/`

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `TerrainChunk` (incl. `getHeightAt`/`getSlopeDegrees` query API), `TerrainSystem` (incl. map playability guarantee, LOD hysteresis distances, `ITerrainLoadProgress` stub), `SceneEntityManager` (incl. CMake placement in `aitown_render`), `TextureCache` full implementation (incl. anisotropy), terrain GLSL shaders (`int` intermediary for `u_srgbLinear`), sRGB upload path, `lod_swap_smoke_test.cpp` promotion |
| `graphics-artist-2d-texture` | Terrain diffuse textures (DXT1 sRGB 2048×2048), terrain normal maps (DXT5nm), splat map PNG, atlas layout sign-off |
| `graphics-artist-3d-model` | Atlas layout co-sign; building atlas sign-off gate (confirm shared cell variant approach, 496×496 px usable area, 16-cell V1 coverage) before Phase 9 UV authoring begins |
| `test-dev-cpp` | `TerrainChunk` tests, `TerrainSystem` rebuild deque tests, `TextureCache` tests, `terrain_tests` CMake target promotion |
| `sound-dev-opensoftal` | validate_assets.py audio checks #16–#19 implementation (OGG/WAV format and duration validation) |
| `sound-artist-opensoftal` | No Phase 5 audio asset deliverables — all V1 audio assets delivered in Phase 10. Checks #16–#19 are graceful no-ops on empty `assets/audio/` directory. |
| `cicd-dev-github` | `--fail-under-percent 80` gate implementation in `coverage-linux` CI job; `aitown_add_tests()` DISCOVERY_TIMEOUT extension (CI-8); `mutagen` install step in `validate-assets` CI job |

### Dependencies

- Requires Phase 4 complete (GLEW CI, validate_assets.py 4-item atomicity, shader_constants.h correctness, artist sign-off gates)
- Requires Phase 3 complete (`TextureCache` skeleton stub, `IRenderer`, `aitown_terrain` INTERFACE CMake target)
- Requires Phase 2 complete (`RenderSystem`, GL capability queries, GLEW initialization)
- **Phase 2 LOD spike result**: `lod_swap_smoke_test.cpp` spike (GTEST_SKIP body in Phase 2) must have confirmed or denied the `setMesh()` `grab()/drop()` contract before terrain LOD rebuild code commits `node->setMesh()` calls. If spike revealed `grab()` is NOT called, scene-graph-ownership.md must be updated and the Phase 5 LOD swap sequence adjusted accordingly. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

### Risks & Spikes

- **RISK**: `SMesh::addMeshBuffer()` may or may not call `grab()` on the buffer — double-free or leak depending on convention. **Spike**: inspect `source/Irrlicht/SMesh.h` at implementation time; record result in `lod_swap_smoke_test.cpp` as a one-line comment; update `scene-graph-ownership.md` accordingly.
- **RISK**: Coverage gate at 80% may not be achievable if `TerrainSystem` has high stub-to-impl ratio. **Spike**: run `coverage-linux` locally after terrain implementation and before gate raise to verify ≥80% is reachable.
- **RISK**: Splat map PNG upload via `glTexImage2D(GL_RGBA8)` may produce incorrect blend weights if the PNG loader applies premultiplied alpha. **Spike**: verify PNG decode produces straight alpha (0–255 values unmodified) before upload; document the loader behavior in `TextureCache::loadSplatMap()` comments.
