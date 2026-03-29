# Irrlicht / Graphics Architecture Review
## AI Town — Senior C++ Developer (graphics-dev-irrlicht) Pass

Review date: 2026-03-29
Scope: all files under `architecture/graphics-architecture/`, `architecture/asset-standards/`,
and `architecture/testing/testability-architecture.md`.

---

## Summary

The specifications are detailed and broadly correct. Several CRITICAL and HIGH issues
exist that will cause silent bugs, double-frees, resource leaks, or incorrect rendering
if implemented literally from the current text. MEDIUM and LOW issues are specification
gaps or minor inconsistencies that will complicate implementation or maintenance.

---

## Issue Catalogue

### CRITICAL

---

#### CRIT-01 — `kCardinalFalloff` / `kDiagonalFalloff` values not specified in spec
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§setTileHeight Neighbour Blending)
**Type**: [GAP]
**Description**: The spec states these falloff constants "are confirmed by gamedesign-lookandfeel sign-off in Phase 10b (reference starting point: cardinal 0.5, diagonal 0.25)" but simultaneously says "MUST NOT be committed before the sign-off is recorded." No sign-off is actually recorded in this file and no final binding values are given. The C++ constants `kCardinalFalloff` and `kDiagonalFalloff` therefore have no authoritative source. Different implementers may use different values, causing divergent terrain flattening behaviour that affects zone placement and road adjacency detection. The lack of confirmed values is a blocking gap for any phase that calls `setTileHeight()`.
**Proposed resolution**: Record the gamedesign sign-off in the spec with the confirmed decimal values and mark them `static constexpr float` in `src/rendering/render_constants.h`. Update the spec to reference that header as the canonical source.

---

#### CRIT-02 — `SceneEntityManager::destroy()` does not release sRGB textures for entities whose material slots contain only `ITexture*` placeholders
**File**: `architecture/graphics-architecture/texture-cache.md` (§sRGB texture entity lifetime)
**Type**: [GAP]
**Description**: The spec defines a two-tier release protocol: Step 1 iterates material slots and calls `releaseLinear(ITexture*)`, Step 1b iterates `m_srgbTextureFilenames` and calls `releaseSRGB(filename)`. However, the spec also states that "sRGB diffuse textures are raw `GLuint` values (not `ITexture*`) and are NOT present in node material slots." This means that any entity whose material slots were never populated with the sRGB texture (because the binding was done per-draw-call via `glActiveTexture/glBindTexture` in `OnSetConstants()`) will correctly fall through to Step 1b. But the spec does NOT specify how `m_srgbTextureFilenames` gets populated for the entity in the first place. If `BuildingAssetLoader::load()` or `SceneEntityManager::spawnBuilding()` fails to push filenames into `m_srgbTextureFilenames`, Step 1b is a no-op and the sRGB pool ref_count is never decremented — causing unbounded VRAM growth over the game session. There is no spec text telling `BuildingAssetLoader` or `spawnBuilding()` exactly when and how to push filenames.
**Proposed resolution**: Add an explicit contract in `texture-cache.md` (and cross-reference in `scene-graph-ownership.md`) specifying that `BuildingAssetLoader::load()` must call `textureCache->loadSRGB(filename, format)` and push the returned filename to the `BuildingAsset::srgbFilenames` vector, and that `SceneEntityManager::spawnBuilding()` must transfer those filenames to the entity's `m_srgbTextureFilenames` before the entity is considered live.

---

#### CRIT-03 — `CloudDomeShaderCallback` stored as `void*` — type erasure breaks safety
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Shader Callback)
**Type**: [PROBLEM]
**Description**: The spec mandates that `m_cloudShaderCbRaw` is stored as `void*` in the `IrrlichtRenderer` header, with a cast to `CloudDomeShaderCallback*` inside the `.cpp`. This is stated as a deliberate choice to avoid exposing `CloudDomeShaderCallback` in the header. However, `void*` member storage defeats every compile-time type check: if the pointer is accidentally cast to the wrong type (another callback class, an Irrlicht object, etc.) the code compiles without error and causes undefined behaviour at runtime when `setCameraY()` accesses members via the wrong type. Using `void*` for a reference-counted Irrlicht callback stored on an internal class member is non-standard, harder to audit, and provides no benefit when the callback is defined in the `.cpp` anyway via a forward declaration.
**Proposed resolution**: Forward-declare `CloudDomeShaderCallback` in `IrrlichtRenderer.h` and store `CloudDomeShaderCallback* m_cloudShaderCb{nullptr}` directly. The forward declaration avoids pulling the full definition into the header and retains full type safety.

---

#### CRIT-04 — `evictUnreferenced()` may be called from `SceneEntityManager::destroy()` which can be invoked during a frame render pass
**File**: `architecture/graphics-architecture/texture-cache.md` (§CRITICAL constraint)
**Type**: [INCONSISTENCY]
**Description**: `texture-cache.md` states: "`evictUnreferenced()` must NOT be called from within `OnSetConstants()`" and further says it is called "strictly between `beginScene()`/`endScene()` boundaries but NOT within any `drawAll()` call." However, the spec does not define a clear lifecycle gate that prevents `SceneEntityManager::destroy()` from being called during a simulation tick that itself is invoked from inside the main render loop. If the game loop calls `citySimulation->tick()` between `beginScene()` and `drawAll()`, and that tick destroys an entity (calls `SceneEntityManager::destroy()`, which calls `evictUnreferenced()`), the eviction happens before `drawAll()` — correct. But if any caller invokes `destroy()` inside an event callback that fires during `sceneManager->drawAll()` (e.g. a pick-result callback), `glDeleteTextures` would execute mid-draw-call. The spec says nothing about ensuring this cannot happen via event callbacks.
**Proposed resolution**: Add an explicit rule in `texture-cache.md` that `evictUnreferenced()` MUST only be called in the game-logic update phase (before `driver->beginScene()`), never in response to Irrlicht scene callbacks or event handlers that may fire inside `drawAll()`. Cross-reference in `irrlicht-device-lifecycle.md` render loop ordering.

