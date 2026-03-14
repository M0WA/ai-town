# Phase 10c: Terrain Texture Wiring & Glass City UI Rework — **DONE**

## Goal

Two parallel workstreams:

1. **Terrain texture wiring** — connect `terrain_*_d.dds` / `terrain_chunk_splat.png` into the
   rendering pipeline. The infrastructure is fully built (shaders, `TextureCache`,
   `TerrainShaderCallback`) but never connected to `IrrlichtRenderer`. After this phase, terrain
   chunks render with the splat-blended multi-layer shader instead of height-based vertex colours.

2. **Glass City UI visual rework** — regenerate the HUD sprite sheet and update
   `IrrlichtUIBackend` to match the approved Glass City visual direction: deep navy panels,
   electric teal active state with baked glow, amber numeric values, outlined-inactive /
   filled-active icon system. Glass City direction established in commit 7412532; hover icon
   state (85% opacity, outlined stroke) subsequently formalised in this phase's spec pass and
   recorded in `architecture/ui-ux/resolution-ui-scaling.md` §Icon States and
   `architecture/asset-standards/2d-texture-standards.md` §UI Sprite Sheet Art Style — Glass City.

---

## Deliverables

### Feature 1: `initTerrainShader()` in IrrlichtRenderer

**Owner**: `graphics-dev-irrlicht`

Mirrors `initRoadShader()` (`src/rendering/IrrlichtRenderer.cpp` line 1252). New private method
`IrrlichtRenderer::initTerrainShader()` called from the constructor after all other init steps.

- [x] Forward-declare `class RenderSystem;` in `IrrlichtRenderer.h` (alongside the existing
  `class TextureCache;` forward declaration line)
- [x] Add `RenderSystem* m_renderSystem{nullptr}` private member to `IrrlichtRenderer`
  (non-owning observer pointer — follows the `ITerrainQuery* m_terrain` pattern)
- [x] Add `setRenderSystem(RenderSystem* rs) { m_renderSystem = rs; }` public setter to
  `IrrlichtRenderer.h` (follows the `setTerrainQuery()` late-bind pattern at line 65;
  called from `main.cpp` after `IrrlichtRenderer` is constructed and before
  `terrainSystem.buildAllChunks()`)
- [x] In `main.cpp`: after `IrrlichtRenderer renderer(device, nullptr);`, add
  `renderer.setRenderSystem(&renderSystem);` (wires the `RenderSystem` that was created at
  line 41 of `main.cpp` into the renderer before terrain chunks are built)
- [x] Add `std::unique_ptr<TextureCache> m_terrainTextureCache` private member to `IrrlichtRenderer`
  (follows the `m_roadTextureCache` pattern at line 325 of `IrrlichtRenderer.h`; destroyed with
  `IrrlichtRenderer` — no per-chunk `releaseSRGB()` needed since textures are global to the renderer)
