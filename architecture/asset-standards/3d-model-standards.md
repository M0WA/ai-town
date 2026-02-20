# 3D Model Standards

- **All building assets (animated and static)**: `.b3d` (Blitz3D format — Irrlicht native, supports multiple UV channels including UV2/lightmap). `.b3d` is mandatory for any asset that participates in the lightmap baking pipeline.
- **Simple props without lightmaps**: `.obj` is acceptable only for props that are explicitly exempt from the UV2/lightmap requirement (e.g., 1-UV-channel street furniture, terrain-level decorations). Such assets must be marked `NOLIGHTMAP` in their asset metadata. `.obj` has no native multi-UV support; importing a multi-UV `.obj` will silently lose UV channel 1.
- **Coordinate system**: Y-up, Z-forward, **left-handed** (Irrlicht convention). Irrlicht uses a left-handed coordinate system: +X right, +Y up, +Z forward (into the screen). This is the opposite handedness from Blender's right-handed default. See the **Coordinate System Export Convention** section below for Blender export settings. Never label the coordinate system as "right-handed" — that produces a mirrored/rotated result in Irrlicht.
- **Unit scale**: 1 Irrlicht unit = 1 meter

## LOD Requirements (mandatory for all city assets)

| Asset category | LOD0 (near) | LOD1 (mid) | LOD2 (far) |
|---|---|---|---|
| Large buildings | 2000–5000 tris | 500–1000 tris | 300–500 tris |
| Small buildings / props (height_floors <= 3) | 500–1500 tris | 100–300 tris | Billboard (point-sprite only) |
| Small buildings / props (height_floors >= 4) | 500–1500 tris | 100–300 tris | 300–500 tris (`_lod2.b3d` geometry shell) |
| Vehicles | 1000–3000 tris | 200–500 tris | Point/sprite |
| Terrain chunk (64×64 m) | 32×32 quad grid | 16×16 quad grid | 8×8 quad grid |
| Road tile (4×4 m) | ≤48 tris (flat quad + kerb geometry) | ≤16 tris (flat quad only) | ≤8 tris (single quad) |
| Infrastructure props (lamp posts, signs) | ≤300 tris | ≤75 tris | Billboard (same system as small buildings) |

**Road tile LOD thresholds**: Road tiles use the same LOD distance thresholds as small buildings/props (LOD0→LOD1 at 30 m / 25 m; LOD1→LOD2 at 100 m / 90 m). At LOD2 (>100 m), road tiles are rendered as flat coloured quads with no kerb or road marking geometry — road marking decals from the road atlas are disabled at LOD2. **Road LOD2 color source**: The LOD2 road quad color is sampled from the road tileable texture's average color, computed at asset pipeline generation time and stored as a named constant `SimulationConstants::road_lod2_color` (type `irr::video::SColor`). This value must be a perceptual match of the center region of `road_asphalt_tileable.dds` when viewed in linear space (approximately a mid-dark gray, e.g. SColor(255, 60, 60, 60) for standard asphalt). Do NOT hardcode a magic color literal inline in rendering code — always use `SimulationConstants::road_lod2_color` so that the color is updated in one place when the road texture changes. The LOD2 road quad does NOT bind a texture — it is drawn as a flat-shaded quad using the material's vertex color channel, set to `road_lod2_color` at entity construction time.

**Note on large building LOD2 budget**: 300–500 tris is required to represent building silhouettes (setbacks, rooftop details, entry bays) at the 185–200 m switch-in distance where tall buildings still occupy 50–80 vertical pixels. A 100–200 tri cap produces a featureless slab that is visually jarring against LOD1 counterparts.

### LOD Distance Thresholds and Hysteresis

**Hysteresis bands are mandatory** to prevent LOD thrashing (continuous mesh rebind stutter) when the camera sits near a threshold:

| Asset category | LOD0→LOD1 switch-out | LOD0→LOD1 switch-in | LOD1→LOD2 switch-out | LOD1→LOD2 switch-in |
|---|---|---|---|---|
| Large buildings | > 50 m | < 45 m | > 200 m | < 185 m |
| Small buildings / props | > 30 m | < 25 m | > 100 m | < 90 m |
| Vehicles | > 40 m | < 35 m | > 100 m | < 90 m |
| Terrain chunk | > 100 m | < 92 m | > 300 m | < 285 m |
| Road tile | > 30 m | < 25 m | > 100 m | < 90 m |
| Infrastructure props | > 30 m | < 25 m | > 100 m | < 90 m |

