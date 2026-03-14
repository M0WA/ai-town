# Phase 10c: Terrain Texture Wiring

## Goal

Wire the terrain texture assets (`terrain_*_d.dds`, `terrain_chunk_splat.png`) into the
rendering pipeline. The infrastructure is fully built (shaders, `TextureCache`, `TerrainShaderCallback`)
but never connected to `IrrlichtRenderer`. After this phase, terrain chunks render with the
splat-blended multi-layer shader instead of the current height-based vertex colours.

---

## Deliverables

### Feature 1: `initTerrainShader()` in IrrlichtRenderer

**Owner**: `graphics-dev-irrlicht`

Mirrors `initRoadShader()` (`src/rendering/IrrlichtRenderer.cpp` line 1252). New private method
`IrrlichtRenderer::initTerrainShader()` called from the constructor after all other init steps.

- [ ] Add `int m_terrainMaterialType` member to `IrrlichtRenderer` (initialised to `-1`)
- [ ] Add `TerrainShaderCallback* m_terrainCallback` member (raw pointer; `grab()`/`drop()` lifecycle
  matching the road callback pattern)
- [ ] Implement `initTerrainShader()`:
  - EDT_NULL early-return guard as first line (matches `initRoadShader()` pattern)
  - Load 4 diffuse textures via `m_textureCache->loadSRGB(path, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)` — paths:
    `assets/textures/terrain/terrain_grass_d.dds`, `terrain_asphalt_d.dds`, `terrain_soil_d.dds`,
    `terrain_concrete_d.dds`
  - Load splat map via `m_textureCache->loadSplatMap("assets/textures/terrain/terrain_chunk_splat.png")`
  - Construct `TerrainShaderCallback` with `m_renderSystem`, `m_textureCache`, splat path, and
    `std::array<std::string,4>` of detail paths (grass, asphalt, soil, concrete — splat channel order)
  - Call `->grab()` on callback, assign to `m_terrainCallback`
  - Load shader via `m_renderSystem->loadShader("assets/shaders/terrain.vert", "assets/shaders/terrain.frag", m_terrainCallback)`
    → store result in `m_terrainMaterialType`
  - On failure (`m_terrainMaterialType == -1`): log warning, leave `m_terrainMaterialType = -1`
    (fallback to `EMT_SOLID` in Feature 2)
  - Call `->drop()` after passing to `loadShader()` (matches road callback lifecycle at line 1315)
- [ ] Call `initTerrainShader()` from `IrrlichtRenderer` constructor after `initRoadShader()`

**Reference**: `initRoadShader()` at `src/rendering/IrrlichtRenderer.cpp:1252`; `TerrainShaderCallback`
constructor at `src/rendering/TerrainShaderCallback.h`; splat channel order locked in
`architecture/asset-standards/2d-texture-standards.md` §Splat map channel-to-material assignment.

---

### Feature 2: Assign material type in `rebuildTerrainChunk()`

**Owner**: `graphics-dev-irrlicht`

The defensive comment at `IrrlichtRenderer.cpp` line ~296 ("Phase 6+ terrain texturing") is the
exact insertion point.

- [ ] After `addMeshSceneNode()` returns `newNode`, assign the terrain shader material:

  ```cpp
  irr::video::SMaterial& mat = newNode->getMaterial(0);
  if (m_terrainMaterialType != -1) {
      mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(m_terrainMaterialType);
  }
  // EMF_LIGHTING stays false — per-pixel lighting is Phase 11+
  mat.setFlag(irr::video::EMF_LIGHTING, false);
  mat.setFlag(irr::video::EMF_BACK_FACE_CULLING, false);
  ```

- [ ] Remove the "Phase 6+ terrain texturing" comment placeholder
- [ ] No changes to vertex colour generation — vertex colours remain in the mesh; the terrain
  shader ignores them (splat weights drive blending). They serve as a fallback when
  `m_terrainMaterialType == -1`.

---

### Feature 3: Tests

**Owner**: `test-dev-cpp`

- [ ] **`TerrainShaderWiring_EDT_NULL_InitDoesNotCrash`** — unit test in
  `tests/rendering/terrain_shader_wiring_test.cpp` (label `unit`, target `terrain_tests` via
  `target_sources`). Constructs `IrrlichtRenderer` with EDT_NULL device; verifies constructor
  completes without crash and `m_terrainMaterialType == -1` (no GL calls possible under EDT_NULL).
- [ ] **`TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded`** — integration test in
  `tests/integration/terrain_shader_wiring_test.cpp` (label `integration`, target `integration_tests`).
  Stubs `IrrlichtRenderer` or uses mock; verifies `rebuildTerrainChunk()` sets a non-`EMT_SOLID`
  material type on the scene node when `m_terrainMaterialType != -1`.
- [ ] Add `tests/rendering/terrain_shader_wiring_test.cpp` to `terrain_tests` via
  `target_sources(terrain_tests PRIVATE ...)` (do NOT call `add_executable` again)

---

## Exit Criteria

- Terrain chunks render with the splat-blended multi-layer shader; grass/asphalt/soil/concrete
  material zones are visually distinct in a real OpenGL window
- `m_terrainMaterialType != -1` after `IrrlichtRenderer` construction on a real GL context
- `TerrainShaderWiring_EDT_NULL_InitDoesNotCrash` passes (label `unit`)
- `TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded` passes (label `integration`)
- No regression in existing terrain unit tests or `TerrainSystem_FlushPendingRebuilds_*` tests
- CI green on Linux and Windows (terrain shader not loaded on Windows CI headless path — EDT_NULL
  guard must ensure no raw GL calls)
- Texture paths in `initTerrainShader()` use `assets/textures/terrain/` (not the old `assets/terrain/`)

---

## Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | `initTerrainShader()`, `m_terrainMaterialType` member, `rebuildTerrainChunk()` material assignment |
| `test-dev-cpp` | Two tests (EDT_NULL unit + integration), CMake wiring via `target_sources` |

---

## Dependencies

- Requires Phase 5 complete: `TerrainShaderCallback`, terrain shaders, `TextureCache` 3-pool
  implementation, terrain DDS placeholder assets — all done
- Can run **parallel to Phase 10b** — `IrrlichtRenderer.cpp` includes none of the headers
  being renamed in 10b's naming convention pass; no merge conflict risk
- Does **not** require Phase 9 artistic textures — placeholder DDS files from Phase 5 are
  sufficient; artistic replacements drop in without code changes

---

## Out of Scope

- Normal map binding (`terrain_*_n.dds`) — terrain shader does not currently sample normal maps;
  add in a future polish phase
- Per-pixel lighting on terrain — deferred to Phase 11+
- Per-chunk splat map painting — `terrain_chunk_splat.png` is a single shared placeholder; per-chunk
  splat maps are a future terrain editing feature