---

#### CRIT-05 — `addMeshSceneNode(static_cast<IMesh*>(lod0))` used for B3D assets: `static_cast` from `IAnimatedMesh*` to `IMesh*` is a base-class upcast and is correct, but the spec phrase "Cast `IAnimatedMesh*` to `IMesh*`" contradicts the SMesh downcast WARNING
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§B3D Building Assets)
**Type**: [INCONSISTENCY]
**Description**: The B3D section correctly describes the `IAnimatedMesh*` → `IMesh*` upcast as "safe — `IAnimatedMesh` publicly inherits `IMesh`". However, the earlier WARNING section states "Never use `static_cast<SMesh*>` on an `IMesh*` pointer" while the B3D section uses a `static_cast` to do the upcast. These two uses of `static_cast` on mesh pointers appear in the same file and may confuse implementers: one is a downcast (UB risk) and one is an upcast (safe), but the file does not clearly distinguish them in the WARNING text. An implementer reading the WARNING may apply it too broadly and avoid the necessary upcast.
**Proposed resolution**: Add a clarifying sentence to the WARNING: "This prohibition applies only to downcasts from a base-class mesh pointer (`IMesh*`) to a derived type (`SMesh*`). Upcasting from `IAnimatedMesh*` to `IMesh*` is always safe because `IAnimatedMesh` publicly inherits `IMesh`."

---

### HIGH

---

#### HIGH-01 — `flushPendingRebuilds()` 100 ms budget not reset between loading-screen frames
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§flushPendingRebuilds)
**Type**: [GAP]
**Description**: The spec defines `flushPendingRebuilds()` as breaking after 100 ms of CPU time measured by `m_clock->nowSeconds()`. It computes `start = m_clock->nowSeconds()` at the beginning of each call. This is correct. However, the spec also says "`TerrainSystem::update(dt)` is also called every loading-screen frame" and describes a cooperative drain. It does not specify whether `flushPendingRebuilds()` and `update()` share any elapsed-time budget or independently consume wall time. If both are called in the same loading-screen loop iteration and `flushPendingRebuilds()` has already consumed 100 ms, calling `update()` immediately afterward in the same frame tick will process two additional rebuilds (the normal 2-per-frame cap) on top of whatever `flushPendingRebuilds()` achieved — causing up to 102 ms of rebuild latency per frame rather than the stated 100 ms cap. On a slow CPU this can stall the loading screen spinner visibly. The spec does not call this out.
**Proposed resolution**: Clarify whether `update()` should be skipped in the same frame if `flushPendingRebuilds()` already exhausted its budget, or whether the 2 extra rebuilds from `update()` are acceptable overhead. A note in the spec either way eliminates implementer ambiguity.

---

#### HIGH-02 — Hover highlight `recalculateBoundingBox()` not specified for the pre-allocated buffer after in-place vertex update
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§Hover Highlight)
**Type**: [GAP]
**Description**: The spec says `setTileHoverHighlight()` "updates vertex positions in the existing buffer … calls `recalculateBoundingBox()` on the buffer, then sets `m_hoverVisible = true`." However, it does not specify that `recalculateBoundingBox()` must also be called on the parent `SMesh*` (`m_hoveredTileMesh`) after the buffer recalculation. The `scene-graph-ownership.md` and `procedural-terrain.md` both require `recalculateBoundingBox()` on the `SMesh` after all buffer recalculations. Since `m_hoveredTileMesh` is drawn via raw `IVideoDriver::drawMeshBuffer()` (not `sceneManager->drawAll()`), Irrlicht's frustum culling does not apply, so the missing mesh-level bounding box does not cause incorrect culling. But it is an inconsistency with the project rule and may silently matter if the draw path changes in a future phase to use a scene node.
**Proposed resolution**: Add an explicit step in `setTileHoverHighlight()` contract: "After buffer `recalculateBoundingBox()`, also call `m_hoveredTileMesh->recalculateBoundingBox()`" with a parenthetical note that culling correctness is not currently affected because the mesh is drawn via raw `drawMeshBuffer()`, but the call must be present for consistency with the project bounding-box policy.

---

#### HIGH-03 — Terrain DDA `pickTerrainTile()` returns the first cell where `rayY <= h`, which is the cell AFTER the crossing — not the cell at the actual intersection
**File**: `architecture/graphics-architecture/procedural-terrain.md` (§pickTerrainTile DDA Algorithm)
**Type**: [PROBLEM]
**Description**: The DDA traversal loop computes `tc` as approximately the ray parameter at the centre of the current cell and tests `if (rayY <= h)`. When the ray descends into terrain at a boundary between two cells, the detection fires at the cell the ray first enters when the Y coordinate drops below the heightmap sample — but the cell's height sample `getHeightAt(cx, cz)` is defined as the height at the grid-centre, not the boundary vertex. If the terrain is sloped, the boundary vertex between cell `(cx, cz)` and the previously traversed cell has a different height than either cell's grid-centre sample. The ray may be detected as "below terrain" one or two cells too late (the ray crossed the boundary vertex but the grid-centre height of the next cell is lower than the boundary vertex). For a 10 m cell size and terrain slopes of up to 26 m / (terrain dimension), this can produce a 1-cell (10 m) positioning error on steeply sloped terrain — a zone placed on the wrong tile.
**Proposed resolution**: Document the 1-cell potential error explicitly as a known limitation of the grid-centre height sampling approach and state the maximum error bound (`kCellSize` metres). Optionally propose a correction: after detecting `rayY <= h`, check whether the previous cell (`cx - stepX`, `cz - stepZ`) would also satisfy the criterion — if not, the current cell is the true hit; if it does, the previous cell is the actual intersection. This refinement is a low-cost one-step backtrack.