**Road tile and Infrastructure props thresholds**: Road tiles and infrastructure props (lamp posts, signs) use the same thresholds as Small buildings/props (5 m close hysteresis, 10 m far hysteresis). Road tile LOD2 is a flat colored quad — not a billboard imposter — consistent with the road-tile LOD2 specification in the LOD Requirements table above.

`LODNode` stores the **last active LOD level** and transitions only when the camera crosses the directional threshold for that level. It never transitions up and down in the same frame. **Hysteresis ranges are mandatory: ≥5 m for close thresholds (LOD0↔LOD1) and ≥10 m for far thresholds (LOD1↔LOD2).** The table above satisfies these requirements: small buildings/props close gap = 5 m (30−25), far gap = 10 m (100−90); vehicles close gap = 5 m (40−35), far gap = 10 m (100−90). Large buildings close gap = 5 m (50−45), far gap = 15 m (200−185). Terrain chunk gap = 8 m and 15 m respectively (terrain uses 8 m close gap by exception — camera is almost always moving when terrain chunks rebind, making 5 m precision unnecessary; the far 15 m gap exceeds the 10 m minimum).

LOD meshes are exported as separate meshes and swapped in code by distance using Irrlicht's scene manager.

#### Camera Pitch Range

- Camera pitch is defined as the angle (degrees) between the camera's forward vector and the world XZ plane, measured as negative = looking downward. Valid range: **[−70°, −20°]** (always looking downward in city view).
- **Minimum look-down angle**: −20° (shallow oblique — prevents looking nearly level at the horizon, which would cause z-fighting and poor city readability).
- **Maximum look-down angle**: −70° (steep overhead — prevents gimbal lock near top-down view).
- This range is enforced by `CameraController`; the billboard bake elevation is **45° below horizontal (camera pitch = −45°)**, the midpoint of the [−70°, −20°] operating range, minimising average mismatch error across all valid camera angles.
- **Sign-off status**: CONFIRMED — camera pitch range [−70°, −20°] and bake midpoint −45° are final. Camera pitch for billboard baking: −45° below horizontal (confirmed). Reviewed and approved by: graphics-artist-3d-model. Phase 6 billboard bake pipeline may proceed on this basis.

#### Density Tier Asset Naming Convention

Building assets must encode both the zone type, density tier, and a variant identifier in their filename to allow the C++ asset loader and export validation script to unambiguously associate assets with their density tier and zone:

```text
<zone>_<tier>_<variant>_lod<N>.<ext>
```

Where:

- `<zone>` is one of: `res` (Residential), `com` (Commercial), `ind` (Industrial)
- `<tier>` is one of: `low`, `med`, `high`
- `<variant>` is a 2-digit integer (`01`, `02`, … `NN`) for visual variety within a tier
- `<N>` is the LOD level (0, 1, 2)
- `<ext>` is `.b3d` (buildings) or `.dds` (billboard atlas)

**Examples**:

- `res_low_01_lod0.b3d` — Residential Low tier, variant 1, LOD0 geometry
- `com_med_03_lod2.b3d` — Commercial Medium tier, variant 3, LOD2 shell
- `res_low_01_billboard.dds` — Residential Low tier, variant 1, billboard atlas (height_floors ≤ 3)

The `<asset_name>` base (e.g. `res_low_01`) is referenced in `<asset_name>.meta` for `height_floors`, `category`, and atlas cell assignments. The C++ `BuildingAssetLoader` parses the naming convention to construct LOD file paths — do not use ad-hoc per-building naming.

#### `.meta` Sidecar File Format

