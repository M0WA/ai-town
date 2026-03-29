# 3D Model Artist Spec Review — Asset Standards and Graphics Architecture

**Reviewer**: Senior 3D Model Artist (graphics-artist-3d-model)
**Date**: 2026-03-29
**Scope**: All files in `architecture/asset-standards/` and `architecture/graphics-architecture/`
**Purpose**: Gap analysis, consistency check, inconsistency and duplication audit — read-only review, no changes made.

---

## Files Reviewed

- `architecture/asset-standards/3d-model-standards.md`
- `architecture/asset-standards/2d-texture-standards.md`
- `architecture/asset-standards/building-atlas-layout.md`
- `architecture/graphics-architecture/scene-graph-ownership.md`
- `architecture/graphics-architecture/model-validator-tool.md`
- `architecture/graphics-architecture/texture-cache.md`
- `architecture/graphics-architecture/procedural-terrain.md`
- `architecture/graphics-architecture/shader-loading.md`
- `architecture/graphics-architecture/irrlicht-device-lifecycle.md`
- `architecture/graphics-architecture/sky-clouds.md`
- `architecture/graphics-architecture/benchmark-tool.md`

---

## Issue Index

| # | Severity | Type | File(s) | Short Description |
|---|---|---|---|---|
| 1 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | V1 minimum coverage: 18 sets stated in old sign-off but current spec requires 36 |
| 2 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | `kTileSize` stated as 4.0 in sign-off comment but 10.0 in the binding spec |
| 3 | CRITICAL | INCONSISTENCY | 3d-model-standards.md | LOD2 geometry shell tri budget conflict: 300–500 vs 400–600 |
| 4 | HIGH | INCONSISTENCY | 3d-model-standards.md, model-validator-tool.md | Model validator tile boundary overlay uses 10 m but sign-off records 4 m kTileSize |
| 5 | HIGH | INCONSISTENCY | 3d-model-standards.md | Sign-off records car LOD0 <= 1500 / LOD1 <= 300 but binding spec is <=2000 / <=400 |
| 6 | HIGH | INCONSISTENCY | 3d-model-standards.md | Sign-off records bus/truck LOD0 <= 2500 / LOD1 <= 450 but binding spec is <=3000 / <=500 |
| 7 | HIGH | GAP | 3d-model-standards.md | LOD2 shell budget (300–500 tris) in Modular Kit section never reconciled with LOD Requirements table (400–600 tris) for large buildings |
| 8 | HIGH | INCONSISTENCY | 3d-model-standards.md, CLAUDE.md | CLAUDE.md says "10 floors maximum" but spec has an exemption range (15–30) for `com_high_*` — the CLAUDE.md summary omits the exemption |
| 9 | HIGH | MISSING | 3d-model-standards.md | No animation spec at all — the `.b3d` format explicitly supports skinned animation, but no bones/joint/animation-frame spec exists for any asset category |
| 10 | HIGH | GAP | 3d-model-standards.md | Road tile UV tiling (2× per 10 m tile) specified only in a prose note; no named shader constant or validation check for it |
| 11 | HIGH | DUPLICATE | 3d-model-standards.md | The 4-floor billboard/geometry-shell rule is restated at least four separate times in the same file with near-identical wording |
| 12 | HIGH | INCONSISTENCY | 3d-model-standards.md, building-atlas-layout.md | Service buildings: spec says all four share atlas cell (3,2) in one section; building-atlas-layout.md cell table gives each its own row-4 cell (cols 4–7) |
| 13 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | `com_high_*` LOD0 budget described as 7,000–10,000 in the LOD table row but 8,000–10,000 in the variant geometry section |
| 14 | MEDIUM | INCONSISTENCY | 3d-model-standards.md, model-validator-tool.md | Validator says "LOD2 assets — validate in-game at distances > 40 m" but LOD2 switch-in for small buildings is 90–100 m, not 40 m |
| 15 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | LOD2 shell spec for small buildings with height_floors >= 4 is "300–500 tris" in the Modular Kit section but "400–600 tris" in the LOD Requirements table |
| 16 | MEDIUM | GAP | 3d-model-standards.md | No specular/roughness map spec for buildings — only normal map UV channel is specified; no _sp channel assignment, atlas binding, or anisotropy for buildings |
| 17 | MEDIUM | GAP | 3d-model-standards.md | Collision mesh vertical extent rule says "authored at Y=0" but gives no maximum X/Z overhang tolerance relative to the tile footprint |
| 18 | MEDIUM | INCONSISTENCY | 3d-model-standards.md | Carriageway lane-width spec: two 3.6 m lanes + center strip (0.3 m) = 7.5 m, but the two 1.25 m kerb strips bring the total tile to 10 m only if the center-line strip is within the carriageway — this maths is ambiguous |
| 19 | MEDIUM | DUPLICATE | 3d-model-standards.md | Export validation check descriptions for checks #2 and #11 are functionally identical (same condition, different phrasing) — likely meant to be complementary; should be clearly labeled as paired |
| 20 | MEDIUM | GAP | 2d-texture-standards.md | Building facade normal map spec is missing — terrain normal maps are fully specified but no equivalent spec exists for building wall normals (intensity, scale, feature list) |
| 21 | MEDIUM | GAP | 2d-texture-standards.md | No specular/roughness map authoring spec for building facades — `_sp` channel packing format is named in the GL dispatch table but never defined per-asset-type |
| 22 | MEDIUM | INCONSISTENCY | 2d-texture-standards.md | The "Frosted Glass" UI sprite art style is marked superseded by "Glass City" but both styles are fully documented inline — the superseded section is not removed or clearly demoted to archive status |
| 23 | MEDIUM | GAP | building-atlas-layout.md | Road marking atlas spec exists (1024×1024, 4×4 cells) but is not cross-referenced from 3d-model-standards.md — road tile spec makes no mention of the road marking atlas for LOD0 decal binding |
| 24 | MEDIUM | INCONSISTENCY | building-atlas-layout.md, texture-cache.md | Building atlas GL_TEXTURE_MAX_LEVEL: building-atlas-layout.md says level=4 for 4096px (5 mips, levels 0–4); texture-cache.md dispatch table says the same — but the building atlas fallback (2048px) uses level=3 and only 4 mips in one table row but the VRAM budget mentions "4-level mip" for the 2k fallback, which would be GL_TEXTURE_MAX_LEVEL=3, consistent. This is technically consistent but the framing differs between files. |
| 25 | MEDIUM | GAP | model-validator-tool.md | Validator only shows LOD0; LOD1 and LOD2 are validated only "in-game at distances > 40 m" — no structured validator mode or CLI flag for LOD1/LOD2 visual review |
| 26 | MEDIUM | GAP | model-validator-tool.md | Billboard imposter atlases are never displayed by the validator — there is no category or mode to review the 8-frame baked billboard strips at any zoom level |
| 27 | MEDIUM | INCONSISTENCY | model-validator-tool.md | Model placement uses `setScale(10,10,10)` for building nodes — but the current spec says buildings are authored at native world scale with no runtime setScale(). Scale 10 at validator would make a 10 m tile-footprint building appear as a 100 m structure |
| 28 | LOW | GAP | 3d-model-standards.md | No spec for prop assets (street furniture, lamp posts, signs) beyond a tri budget row in the LOD table — no naming convention, UV channel requirements, atlas assignment, or collision mesh policy for props specifically |
| 29 | LOW | GAP | 3d-model-standards.md | No `.meta` schema spec for prop or infrastructure prop assets — only building and vehicle `.meta` fields are defined |
| 30 | LOW | DUPLICATE | 3d-model-standards.md, scene-graph-ownership.md | `LODNode::swapMesh()` bounding box recalculation requirement is described in detail in both files, with slightly different code blocks |
| 31 | LOW | INCONSISTENCY | 3d-model-standards.md | The naming convention stated in CLAUDE.md system prompt (`building_residential_low_lod0.b3d`) uses a different separator style from the binding spec (`res_low_01_lod0.b3d`) — the CLAUDE.md example is outdated |
| 32 | LOW | GAP | 2d-texture-standards.md | No wrap mode spec for building atlas, road marking atlas, or vehicle normal atlas — only billboard atlas (`GL_CLAMP_TO_EDGE`) and terrain (`GL_REPEAT`) are specified |
| 33 | LOW | MISSING | architecture/asset-standards/ | No spec file for road marking atlas art content — what each of the 8 reserved cells should contain, their art style, color values, and alpha channel usage |
| 34 | LOW | GAP | building-atlas-layout.md | The `ground_tarmac` cell (5,4) was listed as "industrial zone default" in the table but the notes column says "variant override, no longer the industrial zone default" — the table header and the notes are in direct contradiction |

