## Phase 11f: Building Lot Ground Features (Gardens, Pools, Paving)

### Goal

Add lot-level ground features to every building model so buildings sit on a
recognisable footprint rather than bare terrain. Residential buildings gain garden
patches and optional pools; commercial buildings gain paved forecourts; industrial
buildings gain tarmac lots; service buildings gain contextual ground surfaces. Ground
features are flat quads embedded in each B3D mesh at y = 0.01 (1 cm above terrain,
preventing depth-buffer conflict with the terrain mesh), UV-mapped to new
ground-feature atlas cells allocated from the currently-reserved rows 5–7 of the
8×8 building atlas.

---

### Deliverables

#### 1. Spec Update — `architecture/asset-standards/building-atlas-layout.md`

Allocate five ground-feature cells from row 5 (currently RESERVED). Row 5 col 0
is already assigned as `ROOF_CELL` in `generate_b3d_models.py` and documented in
`building-atlas-layout.md`; cols 1–5 are assigned as follows.

- [ ] Add a **Ground Feature Cells** section to the Cell Assignment Table:
  - `(5, 1)` — `ground_garden`: mid-green grass / garden patch
  - `(5, 2)` — `ground_pool`: blue/aqua pool water
  - `(5, 3)` — `ground_paving`: light-grey concrete forecourt
  - `(5, 4)` — `ground_tarmac`: dark-grey asphalt (industrial lots)
  - `(5, 5)` — `ground_gravel`: beige gravel / service yard
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Update the RESERVED note: rows 5–7 are partially allocated — row 5 cols 0–5
  are now assigned; cols 6–7 and rows 6–7 remain RESERVED for Phase 12+ use.
  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Record `graphics-artist-2d-texture` sign-off on the five new cell assignments.

#### 2. Atlas Texture Authoring — `assets/textures/buildings/buildings_atlas_d.png`

- [ ] Paint cell `(5, 1)` `ground_garden`: solid mid-green (≈ RGB 80,130,60) with
  subtle grass-blade noise pattern; 496×496 px usable area (8 px border).
  (`graphics-artist-2d-texture`)
- [ ] Paint cell `(5, 2)` `ground_pool`: solid pool-blue (≈ RGB 70,160,200) with
  faint tile-grid lines; 496×496 px usable area.
  (`graphics-artist-2d-texture`)
- [ ] Paint cell `(5, 3)` `ground_paving`: light grey concrete (≈ RGB 190,185,178)
  with faint joint lines; 496×496 px usable area.
  (`graphics-artist-2d-texture`)
- [ ] Paint cell `(5, 4)` `ground_tarmac`: dark asphalt (≈ RGB 55,55,58) with
  subtle aggregate texture; 496×496 px usable area.
  (`graphics-artist-2d-texture`)
- [ ] Paint cell `(5, 5)` `ground_gravel`: beige/tan gravel (≈ RGB 180,165,130)
  with irregular speckle noise; 496×496 px usable area.
  (`graphics-artist-2d-texture`)
- [ ] Regenerate `buildings_atlas_d.png` with all five new cells painted and all
  previously-assigned cells (rows 0–4 + row 5 col 0) unchanged.
  (`graphics-artist-2d-texture`)

#### 3. Generator Update — `tools/generate_b3d_models.py`

- [ ] Add a `GROUND_CELLS` constant dict mapping ground-feature type strings to
  `(row, col)` tuples:

  ```python
  GROUND_CELLS = {
      "garden":  (5, 1),
      "pool":    (5, 2),
      "paving":  (5, 3),
      "tarmac":  (5, 4),
      "gravel":  (5, 5),
  }
  ```

  (ref: `architecture/asset-standards/building-atlas-layout.md`)
- [ ] Add a `_add_ground_quad(m, gtype, xmin, xmax, zmin, zmax)` helper that emits
  a single upward-facing flat quad (normal `(0,1,0)`) at `y = 0.01` (1 cm above
  terrain, preventing depth-buffer conflict with the terrain mesh at y = 0) using
  `GROUND_CELLS[gtype]`, covering the given XZ extents. After adding the quad,
  recalculate the mesh bounding box (`m.recalculate_bounding_box()` or equivalent)
  so the extended footprint is included in frustum culling.
- [ ] Apply ground quads to every building variant using the rules below.
  All quads extend the building's footprint by `1*S` on each side (so the ground
  patch is slightly larger than the building base and is visible from any camera
  angle):

  **Residential low** — `ground_garden` patch at full footprint + 1 S margin.\
  **Residential med** — `ground_garden` patch at full footprint + 1 S margin.\
  **Residential high — variants 01, 03** — `ground_garden` rear courtyard
  (full footprint + 1 S margin); variant 01 also adds a `ground_pool` quad
  (4 S × 4 S, centred, at y = 0.01, in front of the building).\
  **Residential high — variants 02, 04** — `ground_paving` front forecourt
  (full footprint + 1 S margin).\
  **Commercial low** — `ground_paving` forecourt at full footprint + 1 S margin.\
  **Commercial med** — `ground_paving` forecourt at full footprint + 1 S margin.\
  **Commercial high** — `ground_paving` plaza at full footprint + 2 S margin;\
  variant 04 also adds a `ground_pool` quad (5 S × 5 S) centred in front.\
  **Industrial low** — `ground_tarmac` lot at full footprint + 1 S margin.\
  **Industrial med** — `ground_tarmac` lot at full footprint + 2 S margin.\
  **Industrial high** — `ground_tarmac` lot at full footprint + 2 S margin.\
  **svc\_fire\_station** — `ground_paving` lot at full footprint + 1 S margin.\
  **svc\_police\_station** — `ground_paving` lot at full footprint + 1 S margin.\
  **svc\_power\_plant** — `ground_gravel` lot at full footprint + 2 S margin.\
  **svc\_water\_tower** — `ground_gravel` base pad (8 S × 8 S, centred).

  LOD1 and LOD2 variants receive the same ground quad as LOD0 (same footprint,
  same ground type). LOD2 for high-density buildings (where LOD2 is a single
  box) still receives a ground quad.
