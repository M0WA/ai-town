# Phase 10c: Terrain Texture Wiring & Glass City UI Rework

## Goal

Two parallel workstreams:

1. **Terrain texture wiring** — connect `terrain_*_d.dds` / `terrain_chunk_splat.png` into the
   rendering pipeline. The infrastructure is fully built (shaders, `TextureCache`,
   `TerrainShaderCallback`) but never connected to `IrrlichtRenderer`. After this phase, terrain
   chunks render with the splat-blended multi-layer shader instead of height-based vertex colours.

2. **Glass City UI visual rework** — regenerate the HUD sprite sheet and update
   `IrrlichtUIBackend` to match the approved Glass City visual direction: deep navy panels,
   electric teal active state with baked glow, amber numeric values, outlined-inactive /
   filled-active icon system. Specs locked in commit 7412532.

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

### Feature 4: Glass City Sprite Sheet Regeneration

**Owner**: `graphics-artist-2d-texture`

Regenerate `assets/textures/ui/hud_sprites_ui.png` (2048×2048 RGBA) with the Glass City
visual direction. The cell grid, sprite IDs, and JSON layout file are unchanged — only the
visual style of each cell changes.

**Canonical spec**: `architecture/ui-ux/resolution-ui-scaling.md` §Visual Design — Glass City
(palette, button tile 3-state spec, icon state rules). All values below are derived from that
section; the spec file is authoritative.

#### Icon state rules

Every icon that has an **inactive** and **active** variant must be regenerated as two distinct
cell styles:

| State | Icon fill | Opacity | Border | Glow |
|---|---|---|---|---|
| Inactive | 2 px outlined stroke only, hollow fill | 65% on white stroke | None | None |
| Hover | 2 px outlined stroke | 85% | 1 px `rgba(255,255,255,0.35)` | None |
| Active | Filled solid silhouette | 100% | 2 px `rgba(0,201,200,0.75)` | 4 px Gaussian blur glow baked into cell |

The glow in the active cell is baked at the source working resolution (256×256 per cell) via a
4 px Gaussian blur of the teal border on a transparent layer, then Lanczos-downsampled to 64×64.

#### Button tile backgrounds

All button tile cells (the background square behind each icon) use:

- **Inactive tile**: `rgba(255,255,255,0.08)` fill, 1 px perimeter border `rgba(255,255,255,0.18)`, 8 px corner radius
- **Hover tile**: `rgba(255,255,255,0.15)` fill, 1 px border `rgba(255,255,255,0.35)`, 8 px corner radius
- **Active tile**: `rgba(0,201,200,0.22)` teal wash fill, 2 px border `rgba(0,201,200,0.75)`, 8 px corner radius

The tile backgrounds and icon overlays are composited into a single 64×64 cell in the PNG —
`IrrlichtUIBackend` does not composite them separately.

#### Panel background cells

Add dedicated panel-background cells in unused rows (rows 16+ in the 32-row grid) for the
dark navy panel surfaces used by `IrrlichtUIBackend::addStaticText` backgrounds:

| Cell purpose | Fill | Opacity | Corner radius |
|---|---|---|---|
| Toolbar / sub-panel background tile | `rgb(13,27,42)` | 88% | 0 px (flush screen edge) |
| Resource bar background tile | `rgb(13,27,42)` | 95% | 0 px |
| Inspector / minimap / modal panel tile | `rgb(13,27,42)` | 82% | 8 px inner edges |

These single-colour tiled cells let `IrrlichtUIBackend` draw navy panel backgrounds via
`IGUIButton::setImage()` without any new API methods.

#### Deliverables

- [ ] Regenerate `assets/textures/ui/hud_sprites_ui.png` with all inactive icon cells as
  outlined-stroke / hover cells at 85% / active cells as filled + teal border + baked glow
- [ ] Add panel-background cells to rows 16+ of the sprite sheet; update
  `assets/textures/ui/hud_sprites_ui_layout.json` with the new cell rects
- [ ] Update `src/ui/hud_sprite_ids.h` with `kSpritePanelToolbar`, `kSpritePanelResourceBar`,
  `kSpritePanelOverlay` constants for the new background cells (follow existing `constexpr
  uint32_t` pattern; add after the last existing constant)
- [ ] Update `tools/generate_hud_sprites.py` to produce the outlined-inactive / filled-active
  style for all future regenerations — no manual hand-editing of the PNG