---

#### HIGH-04 — Zone overlay `SMesh*` lifecycle on `setZoneOverlay()` call: old mesh `drop()` before or after `addMeshSceneNode()`?
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§Zone Overlay SMeshBuffer Batching)
**Type**: [GAP]
**Description**: The spec says the zone overlay mesh IS attached to the scene graph as a persistent `ISceneNode*` and "rebuilt (remove-old / add-new) on every `setZoneOverlay()` call." However, the spec does not specify the exact sequence for destroying the old scene node and creating the new one. Specifically: (a) which scene node pointer member on `IrrlichtRenderer` tracks the zone overlay node; (b) whether `SceneEntityManager::destroy()` is used or `node->remove()` is called directly; (c) whether the eviction sequence (texture clear → `setMaterial({})` → `evictUnreferenced()` → `remove()`) is required for an untextured overlay mesh. Without this sequence, if the overlay mesh is an `ISceneNode*`, leaving texture slots non-null between calls is not applicable here (no textures), but the destroy sequence is still required documentation for implementers.
**Proposed resolution**: Add a "Zone Overlay Node Rebuild Sequence" subsection to `scene-graph-ownership.md` documenting: (1) if `m_zoneOverlayNode != nullptr`, call `m_zoneOverlayNode->remove()` (no texture eviction needed — untextured overlay); (2) drop the old `SMesh*` via `->drop()`; (3) build new `SMesh*`; (4) `addMeshSceneNode()`; (5) drop the new `SMesh*` after `addMeshSceneNode()` grabs it; (6) store the new node pointer.

---

#### HIGH-05 — `TextureCache::loadSRGB()` — DDS header parser reads `dwMipMapCount` at offset 28 but does not validate that the mip level count is non-zero
**File**: `architecture/graphics-architecture/texture-cache.md` (§Truncated DDS files)
**Type**: [GAP]
**Description**: The spec warns about truncated DDS files where `dwMipMapCount` is declared but data is absent, but does not specify what the parser should do when `dwMipMapCount` is 0 (which is valid per the DDS spec — it means "one level, no mip chain"). If the parser iterates `for (int mip = 0; mip < dwMipMapCount; ++mip)` and `dwMipMapCount` is 0, the loop body never executes and the texture object is allocated but contains no uploaded data — resulting in a black surface with no error. The spec must mandate a minimum of 1 upload iteration.
**Proposed resolution**: Add a rule: "Before the mip upload loop, clamp `numMips = max(1, dwMipMapCount)` — DDS files with `dwMipMapCount == 0` must still upload mip level 0. Log a WARNING when `dwMipMapCount == 0` to flag incorrectly authored assets."

---

#### HIGH-06 — `GL_ACTIVE_TEXTURE` save/restore in `OnSetConstants()` for single-texture callbacks (cloud dome, building) is not specified
**File**: `architecture/graphics-architecture/shader-loading.md` (§sRGB texture binding in shader callbacks)
**Type**: [GAP]
**Description**: The 5-unit terrain splat shader sequence correctly specifies the `GL_ACTIVE_TEXTURE` save/restore. However, the cloud dome callback and the single-unit building/road shader callback patterns described earlier in the same section do NOT include this save/restore. The spec says the pattern "is mandatory for ALL raw-GL texture bindings in `OnSetConstants()`" in the splat shader section, but the earlier single-unit examples lack it. An implementer writing a building or road callback following the earlier code block will omit the save/restore and corrupt Irrlicht's internal texture unit tracking on every frame that draws that object.
**Proposed resolution**: Update the single-unit `OnSetConstants()` example to include the `glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit)` / `glActiveTexture(static_cast<GLenum>(savedUnit))` pattern. Add a bold note before the first example: "ALL `OnSetConstants()` implementations that call `glActiveTexture()` MUST save and restore `GL_ACTIVE_TEXTURE` — even single-unit bindings."

---

#### HIGH-07 — `VRAMProfiler` duplicate translation unit in `aitown_benchmark` CMake target
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§CMake Target)
**Type**: [PROBLEM]
**Description**: The CMake target spec lists `src/rendering/VRAMProfiler.cpp` directly as a source of `aitown_benchmark` AND also links `aitown_render` which compiles `VRAMProfiler.cpp` as part of its static library. This produces `VRAMProfiler.cpp` in two translation units in the final link: once inside `libaitown_render.a` and once as a standalone object file in `aitown_benchmark`. On Linux with GNU ld, this typically results in duplicate symbol warnings or linker errors (`multiple definition of VRAMProfiler::init`). On MSVC, the linker silently resolves to one of the definitions — which one is implementation-defined and may produce ODR violations.
**Proposed resolution**: Remove `src/rendering/VRAMProfiler.cpp` from the `aitown_benchmark` sources list. `VRAMProfiler` is already provided transitively by linking `aitown_render`. The comment in the spec "The benchmark target is a separate translation unit context; linking `aitown_render` provides the compiled object transitively via `PRIVATE` linkage" acknowledges this but contradicts the explicit source addition. Remove the explicit source entry.

---