- [ ] Update `tools/validate_assets.py` to accept UV coordinates mapping to ground
  feature cells `(5,1)`–`(5,5)` and `ROOF_CELL (5,0)` in addition to the 40
  building variant cells in rows 0–4. Without this update the validator will
  reject regenerated B3D files that contain ground quad UVs.
  (`cicd-dev-github`)
- [ ] Update `tools/validate_assets.py` to verify that each regenerated B3D
  bounding box encompasses the ground quad vertices: bounding box XZ extent must
  be at least as large as the expected footprint and margin for each building type
  (derive expected extents from the footprint constants already defined in
  `generate_b3d_models.py`); bounding box Y range must include `y = 0.01`
  (i.e., `min.Y ≤ 0` and `max.Y ≥ 0.01`). This ensures silent frustum-culling
  failures from missing bounding box recalculation are caught in CI.
  (`cicd-dev-github`)
- [ ] Regenerate all 102 `.b3d` files; verify all pass `python tools/validate_assets.py`.
  (`graphics-artist-3d-model`)
- [ ] `graphics-artist-3d-model` sign-off: ground quads visible in
  `aitown_model_validator` screenshots for all 10 building categories; no
  z-fighting between ground quad (at y = 0.01) and terrain mesh.

#### 4. Model Validator Visual Confirmation

- [ ] Run `aitown_model_validator` screenshot sweep across all 10 categories and
  confirm ground patches are visible and correctly coloured in every category.
  Reference screenshots stored under `assets/3d/buildings/screenshots/` (one PNG
  per category, named `<category>_ground_validated.png`).
  (`graphics-artist-3d-model`)

---

### Exit Criteria

- [ ] `building-atlas-layout.md` updated: five ground-feature cells `(5,1)`–`(5,5)`
  assigned; RESERVED note updated (row 5 cols 6–7 + rows 6–7 remain reserved).
- [ ] `buildings_atlas_d.png` rebuilt with all five new ground-feature cells painted;
  all previously-assigned cells (rows 0–4 + row 5 col 0) unchanged.
- [ ] `generate_b3d_models.py` updated: `GROUND_CELLS` dict present; `_add_ground_quad()`
  helper present; all ground quads placed at `y = 0.01` (not `y = 0`) to prevent
  depth-buffer conflict with terrain mesh; bounding box recalculated after each quad
  addition; ground quads applied to all 15 building types (12 zone types +
  3 svc variants that share a type each); all 102 `.b3d` files regenerated.
- [ ] `tools/validate_assets.py` updated: accepts UV cells `(5,0)`–`(5,5)` (ROOF_CELL
  and five ground feature cells); rejects any other row-5–7 UV that is not yet assigned;
  verifies each B3D bounding box XZ extent (derived from `generate_b3d_models.py`
  footprint constants) is at least as large as the expected footprint and margin for
  that building type; verifies bounding box Y range satisfies `min.Y ≤ 0`
  and `max.Y ≥ 0.01`.
- [ ] `python tools/validate_assets.py` exits zero after regeneration.
- [ ] `aitown_model_validator` screenshot confirms ground features visible in all
  10 building categories; reference PNGs committed under
  `assets/3d/buildings/screenshots/`.
- [ ] `graphics-artist-2d-texture` sign-off: five ground-feature cells authored and
  atlas rebuilt.
- [ ] `graphics-artist-3d-model` sign-off: ground quads applied, visible, no
  z-fighting (confirmed by placement at y = 0.01 above terrain mesh).
- [ ] `graphics-dev-irrlicht` sign-off: no depth-buffer artifact between ground
  quads (y = 0.01) and terrain mesh (y = 0) observed in `aitown_model_validator`.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
  exits zero.

---

### Team

| Role | Responsibility |
|---|---|
| `graphics-artist-2d-texture` | Author five ground-feature cells in the atlas; sign off on cell content |
| `graphics-artist-3d-model` | Add `GROUND_CELLS` dict + `_add_ground_quad()` helper + apply quads to all variants; regenerate 102 B3D files; sign off on visual result |
| `graphics-dev-irrlicht` | Confirm no z-fighting at y = 0 between building ground quads and terrain mesh |
| `cicd-dev-github` | Update `validate_assets.py` to accept ground feature cells (5,1)–(5,5) and ROOF_CELL (5,0); add bounding-box extent check; verify `validate-assets` CI job exits zero after atlas and B3D regeneration |

---

### Dependencies

- Requires Phase 11e complete: 8×8 atlas with 5-mip DDS pipeline and per-variant
  UV assignments must be in place before new ground-feature cells are added to row 5.
- Phase 12 VRAM budget re-check: ground quads add a small number of triangles per
  building (2 triangles per quad, ≤3 quads per variant) — negligible VRAM impact;
  no budget revision required.