Every `.b3d` building or vehicle asset must ship a `<asset_name>.meta` JSON sidecar (check #14 in the export validation script). Required fields:

```json
{
  "category": "large_building",
  "height_floors": 4,
  "atlas_cell": { "row": 0, "col": 2 },
  "lod_distances": [40.0, 80.0, 200.0]
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `category` | string | yes | One of `large_building`, `small_building`, `prop`, `vehicle`. Controls LOD2 strategy selection (billboard vs geometry shell) and export validation checks. |
| `height_floors` | integer | yes | Total floor count. Used by export validation check #2 (billboard absent and `_lod2.b3d` absent when `height_floors <= 3`; `_lod2.b3d` required when `height_floors >= 4`), check #11 (geometry shell required for `height_floors >= 4`; `_lod2.b3d` prohibited for `height_floors <= 3`), and the C++ `LODNode` runtime upgrade path (billboard ↔ geometry shell switch on density tier change). Also used to compute building height (`height_floors × 3 m`) for collision mesh extrusion. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). |
| `atlas_cell` | object | yes | `{ "row": R, "col": C }` — the asset's assigned cell in the 2048×2048 building atlas (see `building-atlas-layout.md`). Used by export validation check #4 (UV channel 0 within atlas cell). |
| `lod_distances` | array(3) | yes | `[lod0_to_lod1_distance, lod1_to_lod2_distance, cull_distance]` in world units (metres). These values are the canonical source for `LODNode` configuration at runtime. The export validation script check #9 validates: `lod1_to_lod2_distance − lod0_to_lod1_distance ≥ 5` (close hysteresis ≥ 5 m) and `cull_distance − lod1_to_lod2_distance ≥ 10` (far hysteresis ≥ 10 m). Small buildings with billboard imposters must set `lod_distances[1]` to their billboard swap distance (i.e., the distance at which `LODNode` transitions from the LOD1 mesh to the billboard quad). Example: `[30.0, 100.0, 200.0]` — LOD0→LOD1 at 30 m, LOD1→billboard at 100 m, cull at 200 m; hysteresis validated from the switch-out / switch-in thresholds in the `LODNode` config which must match with ≥ 5 m close gap and ≥ 10 m far gap. |

**Hysteresis validation detail for check #9**: The `lod_distances` array stores switch-out distances. The corresponding switch-in distances are stored in the `LODNode` configuration (not in the `.meta` file). The export validation script verifies that the switch-out values in `lod_distances` satisfy the minimum gap constraints above. For runtime `LODNode` configuration, the switch-in distances must be authored as: `lod0_switch_in = lod_distances[0] − 5` (or more) and `lod1_switch_in = lod_distances[1] − 10` (or more), matching the hysteresis table in the **LOD Distance Thresholds and Hysteresis** section above.

#### LOD File Naming Convention

- **Format-aware naming**: LOD variants use the suffix `_lod<N>` before the extension, and the format follows the asset format rule:
  - **Building assets** (LOD0, LOD1, LOD2 shell): must use `.b3d` format — `<asset_name>_lod0.b3d`, `<asset_name>_lod1.b3d`, `<asset_name>_lod2.b3d`. `.b3d` is mandatory to preserve UV channel 1 (lightmap UV) on all LOD levels.
  - **NOLIGHTMAP props only**: May use `<asset_name>_lod0.obj`, `<asset_name>_lod1.obj` where explicitly marked NOLIGHTMAP in asset metadata.
  - An export validation script must reject any `_lod0` or `_lod1` file for a building asset that is not `.b3d`.
- **Small Building / Prop LOD2 Asset Contract (floor-count conditional)**:
  The LOD2 strategy for small buildings and props is determined by `height_floors` in `<asset_name>.meta`. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`).
  - **height_floors <= 3**: These assets use the billboard imposter system at LOD2 and must **NOT** ship a `_lod2.b3d` file. They require a pre-baked imposter atlas named `<asset_name>_billboard.dds` (see `2d-texture-standards.md — Billboard Imposter Atlas` for the full format, mip chain, and sRGB upload path specification). The C++ `LODNode` loads `_lod0.b3d`, `_lod1.b3d`, and `_billboard.dds`. No `_lod2.b3d` is loaded or expected. The export validation script must flag any small building or prop asset with `height_floors <= 3` that has a `_lod2.b3d` file as an error.
  - **height_floors >= 4**: These assets are tall enough that a flat 128×128 px billboard frame cannot reproduce rooftop details and setbacks visible from the valid camera pitch range [−70°, −20°]. They must ship a `_lod2.b3d` geometry shell (300–500 tris) and must **NOT** rely solely on `_billboard.dds` at LOD2. The C++ `LODNode` loads `_lod0.b3d`, `_lod1.b3d`, and `_lod2.b3d`. No `_billboard.dds` is loaded or expected for LOD2 rendering (a `_billboard.dds` may still be authored for a lower-tier variant of the same zone slot if that variant has `height_floors <= 3`). The export validation script must flag any small building or prop asset with `height_floors >= 4` that lacks a `_lod2.b3d` file as an error (see check #11).
  - Large buildings always use `_lod2.b3d` (geometry shell), regardless of floor count. The validation script distinguishes asset categories via `<asset_name>.meta` which specifies `category: large_building | small_building | prop`.
- The C++ `LODNode` (or equivalent wrapper) loads all LOD variants at load time and swaps mesh buffers based on camera distance thresholds
- Billboard LOD (small buildings 100m+): camera-facing quad with a **pre-baked imposter atlas** rendered from the LOD1 mesh using **8 bake angles** (every 45°). The camera-facing quad UV is selected at runtime based on the angle between the camera and the building's facing direction (snapped to nearest 45°). See [`2d-texture-standards.md` — Billboard Imposter Atlas](../asset-standards/2d-texture-standards.md#billboard-imposter-atlas) for the full format, dimensions, mip chain, bake elevation, bake lighting, wrap mode, and sRGB upload path specification.
  - **Billboard floor count limit**: Billboard impostors are only valid for buildings with **height_floors <= 3** (≤ 9 m total height). Buildings with height_floors >= 4 exhibit unacceptable silhouette mismatch at billboard scale (the flat 128×128 px frame cannot reproduce rooftop details, setbacks, or step changes visible from the valid camera pitch range [−70°, −20°] for tall buildings). The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). **Runtime category reassignment rule**: When a zone tile auto-upgrades to a higher density tier (e.g., Low → Medium) and the new density tier's building variant has height_floors >= 4, the C++ `LODNode` must automatically switch from the billboard LOD2 path to the geometry-shell LOD2 path. The asset pipeline must provide a `_lod2.b3d` geometry shell for any building variant with height_floors >= 4. The `<asset_name>.meta` file must specify `height_floors` per variant; `LODNode` reads this at tile upgrade time and selects the correct LOD2 strategy. Buildings with height_floors >= 4 at any density tier must use the full LOD2 geometry shell (`_lod2.b3d`) at the 100 m distance threshold. The export validation script must check the `height_floors` field in `<asset_name>.meta` and flag small-building/prop assets with `height_floors >= 4` that do NOT have a `_lod2.b3d` file as an error (see check #11), and flag small-building/prop assets with `height_floors <= 3` that DO have a `_lod2.b3d` file as an error (see check #2).