#### HIGH-08 — `u_srgbLinear` uniform bool in terrain splat shader: `setPixelShaderConstant` takes `int`, but the fragment shader declares `uniform bool`
**File**: `architecture/graphics-architecture/shader-loading.md` (§sRGB Gamma Fallback)
**Type**: [PROBLEM]
**Description**: The spec shows passing `&srgbLinearInt` (an `int`) to `services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1)` and declares the GLSL uniform as `uniform bool u_srgbLinear`. Irrlicht's `IMaterialRendererServices::setPixelShaderConstant` signature is `virtual bool setPixelShaderConstant(const c8* name, const s32* ints, s32 count)` — it takes `s32*`. Passing `int` (which is `s32` on all target platforms) is fine. However, in GLSL `#version 130` and later, `bool` uniforms are set via `glUniform1i` with value 0 or 1, which is what Irrlicht's GLSL backend does internally. This part is correct. The issue is the GLSL fragment shader has `if (u_srgbLinear) { ... }` — this is valid GLSL `#version 130` (`bool` branching is supported). However, the spec does not state which GLSL version the terrain fragment shader uses. If the shader uses `#version 110` (GLSL 1.10), `bool` uniforms set via `glUniform1i` are undefined behaviour. The spec requires `#version 130` elsewhere for `texture()` but is silent on the terrain splat shader's version pragma.
**Proposed resolution**: Add a requirement that `terrain.frag` must begin with `#version 130` (minimum) and cite the `texture()` and `uniform bool` dependency. Verify that `uniform bool` set via `glUniform1i` is confirmed to work on the Mesa/GLSL 1.30 path.

---

#### HIGH-09 — `model-validator-tool.md` specifies 45 LOD0 `.b3d` files but asset inventory table totals 40 zone buildings + 4 service buildings = 44, not 45
**File**: `architecture/graphics-architecture/model-validator-tool.md` (§Phase 11d Asset Inventory)
**Type**: [INCONSISTENCY]
**Description**: The spec states "The validator tool exercises the 45 LOD0 `.b3d` files across categories 1–11." The asset inventory table shows: Zone buildings LOD0 = 36, Service building LOD0 = 4, and 5 vehicle `.b3d` files (LOD0 for car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo). 36 + 4 + 5 = 45. However, the table lists "Zone building LOD0: 36" and "Service building LOD0: 4" for a sub-total of 40, and vehicles are not listed with a LOD0 count row. The total statement "45" is only correct if vehicles (5) are counted. The table needs a "Vehicle LOD0" row with count 5 and the math must be explicitly shown.
**Proposed resolution**: Add a "Vehicle LOD0 | 5 | car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo" row to the inventory table and update the sub-total to "40 zone+service + 5 vehicles = 45 total" for clarity.

---

#### HIGH-10 — `IrrlichtUIBackend` GL_ARRAY_BUFFER global state rule is in `irrlicht-device-lifecycle.md` but not in `testability-architecture.md` or any test specification
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§CRITICAL GL STATE RULE)
**Type**: [MISSING]
**Description**: The spec documents a critical OpenGL state bug: after `IrrlichtUIBackend`'s constructor creates a VAO/VBO, it must call `glBindBuffer(GL_ARRAY_BUFFER, 0)` to avoid corrupting Irrlicht's subsequent scene rendering. This rule exists only in `irrlicht-device-lifecycle.md`. There is no corresponding test case in `testability-architecture.md` or any headless CI testing spec that verifies this invariant. Under `EDT_NULL` (used in headless CI), this bug is invisible — `IrrlichtUIBackend` is not constructed in headless tests. There is no `requires-opengl` test that constructs `IrrlichtUIBackend` and then calls `sceneManager->drawAll()` to verify vertex geometry is non-degenerate.
**Proposed resolution**: Add a `requires-opengl` test `IrrlichtUIBackend_VBO_GL_ARRAY_BUFFER_Unbound_AfterConstruct` in `tests/rendering/` that: (1) constructs `IrrlichtUIBackend`; (2) queries `GL_ARRAY_BUFFER_BINDING`; (3) asserts the binding is 0. Reference this test in the architecture spec.

---

### MEDIUM

---

#### MED-01 — `drawMeshBuffer()` for overlay quads requires driver material state to be pre-set; spec does not describe this
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `scene-graph-ownership.md` (§raw drawMeshBuffer calls)
**Type**: [GAP]
**Description**: `IVideoDriver::drawMeshBuffer()` draws the provided mesh buffer using the current material state set on the driver via `IVideoDriver::setMaterial()`. The spec says the hover highlight and preview mesh are drawn via `driver->drawMeshBuffer(...)` in `drawScene()` but does not specify that `driver->setMaterial(buf->getMaterial())` must be called first. Without setting the material, Irrlicht uses whatever material was last set by `sceneManager->drawAll()` — which may have blending disabled, depth writes enabled, or a stale texture bound. The overlay mesh relies on `EMT_TRANSPARENT_ALPHA_CHANNEL` blending and depth-write disabled, both of which are in the buffer's material. If the previous scene material does not match, the overlay renders incorrectly (fully opaque or not at all).
**Proposed resolution**: Add to the `drawScene()` ordering spec: "Before each raw `drawMeshBuffer()` call for overlay or preview meshes, call `driver->setMaterial(buf->getMaterial())` where `buf` is the buffer being drawn. This ensures correct blending mode and depth state for transparent overlays."

---

#### MED-02 — LOD hysteresis spec repeated in three files with slightly different phrasing
**File**: `architecture/graphics-architecture/procedural-terrain.md`, `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/asset-standards/3d-model-standards.md`
**Type**: [DUPLICATE]
**Description**: The LOD hysteresis requirement ("Use separate swap-in / swap-out distances (5–10 m band)") is specified in `CLAUDE.md` project rules, `procedural-terrain.md`, `scene-graph-ownership.md`, and the LOD Distance Thresholds table in `3d-model-standards.md`. Each copy uses slightly different wording:
- `CLAUDE.md`: "Use separate swap-in / swap-out distances (5–10 m band). Never bare threshold comparisons."
- `procedural-terrain.md`: "never transitions up and down in the same frame"
- `3d-model-standards.md`: "Hysteresis bands are mandatory; ≥5 m for close thresholds, ≥10 m for far thresholds."
None of the three files cross-references the others as the normative source. An implementer reading only one file may get an incomplete picture. The canonical values (5 m close, 10 m far) exist only in `3d-model-standards.md`.
**Proposed resolution**: Designate `3d-model-standards.md` (LOD Distance Thresholds table) as the normative source for all hysteresis band values. In `procedural-terrain.md` and `scene-graph-ownership.md`, replace the local hysteresis descriptions with a cross-reference sentence pointing to the table.