- [x] Add `int m_terrainMaterialType` member to `IrrlichtRenderer` (initialised to `-1`)
- [x] Implement `initTerrainShader()`:
  - EDT_NULL early-return guard as first line (matches `initRoadShader()` pattern)
  - Lazily create `m_terrainTextureCache` at the start of `initTerrainShader()` (mirrors
    `initRoadShader()` lines 1265–1270: `m_terrainTextureCache = std::make_unique<TextureCache>(
    m_driver->getDriverType(), m_driver, m_device ? m_device->getFileSystem() : nullptr)`)
  - Construct absolute paths using `AITOWN_ASSETS_DIR` macro (mirrors `initRoadShader()` line 1292):
    `std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_grass_d.dds"` etc.
  - Load 4 diffuse textures via `m_terrainTextureCache->loadSRGB(path, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)`
    using the absolute paths above (grass, asphalt, soil, concrete)
  - Load splat map via `m_terrainTextureCache->loadSplatMap(std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_chunk_splat.png")`
  - Construct `TerrainShaderCallback* cb = new TerrainShaderCallback(...)` with `m_renderSystem`,
    `m_terrainTextureCache.get()`, splat path, and `std::array<std::string,4>` of detail paths in splat channel
    order — index must be exactly (R=grass, G=asphalt, B=soil, A=concrete):
    `{{std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_grass_d.dds",`
    `std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_asphalt_d.dds",`
    `std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_soil_d.dds",`
    `std::string(AITOWN_ASSETS_DIR) + "/textures/terrain/terrain_concrete_d.dds"}}`
  - Build absolute shader paths: `const std::string vsPath = std::string(AITOWN_ASSETS_DIR) + "/shaders/terrain.vert";`
    and `fsPath` (analogous to `initRoadShader()` lines 1310–1311)
  - Get GPU services: `IGPUProgrammingServices* gpu = m_driver->getGPUProgrammingServices();` — return
    early (set `m_terrainMaterialType = -1`) if null (EDT_NULL guard matching `initRoadShader()` line 1302–1308)
  - Load shader via `gpu->addHighLevelShaderMaterialFromFiles(vsPath.c_str(), "main", EVST_VS_1_1,`
    `fsPath.c_str(), "main", EPST_PS_1_1, cb, EMT_SOLID)` → store result in `m_terrainMaterialType`
  - On failure (`m_terrainMaterialType == -1`): log warning (matches `initRoadShader()` pattern)
  - Call `cb->drop()` unconditionally after `addHighLevelShaderMaterialFromFiles()` returns —
    road callback pattern (line 1320 in `initRoadShader()`): `new` → Irrlicht grabs internally
    → `drop()` releases caller's reference; Irrlicht owns the remaining reference
  - No `m_terrainCallback` member needed — the callback requires no per-frame access
- [x] Add test-only accessors to `IrrlichtRenderer.h` (follows the `cloudNodeForTest()` pattern
  at line 168):
  - `int terrainMaterialTypeForTest() const { return m_terrainMaterialType; }` — read accessor
    (used by the unit test to verify `m_terrainMaterialType == -1` under EDT_NULL)
  - `void setTerrainMaterialTypeForTest(int t) { m_terrainMaterialType = t; }` — write accessor
    (used by the integration test to inject a sentinel value without a real GL shader load)
- [x] Call `initTerrainShader()` from `setRenderSystem()` after assigning `m_renderSystem = rs` —
  ensures `m_renderSystem` is non-null when `TerrainShaderCallback` is constructed (do **not**
  call from the constructor; `setRenderSystem()` is invoked from `main.cpp` after construction)
- [x] Add CI preflight check in `.github/workflows/ci.yml` (all three jobs) to hard-fail if any
  terrain texture or terrain shader asset is absent. Check all seven files:
  `assets/textures/terrain/terrain_grass_d.dds`,
  `assets/textures/terrain/terrain_asphalt_d.dds`,
  `assets/textures/terrain/terrain_soil_d.dds`,
  `assets/textures/terrain/terrain_concrete_d.dds`,
  `assets/textures/terrain/terrain_chunk_splat.png`,
  `assets/shaders/terrain.vert`, and
  `assets/shaders/terrain.frag`. Insertion point per job:
  - `build-linux` and `coverage-linux`: insert **after** the existing "Verify UI shader assets"
    step (`ci.yml` lines 293 and 892 respectively); use bash
    `test -f ... || { echo "ERROR:..."; exit 1; }` pattern for all seven files.
  - `build-windows`: insert **after** the existing "Verify Phase 10 font assets present" step
    (`ci.yml` line 576); `build-windows` has no "Verify UI shader assets" step. Use
    PS 5.1-compatible syntax `if (-not (Test-Path 'assets/textures/terrain/terrain_grass_d.dds'))
    { exit 1 }` etc. for all seven files (NOT `Test-Path ... || exit 1` which is PS 7+ only —
    see CLAUDE.md "CI PowerShell" note).

**Reference**: `initRoadShader()` at `src/rendering/IrrlichtRenderer.cpp:1252`; `TerrainShaderCallback`
constructor at `src/rendering/TerrainShaderCallback.h`; splat channel order locked in
`architecture/asset-standards/2d-texture-standards.md` §Splat map channel-to-material assignment.

---

### Feature 2: Assign material type in `rebuildTerrainChunk()`

**Owner**: `graphics-dev-irrlicht`