#### Vehicle Polygon Budget (LOD0 / LOD1)

| Vehicle class | LOD0 budget | LOD1 budget |
|---|---|---|
| Car (sedan, hatchback, SUV) | ≤1,500 tris | ≤300 tris |
| Bus | ≤2,500 tris | ≤450 tris |
| Truck | ≤2,500 tris | ≤450 tris |

The LOD Requirements table above lists the general Vehicles row (1000–3000 tris LOD0, 200–500 tris LOD1) as a range covering all vehicle classes. The per-class caps above are the binding limits within that range. All vehicle assets must be exported as a **single solid mesh** (body + windows + wheels unified into one `IMesh`); modular sub-mesh assembly is not used for vehicles.

#### Vehicle UV Channel Convention

Vehicles use **UV channel 0 only** (diffuse/albedo atlas UV). UV channel 1 (lightmap baking) is **not required** for vehicles — vehicles are dynamic scene objects and do not participate in static lightmap baking. Exporting a UV channel 1 on vehicle assets is permitted but will be ignored by the runtime.

- **UV channel 0 layout**: Vehicle UVs must map into the vehicle diffuse atlas (512×512 px per vehicle type, packed into a 2048×2048 DDS DXT1 atlas — 16 vehicle types per sheet, named `vehicles_diffuse_atlas_d.dds`). Vehicle UV islands must not overlap and must stay within [0, 1] UV space. Blender export must use the vehicle's assigned atlas cell — UV coordinates must be authored in atlas space (e.g., a vehicle in cell row 0, column 2 uses U range [2/4, 3/4] on a 4×4 grid atlas).
- **UV channel 0 validation**: The export validation script must verify that all vehicle UV channel 0 coordinates fall within [0, 1] UV space and flag any vehicle with UV coordinates outside its assigned atlas cell.

- Vehicle point/sprite LOD (100m+): 16×16 px solid-color sprite; authored as part of a sprite atlas
- **Vehicle sprite LOD2**: 16×16 px sprite representing the vehicle's roof color/type, packed into `vehicles_sprite_atlas_d.dds`. The runtime draws a camera-facing billboard quad (1 m × 0.5 m) sampling the vehicle's assigned 16×16 px atlas cell. See `building-atlas-layout.md — Vehicle Sprite Atlas` for format, resolution, mip chain, and upload path.

#### Coordinate System Export Convention

- Artists export from Blender using: **−Z Forward, Y Up** in Blender's export dialog — this produces Y-up, Z-forward output matching Irrlicht's coordinate expectations. **"Y Forward, Z Up" is INCORRECT** — it outputs Z-up (Blender native space) and will produce rotated assets in-engine. The correct setting is `-Z Forward, Y Up`.
- No runtime coordinate transform is applied on import; the export settings are the single source of truth
- Verify correct orientation on first export: the asset's front face must point down the +Z axis in Irrlicht's scene view

#### Collision Meshes