---

#### MED-03 — `getTexture()` for cloud PNG: no `ET_NULL` / null return guard specified at the call site
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Cloud Asset)
**Type**: [GAP]
**Description**: The spec says `IVideoDriver::getTexture()` is used to load `clouds.png`. It specifies an `EDT_NULL` early-return guard at the top of `initCloudPlane()`. However, it does not specify what happens if `getTexture()` returns null on a real OpenGL driver (e.g., because the file is missing). The sky dome init proceeds, `setTexture(0, nullptr)` is called on the material (which is valid in Irrlicht but produces a grey flat-shaded surface), and the cloud dome renders incorrectly with no error. The CI asset gate checks the file exists, but at runtime the file may be absent after incorrect deployment.
**Proposed resolution**: Add: "After `getTexture()`, null-check the returned `ITexture*`. If null: log an error, fall back to `EMT_TRANSPARENT_VERTEX_ALPHA` with no texture (invisible dome), and do not set `m_cloudNode->getMaterial(0).Texture[0]`."

---

#### MED-04 — Benchmark Scene 2 uses `addShadowVolumeSceneNode()` but no spec exists for shadow volume ownership
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§Scene 2)
**Type**: [GAP]
**Description**: Scene 2 adds stencil shadow volumes to each building proxy box via `addShadowVolumeSceneNode()`. The returned `IShadowVolumeSceneNode*` is not mentioned in the cleanup sequence. Shadow volume nodes are child nodes of the mesh scene node and are automatically removed when their parent is removed. The spec's cleanup at the end of Scene 2 only says `smgr2->drop()`. This is correct (dropping the scene manager removes all its nodes recursively). However, the spec does not document why `drop()` alone is sufficient and a naive implementer may attempt to store and manually remove the shadow volume nodes, potentially calling `remove()` on already-removed children.
**Proposed resolution**: Add a comment in the benchmark spec: "Shadow volume nodes are children of their parent mesh node and are automatically removed by `smgr2->drop()`. Do not store or manually remove `IShadowVolumeSceneNode*` pointers."

---

#### MED-05 — `PolygonOffsetFactor = 1` for road tiles contradicts the center-line strip which requires `PolygonOffsetFactor = 5`
**File**: `architecture/asset-standards/3d-model-standards.md` (§Center-line strip) and `architecture/graphics-architecture/procedural-terrain.md` (§Z-fighting)
**Type**: [INCONSISTENCY]
**Description**: `procedural-terrain.md` specifies `PolygonOffsetFactor = 1` for road tiles. `3d-model-standards.md` specifies that the center-line strip uses `PolygonOffsetFactor = 5` (one step above the carriageway's `factor = 4`). But `procedural-terrain.md` says the carriageway itself uses `factor = 1`. If the carriageway is at `factor = 1` and the center-line is at `factor = 5`, the carriageway and center-line are 4 steps apart. The `3d-model-standards.md` comment "one step above the carriageway's `factor = 4`" implies the carriageway uses `factor = 4`, contradicting `procedural-terrain.md`'s value of `factor = 1`. One of these values is wrong.
**Proposed resolution**: Reconcile the two documents. Decide on the carriageway polygon offset factor and propagate it consistently. If the carriageway is `factor = 1` (as in `procedural-terrain.md`), the center-line strip should be `factor = 2`. If the carriageway is `factor = 4` (as implied by `3d-model-standards.md`), update `procedural-terrain.md`. Remove the contradiction.

---

#### MED-06 — No spec for how `IrrlichtRenderer::renderer->update(realDeltaSeconds)` is sequenced relative to terrain LOD and the cloud UV scroll
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§mandatory 11-step sequence)
**Type**: [INCONSISTENCY]
**Description**: The render loop spec lists the mandatory 11-step sequence and includes `renderer->update(realDeltaSeconds)` (cloud UV scroll) as step 5, between `terrainSystem->update()` (step 4) and `driver->beginScene()` (step 6). The code block at the top of the file does NOT include `renderer->update()` in the inline code sample — it jumps directly from `terrainSystem->update()` to `driver->beginScene()`. An implementer following the code sample will skip `renderer->update()` and the cloud UV offset will never scroll.
**Proposed resolution**: Add `renderer->update(realDeltaSeconds)` to the code sample immediately after `terrainSystem->update()` with a comment `// cloud UV scroll and per-frame renderer state`.

---

#### MED-07 — Building atlas `buildings_atlas_d.png` PNG workaround: spec does not define the sRGB upload path for the PNG fallback
**File**: `architecture/asset-standards/building-atlas-layout.md` (§V1 implementation exception)
**Type**: [GAP]
**Description**: The spec says the V1 atlas is loaded as a PNG via `IVideoDriver::getTexture()` and assigned to material slots via `node->getMaterial(m).setTexture(0, atlas)`. PNG loaded through `IVideoDriver::getTexture()` is decoded to `ECF_A8R8G8B8` (linear RGBA) by Irrlicht's PNG loader. There is no sRGB decoding on this path. The spec does not state whether the PNG version of the atlas is authored in linear or sRGB space, or whether the building shader must apply a manual `pow(color.rgb, 2.2)` gamma correction on the PNG path analogous to the terrain shader's `u_srgbLinear` uniform. If the PNG is authored as sRGB but uploaded as linear, all building facades will appear visually washed out (linear-decoded diffuse). This is a production rendering correctness issue for the PNG fallback path.
**Proposed resolution**: Add an explicit statement: "The V1 PNG atlas (`buildings_atlas_d.png`) must be authored in linear color space (not sRGB) to match the linear upload path via `IVideoDriver::getTexture()`, OR the building shader must apply `pow(color.rgb, 2.2)` gamma correction when sampling the PNG atlas. Document the chosen approach in `shader-loading.md` and cross-reference from `building-atlas-layout.md`."

