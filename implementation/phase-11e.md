## Phase 11e: Building Atlas Expansion (2048 → 4096)

**Status: DONE**

### Goal

Expand the building facade atlas from 2048×2048 (4×4 grid, 16 cells) to 4096×4096 (8×8 grid,
64 cells at 512×512 px each), lifting the binding constraint that forced all four variants within
a zone-tier to share a single atlas cell. Each of the 36 zone-building variants and 4 service
buildings receives its own unique cell, enabling visual differentiation through both geometry
and texture.

---

### Deliverables

#### 1. Spec Update — `architecture/asset-standards/building-atlas-layout.md`

The primary purpose of the 8×8 expansion is to assign each of the 36 zone-building variants and
4 service buildings its own unique 512×512 atlas cell, so variants are differentiated by both
geometry and texture — no two variants within a zone-tier share a cell.

- [ ] Change atlas resolution from 2048×2048 to 4096×4096.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Change cell grid from 4×4 (16 cells) to 8×8 (64 cells); each cell remains 512×512 px,
  preserving texel density per cell.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Replace the "Variant sharing of wall cells (binding decision)" rule: variants within the
  same zone-tier MAY now use distinct atlas cells (one cell per variant) or may still share —
  the 64-cell capacity makes both strategies valid. The previous constraint that required all four
  variants to share one cell is lifted.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Document the new Cell Assignment Table covering all 40 assigned cells and reserved rows:
  - Row 0: res\_low\_01 (0,0), res\_low\_02 (0,1), res\_low\_03 (0,2), res\_low\_04 (0,3),
    res\_med\_01 (0,4), res\_med\_02 (0,5), res\_med\_03 (0,6), res\_med\_04 (0,7)
  - Row 1: res\_high\_01 (1,0), res\_high\_02 (1,1), res\_high\_03 (1,2), res\_high\_04 (1,3),
    com\_low\_01 (1,4), com\_low\_02 (1,5), com\_low\_03 (1,6), com\_low\_04 (1,7)
  - Row 2: com\_med\_01 (2,0), com\_med\_02 (2,1), com\_med\_03 (2,2), com\_med\_04 (2,3),
    com\_high\_01 (2,4), com\_high\_02 (2,5), com\_high\_03 (2,6), com\_high\_04 (2,7)
  - Row 3: ind\_low\_01 (3,0), ind\_low\_02 (3,1), ind\_low\_03 (3,2), ind\_low\_04 (3,3),
    ind\_med\_01 (3,4), ind\_med\_02 (3,5), ind\_med\_03 (3,6), ind\_med\_04 (3,7)
  - Row 4: ind\_high\_01 (4,0), ind\_high\_02 (4,1), ind\_high\_03 (4,2), ind\_high\_04 (4,3),
    svc\_fire\_station (4,4), svc\_police\_station (4,5), svc\_power\_plant (4,6),
    svc\_water\_tower (4,7)
  - Rows 5–7 (24 cells): RESERVED for Phase 12+ use
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Record `graphics-artist-2d-texture` sign-off on the new cell layout before this deliverable
  is checked off.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] (`graphics-dev-irrlicht`) Add a section to
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md` documenting the 4096 atlas
  runtime fallback strategy: (1) `TextureCache` detects `m_maxTextureSize < 4096` at load time;
  (2) loads `buildings_atlas_d_2k.dds` (the 2048×2048 fallback atlas) instead; (3) logs a
  `WARNING` to stderr; (4) the fallback asset naming convention is `<basename>_2k.dds`
  (e.g., `buildings_atlas_d_2k.dds`). This deliverable must be completed before texture authoring
  (Deliverable 5) begins.
  (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`,
  `architecture/asset-standards/2d-texture-standards.md`)
- [ ] Record `graphics-dev-irrlicht` sign-off confirming GL\_TEXTURE\_MAX\_LEVEL=4 is correct for
  a 4096-px atlas (5 mip levels: 4096, 2048, 1024, 512, 256) and that the net VRAM increase
  (~8 MB: 4096² DXT1 ≈ 10.7 MB with mipmaps vs 2048² DXT1 ≈ 2.7 MB) is within the 170 MB scene
  VRAM budget defined in Phase 12; and that the runtime `GL_MAX_TEXTURE_SIZE` guard and graceful
  2048×2048 fallback strategy are documented in
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md` before texture authoring begins.
  (ref: `architecture/asset-standards/building-atlas-layout.md`,
  `architecture/graphics-architecture/texture-cache.md`,
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md`)