---

## Detailed Issue Descriptions

### Issue 1 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Sign-off block (2026-02-28), item (20) vs. V1 Minimum Building Coverage section

The 2026-02-28 sign-off comment block states:
> "(20) V1 minimum building coverage (18 sets: 2 variants x 3 zones x 3 tiers) verified."

The V1 Minimum Building Coverage section in the same file states:
> "Artists must deliver a minimum of **36 building sets** across all zone/tier combinations: 4 variants × 3 zones × 3 density tiers = 36 sets total."

These two figures are irreconcilable. 18 = 2 variants × 3 zones × 3 tiers (old spec). 36 = 4 variants × 3 zones × 3 tiers (current spec). The sign-off artifact is permanently embedded in the file and reads as a contradiction to every reader. The sign-off predates the Phase-11e expansion to 4 variants per slot, but it has not been updated.

**Proposed resolution**: Append a correction note to the 2026-02-28 sign-off block stating that item (20) is superseded by the Phase-11e 36-set count, and update item (20) text in-place to read "36 sets (4 variants × 3 zones × 3 tiers)". Alternatively, strike the old value in the sign-off with an inline comment linking to the updated coverage section.

---

### Issue 2 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Sign-off block (2026-03-04) vs. World-Space Tile Positioning section

The 2026-03-04 sign-off comment (near line 835) states:
> "kTileSize = 4.0f constexpr float declared in src/rendering/render_constants.h; consistent with 4×4 m road tile mesh and 4×4×3 m modular building kit grid"

The binding World-Space Tile Positioning section (near line 226) states:
> "**`kTileSize` value**: `10.0f` Irrlicht units (10 metres). Each simulation tile occupies a 10 m × 10 m footprint. This is consistent with the road tile LOD budget (road tile mesh = 10×10 m quad)."