---

#### MED-08 — LOD Distance Thresholds table in `3d-model-standards.md` says "switch-out" / "switch-in" but `LODNode::swapMesh()` comment only references "setMesh" — hysteresis implementation responsibility unclear
**File**: `architecture/asset-standards/3d-model-standards.md` and `architecture/graphics-architecture/scene-graph-ownership.md`
**Type**: [GAP]
**Description**: The LOD Threshold table specifies four distances per asset category (switch-out and switch-in for both LOD0↔LOD1 and LOD1↔LOD2). `scene-graph-ownership.md` says "Canonical implementation: This sequence is encapsulated in `LODNode::swapMesh()`." But `LODNode::swapMesh()` performs the mesh swap — it does not contain the distance comparison or hysteresis logic. The spec never identifies where the per-frame distance check and hysteresis comparison live: is it in `LODNode::update(float cameraDistance)` (which is not mentioned anywhere as a method), in `SceneEntityManager::update()`, in `CitySimulation`, or somewhere else? No component is designated as responsible for calling `swapMesh()` at the right time.
**Proposed resolution**: Add a `LODNode::update(float cameraDistanceSq)` method description to `scene-graph-ownership.md` that documents the hysteresis state machine (current LOD, switch-in threshold, switch-out threshold) and specifies that `SceneEntityManager::update()` calls `lodNode->update(distSq)` per frame for all live entities.

---

#### MED-09 — `buildingAtlasLayout.md` references `buildings_atlas_d.dds` mip count via `GL_TEXTURE_MAX_LEVEL = 4`, but `texture-cache.md` dispatch table shows `GL_TEXTURE_MAX_LEVEL = 4` for the primary atlas and `GL_TEXTURE_MAX_LEVEL = 3` for fallback; the `TextureCache::loadSRGB()` path must select between them at runtime
**File**: `architecture/graphics-architecture/texture-cache.md` (§GL_TEXTURE_MAX_LEVEL Dispatch Table)
**Type**: [GAP]
**Description**: The dispatch table has two rows for the building atlas (primary 4096×4096 with `GL_TEXTURE_MAX_LEVEL = 4`, and fallback 2048×2048 with `GL_TEXTURE_MAX_LEVEL = 3`). The runtime selection between primary and fallback atlas is triggered by `GL_MAX_TEXTURE_SIZE < 4096`. However, the spec does not document where this selection logic lives — in `TextureCache::loadSRGB()`? In `BuildingAssetLoader`? In `IrrlichtRenderer`? And which component holds the reference to `m_maxTextureSize` to make the comparison? If `TextureCache` does not have access to `m_maxTextureSize` from `RenderSystem`, the dispatch cannot be performed.
**Proposed resolution**: Specify: (1) which class performs the atlas selection (primary vs fallback); (2) that `TextureCache` receives `maxTextureSize` at construction from `RenderSystem::getMaxTextureSize()`, or that `BuildingAssetLoader` queries `RenderSystem::getMaxTextureSize()` and passes the correct atlas path to `loadSRGB()`; (3) the exact condition (`m_maxTextureSize >= 4096` → primary; else → fallback).

---

#### MED-10 — No spec for what happens to `m_cloudNode`'s texture when `IrrlichtRenderer` is destroyed — `device->drop()` releases the scene node but the cloud PNG `ITexture*` may remain in the linear pool
**File**: `architecture/graphics-architecture/sky-clouds.md` (§Headless / EDT_NULL Guard) and `architecture/graphics-architecture/scene-graph-ownership.md` (§Renderer-Internal Permanent Scene Nodes)
**Type**: [GAP]
**Description**: `scene-graph-ownership.md` says renderer-internal nodes (sky dome, cloud plane) are "released automatically by `device->drop()`." Irrlicht's scene node destructor calls `drop()` on the node's materials' textures — but only for `ITexture*` objects stored via `setTexture()`. The cloud PNG was loaded via `IVideoDriver::getTexture()` (linear pool, NOT `TextureCache`). When `device->drop()` is called, Irrlicht's video driver drops all textures it owns (those loaded via `getTexture()`) as part of its own cleanup. This is correct and no action is required from `IrrlichtRenderer`. However, the spec never states this — implementers may add an unnecessary explicit `driver->removeTexture()` call in `IrrlichtRenderer::~IrrlichtRenderer()` that would double-free the texture. The spec should state explicitly that the cloud texture is released by `device->drop()` and requires no manual removal.
**Proposed resolution**: Add a sentence in `sky-clouds.md` (§Headless Guard) and `scene-graph-ownership.md` (§Renderer-Internal Permanent Scene Nodes): "The cloud texture (`clouds.png`, loaded via `IVideoDriver::getTexture()`) is released by Irrlicht's video driver as part of `device->drop()`. Do NOT call `driver->removeTexture()` on this texture in `IrrlichtRenderer`'s destructor — it would double-free the texture object."

---

### LOW

---