#### 2. Spec Update — `architecture/asset-standards/2d-texture-standards.md`

- [ ] Update building facade atlas resolution from 2048×2048 to 4096×4096.
  (ref: `architecture/asset-standards/2d-texture-standards.md`)
- [ ] Update mip level count from 4 levels (GL\_TEXTURE\_MAX\_LEVEL=3) to 5 levels
  (GL\_TEXTURE\_MAX\_LEVEL=4).
  (ref: `architecture/asset-standards/2d-texture-standards.md`)

#### 3. Meta File Updates (40 files)

- [ ] Update all 36 zone-building `.meta` files
  (`assets/3d/buildings/<zone>_<tier>_<variant>.meta`) with new `atlas_cell` row/col values
  per the Cell Assignment Table in Deliverable 1.
  (ref: `architecture/asset-standards/building-atlas-layout.md`,
  `architecture/asset-standards/3d-model-standards.md`)
- [ ] Update all 4 service-building `.meta` files
  (`assets/3d/buildings/svc_fire_station.meta`, `svc_police_station.meta`,
  `svc_power_plant.meta`, `svc_water_tower.meta`) with new `atlas_cell` assignments
  (row 4, cols 4–7).
  (ref: `architecture/asset-standards/building-atlas-layout.md`)

#### 4. Generator Update — `tools/generate_b3d_models.py`

- [ ] Change the `atlas_uv(row, col, face_u, face_v)` divisor from `/4.0` to `/8.0` to match
  the new 8×8 grid.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Update `WALL_CELLS`, `ROOF_CELL`, and `BASE_CELL` dictionaries to use the new per-variant
  cell assignments from the 8×8 grid Cell Assignment Table. `WALL_CELLS` must become a per-variant
  dict mapping each `(zone, tier, variant_number)` tuple to its own `(row, col)` cell — not a
  shared cell per zone-tier. `variant_number` is the 1-based numeric suffix of the asset name
  (e.g., `res_low_01` → `variant_number=1`, `res_low_04` → `variant_number=4`).
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Regenerate all 102 `.b3d` files after the changes; verify all regenerated files pass
  `validate_assets.py`.
  (ref: `architecture/asset-standards/3d-model-standards.md`)
- [ ] `graphics-artist-3d-model` sign-off: per-variant UV assignments in the updated generator
  verified correct; all models visually distinct across zone-tiers after regeneration. Acceptance
  criteria: (1) each variant's LOD0 model passes `validate_assets.py` Check #4 (UV coordinates
  within assigned cell boundaries); (2) renders correctly in `aitown_model_validator` (no missing
  textures or black faces); (3) displays recognizable geometry differentiation from other variants
  in the same zone-tier when viewed at 12 m showcase spacing.

#### 5. Texture Rebuild — `assets/textures/buildings_atlas_d.dds`

- [ ] Rebuild `buildings_atlas_d.dds` at 4096×4096 DXT1 sRGB with 5 mip levels.
  (ref: `architecture/asset-standards/2d-texture-standards.md`,
  `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Author `buildings_atlas_d_2k.dds` at 2048×2048 DXT1 sRGB (the fallback atlas for GPUs
  where `GL_MAX_TEXTURE_SIZE < 4096`); this file must contain the same 40 cell assignments
  downscaled to 256×256 per cell. The `_2k.dds` naming convention is defined in
  Deliverable 1.
  (ref: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`,
  `architecture/asset-standards/2d-texture-standards.md`)
- [ ] Author unique 512×512 texture content for each of the 40 assigned cells: each variant's
  512×512 cell must contain unique texture content appropriate to that variant's architectural
  character (material, colour palette, facade detail) — variants within the same zone-tier must
  not share cell content. `graphics-artist-2d-texture` is responsible for authoring 36 unique
  zone-building cell textures (one per variant, covering all zone-tier combinations) plus 4
  service building cells.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Validate both DDS files against reference byte sizes (11,174,016 bytes for
  `buildings_atlas_d.dds`; 2,785,408 bytes for `buildings_atlas_d_2k.dds`) per
  `architecture/asset-standards/2d-texture-standards.md` — truncated files render silently black
  with no GL error; confirm file size before committing.
  (`graphics-artist-2d-texture`)
- [ ] `graphics-artist-2d-texture` sign-off: atlas content authored and 64-cell layout confirmed
  before this deliverable is checked off.

#### 6. TextureCache Update — `src/rendering/TextureCache.cpp`