And the kTileSize declaration spec says:
> "`kTileSize` is declared as `static constexpr float kTileSize = 10.0f;` directly on `IrrlichtRenderer`"

The sign-off embeds the old value (4.0 m) which was retired when tile size changed to 10 m. Any reader consulting the sign-off gets a contradictory picture of the canonical value. The Modular Building Kit also references "4 m × 4 m × 3 m per floor unit" which describes the module grid step, not the tile size — this is a different concept but adds to the confusion.

**Proposed resolution**: Append a correction note to the 2026-03-04 sign-off block clearly stating that `kTileSize` was updated from 4.0 to 10.0 m after this sign-off was written, referencing the World-Space Tile Positioning section as authoritative. Add a prominent inline note in the Modular Building Kit section distinguishing the 4 m module grid from the 10 m tile size.

---

### Issue 3 — CRITICAL — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table vs. Modular Building Kit per-module caps

The LOD Requirements table states for large buildings (general):
> "LOD2 (far): 400–600 tris"

The Modular Building Kit per-module caps section states:
> "LOD2: single hand-authored baked shell mesh (not assembled from modules) — ≤500 tris total for large building (300–500 tris range allows meaningful silhouette features)"

These two ranges do not agree: 400–600 tris vs. 300–500 tris. A budget of 300 tris satisfies the Modular Kit floor but fails the LOD Requirements table floor (400 tris). A budget of 600 tris satisfies the table ceiling but exceeds the Modular Kit ceiling (500 tris). The current validation check #3 references "300–500 tri budget" specifically. If an asset is authored to 550 tris it passes check #3 but violates the LOD Requirements table upper bound.

**Proposed resolution**: Resolve to a single authoritative range. Given that the validation script uses 300–500 and that the table is intended as a guideline while the script is the enforcement mechanism, either (a) update the table to read "300–500 tris" to match the enforced range, or (b) update check #3 to use 400–600 tris to match the table. The chosen range should be documented as binding in exactly one place and cross-referenced from the other.

---

### Issue 4 — HIGH — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Tile Boundary Overlay section

The model validator tile boundary overlay section states:
> "a **red 10×10 m square outline** is rendered on the ground centred on each loaded model slot"

This is consistent with the current 10 m tile size (`kTileSize = 10.0f`). However, model validator building nodes are scaled `10×10×10` (see "Building nodes are scaled 10×10×10 m (same as `IrrlichtRenderer`)"). If buildings are now authored at native world scale (no runtime setScale) per the current 3d-model-standards.md ("No runtime `setScale()` is applied"), then applying `setScale(10, 10, 10)` in the validator would make native-10m-footprint buildings appear as 100 m × 100 m structures, completely overflowing the 10 m tile boundary overlay.

This is internally inconsistent within `model-validator-tool.md` itself and contradicts the native-scale authoring convention in `3d-model-standards.md`.

**Proposed resolution**: The validator building scale should be `1.0` (no scaling) to match the native-scale authoring convention. Update the model-validator-tool.md to remove the `setScale(10,10,10)` statement and confirm the tile overlay correctly represents the building's natural footprint.

---

### Issue 5 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: 2026-02-28 sign-off block item (12) vs. Vehicle Polygon Budget section

The 2026-02-28 sign-off states:
> "car: LOD0 <= 1500 / LOD1 <= 300"

The Vehicle Polygon Budget table states:
> "Car (sedan, hatchback, SUV): LOD0 ≤2,000 tris | LOD1 ≤400 tris"

The sign-off records tighter values (1500/300) than the current binding budget (2000/400). An artist reading the sign-off would under-budget their car models.

**Proposed resolution**: Append a correction note to the sign-off block noting that car budgets were subsequently raised from 1500/300 to 2000/400, citing the Vehicle Polygon Budget table as authoritative.

---

### Issue 6 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: 2026-02-28 sign-off block item (12) vs. Vehicle Polygon Budget section

The 2026-02-28 sign-off states:
> "bus/truck: LOD0 <= 2500 / LOD1 <= 450"

The Vehicle Polygon Budget table states:
> "Bus: ≤3,000 tris LOD0 | ≤500 tris LOD1"
> "Truck: ≤3,000 tris LOD0 | ≤500 tris LOD1"

The sign-off records tighter values (2500/450) than the current binding budget (3000/500). Same class of problem as Issue 5.

**Proposed resolution**: Same approach as Issue 5 — append a correction note to the sign-off block.

---

### Issue 7 — HIGH — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Modular Building Kit — LOD2 per-module cap

The per-module caps state "LOD2 ≤500 tris total for large building". But the LOD Requirements table row for "Large buildings (general)" specifies "400–600 tris" as the LOD2 budget, with a note explaining why that range is needed:
> "400–600 tris is required to represent building silhouettes (setbacks, rooftop details, entry bays) at the 185–200 m switch-in distance where tall buildings still occupy 50–80 vertical pixels."

A 500 tri cap from the Modular Kit section is below the 600 tri upper bound that the architectural rationale considers achievable. No explanation is given for why the Modular Kit section uses a tighter cap than the LOD Requirements table's upper end allows.

**Proposed resolution**: Reconcile by either (a) raising the Modular Kit cap to 600 to match the table, or (b) explicitly documenting that the LOD2 shell is capped at 500 as a conservative budget choice despite the 600 tris upper bound being technically permitted, and updating the LOD Requirements table ceiling to 500.