The insertion point is inside `rebuildTerrainChunk()`, in the `if (newNode) {...}` block (line ~460),
after the existing `setMaterialFlag()` calls (lines 463–464) and before the node is registered in
`m_chunkNodes` (line 469). The "Phase 6+ terrain texturing" comment at line ~296 is in the node
eviction path (teardown), not in `rebuildTerrainChunk()` — do **not** use that as the insertion point.

- [x] After `addMeshSceneNode()` returns `newNode`, assign the terrain shader material:

  ```cpp
  irr::video::SMaterial& mat = newNode->getMaterial(0);
  if (m_terrainMaterialType != -1) {
      mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(m_terrainMaterialType);
  }
  // EMF_LIGHTING stays false — per-pixel lighting is Phase 11+
  mat.setFlag(irr::video::EMF_LIGHTING, false);
  mat.setFlag(irr::video::EMF_BACK_FACE_CULLING, false);
  ```

- [x] Update the comment at line 463 (`// unlit until Phase 6 lighting pass`) to reference
  Phase 10c: `// material type set in Phase 10c — see initTerrainShader()`
- [x] No changes to vertex colour generation — vertex colours remain in the mesh; the terrain
  shader ignores them (splat weights drive blending). They serve as a fallback when
  `m_terrainMaterialType == -1`.

---

### Feature 3: Tests

**Owner**: `test-dev-cpp`

- [x] **`TerrainShaderWiring_EDT_NULL_InitDoesNotCrash`** — integration test in
  `tests/integration/terrain_shader_wiring_test.cpp` (label `integration`, target `integration_tests`
  via `target_sources`). Constructs `IrrlichtRenderer` with EDT_NULL device; verifies constructor
  completes without crash and `m_terrainMaterialType == -1` (no GL calls possible under EDT_NULL).
  Per `architecture/testing/framework.md`, any test that constructs an EDT_NULL `IrrlichtRenderer`
  requires Irrlicht and is therefore `integration`, not `unit`.
- [x] **`TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded`** — integration test in
  `tests/integration/terrain_shader_wiring_test.cpp` (label `integration`, target `integration_tests`).
  Uses a **real `IrrlichtRenderer` with EDT_NULL device** (consistent with project integration test
  policy of testing against real objects); calls `setTerrainMaterialTypeForTest(999)` to inject
  a sentinel value, then calls `rebuildTerrainChunk()`; verifies the scene node's material type
  is set to `999` (non-`EMT_SOLID`) via `getMaterial(0).MaterialType`.
- [x] Add `tests/integration/terrain_shader_wiring_test.cpp` to `integration_tests` via
  `target_sources(integration_tests PRIVATE ...)` (do NOT call `add_executable` again); this
  single file contains both `TerrainShaderWiring_EDT_NULL_InitDoesNotCrash` and
  `TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded`

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

Every icon that has an **inactive** and **active** variant must be regenerated as three distinct
cell styles (inactive, hover, and active):

| State | Icon fill | Opacity | Border | Glow |
|---|---|---|---|---|
| Inactive | 2 px outlined stroke only, hollow fill | 65% (near-white `#EBF4F6` stroke on transparent cell — alpha = 165) | None | None |
| Hover | 2 px outlined stroke | 85% | 1 px `rgba(255,255,255,0.35)` | None |
| Active | Filled solid silhouette | 100% | 2 px `rgba(0,201,200,0.75)` | 4 px Gaussian blur glow baked into cell |

The Hover icon state (85% opacity, outlined stroke) is now formally specified in `architecture/ui-ux/resolution-ui-scaling.md` §Visual Design — Glass City.

The glow in the active cell is baked at the source working resolution (256×256 per cell) via a
4 px Gaussian blur in the role-specific glow colour (per the Active Tint / Glow Colour by Button
Role table — see reference below) on a transparent layer, then Lanczos-downsampled to 64×64.
The 2 px border is always teal `rgba(0,201,200,0.75)` regardless of role; the glow colour varies.