- [ ] Update the `GL_TEXTURE_MAX_LEVEL` dispatch for `buildings_atlas_d.dds` from 3 to 4 (5 mip
  levels on a 4096-px atlas).
  (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] Update any hardcoded 2048-related atlas size references in `TextureCache.cpp`.
  (ref: `architecture/graphics-architecture/texture-cache.md`)
- [ ] Add C++ test coverage for the new `TextureCache` atlas-selection logic in
  `tests/rendering/texture_cache_test.cpp` (authored and owned by `graphics-dev-irrlicht`):
  Test case 1 — `loadSRGB()` selects `buildings_atlas_d_2k.dds` when `maxTextureSize < 4096`;
  Test case 2 — `GL_TEXTURE_MAX_LEVEL` is set to 3 for the 2k fallback atlas and 4 for the
  primary 4096 atlas; Test case 3 — Both atlases load without GL errors (integration test under
  `xvfb-run`, labeled `requires-opengl`). These tests ensure the fallback path operates correctly
  on GPUs without 4096-px texture support.
  (ref: `architecture/graphics-architecture/texture-cache.md`)

#### 7. validate\_assets.py Update

- [ ] Extend Check #4 (UV within atlas cell) with new UV cell boundary validation logic — this is
  a new implementation, not a formula update. Check #4 currently only validates file existence;
  it must be extended to parse UV coordinates from each `.b3d` building model and verify they
  fall within `[cell_col/8, (cell_col+1)/8] × [cell_row/8, (cell_row+1)/8]`, using the model's
  `atlas_cell` from its `.meta` file. This requires a B3D UV-coordinate reader in
  `validate_assets.py` and is a new addition on top of the existing file-existence check.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Remove the deferred/no-op path from Check #4 — the check must actively parse UV coordinates
  from every `.b3d` building model and fail non-zero for any model whose UV coordinates fall
  outside `[cell_col/8, (cell_col+1)/8] × [cell_row/8, (cell_row+1)/8]`. A CI run where
  Check #4 silently skips UV validation (e.g., by hitting a "B3D UV parsing deferred" branch and
  exiting 0 without performing any validation) is a CI failure.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] `validate_assets.py` is a Python CI tool; the B3D UV-coordinate reader is not subject to
  the C++ Google Test coverage gate. Correctness is verified by running
  `python tools/validate_assets.py` against all 102 building `.b3d` files and confirming exit
  code 0 with non-deferred Check #4 output. No separate Python unit test suite is required for
  this phase.
- [ ] Check #4 must also verify that the per-variant `atlas_cell` row/col in each `.meta` file
  matches the Cell Assignment Table in `building-atlas-layout.md`.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)

---

### Exit Criteria

- [ ] `irrlicht-device-lifecycle.md` updated with 4096 atlas GL\_MAX\_TEXTURE\_SIZE fallback
  strategy and `_2k.dds` naming convention documented (must be complete before Deliverable 5).
- [ ] `building-atlas-layout.md` updated: 8×8 grid, 64 cells, new Cell Assignment Table, old
  variant-sharing binding decision replaced.
- [ ] `2d-texture-standards.md` updated: 4096×4096 resolution, GL\_TEXTURE\_MAX\_LEVEL=4.
- [ ] All 40 `.meta` files updated with new `atlas_cell` assignments matching the Cell Assignment
  Table.
- [ ] `generate_b3d_models.py` updated: `atlas_uv` divisor changed from 4.0 to 8.0, `WALL_CELLS`
  updated to per-variant assignments; all 102 `.b3d` files regenerated and pass
  `validate_assets.py`.
- [ ] `buildings_atlas_d.dds` rebuilt at 4096×4096 DXT1 sRGB with 5 mip levels and per-variant
  texture cells; `buildings_atlas_d_2k.dds` authored at 2048×2048 DXT1 sRGB as the
  GL\_MAX\_TEXTURE\_SIZE fallback atlas.
- [ ] `buildings_atlas_d.dds` confirmed as 11,174,016 bytes (header + 5-level mip data) and
  `buildings_atlas_d_2k.dds` confirmed as 2,785,408 bytes (header + 4-level mip data) before
  committing; reference values from `architecture/asset-standards/2d-texture-standards.md` DDS
  Reference Byte-Size table. A truncated DDS with wrong byte count renders silently black — this
  check is mandatory before Deliverable 5 is marked complete.