---

### Issue 8 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`, `CLAUDE.md`
**Location**: CLAUDE.md "Floor cap: 10 floors maximum" project rule vs. spec exemption

CLAUDE.md (system-level rules) states:
> "**Floor cap**: 10 floors maximum for any building in V1."

`3d-model-standards.md` Modular Building Kit states:
> "**Exemption — `com_high_*` only**: Commercial High skyscraper variants (`com_high_01` through `com_high_04`) are the sole exception to the 10-floor cap. Their `height_floors` must be in the range 15–30."

CLAUDE.md is missing this exemption entirely. The omission is understandable as a summary document, but it creates a genuine contradiction for anyone reading CLAUDE.md as authoritative: the system prompt rules say no building exceeds 10 floors, the spec permits 15–30 for `com_high_*`. Any automated rule-checking or agent acting on CLAUDE.md will incorrectly flag `com_high_*` assets.

**Proposed resolution**: Add "(except `com_high_*` skyscrapers: 15–30 floors)" to the floor cap entry in CLAUDE.md.

---

### Issue 9 — HIGH — MISSING
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Throughout — no animation section exists

The `.b3d` format is described as "Blitz3D format — Irrlicht native, supports multiple UV channels including UV2/lightmap" and is also the format used for all animated and rigged assets. However, there is no animation specification anywhere in the asset standards:

- No bones/joint count per asset type
- No animation clip naming convention (idle, drive, etc.)
- No frame rate or frame count requirements
- No constraint on IK or weighted influences per vertex
- No guidance on whether any V1 building or vehicle uses skeletal animation at all

If all `.b3d` assets are static, this should be explicitly stated. If some vehicles (e.g., wheels) use bone animation, that pipeline is entirely unspecified.

**Proposed resolution**: Add a dedicated "Animation" section to `3d-model-standards.md` that either (a) explicitly states "No V1 asset uses skeletal animation — all `.b3d` files are static meshes" and explains what that means for Blender export settings, or (b) defines the animation pipeline for any asset type that does use animation.

---

### Issue 10 — HIGH — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Road tile spec

The road tile UV tiling factor (2× per 10 m tile) is described in prose:
> "Road tile UV-channel 0 tiling is specified in the road shader (UV tiles 2× per 10 m road quad — both U and V scale by 2.0 in the vertex shader), not authored per-asset."

There is no named shader constant for this tiling factor, no export validation check for road tile UV, and no cross-reference to the road shader source. If the tiling factor changes for a different road texture, there is no single canonical place to update it and no way for a validator to verify it.

**Proposed resolution**: Define `static constexpr float kRoadUVTilingFactor = 2.0f` in `render_constants.h` alongside `kLaneCenterOffset` and `kCarriagewayHalfWidth`. Add a cross-reference note in `3d-model-standards.md` pointing to that constant. Add a comment to the road shader binding it to that constant.

---

### Issue 11 — HIGH — DUPLICATE
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Multiple sections

The 4-floor billboard/geometry-shell threshold rule is restated at least four times in full:
1. LOD File Naming Convention section (small building paragraph)
2. Billboard LOD note in the same section (longer version)
3. Export validation check #2 description
4. Export validation check #11 description
5. Service Building Model Standards section

Each restatement is nearly word-for-word identical. This creates a maintenance burden: any change to the threshold (e.g., changing it from 4 to 5) requires updates in at least five places, with high risk of inconsistency. The repetition also inflates file size significantly and makes the spec harder to scan.

**Proposed resolution**: Define the rule exactly once in a clearly labeled canonical section (e.g., "Billboard vs. Geometry Shell LOD2 Selection Rule") and replace all other occurrences with a cross-reference: "See LOD2 Selection Rule above" or "(per the 4-floor threshold rule)".

---

### Issue 12 — HIGH — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md` vs. `architecture/asset-standards/building-atlas-layout.md`
**Location**: Service Building Model Standards section vs. Cell Assignment Table

`3d-model-standards.md` Service Building section states:
> "Service buildings share atlas cell (3, 2) for all four types in V1 — they share a common material palette (concrete, glass, utility panels). A second reserved cell (3, 3) is available if a distinct material per service type is required in a later phase."

`building-atlas-layout.md` Cell Assignment Table shows:
> Row 4, col 4: `svc_fire_station` — dedicated cell
> Row 4, col 5: `svc_police_station` — dedicated cell
> Row 4, col 6: `svc_power_plant` — dedicated cell
> Row 4, col 7: `svc_water_tower` — dedicated cell

These are mutually exclusive. One source says all four service buildings share cell (3,2); the other gives each a separate dedicated cell in row 4. The sign-off in `3d-model-standards.md` also references "reserved cells (3,2) and (3,3) for service buildings" which is now superseded by the row-4 assignment but the old text remains.

**Proposed resolution**: Update the Service Building Model Standards section to reference the current row-4 dedicated cell assignments from `building-atlas-layout.md`. Remove the outdated shared-cell (3,2) language. Update the `.meta` sidecar example JSON (currently shows `"atlas_cell": {"row": 3, "col": 2}`) to show the correct per-type cells, or provide a table with one example per type.

---

### Issue 13 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table row vs. Building Variant Geometry section

LOD Requirements table (Commercial High sub-row):
> "7,000–10,000 tris LOD0"