#### LOW-01 — `irrlicht-device-lifecycle.md` construction sequence shows `IrrlichtUIBackend` as step 2, before `glewInit()` which is inside `RenderSystem` constructor body — but step 2 is listed after step 1 (RenderSystem) implying glewInit already ran
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` (§IrrlichtRenderer Late-Binding Pattern, Construction Sequence)
**Type**: [INCONSISTENCY]
**Description**: The construction sequence table in `irrlicht-device-lifecycle.md` lists step 1 as `RenderSystem` and step 2 as `IrrlichtUIBackend`. A separate section ("IrrlichtUIBackend construction ordering constraint") says `IrrlichtUIBackend` must be constructed AFTER `glewInit()` returns. This is correct if `glewInit()` is called inside the `RenderSystem` constructor body. However, the sequence table implies `IrrlichtUIBackend` is constructed in `main.cpp` after `RenderSystem` (i.e., after `RenderSystem`'s constructor has completed, including `glewInit()`). This ordering is correct but the table does not note the dependency. An implementer who changes the construction order (e.g., tries to construct `IrrlichtUIBackend` as a member of `RenderSystem`) will silently break the GLEW initialization ordering.
**Proposed resolution**: Add a note to step 2 in the construction sequence table: "`IrrlichtUIBackend` must be constructed in `main.cpp` AFTER `RenderSystem`'s constructor completes (which includes `glewInit()`). Do NOT construct it as a member of `RenderSystem` or before `RenderSystem` returns from its constructor."

---

#### LOW-02 — `benchmark-tool.md` Scene 1 ground plane uses a "checkerboard texture" that is never defined or cross-referenced
**File**: `architecture/graphics-architecture/benchmark-tool.md` (§Scene 1)
**Type**: [GAP]
**Description**: Scene 1 uses a "checkerboard texture … with 4x UV repetition per tile." This texture has no defined path, resolution, format, or generation method. It is not in `2d-texture-standards.md` or `building-atlas-layout.md`. If this texture must be a file on disk, it needs a spec entry. If it is procedurally generated (e.g., via `IVideoDriver::createImageFromData()`), the generation code must be documented.
**Proposed resolution**: Add a note specifying either: "Checkerboard texture is generated procedurally at runtime using `IVideoDriver::createImageFromData()` with an 8×8 black/white pattern uploaded via `addTexture()`," or define a `assets/textures/benchmark/checkerboard.png` file at a specified resolution in the asset directory.

---

#### LOW-03 — `scene-graph-ownership.md` mentions `IMeshSceneNode*` for `LODNode::m_node` but `addMeshSceneNode()` returns `IMeshSceneNode*` while `getNode()` returns `ISceneNode*` — the cast is implicit and undocumented
**File**: `architecture/graphics-architecture/scene-graph-ownership.md` (§B3D Building Assets — LOD swap contract)
**Type**: [GAP]
**Description**: The spec says "`LODNode` stores `IMeshSceneNode*` for `m_node` (to call `setMesh(IMesh*)`)". It also says "`getNode()` returns `ISceneNode*` via implicit upcast." The implicit upcast from `IMeshSceneNode*` to `ISceneNode*` is safe (public inheritance), but the spec does not document the `ISceneNode*` return type of `getNode()` or why it is `ISceneNode*` rather than `IMeshSceneNode*`. If callers need to call `setMesh()` on the returned node, they will need to cast back to `IMeshSceneNode*` — but the WARNING section says downcasts are dangerous. The spec should clarify that `LODNode::setMesh()` (the public method) internally calls `m_node->setMesh()` (on `IMeshSceneNode*`) so that external callers NEVER need to downcast the return of `getNode()`.
**Proposed resolution**: Add one sentence: "`LODNode` exposes `void swapMesh(IMesh*)` as the public LOD swap entry point so that callers never need to access `m_node` directly or downcast the `ISceneNode*` returned by `getNode()`. `getNode()` returning `ISceneNode*` is intentional — callers that need scene-node operations (transform, visibility) use the base-class interface."

---

#### LOW-04 — `billboard imposter atlas` listed in `building-atlas-layout.md` references `2d-texture-standards.md` for the mip chain spec, but `2d-texture-standards.md` has no heading "Billboard Imposter Atlas"
**File**: `architecture/asset-standards/building-atlas-layout.md` (§Billboard Imposter Atlas) and `architecture/asset-standards/2d-texture-standards.md`
**Type**: [GAP]
**Description**: `building-atlas-layout.md` says "See `architecture/asset-standards/2d-texture-standards.md` 'Billboard Imposter Atlas' section for the full authoring spec (bake angles, elevation, lighting, cell padding, usable content area, naming convention)." A search of `2d-texture-standards.md` finds no heading with this exact title. The billboard-related content is present (references to 1024×128 DXT5 sRGB, 4-level mip chain, `_billboard` suffix) but not under a dedicated heading. Cross-references that use heading names that do not exist produce documentation dead-ends.
**Proposed resolution**: Add a `## Billboard Imposter Atlas` section heading to `2d-texture-standards.md` that aggregates all billboard-specific requirements (currently scattered in several places). Update `building-atlas-layout.md`'s cross-reference to use the correct heading anchor.

---