**Glow colour is role-specific** — the 2 px border is always teal `rgba(0,201,200,0.75)` on all
active cells, but the baked glow colour varies by button role. See
`architecture/asset-standards/2d-texture-standards.md` §Active Tint / Glow Colour by Button Role
for the full mapping (e.g. Zone Residential = green, Zone Commercial = blue, Zone Industrial =
yellow, Demolish = red `#F04E37`, all other tools = teal `#00C9C8`).

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
| Grace period indicator background tile | `rgb(13,27,42)` | 78% | 8 px all edges |
| Zone / Utilities sub-panel background tile | `rgb(13,27,42)` | 80% | 8 px inner edges |
| Left toolbar / minimap legend / normal toast tile | `rgb(13,27,42)` | 82% | 8 px inner edges |
| Inspector / minimap map bg / tax rate / detail tile | `rgb(13,27,42)` | 85% | 8 px inner edges |
| Resource bar / modal / CRITICAL toast tile | `rgb(13,27,42)` | 88% | 0 px (resource bar) / 8 px (modal) |

These single-colour tiled cells let `IrrlichtUIBackend` draw navy panel backgrounds via
`IGUIButton::setImage()` without any new API methods.

#### Deliverables

- [x] Regenerate `assets/textures/ui/hud_sprites_ui.png` with all inactive icon cells as
  outlined-stroke / hover cells at 85% / active cells as filled + teal border + baked glow
- [x] Add panel-background cells to rows 16+ of the sprite sheet; update
  `assets/textures/ui/hud_sprites_ui_layout.json` with the new cell rects
- [x] Update `src/ui/hud_sprite_ids.h` with five background-cell constants for the new panel
  tiles: `kSpritePanelGracePeriod` (78%), `kSpritePanelSubPanel` (80%),
  `kSpritePanelToolbar` (82%), `kSpritePanelDetail` (85%), `kSpritePanelResourceBar` (88%)
  (follow existing `constexpr uint32_t` pattern; add after the last existing constant)
- [x] Add `kSpriteXxxHover` constants to `src/ui/hud_sprite_ids.h` for every icon that has an
  inactive/active pair — one hover constant per icon, named by convention `kSprite<Name>Hover`
  (e.g. `kSpriteToolZoneHover`, `kSpriteZoneResLowHover`, `kSpriteUtilPowerHover`); value = the sprite cell ID
  of the corresponding hover cell baked into `hud_sprites_ui.png`
- [x] Update `tools/generate_hud_sprites.py` to produce the outlined-inactive / outlined-hover /
  filled-active three-state style for all future regenerations — no manual hand-editing of the PNG