Building Variant Geometry — Commercial High section:
> "LOD0 target: 8,000–10,000 tris (elevated budget reflecting landmark status)"

The lower bound differs: 7,000 tris (table) vs. 8,000 tris (variant section). An asset with 7,500 LOD0 tris satisfies the table but is below the variant section target.

**Proposed resolution**: Standardize to one range. Since the variant section explicitly justifies the 8,000 lower bound with "landmark status," prefer 8,000–10,000 as the binding range and update the LOD Requirements table row to match.

---

### Issue 14 — MEDIUM — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: LOD display section

The validator states:
> "LOD1 (`_lod1.b3d`) and LOD2 (geometry shells `_lod2.b3d` for High-density zones, billboard imposters for Low/Med) are not displayed by the validator — validate LOD2 assets visually in the game at distances > 40 m."

The LOD Distance Thresholds table shows LOD2 switch-in for large buildings is at < 185 m, and for small buildings/props at < 90 m. The guidance "distances > 40 m" is far too close: at 40 m, large buildings would still be in LOD0 and small buildings would still be in LOD0 or only just at LOD1. This is a significant navigation error for artists reviewing their LOD2 assets.

**Proposed resolution**: Update the validator guidance to state "validate LOD2 assets visually in-game at distances > 100 m (small buildings) and > 185 m (large buildings)" to match the actual thresholds.

---

### Issue 15 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Modular Building Kit section vs. LOD File Naming Convention section

Modular Building Kit states (for small/prop buildings with `height_floors >= 4`):
> "must ship a `_lod2.b3d` geometry shell (300–500 tris)"

LOD Requirements table states (large buildings general):
> "LOD2 (far): 400–600 tris"

The "small building / props (height_floors >= 4)" row in the LOD Requirements table states:
> "400–600 tris (`_lod2.b3d` geometry shell)"

So a tall small-building asset has the 400–600 tris bound from the LOD Requirements table, but the Modular Kit section says 300–500. This is the same root cause as Issue 3 but affects the small-building category rather than large buildings.

**Proposed resolution**: Same resolution as Issue 3 — pick one authoritative range and cross-reference it from all other locations.

---

### Issue 16 — MEDIUM — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: UV Channel Convention section

Buildings are specified to use UV channel 0 (diffuse atlas) and UV channel 1 (lightmap). Vehicles are specified to use UV channel 0 only. However, there is no spec for a **specular/roughness map** UV channel for buildings:
- No `_sp` (specular packed) atlas cell is assigned anywhere in the building atlas
- No roughness or metalness pipeline is described for buildings
- The anisotropy spec in `2d-texture-standards.md` lists "specular/roughness maps: minimum 4× anisotropy" and the GL dispatch table has a `_sp` row — but no corresponding artist guidance for building `_sp` authoring

This leaves the building PBR material pipeline incomplete: artists know the diffuse and lightmap channels but have no spec for reflectance data.

**Proposed resolution**: Either (a) explicitly state "V1 building assets use diffuse + lightmap only; no specular/roughness channel is authored for buildings" in the UV Channel Convention section, or (b) define a building specular atlas cell assignment, per-asset naming convention, and `_sp` authoring guidance.

---

### Issue 17 — MEDIUM — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Collision Meshes section

The collision mesh spec states geometry must be "flat at Y=0" and not exceed 24 triangles. It defines the collision volume as extruded vertically at runtime. However, it gives no tolerance for X/Z overhang beyond the tile footprint. A collision mesh that slightly overhangs into adjacent tiles would cause incorrect road adjacency blocking without any validation error. The 5 mm Y-axis tolerance is specified for floor modules, but no equivalent XZ tolerance is given for collision footprints.