#### LOW-05 — `irrlicht-device-lifecycle.md` and `procedural-terrain.md` both describe the hover highlight bounding box rule but with different scope
**File**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md` and `architecture/graphics-architecture/scene-graph-ownership.md`
**Type**: [DUPLICATE]
**Description**: The `irrlicht-device-lifecycle.md` §Hover Highlight section describes the full lifecycle of `m_hoveredTileMesh` including the `setTileHoverHighlight()` update protocol. `scene-graph-ownership.md` §Hover Highlight — Single Buffer, Pre-Allocated also describes the same lifecycle. Both files describe the same allocation, the same update pattern, and the same draw-via-`drawMeshBuffer` rule. While each has slightly different emphasis (lifecycle vs geometry), the two descriptions can diverge if one is updated and the other is not.
**Proposed resolution**: Designate one file as the normative lifecycle spec for `m_hoveredTileMesh` (recommend `scene-graph-ownership.md` since it already covers all scene-graph ownership rules) and reduce the other to a forward-reference. Move all definitive lifecycle text to the designated file.

---

#### LOW-06 — `testability-architecture.md` `IRenderer` interface is referenced by `CameraController testability` but `IRenderer.h` is not listed as the source of `ScreenRect`
**File**: `architecture/testing/testability-architecture.md` (§QueryPanel testability)
**Type**: [GAP]
**Description**: `testability-architecture.md` says "`ScreenRect` is added to `IRenderer.h` by Phase 9b Deliverable B as `struct ScreenRect { int x{0}, y{0}, w{0}, h{0}; }`". However, `IUIBackend.h` already defines `struct Rect { int x{0}, y{0}, w{0}, h{0}; }` (used by `getElementRect()`). Two distinct POD structs with identical fields but different names exist in the public interfaces. An implementer may confuse `Rect` (from `IUIBackend`) with `ScreenRect` (from `IRenderer`) or inadvertently introduce an implicit conversion. The rationale for having two separate structs is not stated.
**Proposed resolution**: Add a note in `testability-architecture.md` (and `IRenderer.h` spec) explaining why `ScreenRect` cannot reuse `IUIBackend::Rect`: "Although the fields are identical, `ScreenRect` lives in `IRenderer.h` (rendering domain) and `Rect` lives in `IUIBackend.h` (UI domain). Including `IUIBackend.h` from `IRenderer.h` would create a cross-domain header dependency. The duplicate definition is intentional."

---

## Summary Table

| ID | File | Type | Severity |
|---|---|---|---|
| CRIT-01 | `procedural-terrain.md` | GAP | CRITICAL |
| CRIT-02 | `texture-cache.md` | GAP | CRITICAL |
| CRIT-03 | `sky-clouds.md` | PROBLEM | CRITICAL |
| CRIT-04 | `texture-cache.md` | INCONSISTENCY | CRITICAL |
| CRIT-05 | `scene-graph-ownership.md` | INCONSISTENCY | CRITICAL |
| HIGH-01 | `procedural-terrain.md` | GAP | HIGH |
| HIGH-02 | `irrlicht-device-lifecycle.md` | GAP | HIGH |
| HIGH-03 | `procedural-terrain.md` | PROBLEM | HIGH |
| HIGH-04 | `scene-graph-ownership.md` | GAP | HIGH |
| HIGH-05 | `texture-cache.md` | GAP | HIGH |
| HIGH-06 | `shader-loading.md` | GAP | HIGH |
| HIGH-07 | `benchmark-tool.md` | PROBLEM | HIGH |
| HIGH-08 | `shader-loading.md` | PROBLEM | HIGH |
| HIGH-09 | `model-validator-tool.md` | INCONSISTENCY | HIGH |
| HIGH-10 | `irrlicht-device-lifecycle.md` | MISSING | HIGH |
| MED-01 | `irrlicht-device-lifecycle.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| MED-02 | `procedural-terrain.md` / `scene-graph-ownership.md` / `3d-model-standards.md` | DUPLICATE | MEDIUM |
| MED-03 | `sky-clouds.md` | GAP | MEDIUM |
| MED-04 | `benchmark-tool.md` | GAP | MEDIUM |
| MED-05 | `3d-model-standards.md` / `procedural-terrain.md` | INCONSISTENCY | MEDIUM |
| MED-06 | `irrlicht-device-lifecycle.md` | INCONSISTENCY | MEDIUM |
| MED-07 | `building-atlas-layout.md` | GAP | MEDIUM |
| MED-08 | `3d-model-standards.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| MED-09 | `texture-cache.md` | GAP | MEDIUM |
| MED-10 | `sky-clouds.md` / `scene-graph-ownership.md` | GAP | MEDIUM |
| LOW-01 | `irrlicht-device-lifecycle.md` | INCONSISTENCY | LOW |
| LOW-02 | `benchmark-tool.md` | GAP | LOW |
| LOW-03 | `scene-graph-ownership.md` | GAP | LOW |
| LOW-04 | `building-atlas-layout.md` / `2d-texture-standards.md` | GAP | LOW |
| LOW-05 | `irrlicht-device-lifecycle.md` / `scene-graph-ownership.md` | DUPLICATE | LOW |
| LOW-06 | `testability-architecture.md` | GAP | LOW |

---

## Subsystems With No Spec (MISSING)

The following rendering subsystems are referenced in passing but have no dedicated specification file. These are gaps that will require spec work before implementation:

1. **Shadow system**: `irrlicht-device-lifecycle.md` sets `params.Stencil = true` and the benchmark uses `addShadowVolumeSceneNode()` in Scene 2, but no architecture file specifies shadow volume usage in the game runtime (which buildings cast shadows, update frequency, perf cost, interaction with the LOD system). No `shadow-volumes.md` exists.

2. **Post-processing / render effects pipeline**: No spec for any post-processing pass (bloom, tone-mapping, FXAA). `params.AntiAlias = 4` implies MSAA but there is no spec for how MSAA interacts with the custom sRGB shader path or the raw `drawMeshBuffer()` overlay calls.

3. **Lighting model for buildings**: `shader-loading.md` defines terrain shader constants (units 0–8) and a building shader entry for unit 0 (diffuse) and unit 1 (normal map), but no `lighting.frag` specification exists beyond "constant color fragment shader." The building shader's lighting model (diffuse + specular, PBR roughness/metallic, or Phong) is unspecified. Phase 6 is referenced for lighting, but no architecture file documents what the building and road shaders actually compute.

4. **Road shader full specification**: `shader-loading.md` lists `road.vert` / `road.frag` and mentions a `RoadShaderCallback`, but no document specifies what `road.frag` does beyond sampling `u_diffuseMap` and `u_srgbLinear`. UV tiling factor (2×), road marking decal blending from the marking atlas, and lane color are mentioned in `3d-model-standards.md` and `building-atlas-layout.md` but not in a normative shader specification.

5. **UI rendering pipeline (`IrrlichtUIBackend`)**: The GL_ARRAY_BUFFER VAO/VBO construction in `IrrlichtRenderer.h`'s construction sequence note is the only technical description of `IrrlichtUIBackend`'s rendering mechanism. A dedicated `ui-rendering-pipeline.md` would prevent the `ui_quad.vert/frag` shader contract from being scattered across `shader-loading.md` and `irrlicht-device-lifecycle.md`.