- [x] Verify final PNG is still 2048×2048 RGBA (Check #23 must stay green)

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
| Resource bar / modal / CRITICAL toast background | `SColor(140,230,230,230)` (milky white) | `SColor(224,13,27,42)` (dark navy 88%) |
| Left toolbar / minimap legend / normal toast background | `SColor(100,220,220,220)` | `SColor(209,13,27,42)` (dark navy 82%) |
| Zone / Utilities sub-panel background | `SColor(100,220,220,220)` | `SColor(204,13,27,42)` (dark navy 80%) |
| Inspector / minimap map bg / tax rate / detail background | `SColor(230,20,20,20)` | `SColor(217,13,27,42)` (dark navy 85%) |
| Grace period indicator background | `SColor(100,220,220,220)` | `SColor(199,13,27,42)` (dark navy 78%) |
| HUD numeric text (treasury, pop, date) | white / default | `SColor(255,240,180,41)` amber `#F0B429` |
| HUD label text (static labels) | default | `SColor(255,235,244,246)` near-white `#EBF4F6` |
| HUD secondary labels | default | `SColor(255,74,127,165)` mid-blue `#4A7FA5` |
| Deficit / error text | red (existing) | `SColor(255,240,78,55)` `#F04E37` |
| Warning / grace-period text | default | `SColor(255,232,150,12)` amber `#E8960C` |
| Active tint (button highlight) | `SColor(35,0,200,220)` alpha-35 | removed — active state is in sprite, not overlay |

The `SColor(A,R,G,B)` constructor takes A=0–255. Convert the `rgba(R,G,B,A_0–1)` values
above: multiply A_0–1 by 255.

#### Specific method changes

- [x] `IrrlichtUIBackend` constructor: change all panel `setBackgroundColor()` calls to dark
  navy values per table above
- [x] `IrrlichtUIBackend::addStaticText()`: set default text color to near-white `#EBF4F6`
  on the created `IGUIStaticText` (via `->setOverrideColor(SColor(255,235,244,246))`)
- [x] All HUD numeric value elements (treasury balance, population, date/time): set override
  color to amber `#F0B429` (`SColor(255,240,180,41)`) at creation time or in the relevant
  `setElementText()` path
- [x] All sub-label elements (secondary info text): set override color to mid-blue `#4A7FA5`
- [x] Remove or replace any code that applies a translucent active-tint overlay via `SColor`
  with alpha-35 — the active state is now expressed entirely through the sprite (Feature 4);
  no programmatic color overlay needed on activation
- [x] Implement hover state switching in `IrrlichtUIBackend`: on `IGUIElement::OnEvent`
  `EGET_ELEMENT_HOVERED` / `EGET_ELEMENT_LEFT`, swap the button image to the corresponding
  `kSpriteXxxHover` / `kSpriteXxx` (inactive) cell using `IGUIButton::setImage()` — no new
  `IUIBackend` interface methods required (hover switching is internal to the Irrlicht backend)
  - Before swapping images on `EGET_ELEMENT_HOVERED`, check `btn->isPressed()`; skip the image
    swap if the button is active — the active sprite must persist even while hovering; on
    `EGET_ELEMENT_LEFT`, check `btn->isPressed()`; if true, restore the active sprite (active
    visual must persist); if false, restore the inactive `kSpriteXxx` cell
- [x] `IrrlichtUIBackend`: load `hud_font.xml` (not the old `ui_font.xml` path which no longer
  exists); this was already fixed in commit 1100241 — verify no regression

---

## Exit Criteria

### Terrain texture wiring

- Terrain chunks render with the splat-blended multi-layer shader; grass/asphalt/soil/concrete
  material zones are visually distinct in a real OpenGL window
- `m_terrainMaterialType != -1` after `setRenderSystem()` is called with a valid `RenderSystem*` on a real GL context
- `TerrainShaderWiring_EDT_NULL_InitDoesNotCrash` passes (label `integration`)
- `TerrainChunk_RebuildAssignsMaterialType_WhenShaderLoaded` passes (label `integration`)
- No regression in existing terrain unit tests or `TerrainSystem_FlushPendingRebuilds_*` tests
- CI green on Linux and Windows (terrain shader not loaded on Windows CI headless path — EDT_NULL
  guard must ensure no raw GL calls)
- CI preflight checks pass for all 5 terrain texture assets and 2 terrain shader assets
  (`assets/shaders/terrain.vert`, `assets/shaders/terrain.frag`) in all three jobs
  (`build-linux`, `coverage-linux`, `build-windows`)
- Texture paths in `initTerrainShader()` use `assets/textures/terrain/` (not the old `assets/terrain/`)

### Glass City UI rework

- All toolbar, zone, and utilities button inactive states render as 2 px outlined stroke icons
  at 65% opacity; active state renders as filled solid with visible teal border and glow
- Panels (resource bar, toolbar, sub-panels, inspector, minimap, modal) render with dark navy
  background — no milky white semi-transparent surface visible
- All numeric HUD values (treasury balance, population count, demand values) render in amber
  `#F0B429`; labels render in near-white `#EBF4F6`
- `hud_sprites_ui.png` remains 2048×2048 RGBA (Check #23 stays green)
- `hud_sprite_ids.h` contains `kSpritePanelGracePeriod`, `kSpritePanelSubPanel`,
  `kSpritePanelToolbar`, `kSpritePanelDetail`, `kSpritePanelResourceBar` constants;
  `hud_sprites_ui_layout.json` updated
- `tools/generate_hud_sprites.py` produces the outlined-inactive / outlined-hover / filled-active
  three-state style on regeneration
- `hud_sprite_ids.h` contains `kSpriteXxxHover` constants for every icon with an inactive/active
  pair; hovering a button swaps its image to the hover cell and restores on mouse-leave
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
- Feature 5 (`IrrlichtUIBackend` colour pass) **blocks on Feature 4 complete**: Feature 5
  hover switching (lines 288–291) requires `kSpriteXxxHover` constants from `hud_sprite_ids.h`
  (Feature 4 deliverable, lines 232–235) and the active sprite cells to exist before
  colour-override code is removed; Feature 5 must not start until Feature 4 delivers all
  `kSpriteXxxHover` constants to `hud_sprite_ids.h`

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