**Proposed resolution**: Add an explicit XZ footprint constraint to the collision mesh spec: "Collision mesh vertices must not exceed the building's tile footprint boundary (±N×5 m for tier N, consistent with the geometry bounds in the Multi-Tile Footprint table). Tolerance: 0.1 m (100 mm)." Add a corresponding validation check (check #21 if needed).

---

### Issue 18 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Carriageway width and center-line strip sections

The spec states:
- "The asphalt surface covers **7.5 m** of the 10 m tile width"
- "remaining 1.25 m on each side is rendered as a kerb/verge strip"
- "A 0.3 m wide white painted strip implements a two-way road divider"
- "Left lane (local X = −1.875 m center, 3.6 m wide)"
- "Right lane (local X = +1.875 m center, 3.6 m wide)"

Check: 3.6 m + 3.6 m + 0.3 m center strip = 7.5 m. This sums correctly. But the lane center offsets: if left lane center is at −1.875 m and it is 3.6 m wide, it occupies −3.675 m to −0.075 m. Right lane occupies +0.075 m to +3.675 m. The center strip of 0.3 m would occupy −0.15 m to +0.15 m. Left lane right edge is at −0.075 m, which overlaps with the center strip's left edge at −0.15 m by 0.075 m. This geometry overlaps and the spec's stated lane center offsets do not account for the center-line strip thickness correctly.

**Proposed resolution**: Recalculate lane offsets to account for the center strip: left lane center at −(1.8 + 0.15) = −1.95 m, right lane at +1.95 m, with each lane being exactly 3.6 m wide, leaving 0.3 m for the center strip. Alternatively verify the implementation is correct and update the spec to reflect actual authored vertices.

---

### Issue 19 — MEDIUM — DUPLICATE
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: Export validation checks #2 and #11

Check #2:
> "Small building / prop `_lod2.b3d` file presence is floor-count conditional... if `height_floors <= 3`, must NOT have a `_lod2.b3d` file and must have a `_billboard.dds` instead; if `height_floors >= 4`, must have a `_lod2.b3d` geometry shell... and must NOT rely on `_billboard.dds`"

Check #11:
> "Small building / prop assets with `height_floors >= 4` must have a `_lod2.b3d` geometry shell (not just billboard). Conversely, small building / prop assets with `height_floors <= 3` must NOT have a `_lod2.b3d` file — they use point-sprite LOD2 only."

These two checks are testing the same condition from both directions. While having complementary checks is reasonable for a validator, the descriptions are so nearly identical that the intent of having two separate checks is not clear from the spec. If they are intentionally separate checks (e.g., check #2 is about billboard presence and check #11 is about geometry shell presence), the distinction should be clearly stated.

**Proposed resolution**: Add an explicit note at check #2 and check #11 explaining how they differ: "Check #2 validates the billboard DDS presence/absence; check #11 validates the `_lod2.b3d` presence/absence. Both check the same condition (4-floor threshold) but test different file types."

---

### Issue 20 — MEDIUM — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Throughout — building texture authoring

The terrain normal map section is exhaustively specified (per-texture intensity, pixel scale, primary features, mip behavior, authoring checklist). But there is no equivalent specification for:
- Building facade normal maps: what intensity ranges are appropriate for glass, brick, concrete?
- Which UV space is the normal authored in (tangent-space, per atlas cell)?
- What mip levels are required for building normal maps?
- Are building normals ever authored (Phase 9 assets exist but no normal map is referenced in the building pipeline)?

If building normals are deferred post-V1, that should be explicitly stated. Currently the reader cannot determine whether building normal maps are expected.

**Proposed resolution**: Add a "Building Normal Map Authoring" subsection that either defines building normal map standards (intensity, format, atlas layout) or explicitly states "V1 building assets do not include normal maps; the building shader uses diffuse + lightmap only."

---

### Issue 21 — MEDIUM — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Throughout — specular pipeline for buildings

The `_sp` suffix (specular packed, BC3) appears in the GL_TEXTURE_MAX_LEVEL dispatch table and in the `2d-texture-standards.md` anisotropy section ("Specular/roughness maps: minimum 4× anisotropy"). But there is no:
- Specular/roughness authoring pipeline for buildings
- Atlas cell assignment for building specular data
- Named `_sp` file for any building asset type
- Guidance on channel packing (which channel is roughness, metalness, AO?)

This creates an underspecified material pipeline for buildings.

**Proposed resolution**: Either (a) state explicitly that V1 buildings have no specular map and are rendered with Lambertian + lightmap only, or (b) define the channel packing convention, atlas strategy, and per-asset naming for building specular maps.

---

### Issue 22 — MEDIUM — INCONSISTENCY
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Frosted Glass vs. Glass City sections

The "Frosted Glass" section ends with:
> "The 'Frosted Glass' art style described above documents the Phase 10 signed-off sprite sheet... and is retained as a historical record. All new icon authoring... must follow the Glass City spec below."

Both styles remain fully documented in the file: Frosted Glass occupies roughly 60 lines and Glass City occupies roughly another 80 lines. The "Superseded Frosted Glass Active-State Values" table at the end lists differences. While keeping historical context is fine, the two styles are interleaved in a way that a new artist authoring icons must determine which guidance applies. The section header for Frosted Glass does not include any "SUPERSEDED" or "ARCHIVED" label.

**Proposed resolution**: Add a bold "**SUPERSEDED — retained for historical reference only — do not use for new authoring**" note at the very top of the Frosted Glass section header so artists cannot accidentally author to the wrong style.

---

### Issue 23 — MEDIUM — GAP
**File**: `architecture/asset-standards/building-atlas-layout.md` vs. `architecture/asset-standards/3d-model-standards.md`
**Location**: Road marking atlas

`building-atlas-layout.md` defines the Road Marking Atlas (1024×1024, DXT5, 4×4 cells, 8 assigned decal types) but `3d-model-standards.md` makes no mention of this atlas when describing road tile mesh authoring. The road tile spec describes the road shader binding `road_asphalt_tileable.dds` but does not state where or how road decals (lane markings, crosswalks, turn arrows) are applied, which UV channel binds the decal atlas, or how decals interact with the road LOD system (e.g., "road marking decals from the road atlas are disabled at LOD2" appears in the LOD table note but the binding mechanism is not described).

**Proposed resolution**: Add a cross-reference from the road tile authoring section in `3d-model-standards.md` to the Road Marking Atlas section in `building-atlas-layout.md`, and specify which texture unit and shader uniform binds the road marking atlas.

---

### Issue 24 — MEDIUM — INCONSISTENCY (minor framing)
**File**: `architecture/asset-standards/building-atlas-layout.md`, `architecture/graphics-architecture/texture-cache.md`
**Location**: Building atlas VRAM budget

`texture-cache.md` dispatch table lists the primary building atlas as GL_TEXTURE_MAX_LEVEL=4 (5 mip levels, levels 0–4, 4096×4096). `building-atlas-layout.md` states "Mip chain: 5-level mandatory (`GL_TEXTURE_MAX_LEVEL = 4`; 4096→2048→1024→512→256)". These are consistent.

However, the VRAM budget table in `2d-texture-standards.md` states:
> "City building atlas — primary (4096×4096 DXT1, 5-level mip) | ≤12 MB (`ceil(4096/4)^2 × 8 × 1.33 ≈ 10.6 MB`)"

And the sign-off in `building-atlas-layout.md` states "4096×4096 DXT1 = 8,388,608 bytes ≈ 8 MB for the primary atlas". The 8 MB figure is for mip level 0 only (base level). With the full 5-level mip chain, the total is ~10.6 MB. The sign-off is citing the base level only, which is correct raw data but misleading in a VRAM context that includes all mip levels.

**Proposed resolution**: Add a clarifying note to the sign-off that the 8 MB figure is for the base mip level only, and the total VRAM including the full 5-level mip chain is approximately 10.6 MB as stated in the VRAM budget table.

---

### Issue 25 — MEDIUM — GAP
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Asset categories and display modes

The validator tool only displays LOD0 for all categories. LOD1 and LOD2 are not shown. There is no `--lod 1` or `--lod 2` CLI argument described. For a tool described as "the canonical tool for per-release asset sign-off," the omission of LOD1 and LOD2 visual review is a significant gap. LOD1 silhouette fidelity (e.g., "must retain balcony slab extrusion profile") is a binding spec requirement but has no tooling support.

**Proposed resolution**: Add a `--lod N` command-line argument to the validator (N = 0, 1, 2) that selects which LOD level to display. For LOD2, show geometry shells for High-density and billboard impostors for Low/Med categories, with the billboard rotating to show all 8 frames.

---

### Issue 26 — MEDIUM — GAP
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Asset categories

Billboard imposter atlases (`*_billboard.dds`, 28 files per the Phase 11d inventory) are never displayed by the validator. The spec states "validate LOD2 assets visually in the game at distances > 40 m" but billboard atlases should have a direct review path. Bake errors (wrong elevation, incorrect alpha, frame ordering) are only catchable visually but have no tooling support.

**Proposed resolution**: Add a "Billboard" review category to the validator that displays each small building's 8 bake frames as a flat 2D strip on a plane, allowing the operator to check elevation, alpha, and frame count directly.

---

### Issue 27 — MEDIUM — INCONSISTENCY
**File**: `architecture/graphics-architecture/model-validator-tool.md`
**Location**: Model Placement section

The validator states:
> "Building nodes are scaled 10×10×10 m (same as `IrrlichtRenderer`)."

But `3d-model-standards.md` (Native-size authoring convention) states:
> "**No runtime `setScale()` is applied** — `placeBuildingMesh()` and `placeServiceBuildingMesh()` place nodes at scale 1.0."

These contradict each other. If the validator applies `setScale(10, 10, 10)` and the game engine does not, the validator is not exercising the same rendering code path as production, which undermines its purpose as a sign-off tool.

**Proposed resolution**: Update `model-validator-tool.md` to remove the `setScale(10, 10, 10)` statement and confirm that building nodes are placed at `scale = 1.0`, consistent with the game engine's `placeBuildingMesh()`.

---

### Issue 28 — LOW — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: LOD Requirements table

Infrastructure props (lamp posts, signs) have an entry in the LOD Requirements table (≤300 tris LOD0, ≤75 tris LOD1, Billboard LOD2). But there is no:
- Naming convention for prop assets (`lamp_post_01_lod0.b3d`? or something else?)
- UV channel requirement (lightmap UV required or NOLIGHTMAP?)
- Collision mesh requirement (required for props? 4 m radius threshold applies?)
- `.meta` sidecar requirement (required for all `.b3d` files per check #15, but no `category` value "prop" is defined in the schema)

The category value "prop" appears in the `.meta` schema description (`category: large_building | small_building | prop | vehicle`) but no corresponding prop pipeline exists.

**Proposed resolution**: Add a "Prop and Infrastructure Prop Standards" subsection defining the naming convention, UV requirements, collision mesh policy, and `.meta` schema usage for lamp posts, signs, and other infrastructure props.

---

### Issue 29 — LOW — GAP
**File**: `architecture/asset-standards/3d-model-standards.md`
**Location**: `.meta` sidecar schema

The `.meta` schema defines `category` as one of: `large_building | small_building | prop | vehicle`. The `vehicle` category is covered by the Vehicle section. `large_building` and `small_building` are covered by the building sections. But the `prop` category has no documented behavior: no LOD strategy, no collision mesh policy, no `height_floors` interpretation. The validation script would read `category: "prop"` and have no policy to apply.

**Proposed resolution**: Document `category: "prop"` behavior explicitly: which LOD strategy applies (same as small_building? NOLIGHTMAP exception?), whether `height_floors` is required, and how the export validation script handles props.

---

### Issue 30 — LOW — DUPLICATE
**File**: `architecture/graphics-architecture/scene-graph-ownership.md`, `architecture/graphics-architecture/procedural-terrain.md`
**Location**: LOD swap bounding box requirement

Both files contain a detailed description of the mandatory bounding box recalculation before `setMesh()`, including code blocks that are nearly identical. `scene-graph-ownership.md` is the canonical home for this rule and `procedural-terrain.md` has its own copy. While cross-referencing with brief code examples is acceptable, the full copy in `procedural-terrain.md` creates a maintenance hazard.

**Proposed resolution**: Replace the full code block in `procedural-terrain.md` with a reference: "See `scene-graph-ownership.md — LOD Swap — Bounding Box Requirement` for the full rule and code sequence." Retain a brief one-line summary in `procedural-terrain.md` for context.

---

### Issue 31 — LOW — INCONSISTENCY
**File**: `CLAUDE.md` (system prompt), `architecture/asset-standards/3d-model-standards.md`
**Location**: Naming convention example in CLAUDE.md

The CLAUDE.md system prompt Project-Specific Rules section states:
> "**Naming convention**: `<type>_<zone>_<tier>_lod<N>.b3d` (e.g. `building_residential_low_lod0.b3d`)"

The binding spec in `3d-model-standards.md` uses:
> "`<zone>_<tier>_<variant>_lod<N>.<ext>` (e.g. `res_low_01_lod0.b3d`)"

The CLAUDE.md example is wrong in two ways: (1) it includes a `building_` prefix that does not exist in the spec, and (2) it uses long zone names (`residential`) instead of the short form (`res`), and omits the mandatory `<variant>` field (`_01` etc.).

**Proposed resolution**: Update the CLAUDE.md naming convention entry to use the correct format from the spec: `<zone>_<tier>_<variant>_lod<N>.b3d` with the example `res_low_01_lod0.b3d`.

---

### Issue 32 — LOW — GAP
**File**: `architecture/asset-standards/2d-texture-standards.md`
**Location**: Wrap mode conventions

`texture-cache.md` specifies that billboard atlases use `GL_CLAMP_TO_EDGE` and non-billboard sRGB atlases use `GL_REPEAT`. But `2d-texture-standards.md` itself does not document wrap mode requirements per texture type. An artist authoring a new atlas has no guidance in the primary texture standards document on wrap mode unless they also read the implementation file.

**Proposed resolution**: Add a "Wrap Mode per Texture Category" table to `2d-texture-standards.md` mirroring the policy in `texture-cache.md`: billboard `_billboard` = CLAMP_TO_EDGE, diffuse atlases and road tileables = REPEAT, splat maps = CLAMP_TO_EDGE (to prevent border bleed between terrain tiles).

---

### Issue 33 — LOW — MISSING
**File**: `architecture/asset-standards/` (no file)
**Topic**: Road marking atlas art content spec

`building-atlas-layout.md` defines the Road Marking Atlas structure (cell count, format, cell assignment table listing 8 types: straight lane, dashed lane, crosswalk, turn arrows left/right/straight, stop line, intersection center). However, there is no art specification for these markings:
- What color are the lane markings (white? yellow?)
- What are the dimensions and proportions of each marking type within the 240×240 px usable cell?
- What is the alpha channel usage (decal mask)?
- What is the correct rendering blend mode (additive? alpha blend over asphalt)?

**Proposed resolution**: Add art content guidance for each of the 8 road marking types to `building-atlas-layout.md` (since it already owns the cell assignment table), or create a new `architecture/asset-standards/road-marking-standards.md` file.

---

### Issue 34 — LOW — INCONSISTENCY
**File**: `architecture/asset-standards/building-atlas-layout.md`
**Location**: Cell Assignment Table, cell (5,4) `ground_tarmac`

The Cell Assignment Table "Notes" column for `ground_tarmac` at (5,4) states:
> "Phase 11f: dark-grey asphalt. Used by industrial building ground quads."

But the Ground Feature Cells section for the same cell states:
> "Zone default: — (variant override) | Artistic tarmac choice for specific variants only (e.g., urban residential forecourts, auto garage forecourts); no longer the industrial zone default"

The "Notes" column in the Cell Assignment Table and the Ground Feature Cells table give contradictory information about whether `ground_tarmac` is the industrial zone default. One says it is used by industrial building ground quads (implying a zone default), the other says it is not the industrial zone default.

**Proposed resolution**: Update the Cell Assignment Table Notes column for `ground_tarmac` to read: "Phase 11f: dark-grey asphalt. Variant override for specific buildings only (e.g. auto garage forecourts); NOT the industrial zone default (which is `ground_paving`)."

---

## Summary by Severity

| Severity | Count |
|---|---|
| CRITICAL | 3 |
| HIGH | 8 |
| MEDIUM | 13 |
| LOW | 10 |
| **Total** | **34** |

## Priority Fixes Before Phase 12 Asset Authoring

The following issues should be resolved before any new asset authoring begins, as they create direct contradictions that will mislead artists:

1. **Issue 1** (CRITICAL) — V1 coverage count in sign-off (18 vs. 36)
2. **Issue 2** (CRITICAL) — `kTileSize` in sign-off (4.0 vs. 10.0)
3. **Issue 3** (CRITICAL) — LOD2 shell budget (300–500 vs. 400–600 tris)
4. **Issue 12** (HIGH) — Service building atlas cell assignment conflict
5. **Issue 27** (MEDIUM) — Model validator applies `setScale(10,10,10)` against the no-scale policy
6. **Issue 31** (LOW) — CLAUDE.md naming convention example is wrong

These six issues will directly mislead any new artist reading the spec for the first time and should be treated as blocking.