- [ ] Verify final PNG is still 2048×2048 RGBA (Check #23 must stay green)

**Reference**: `architecture/ui-ux/resolution-ui-scaling.md` §Glass City Canonical Colour
Palette; `architecture/asset-standards/2d-texture-standards.md` §UI Sprite Sheet Art Style —
Glass City.

---

### Feature 5: `IrrlichtUIBackend` Glass City Colour Pass

**Owner**: `graphics-dev-irrlicht`

Update all hardcoded `SColor` values in `src/rendering/IrrlichtUIBackend.cpp` and
`src/rendering/IrrlichtUIBackend.h` to match the Glass City palette. No new `IUIBackend`
interface methods are required — all changes are internal to the Irrlicht backend.

**Canonical spec**: `architecture/ui-ux/resolution-ui-scaling.md` §Visual Design — Glass City.

#### Colour token mapping

| UI element | Old value | New value |
|---|---|---|
| Resource bar background | `SColor(140,230,230,230)` (milky white) | `SColor(224,13,27,42)` (dark navy 88%) |
| Toolbar / left panel background | `SColor(100,220,220,220)` | `SColor(200,13,27,42)` (dark navy 78%) |
| Minimap / inspector panel background | `SColor(230,20,20,20)` | `SColor(209,13,27,42)` (dark navy 82%) |
| HUD numeric text (treasury, pop, date) | white / default | `SColor(255,240,180,41)` amber `#F0B429` |
| HUD label text (static labels) | default | `SColor(255,235,244,246)` near-white `#EBF4F6` |
| HUD secondary labels | default | `SColor(255,74,127,165)` mid-blue `#4A7FA5` |
| Deficit / error text | red (existing) | `SColor(255,240,78,55)` `#F04E37` |
| Warning / grace-period text | default | `SColor(255,232,150,12)` amber `#E8960C` |
| Active tint (button highlight) | `SColor(35,0,200,220)` alpha-35 | removed — active state is in sprite, not overlay |

The `SColor(A,R,G,B)` constructor takes A=0–255. Convert the `rgba(R,G,B,A_0–1)` values
above: multiply A_0–1 by 255.

#### Specific method changes

- [ ] `IrrlichtUIBackend` constructor: change all panel `setBackgroundColor()` calls to dark
  navy values per table above
- [ ] `IrrlichtUIBackend::addStaticText()`: set default text color to near-white `#EBF4F6`
  on the created `IGUIStaticText` (via `->setOverrideColor(SColor(255,235,244,246))`)
- [ ] All HUD numeric value elements (treasury balance, population, date/time): set override
  color to amber `#F0B429` (`SColor(255,240,180,41)`) at creation time or in the relevant
  `setElementText()` path
- [ ] All sub-label elements (secondary info text): set override color to mid-blue `#4A7FA5`
- [ ] Remove or replace any code that applies a translucent active-tint overlay via `SColor`
  with alpha-35 — the active state is now expressed entirely through the sprite (Feature 4);
  no programmatic color overlay needed on activation
- [ ] `IrrlichtUIBackend`: load `hud_font.xml` (not the old `ui_font.xml` path which no longer
  exists); this was already fixed in commit 1100241 — verify no regression

---

## Exit Criteria

### Terrain texture wiring

- Terrain chunks render with the splat-blended multi-layer shader; grass/asphalt/soil/concrete
  material zones are visually distinct in a real OpenGL window
- `m_terrainMaterialType != -1` after `IrrlichtRenderer` construction on a real GL context
- `TerrainShaderWiring_EDT_NULL_InitDoesNotCrash` passes (label `unit`)
- `TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded` passes (label `integration`)
- No regression in existing terrain unit tests or `TerrainSystem_FlushPendingRebuilds_*` tests
- CI green on Linux and Windows (terrain shader not loaded on Windows CI headless path — EDT_NULL
  guard must ensure no raw GL calls)
- Texture paths in `initTerrainShader()` use `assets/textures/terrain/` (not the old `assets/terrain/`)

### Glass City UI rework

- All toolbar, zone, and utilities button inactive states render as 2 px outlined stroke icons
  at 65% opacity; active state renders as filled solid with visible teal border and glow
- Panels (resource bar, toolbar, sub-panels, inspector, minimap, modal) render with dark navy
  background — no milky white semi-transparent surface visible
- All numeric HUD values (treasury balance, population count, demand values) render in amber
  `#F0B429`; labels render in near-white `#EBF4F6`
- `hud_sprites_ui.png` remains 2048×2048 RGBA (Check #23 stays green)
- `hud_sprite_ids.h` contains `kSpritePanelToolbar`, `kSpritePanelResourceBar`,
  `kSpritePanelOverlay` constants; `hud_sprites_ui_layout.json` updated
- `tools/generate_hud_sprites.py` produces the outlined-inactive / filled-active style on
  regeneration
- No regression in existing `ui_tests` or `opengl_tests`; `validate_assets.py` Check #23 green

---

## Team

| Role | Responsibility |
|---|---|
| `graphics-dev-irrlicht` | Features 1–2: `initTerrainShader()`, `m_terrainMaterialType` member, `rebuildTerrainChunk()` assignment; Feature 5: `IrrlichtUIBackend` colour pass |
| `graphics-artist-2d-texture` | Feature 4: sprite sheet regeneration with Glass City icon/tile styles, panel background cells, layout JSON update, `generate_hud_sprites.py` update |
| `test-dev-cpp` | Feature 3: terrain shader tests (EDT_NULL unit + integration), CMake wiring |

---

## Dependencies

- Requires Phase 5 complete: `TerrainShaderCallback`, terrain shaders, `TextureCache` 3-pool
  implementation, terrain DDS placeholder assets — all done
- Requires Glass City specs committed (commit 7412532) — done
- Can run **parallel to Phase 10b** — `IrrlichtRenderer.cpp` includes none of the headers
  being renamed in 10b's naming convention pass; no merge conflict risk
- Does **not** require Phase 9 artistic textures — placeholder DDS files from Phase 5 are
  sufficient; artistic replacements drop in without code changes
- Feature 5 (`IrrlichtUIBackend` colour pass) depends on Feature 4 (sprite sheet) being
  complete so that the correct active sprite cells exist before colour-override code is removed

---

## Out of Scope

- Normal map binding (`terrain_*_n.dds`) — terrain shader does not currently sample normal maps;
  add in a future polish phase
- Per-pixel lighting on terrain — deferred to Phase 11+
- Per-chunk splat map painting — `terrain_chunk_splat.png` is a single shared placeholder; per-chunk
  splat maps are a future terrain editing feature
- New `IUIBackend` interface methods — the Glass City rework is contained entirely within
  `IrrlichtUIBackend` and the sprite sheet; no interface changes required
- `ui_constants.h` hover/overlay ARGB values — tile hover highlights and zone overlays are
  gameplay colours, not UI chrome; unchanged by the Glass City direction