- Required for all buildings and terrain-blocking objects
- **Convex footprint** (simple/rectangular buildings): single file `<asset_name>_col.obj`; maximum **24 triangles**; convex hull; no UV unwrap required
- **Non-convex footprints** (L-shaped, U-shaped, or complex buildings): split into up to 3 convex sub-meshes named `<asset_name>_col_0.obj`, `<asset_name>_col_1.obj`, `<asset_name>_col_2.obj`. Each sub-mesh maximum 24 triangles. The C++ loader detects `_col_0` suffix and loads all numbered sub-mesh files for that asset.
  **Buildings requiring more than 3 convex sub-meshes** (e.g., H-shaped, cross-shaped, campus-style): redesign the building footprint to fit within 3 convex sub-meshes by simplifying the concavities — the road graph boundary system operates at tile resolution, so sub-tile concavity accuracy is not required. A conservative overestimate of the blocked area using 3 convex hulls is acceptable. If a 4th sub-mesh is genuinely necessary for structural reasons, the limit must be explicitly raised in both the C++ loader documentation and this spec entry. Do not silently ship a `_col_3.obj` file — the loader will not pick it up and the discrepancy will be undetected.
- **Curved/circular footprints** (towers, rotundas, stadiums): N-sided convex polygon prism (side faces only — no top/bottom caps; the physics system extrudes collision vertically), where **N ≤ 8 sides**. Side faces = N quads = 2N triangles; at N=8 this yields **16 triangles** (within the 24-triangle budget for standard footprints). Named `<asset_name>_col_circle.obj`. An 8-sided polygon approximation is sufficient for road graph boundary detection at typical city tile resolution. Do not use a rectangular hull for circular buildings — it incorrectly blocks road tiles adjacent to the building's curved face. Do not exceed N=8; at N=16 the side-faces-only count would be 32 triangles which exceeds the standard budget.
- **Very small props** (footprint radius < 4 m, i.e. less than one tile): a single 2-triangle quad collision mesh is sufficient.
- **Terrain decoration assets** (rocks, embankments, barriers that sit on sloped terrain): collision mesh must be authored relative to the **terrain-normal direction** at the asset's placement tile, not world Y-up. Bottom plane aligns to the terrain surface. Maximum **12 triangles** for decorative rocks and small barriers. Named `<asset_name>_col.obj`. For **procedurally generated terrain**, no artist-authored collision mesh is required — `TerrainChunk` exposes its heightmap grid directly to the buildability and road graph systems. Terrain decoration collision meshes follow standard convex footprint naming.
- **Collision mesh vertical extent**: All collision meshes (buildings and vehicles) use **side faces only — no top or bottom caps** because the physics and road-graph systems extrude collision vertically at runtime. **Artist-facing authoring rule**: collision meshes must be authored as **flat footprints at Y=0** (the ground plane). The C++ loader extrudes the collision volume to the building's total height (`height_floors × 3 m` from `<asset_name>.meta`) at runtime. Do NOT add top or bottom faces to collision meshes — they will not be used by the physics system and waste triangle budget. Do NOT offset collision mesh vertices below Y=0 — all collision footprint geometry must start at Y=0 (ground level). For terrain decoration assets placed on slopes, the collision mesh is authored in local space (Y=0 is the contact point with the terrain surface); the physics system applies the terrain-normal rotation after placement.
- **Vehicle collision meshes**: Vehicles use a simple rectangular bounding-box collision hull (`<vehicle_name>_col.obj`) with **8 triangles** (4 side faces only — no top/bottom caps; the physics system extrudes collision vertically, so 4 side quads = 8 triangles is sufficient). Vehicles are dynamic scene objects; their collision mesh is used only for road graph boundary detection, not physics. Footprint: the vehicle's maximum XZ extents at Y=0 plane. Named `<vehicle_name>_col.obj`. **Vehicle collision mesh LOD policy**: Vehicles use a **single collision mesh** across all LOD levels (LOD0, LOD1, Point/sprite) — the bounding-box hull does not change with visual LOD. At LOD2 (Point/sprite) the vehicle is still a collideable entity and the same `_col.obj` is used. No LOD-specific collision meshes are authored for vehicles.
- **Collision mesh loader dispatch order**: The C++ loader checks suffixes in this order to determine which collision mesh type to load: (1) `_col_0.obj` exists → load multi-convex non-convex set (load all numbered `_col_0`, `_col_1`, `_col_2` files); (2) `_col_circle.obj` exists → load circular N-sided prism; (3) `_col.obj` exists → load single convex hull; (4) none found → log error and skip collision registration. This order prevents `_col_0` from shadowing `_col` on assets that have both files present.
- A single 50-tri convex hull is **insufficient** for non-convex footprints — it will produce a collision volume that incorrectly blocks walkable/buildable areas inside concavities.
- Used by terrain buildability check (slope >15° detection) and traffic road graph boundary detection