- [ ] `TextureCache.cpp` updated: GL\_TEXTURE\_MAX\_LEVEL=4 for the buildings atlas, no remaining
  hardcoded 2048-px atlas size references.
- [ ] `TextureCache` atlas-selection C++ tests pass in CI: three test cases verify fallback atlas
  selection, GL\_TEXTURE\_MAX\_LEVEL dispatch (3 for 2k, 4 for 4k), and error-free loading of both
  atlases under `xvfb-run` with label `requires-opengl`. Tests located in
  `tests/rendering/texture_cache_test.cpp`.
- [ ] `validate_assets.py` Check #4 extended with UV cell boundary validation (new B3D UV-coordinate
  reader; boundaries `[cell_col/8, (cell_col+1)/8] × [cell_row/8, (cell_row+1)/8]`) and per-variant
  `.meta` assignment verification.
- [ ] `graphics-artist-2d-texture` sign-off: atlas content authored, 64-cell layout confirmed.
- [ ] `graphics-dev-irrlicht` sign-off: GL\_TEXTURE\_MAX\_LEVEL=4 correct for 4096-px atlas; net
  VRAM increase (~8 MB) confirmed within 170 MB scene VRAM budget; runtime `GL_MAX_TEXTURE_SIZE`
  guard and graceful 2048×2048 fallback strategy documented in
  `architecture/graphics-architecture/irrlicht-device-lifecycle.md`.
- [ ] `graphics-artist-3d-model` sign-off: per-variant UV assignments in generator verified
  correct; all models visually distinct across zone-tiers.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` exits zero.

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-artist-2d-texture` | Author unique 512×512 texture cells for all 40 assigned atlas positions; sign off on cell layout and DDS export |
| `graphics-artist-3d-model` | Verify per-variant UV assignments in `generate_b3d_models.py`; sign off after regeneration that all models are visually distinct |
| `graphics-dev-irrlicht` | Add fallback strategy section to `irrlicht-device-lifecycle.md` (must complete before Deliverable 5); update `TextureCache.cpp` GL\_TEXTURE\_MAX\_LEVEL dispatch; sign off on VRAM budget impact |
| `cicd-dev-github` | Verify `validate_assets.py` Check #4 extension (new UV cell boundary validation) passes in CI; confirm `validate-assets` job exits zero after atlas and `.meta` changes; verify via CI log that Check #4 reports per-model UV validation results (not a skip/deferred message) before marking this phase complete |

---

### Dependencies

- Requires Phase 11d complete: all 36 zone-building models exist at final LOD0 polygon budgets
  and all 40 `.meta` files are present before this phase updates their `atlas_cell` fields.
- Phase 12 must not begin until this phase's exit criteria are fully met; the VRAM budget
  re-verification in Phase 12 must use the 4096×4096 atlas footprint (~10.7 MB with mipmaps).

---

### Risks & Spikes

- **RISK (RESOLVED)**: 4096×4096 DXT1 atlas requires ~8 MB more VRAM than the 2048×2048
  predecessor, pushing total scene VRAM consumption closer to the Phase 12 budget ceiling.
  **Resolution**: Phase 12 ceiling raised to ≤180 MB (from 170 MB) in
  `implementation/phase-12.md` and `architecture/asset-standards/2d-texture-standards.md`
  §Scene VRAM Budget. Actual worst-case sum with primary 4096 atlas ≈166.5 MB, leaving
  ~13.5 MB headroom — equivalent to the pre-11e margin. No VRAM profiling spike needed.
- **RISK (RESOLVED)**: Some target GPUs may not support 4096-px textures
  (`GL_MAX_TEXTURE_SIZE < 4096`).
  **Resolution**: `architecture/graphics-architecture/irrlicht-device-lifecycle.md`
  §Building Atlas Resolution Fallback fully documents the detection logic
  (`m_maxTextureSize` queried in `RenderSystem`), fallback asset
  (`buildings_atlas_d_2k.dds`), `GL_TEXTURE_MAX_LEVEL` dispatch (4 for primary, 3 for
  fallback), and stderr warning. No further spec work required.
- **RISK (RESOLVED)**: Regenerating all 102 `.b3d` files in one pass may surface UV
  precision errors only visible at the new 8×8 scale.
  **Resolution**: `validate_assets.py` Check #13 validates UV range compliance within
  each 512×512 cell (8–504 px usable area); the single-model validation gate
  (`res_low_01_lod0.b3d` with updated Check #4 boundary 0.125) is a mandatory step in
  Deliverable 2 before the full batch regeneration. No spec gap remains.
