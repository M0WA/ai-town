## Phase 5: Procedural Terrain

### Goal

Deliver the fully functional procedural terrain system: chunked `IMeshBuffer` generation, `TerrainSystem` with LOD rebuild deque, `SceneEntityManager`, `TextureCache` full 3-pool implementation with sRGB and splat map upload paths, terrain GLSL shaders, terrain textures, and the full `validate_assets.py` 13-check implementation — plus raising the coverage gate to 80%.

### Deliverables

#### Terrain Generation

- [ ] `TerrainChunk` class: accepts float heightmap buffer, `gridSize`, and `cellSize`; builds an `SMesh` with `IMeshBuffer` grids; correct bounding-box recalculation sequence (`recalculateBoundingBox()` on each `SMeshBuffer`, then on the `SMesh`, BEFORE `addMeshSceneNode()`); `smesh->drop()` after `addMeshSceneNode()` (safe because `addMeshSceneNode()` calls `grab()` internally). **NEVER use `ITerrainSceneNode`** — terrain is chunked `IMeshBuffer` only. (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] LOD grid sizes: LOD0 = 32×32, LOD1 = 16×16, LOD2 = 8×8 per chunk. Each `ChunkRebuildRequest` struct stores `uint64_t chunkId` (not raw pointer) and `targetLOD`. (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `TerrainSystem` class:
  - `update(float dt)` processes a `std::deque<ChunkRebuildRequest>` (distance-weighted, nearest-first priority); pops **at most 2 entries per call** to amortize GPU upload cost
  - Per-frame deduplication: `std::unordered_set<uint64_t> processedThisFrame` in `update()`; skip if chunk ID already processed this frame; also skip if `currentLOD == req.targetLOD`
  - `flushPendingRebuilds()`: processes the entire deque until empty or 100 ms CPU budget exhausted (whichever comes first); bypasses the 2-per-frame cap; called once during the loading screen; loading spinner must animate during this flush
  - Chunk load/unload based on camera distance; `m_activeChunks` map keyed by `uint64_t` chunk ID
  - LOD rebuild calls `SceneEntityManager::destroy()` on the old node (via chunk ID lookup) BEFORE creating the new node — prevents orphaned node accumulation
  (ref: `architecture/graphics-architecture/procedural-terrain.md`)
- [ ] `SceneEntityManager` class: authoritative entity list; sole caller of `addXxxSceneNode()` and `node->remove()`; `destroy(entity)` sequence:
  - Step 1: iterate all material slots on the scene node, call `textureCache->releaseLinear(tex)` for each `ITexture*`, clear the slot via `mat.setTexture(t, nullptr)` — `getMaterial()` called ONCE per loop iteration, result cached as `SMaterial&`
  - Step 1b: for each filename in `entity->getSRGBTextureFilenames()`, call `textureCache->releaseSRGB(filename)`
  - Step 1c: for each filename in `entity->getSplatMapFilenames()`, call `textureCache->releaseSplatMap(filename)`
  - Step 2: `driver->setMaterial(SMaterial{})` — flushes driver's last-bound state
  - Step 3: `textureCache->evictUnreferenced()` — covers all three pools
  - Step 4: set entity's node pointer to `nullptr` BEFORE `node->remove()`; do NOT access node pointer after this line
  (ref: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/texture-cache.md`)

#### TextureCache Full Implementation

- [ ] `TextureCache` full 3-pool implementation in `src/rendering/texture_cache.h` and `texture_cache.cpp`:
  - **Linear pool** (`m_linearTextures`): `std::unordered_map<std::string, CacheEntry>` where `CacheEntry = { ITexture*, ref_count, last_access_timestamp, estimated_vram_bytes }`; loaded via `IVideoDriver::getTexture()`; `releaseLinear(ITexture*)` decrements ref_count (does NOT immediately delete); `releaseLinear(const std::string& key)` by string key; `evictUnreferenced()` calls `IVideoDriver::removeTexture()` for zero-ref linear entries
  - **sRGB pool** (`m_srgbTextures`): `std::unordered_map<std::string, SRGBEntry>` where `SRGBEntry = { GLuint texId, ref_count, last_access_timestamp, estimated_vram_bytes }`; loaded via fully raw-GL path only (`glGenTextures` + `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` or `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`); `releaseSRGB(filename)` decrements ref_count; `evictUnreferenced()` calls `glDeleteTextures(1, &texId)` for zero-ref sRGB entries — NO pre-delete `glBindTexture(0)` (OpenGL 3.0+ auto-unbind guarantee); `getSRGBGLuint(path) const` accessor — canonical name `getSRGBGLuint` (NOT `getGLuint`)
  - **Splat map pool** (`m_splatMaps`): `std::unordered_map<std::string, SplatEntry>` where `SplatEntry = { GLuint texId, ref_count, last_access_timestamp, estimated_vram_bytes }`; canonical member name `m_splatMaps` (NOT `m_splatMapTextures`); loaded via `glTexImage2D(GL_RGBA8)` (NEVER via `glCompressedTexImage2D` — DXT compression destroys smooth blend weight gradients); `releaseSplatMap(filename)` decrements ref_count; `evictUnreferenced()` evicts zero-ref splat map entries in the SAME call as sRGB eviction; `getSplatMapGLuint(path) const` accessor
  - **EDT_NULL guard**: `evictUnreferenced()` MUST check `m_driverType == EDT_NULL` at top and return early — no raw GL deletion under EDT_NULL
  - **VRAM estimation**: DXT1/BC1 = `ceil(w/4) × ceil(h/4) × 8 × 1.33`; DXT5/BC3 = `ceil(w/4) × ceil(h/4) × 16 × 1.33`; RGBA8 splat = `w × h × 4` (NO ×1.33 — single mip)
  - **Eviction policy**: LRU zero-reference entries evicted first; error-log and skip if no zero-reference entries available
  - **`evictUnreferenced()` must NOT be called from within `OnSetConstants()`** — eviction during draw call is UB; call only from game logic update phase (strictly between `beginScene()`/`endScene()` but NOT inside `drawAll()`)
  (ref: `architecture/graphics-architecture/texture-cache.md`)

- [ ] **sRGB upload dispatch — `_d` suffix exception (2D-1)**: `loadSRGB()` routes `_d`-suffixed textures to `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` by default, EXCEPT `vehicles_sprite_atlas_d.dds` → LINEAR pool via `loadLinear()`. Vehicle sprites encode RGBA8 color data, not sRGB diffuse. This exception must be documented as a dispatch comment in `TextureCache::loadSRGB()`. (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] **Billboard atlas wrap mode**: filename suffix `_billboard` must set `GL_TEXTURE_WRAP_S/T = GL_CLAMP_TO_EDGE` during upload — default `GL_REPEAT` causes ghost-frame artifacts at the 1×8 strip boundary. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] **sRGB texture entity lifetime tracking**: each entity class loading sRGB textures maintains `std::vector<std::string> m_srgbTextureFilenames`; push filename after each `textureCache->loadSRGB()` call; Step 1b in `SceneEntityManager::destroy()` releases all tracked filenames. LOD swap does NOT call `releaseSRGB()` at swap time — only at entity destruction. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] **`GL_TEXTURE_MAX_LEVEL` dispatch** (set via `glTexParameteri` immediately after `glGenTextures`/`glBindTexture` in the raw-GL path):
  - Diffuse atlas (`_d`): `GL_TEXTURE_MAX_LEVEL = 3` (4-level mip chain mandatory)
  - Billboard imposter atlas (`_billboard`): `GL_TEXTURE_MAX_LEVEL = 3`
  - Splat map (`_splat` category): `GL_TEXTURE_MAX_LEVEL = 0` (single mip only)
  - Normal map (`_n`) and specular (`_s`): loaded via `loadLinear()` — `GL_TEXTURE_MAX_LEVEL` cannot be set via the `IVideoDriver` path; use driver default
  (ref: `architecture/graphics-architecture/texture-cache.md`)

#### GLSL Shaders (Terrain)

- [ ] Terrain GLSL shaders: sRGB terrain shader (vertex + fragment) and splat-map terrain shader (vertex + fragment) — delivered in `assets/shaders/`; loaded via `getGPUProgrammingServices()->addHighLevelShaderMaterialFromFiles()` with raw heap `new` callback + `->drop()` after passing to Irrlicht; shader error fallback to `EMT_SOLID` magenta in release. **`GL_ACTIVE_TEXTURE` save/restore in `OnSetConstants()`** for all multi-texture shaders. (ref: `architecture/graphics-architecture/shader-loading.md`)
- [ ] **Gamma fallback**: if `GL_EXT_texture_sRGB` absent (`isSRGBTextureSupported()` returns false), terrain fragment shader applies `pow(color.rgb, vec3(2.2))` gamma correction on the diffuse sample. This is the same fallback as the billboard imposter shader; both shaders must check the same `RenderSystem::isSRGBTextureSupported()` flag. (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] **sRGB texture binding to shader units**: sRGB diffuse textures are raw `GLuint` (not `ITexture*`) and cannot be set via `SMaterial::setTexture()`; before each draw call, bind via `glActiveTexture(GL_TEXTURE0 + kTexUnitDiffuse); glBindTexture(GL_TEXTURE_2D, texId);`; unbind after draw call; shader sampler uniform assigned via `glUniform1i(diffuseLocation, kTexUnitDiffuse)` (ref: `architecture/graphics-architecture/texture-cache.md`)

#### Terrain Textures

- [ ] Terrain diffuse textures (`graphics-artist-2d-texture`): at least 2 biome variants; 2048×2048 DDS DXT1 sRGB (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`); 4-level mip chain; anisotropy ≥ 8× (terrain requirement per `architecture/asset-standards/2d-texture-standards.md`)
- [ ] Terrain normal maps (`graphics-artist-2d-texture`): DXT5nm packed (X→alpha, Y→green, Z discarded; Y-flip before swizzle for OpenGL convention); uploaded via `loadLinear()`; anisotropy ≥ 4×
- [ ] Splat map PNG (`graphics-artist-2d-texture`): RGBA8 PNG (blend weights, NOT a DDS/DXT compressed file); uploaded via `loadSplatMap()` → `glTexImage2D(GL_RGBA8)`; `GL_TEXTURE_MAX_LEVEL = 0`; anisotropy disabled for splat maps
- [ ] **Atlas layout sign-off** (`graphics-artist-2d-texture`, `graphics-dev-irrlicht`): joint approval of `architecture/asset-standards/building-atlas-layout.md` confirming building variants within the same zone-tier share wall module atlas cells; 16-cell atlas is sufficient if this sharing is enforced; sign-off must explicitly confirm the sharing rule. Required before any Phase 9 building mesh UV channel 0 authoring proceeds. (ref: `architecture/asset-standards/building-atlas-layout.md`)

#### LOD Smoke Test Promotion

- [ ] **`lod_swap_smoke_test.cpp` promoted from Phase 2 stub to real test** (`graphics-dev-irrlicht`): fill in the `SetMeshGrabDropContract` test body (which has `GTEST_SKIP()` in Phase 2). After reading `source/Irrlicht/CMeshSceneNode.cpp` and `source/Irrlicht/SMesh.h` to confirm `setMesh()` calls `grab()/drop()` and `addMeshBuffer()` grab behavior, update the test body to verify the LOD swap sequence without crash or ASAN fault. Record the spike result as a one-line comment in the test file. This test is labelled `requires-opengl` and run under `xvfb-run` in CI. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

#### Terrain Tests

- [ ] `TerrainChunk` unit tests in `tests/terrain/terrain_chunk_test.cpp` (`test-dev-cpp`): `TerrainChunk_BuildsMesh_WithCorrectVertexCount` (32×32 grid → (33×33) vertices); `TerrainChunk_BoundingBox_NotDegenerate` (verify extent > 0 in all axes after `recalculateBoundingBox()`); property-based test `TerrainChunk_ArbitraryGrid_NeverDegenerateBoundingBox` using `rc::gen::inRange(4, 64)` for gridSize
- [ ] `TerrainSystem` rebuild deque tests in `tests/terrain/terrain_system_test.cpp` (`test-dev-cpp`): `TerrainSystem_RebuildDeque_ProcessesAtMostTwoPerFrame`; `TerrainSystem_RebuildDeque_DeduplicatesWithinFrame`; `TerrainSystem_RebuildDeque_SkipsIfAlreadyAtTargetLOD`; `TerrainSystem_FlushPendingRebuilds_EDT_NULL_DoesNotCrash` (label `integration`; runs in CI without GPU)
- [ ] `TextureCache` tests in `tests/terrain/texture_cache_test.cpp` (`test-dev-cpp`): `TextureCache_EvictUnreferenced_EDT_NULL_DoesNotCallGL` (verify no GL calls under EDT_NULL driver); `TextureCache_ReleaseSRGB_DecrementsRefCount`; `TextureCache_EvictUnreferenced_ZeroRefSRGB_DeletesGLTexture`; `TextureCache_SplatMap_ReleaseThenEvict_NoLeak`
- [ ] `terrain_tests` CMake target: promoted from Phase 3 INTERFACE skeleton to a real STATIC or OBJECT library `aitown_terrain`; add all terrain test source files; maintain `DISCOVERY_TIMEOUT 60` per `aitown_add_tests(terrain_tests LABEL "unit" TIMEOUT 300 DISCOVERY_TIMEOUT 60)` (ref: `architecture/testing/framework.md`)

#### Validate Assets — Full 13-Check Implementation

- [ ] `tools/validate_assets.py` full 13-check implementation (building on Phase 4 stub; body stubs filled in):
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
  - Check #11: Small-building height check — `height_floors >= 4` requires `_lod2.b3d` geometry shell (NOT `height_floors > 3`; the spec uses `>=4` — corrected from earlier stub wording, 3D-1)
  - Check #12: Vehicle normal atlas UV (8×8 grid of 256×256 px cells)
  - Check #13: Facade atlas cell pixels — all non-transparent pixel content within [8, 504] texel range on both U and V axes per cell; DDS fourCC determines transparency interpretation (DXT1: 1-bit alpha; DXT5/BC3: alpha > 0)
  (ref: `architecture/asset-standards/3d-model-standards.md`, `architecture/asset-standards/2d-texture-standards.md`)
- [ ] Check #14 (sidecar file) documented in `tools/validate_assets.py` as a comment stub — body implemented in Phase 9 when music stem CI gate is wired. Check #15 (road LOD2 color) also commented stub for Phase 9. (ref: `architecture/audio-architecture/audio-asset-formats.md`)

#### Coverage Gate Raise

- [ ] **`--fail-under-percent` raised from 0 to 80 (BLOCKING)**: update the `coverage-linux` CI job to add a real 80% gate via the `awk` pipeline from `architecture/testing/coverage.md` (or upgrade to `lcov 2.1+` if available). The gate covers `src/simulation/`, `src/terrain/`, `src/ui/`; `src/rendering/`, `src/audio/`, `src/platform/` remain excluded. This gate is BLOCKING — no subsequent phase is considered clean until this gate is green. (ref: `architecture/testing/coverage.md`, `architecture/ci-cd/github-actions-workflow.md`)

### Exit Criteria

- Terrain generates and renders chunked `IMeshBuffer` LOD0/1/2 correctly in Irrlicht; no `ITerrainSceneNode` used
- `TerrainSystem` deque processes ≤ 2 rebuilds per frame; `flushPendingRebuilds()` empties deque within 100 ms budget
- `TextureCache` all three pools operational: linear, sRGB (raw-GL), splat map (raw-GL); `evictUnreferenced()` passes EDT_NULL guard test
- Terrain GLSL shaders compile and render without GL errors; gamma fallback engaged when `isSRGBTextureSupported()` is false
- `lod_swap_smoke_test.cpp` `SetMeshGrabDropContract` test body filled in with spike result recorded; test passes under `xvfb-run`
- `validate_assets.py` all 13 checks implemented; runs cleanly in `validate-assets` CI job
- Atlas layout sign-off document updated in `building-atlas-layout.md` with explicit building variant sharing confirmation
- `coverage-linux` gate green at ≥80% on `src/simulation/`, `src/terrain/`, `src/ui/`

### Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `TerrainChunk`, `TerrainSystem`, `SceneEntityManager`, `TextureCache` full implementation, terrain GLSL shaders, sRGB upload path, `lod_swap_smoke_test.cpp` promotion |
| `graphics-artist-2d-texture` | Terrain diffuse textures (DXT1 sRGB 2048×2048), terrain normal maps (DXT5nm), splat map PNG, atlas layout sign-off |
| `graphics-artist-3d-model` | Atlas layout co-sign |
| `test-dev-cpp` | `TerrainChunk` tests, `TerrainSystem` rebuild deque tests, `TextureCache` tests, `terrain_tests` CMake target promotion |
| `cicd-dev-github` | `--fail-under-percent 80` gate implementation in `coverage-linux` CI job |

### Dependencies

- Requires Phase 4 complete (GLEW CI, validate_assets.py 4-item atomicity, shader_constants.h correctness, artist sign-off gates)
- Requires Phase 3 complete (`TextureCache` skeleton stub, `IRenderer`, `aitown_terrain` INTERFACE CMake target)
- Requires Phase 2 complete (`RenderSystem`, GL capability queries, GLEW initialization)
- **Phase 2 LOD spike result**: `lod_swap_smoke_test.cpp` spike (GTEST_SKIP body in Phase 2) must have confirmed or denied the `setMesh()` `grab()/drop()` contract before terrain LOD rebuild code commits `node->setMesh()` calls. If spike revealed `grab()` is NOT called, scene-graph-ownership.md must be updated and the Phase 5 LOD swap sequence adjusted accordingly. (ref: `architecture/graphics-architecture/scene-graph-ownership.md`)

### Risks & Spikes

- **RISK**: `SMesh::addMeshBuffer()` may or may not call `grab()` on the buffer — double-free or leak depending on convention. **Spike**: inspect `source/Irrlicht/SMesh.h` at implementation time; record result in `lod_swap_smoke_test.cpp` as a one-line comment; update `scene-graph-ownership.md` accordingly.
- **RISK**: Coverage gate at 80% may not be achievable if `TerrainSystem` has high stub-to-impl ratio. **Spike**: run `coverage-linux` locally after terrain implementation and before gate raise to verify ≥80% is reachable.
- **RISK**: Splat map PNG upload via `glTexImage2D(GL_RGBA8)` may produce incorrect blend weights if the PNG loader applies premultiplied alpha. **Spike**: verify PNG decode produces straight alpha (0–255 values unmodified) before upload; document the loader behavior in `TextureCache::loadSplatMap()` comments.