#### Modular Building Kit

- Buildings assembled from reusable mesh modules: base, mid-floor, roof, facade details
- Module grid: 4 m × 4 m × 3 m per floor unit
- **Maximum floor count**: Large buildings have a **hard cap of 10 floors** (30 m total height at 3 m/floor). At 10 floors: assembled LOD0 maximum ≈ base (400) + 8 mid-floor (8×300=2,400) + roof (500) + 10 facade details (10×100=1,000) = 4,300 tris — within the 5,000 tri LOD0 budget. 11+ floors risk budget overrun. The 10-floor limit is enforced by the export validation script using the `height_floors` field in `<asset_name>.meta`; any override requires a polygon audit and explicit approval.
- **Pivot convention**: Pivot at bottom-center of footprint. For a standard 4×4×3 m unit, pivot is at (0,0,0) with geometry in X:−2 to +2, Y:0 to +3, Z:−2 to +2 local space. This is a **hard export requirement**.
- **Vertical geometry bounds**: Geometry must not exceed Y=3.0 (hard upper bound for floor modules). Wall tiles with decorative tops (parapets, cornices) must stay within the 3 m budget. Maximum tolerated vertex deviation from Y=0 (bottom) or Y=3.0 (top): **0.005 Irrlicht units (5 mm)**. This tolerance reflects practical floating-point precision limits in DCC tools — a 1 mm tolerance is unreliably tight for polygon modelling workflows. An export validation script checks all wall tile Y extents and rejects files that violate this tolerance.
- **LOD2 pivot conformance**: The LOD2 baked shell mesh pivot MUST be at bottom-center (X=0, Y=0, Z=0 relative to the building's ground footprint center) — identical to LOD0 and LOD1 pivot convention. Using the bounding box centroid (center of mass vertically) will produce a position pop at the LOD1→LOD2 transition equal to half the building height. LOD transitions from LOD1 modules → LOD2 shell must not produce a position pop. Asset sign-off checklist includes: "Stack two identical floor modules in Irrlicht scene view; confirm no visible gap at join." Also: "Verify LOD2 shell silhouette matches LOD1 assembled building silhouette within 10% area deviation when viewed from the 8 standard bake angles at 45° below horizontal (camera pitch = −45°)."
- **LOD2 shell lightmap requirement**: The LOD2 baked shell mesh must carry UV channel 1 (non-overlapping, covering the entire shell mesh) and be lightmap-baked to a dedicated `<asset_name>_lod2_lm.dds` texture at **256x256** resolution in **DDS DXT5/BC3 format** (quarter of the full-size lightmap, proportional to LOD2 viewing distance and reduced screen footprint; DXT5 used for consistency with full-resolution lightmaps and to allow alpha channel for AO data). Lightmap baking for LOD2 shells uses flat ambient-only lighting (same as billboard bakes) for consistency with the billboard system at similar distances. **LOD2 lightmap mip chain**: `_lod2_lm.dds` follows the **lightmap exemption rule** — lightmap textures (`_lm` suffix) are explicitly exempt from mip chain requirements. The LOD2 shell lightmap must be uploaded with `GL_TEXTURE_MAX_LEVEL = 0` (single mip level only), matching the lightmap exemption in `2d-texture-standards.md`. Do NOT generate a mip chain for `_lod2_lm.dds` — the VRAM budget calculation for LOD2 shell lightmaps assumes unmipmapped textures (0.0625 MB/texture at 256×256 DXT5, no ×1.33 overhead). Aliasing at distances beyond 200 m is acceptable for LOD2 shell lightmaps given the reduced screen footprint (typically fewer than 40 vertical pixels for a large building at 200 m). The absence of a mip chain is a deliberate VRAM budget tradeoff. The export validation script must NOT generate mip chains for `_lod2_lm.dds` files. The export validation script must verify UV channel 1 is present and non-degenerate on all `_lod2.b3d` building asset files.
- **LOD2 shell UV channel 0 (diffuse)**: The LOD2 shell UV channel 0 maps into the same 2048×2048 city building atlas as LOD0/LOD1 modules. The shell UV islands are authored to cover the atlas cells of its dominant facade materials. This preserves texture continuity across LOD transitions. The export validation script must verify that LOD2 UV channel 0 coordinates fall within [0, 1] UV space.
- **LOD2 baked shell lightmap blend mode**: The lightmap texture (UV channel 1, `_lod2_lm.dds`) is blended using **multiply blend mode at 100% opacity** over the diffuse. The runtime shader samples UV0 for diffuse and UV1 for lightmap, then multiplies: `finalColor = diffuseColor * lightmapColor`. No directional lighting is applied to LOD2 shell meshes (V1 scope) — the baked lightmap encodes all static shading. This is consistent with the flat ambient-only bake used for billboard LODs at similar distances.
- **Per-module polygon caps**:
  Per-module polygon caps (LOD0):
    Wall tile (mid-floor):       ≤300 triangles
    Base module (ground floor):  ≤400 triangles (lobby entry detail; slightly higher than mid-floor)
    Roof module:                 ≤500 triangles (parapets, HVAC, cornices within 3 m budget)
    Facade detail (snap-on):     ≤100 triangles per piece (balcony, pilaster, window bay)

  Per-module polygon caps (LOD1):
    Wall tile (mid-floor):       ≤75 triangles
    Base module:                 ≤100 triangles
    Roof module:                 ≤125 triangles
    Facade detail:               ≤25 triangles per piece

  LOD2: single hand-authored baked shell mesh (not assembled from modules) — ≤500 tris total for large building (300–500 tris range allows meaningful silhouette features)

  **Facade detail piece count cap**: A fully assembled LOD0 building must use **at most 10 facade detail pieces** (balcony, pilaster, window bay, etc.) per assembled stack across all sides. This cap prevents a runaway polygon total when decorative pieces are applied densely. At ≤100 tris per piece, 10 pieces = 1,000 tris maximum from facade details, keeping the compositional total within the 5,000 tri LOD0 budget for large buildings (base ≤400 + N × wall ≤300 + roof ≤500 + 10 × facade ≤100 = stays within budget for typical floor counts). The export validation script must count facade detail instances in a representative assembled stack and reject configurations exceeding 10 pieces.

  **Compositional budget check (mandatory export validation)**:
  A fully assembled building (base + N floor tiles + roof + facade details) must not exceed:
    LOD0 assembled total: 5,000 triangles (large building), 1,500 triangles (small/prop building)
    LOD1 assembled total: 1,000 triangles (large building), 300 triangles (small/prop building)
  The export validation script must measure assembled totals for a representative N-floor stack at
  each density tier and reject any combination that exceeds these limits.

  **Export validation script — required checks**: The export validation script (`tools/validate_assets.py` or equivalent) must perform all 14 required checks and produce a per-asset PASS/FAIL report:
  1. Building `_lod0`, `_lod1` files use `.b3d` format (not `.obj`).
  2. Small building / prop `_lod2.b3d` file presence is floor-count conditional (read `height_floors` from `<asset_name>.meta`): if `height_floors <= 3`, the asset must NOT have a `_lod2.b3d` file (flag its presence as an error) and must have a `_billboard.dds` instead; if `height_floors >= 4`, the asset must have a `_lod2.b3d` geometry shell (flag its absence as an error) and must NOT rely on `_billboard.dds` for LOD2 rendering. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). (See check #11 for the symmetric geometry-shell presence requirement.)
  3. Large building `_lod2.b3d` is present, within 300–500 tri budget, and uses **DXT5/BC3 format for `_lod2_lm.dds`** (not DXT1). Validate by reading the DDS fourCC. DXT1 on a LOD2 lightmap is a silent error — DXT5 is required to preserve the alpha channel for ambient occlusion data.
  4. UV channel 0 coordinates on all LOD levels fall within [0, 1] UV space.
  5. UV channel 1 (lightmap) is present and non-degenerate on all building `.b3d` files (non-overlapping islands).
  6. Assembled LOD0 total ≤ 5,000 tris (large) or ≤ 1,500 tris (small) for a representative N-floor stack.
  7. Facade detail piece count ≤ 10 per assembled stack.
  8. Asset pivot at bottom-center (Y=0); geometry Y extent stays within [0, 3.0] per floor module (tolerance **0.005 units / 5 mm**).
  9. LOD distance hysteresis ≥ 5 m (close), ≥ 10 m (far) — validated by reading LODNode config, not geometry.
  10. Vehicle UV channel 0 coordinates fall within the asset's assigned atlas cell (see Vehicle Atlas Cell Registry below).
  11. Small building / prop assets with `height_floors >= 4` must have a `_lod2.b3d` geometry shell (not just billboard). Conversely, small building / prop assets with `height_floors <= 3` must NOT have a `_lod2.b3d` file — they use point-sprite LOD2 only. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`).
  12. Vehicle normal map UV channel 0 coordinates fall within the asset's assigned atlas cell in `vehicles_normal_atlas_n.dds` (8×8 grid of 256×256 cells in 2048×2048; vehicle row/column assignments match the diffuse atlas registry in `vehicle_atlas_registry.json` — same row R and column C, but cell UV range is `U ∈ [C/8, (C+1)/8]`, `V ∈ [R/8, (R+1)/8]` since the normal atlas has an 8×8 grid). The V-axis origin convention (OpenGL, V=0 at bottom, row 0 is the bottom row) applies identically to normal atlas UV verification — artists must apply V-flip (`V_opengl = 1 − V_blender`) when authoring UV islands for the normal atlas in Blender, using the same convention documented in the Vehicle Atlas Cell Registry.
  13. Facade atlas cell pixels — all non-transparent pixel content falls within the [8, 504] texel range on both U and V axes per 512×512 cell (496×496 usable zone; 8-texel border on each edge). Validate by reading pixel alpha values in the border zone for each cell in the 2048×2048 building atlas.
  14. `.meta` sidecar file presence — every `.b3d` building or vehicle file must have a corresponding `<asset_name>.meta` sidecar file. Missing sidecar: validation error. This check must be present in the Phase 9 `validate_assets.py` extension and active from Phase 9 onward.
  The script must be run as part of the asset pipeline before any asset is checked into the repository. CI must run the script and fail the build if any asset fails validation.

  **Vehicle Atlas Cell Registry**: Each vehicle type must be assigned a unique atlas cell in `vehicles_diffuse_atlas_d.dds` (2048×2048 DDS DXT1, 4×4 grid of 16 cells at 512×512 px each). The registry is maintained in `tools/vehicle_atlas_registry.json` and must be updated whenever a new vehicle type is added.

  **Vehicle Atlas Registry sign-off**: Before Phase 6 vehicle UV authoring begins, `graphics-artist-3d-model` must review and co-sign `tools/vehicle_atlas_registry.json` alongside `graphics-dev-irrlicht`. Specifically, `graphics-artist-3d-model` must confirm:

  1. The V-flip convention (`V_opengl = 1 - V_blender`) is documented correctly for each vehicle type
  2. The atlas cell boundary UV formulas for the 4×4 diffuse grid and 8×8 normal grid are correct
  3. All five V1 vehicle type assignments (car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo) are in the correct cell positions

  This co-sign-off is a Phase 4 exit criterion alongside the existing schema conformance check.

  ARTIST UV WARNING — V-axis convention: The atlas UV formula above uses OpenGL convention (V=0 at bottom-left of the atlas, V increases upward). Blender's UV editor shows V=0 at the top (V increases downward). Any artist authoring vehicle UV islands in Blender MUST apply `V_opengl = 1 - V_blender` before setting final UV coordinates in atlas space. Failure to apply this flip will place UV islands in the mirror-image vertical position, causing the vehicle to sample texture data from an adjacent atlas cell. The export validation script (check #10) uses OpenGL convention to verify coordinates — V-flipped Blender values will fail this check.

  Format:

  ```json
  {
    "atlas_file": "vehicles_diffuse_atlas_d.dds",
    "grid": { "cols": 4, "rows": 4, "cell_size_px": 512 },
    "normal_atlas_file": "vehicles_normal_atlas_n.dds",
    "normal_atlas_grid": { "cols": 8, "rows": 8, "cell_size_px": 256 },
    "_comment_normal_atlas": "normal atlas uses same row/col assignments but 8x8 grid, 256x256 cell_size_px",
    "assignments": [
      { "vehicle_type": "car_sedan",   "row": 0, "col": 0 },
      { "vehicle_type": "car_hatchback", "row": 0, "col": 1 },
      { "vehicle_type": "car_suv",     "row": 0, "col": 2 },
      { "vehicle_type": "bus_standard","row": 1, "col": 0 },
      { "vehicle_type": "truck_cargo", "row": 1, "col": 1 }
    ]
  }
  ```

  The export validation script reads this registry when checking vehicle UV channel 0 coordinates (check #10). A vehicle with no registry entry fails validation. A vehicle with UV coordinates outside its assigned cell fails validation. **Atlas UV calculation**: For a cell at (row R, col C) on a 4×4 grid, the atlas UV range is `U ∈ [C/4, (C+1)/4]`, `V ∈ [R/4, (R+1)/4]`. **V-axis origin convention (OpenGL)**: This formula uses **OpenGL UV convention** — V origin is at the bottom-left of the atlas; V increases upward; row 0 (R=0) is the BOTTOM row. DDS files store texels top-row-first, and Blender's UV editor shows V=0 at the top. Artists authoring vehicle UV islands in Blender must apply V-flip (`V_opengl = 1 − V_blender`) before mapping to atlas cells. The export validation script must use the OpenGL convention when checking UV coordinates against assigned cells.
