# 3D Model Standards

- **All building assets (animated and static)**: `.b3d` (Blitz3D format — Irrlicht native, supports multiple UV channels including UV2/lightmap). `.b3d` is mandatory for any asset that participates in the lightmap baking pipeline.
- **Simple props without lightmaps**: `.obj` is acceptable only for props that are explicitly exempt from the UV2/lightmap requirement (e.g., 1-UV-channel street furniture, terrain-level decorations). Such assets must be marked `NOLIGHTMAP` in their asset metadata. `.obj` has no native multi-UV support; importing a multi-UV `.obj` will silently lose UV channel 1.
- **Coordinate system**: Y-up, Z-forward, **left-handed** (Irrlicht convention). Irrlicht uses a left-handed coordinate system: +X right, +Y up, +Z forward (into the screen). This is the opposite handedness from Blender's right-handed default. See the **Coordinate System Export Convention** section below for Blender export settings. Never label the coordinate system as "right-handed" — that produces a mirrored/rotated result in Irrlicht.
- **Unit scale**: 1 Irrlicht unit = 1 meter

## LOD Requirements (mandatory for all city assets)

| Asset category | LOD0 (near) | LOD1 (mid) | LOD2 (far) |
|---|---|---|---|
| Large buildings (general) | 4,000–8,000 tris | 1,000–1,500 tris | 400–600 tris |
| Large buildings — Commercial High only (skyscrapers) | 7,000–10,000 tris | 1,200–2,000 tris | 500–700 tris |
| Small buildings / props (height\_floors <= 3) | 1,500–3,000 tris | 200–400 tris | Billboard (point-sprite only) |
| Small buildings / props (height\_floors >= 4) | 1,500–3,000 tris | 200–400 tris | 400–600 tris (`_lod2.b3d` geometry shell) |
| Service buildings (`fire_station`, `police_station`, `power_plant`, `water_tower`) | 2,000–4,000 tris | 200–400 tris | Billboard |
| Vehicles (cars) | ≤2,000 tris | ≤400 tris | Point/sprite |
| Vehicles (bus, truck) | ≤3,000 tris | ≤500 tris | Point/sprite |
| Vehicles (general indicative range) | 1,000–3,000 tris (indicative range — see per-class table in § Vehicle Polygon Budget for binding limits) | 200–500 tris (indicative range — see per-class table for binding limits) | Point/sprite |
| Terrain chunk (64×64 m) | 32×32 quad grid | 16×16 quad grid | 8×8 quad grid |
| Road tile (10×10 m) | ≤50 tris (flat quad + kerb geometry + center-line strip; Phase-11h adds a 2-tri center-line quad bringing the total from the prior ≤48 to ≤50) | ≤16 tris (flat quad only) | ≤8 tris (single quad) |
| Infrastructure props (lamp posts, signs) | ≤300 tris | ≤75 tris | Billboard (same system as small buildings) |

**Commercial High skyscraper sub-row**: The `com_high_*` row (7,000–10,000 tris LOD0) applies
exclusively to V1 skyscrapers — glass towers with `height_floors` 15–30. These buildings feature
stepped or tapered forms, glass curtain-wall facades, and distinctive crown treatments (spire,
antenna cluster, or setback pyramid) that require a higher polygon budget to preserve their
silhouette fidelity at LOD0 and LOD1 viewing distances. See the **Commercial High Skyscraper
Standards** section below for full design requirements.

**Road tile LOD thresholds**: Road tiles use the same LOD distance thresholds as small buildings/props (LOD0→LOD1 at 30 m / 25 m; LOD1→LOD2 at 100 m / 90 m). At LOD2 (>100 m), road tiles are rendered as flat coloured quads with no kerb or road marking geometry — road marking decals from the road atlas are disabled at LOD2. **Road LOD2 color source**: The LOD2 road quad color is sampled from the road tileable texture's average color, computed at asset pipeline generation time and stored as a named constant `RenderConstants::road_lod2_color` (type `irr::video::SColor`) in `src/rendering/render_constants.h`. This value must be a perceptual match of the center region of `road_asphalt_tileable.dds` when viewed in linear space (approximately a mid-dark gray, e.g. SColor(255, 60, 60, 60) for standard asphalt). Do NOT hardcode a magic color literal inline in rendering code — always use `RenderConstants::road_lod2_color` so that the color is updated in one place when the road texture changes. The LOD2 road quad does NOT bind a texture — it is drawn as a flat-shaded quad using the material's vertex color channel, set to `road_lod2_color` at entity construction time.

**Road tile mesh authoring source (binding decision, `graphics-artist-3d-model`, 2026-03-04)**: Road tile LOD0 and LOD1 geometry is **procedurally generated in C++ at runtime via `SMesh`/`IMeshBuffer`** — no `.b3d` file is authored on disk for road tiles. `IrrlichtRenderer::placeRoadMesh()` constructs the LOD0 quad+kerb mesh (≤50 tris) and LOD1 flat quad mesh (≤16 tris) directly in code using hardcoded vertex data for a 10 m × 10 m tile. The LOD2 flat colored quad is also constructed in code (≤8 tris, `road_lod2_color` vertex color, no texture). Rationale: (a) road tiles do not participate in the lightmap baking pipeline and therefore do not require UV channel 1 or the `.b3d` format; (b) the road custom shader binds `road_asphalt_tileable.dds` via the raw GL path, which is incompatible with a standard `IMeshSceneNode` loaded from a `.b3d` file via the Irrlicht mesh loader; (c) no road tile `.b3d` filename appears in any phase deliverable — road geometry is implicitly a code deliverable of `graphics-dev-irrlicht`, not an artist asset. **Artist action: none**. No road tile `.b3d`, `.obj`, or `.meta` file is required from the 3D model artist pipeline. The `validate_assets.py` script must NOT look for road tile `.b3d` files — they do not exist. Road tile UV-channel 0 tiling is specified in the road shader (UV tiles 2× per 10 m road quad — both U and V scale by 2.0 in the vertex shader), not authored per-asset.

**Carriageway width**: The asphalt surface covers **7.5 m** of the 10 m tile width (¾ of the tile). The remaining 1.25 m on each side is rendered as a kerb/verge strip using bevelled edge strips. The carriageway is centered within the tile.

**Center-line strip**: A 0.3 m wide white painted strip implements a two-way road divider.
Its orientation depends on the tile direction detected by `placeRoadMesh()`:

- **N/S tile** (`isEW = false`): strip runs along the local Z-axis at X = 0 (south to north).
- **E/W tile** (`isEW = true`): strip runs along the local X-axis at Z = 0 (west to east);
  heights are interpolated at Z = 0 from the west-pair corners and the east-pair corners.

The strip is part of the LOD0 road mesh (mesh buffer index 3), implemented as a thin raised
quad (+0.005 m Y above the asphalt surface) with white vertex color (`SColor(255, 255, 255,
255)`) and `EMT_SOLID` material. `PolygonOffsetFactor = 5` (one step above the carriageway's
`factor = 4`) is set on this buffer at mesh-creation time and **must NOT be overwritten** by
the post-bind material loop in `placeRoadMesh()` — that loop must skip buffer index 3 when
resetting `PolygonOffsetFactor`. The strip does NOT appear at LOD1 or LOD2.

**Lane layout** (two-way, keep-right):

- **Left lane** (local X = −1.875 m center, 3.6 m wide): vehicle agents traveling in the **−Z direction** (southbound).
- **Right lane** (local X = +1.875 m center, 3.6 m wide): vehicle agents traveling in the **+Z direction** (northbound).
- E/W tiles build carriageway geometry oriented along X (`isEW = true` in `buildTileRoadMesh`)
  so the same lane rules hold in all cardinal directions. Scene-node Y-rotation is not used
  (vertex Y heights are baked in world space; rotation would mismap corner heights).

Named constants (declared in `src/rendering/render_constants.h`):

```cpp
static constexpr float kLaneCenterOffset = 1.875f;   // metres from road center
static constexpr float kCarriagewayHalfWidth = 3.75f; // half of 7.5 m carriageway
```

The road kerb geometry vertices are authored inline in `IrrlichtRenderer` as a unit of 4 bevelled edge strips (each strip = 6 tris, 4 strips = 24 tris) plus a central flat quad (2 tris), totaling 26 tris for LOD0, plus the center-line strip (2 tris) = 28 tris — well within the ≤50 tri budget (Phase 11h raised from ≤48 to accommodate the 2-tri center-line quad). LOD1 is a single flat quad (2 tris) with no kerb or center-line, within the ≤16 tri budget.

**Note on large building LOD2 budget**: 400–600 tris is required to represent building silhouettes (setbacks, rooftop details, entry bays) at the 185–200 m switch-in distance where tall buildings still occupy 50–80 vertical pixels. A 100–200 tri cap produces a featureless slab that is visually jarring against LOD1 counterparts.

### LOD Distance Thresholds and Hysteresis

**Hysteresis bands are mandatory** to prevent LOD thrashing (continuous mesh rebind stutter) when the camera sits near a threshold:

| Asset category | LOD0→LOD1 switch-out | LOD0→LOD1 switch-in | LOD1→LOD2 switch-out | LOD1→LOD2 switch-in |
|---|---|---|---|---|
| Large buildings | > 50 m | < 45 m | > 200 m | < 185 m |
| Small buildings / props | > 30 m | < 25 m | > 100 m | < 90 m |
| Service buildings (`fire_station`, `police_station`, `power_plant`, `water_tower`) | > 30 m | < 25 m | > 100 m | < 90 m |
| Vehicles | > 40 m | < 35 m | > 100 m | < 90 m |
| Terrain chunk | > 100 m | < 92 m | > 300 m | < 285 m |
| Road tile | > 30 m | < 25 m | > 100 m | < 90 m |
| Infrastructure props | > 30 m | < 25 m | > 100 m | < 90 m |

**Service buildings thresholds**: Service buildings (`fire_station`, `police_station`, `power_plant`, `water_tower`) are treated as a subtype of small buildings (`height_floors <= 3`) and use identical LOD distance thresholds. All four V1 service building types have `height_floors = 2`.

**Road tile and Infrastructure props thresholds**: Road tiles and infrastructure props (lamp posts, signs) use the same thresholds as Small buildings/props (5 m close hysteresis, 10 m far hysteresis). Road tile LOD2 is a flat colored quad — not a billboard imposter — consistent with the road-tile LOD2 specification in the LOD Requirements table above.

`LODNode` stores the **last active LOD level** and transitions only when the camera crosses the directional threshold for that level. It never transitions up and down in the same frame. **Hysteresis ranges are mandatory: ≥5 m for close thresholds (LOD0↔LOD1) and ≥10 m for far thresholds (LOD1↔LOD2).** The table above satisfies these requirements: small buildings/props close gap = 5 m (30−25), far gap = 10 m (100−90); vehicles close gap = 5 m (40−35), far gap = 10 m (100−90). Large buildings close gap = 5 m (50−45), far gap = 15 m (200−185). Terrain chunk gap = 8 m and 15 m respectively (terrain uses 8 m close gap by exception — camera is almost always moving when terrain chunks rebind, making 5 m precision unnecessary; the far 15 m gap exceeds the 10 m minimum).

LOD meshes are exported as separate meshes and swapped in code by distance using Irrlicht's scene manager.

#### Camera Pitch Range

- Camera pitch is defined as the angle (degrees) between the camera's forward vector and the world XZ plane, measured as negative = looking downward. Valid range: **[−70°, −20°]** (always looking downward in city view).
- **Minimum look-down angle**: −20° (shallow oblique — prevents looking nearly level at the horizon, which would cause z-fighting and poor city readability).
- **Maximum look-down angle**: −70° (steep overhead — prevents gimbal lock near top-down view).
- This range is enforced by `CameraController`; the billboard bake elevation is **45° below horizontal (camera pitch = −45°)**, the midpoint of the [−70°, −20°] operating range, minimising average mismatch error across all valid camera angles.
- **Sign-off status**: CONFIRMED — camera pitch range [−70°, −20°] and bake midpoint −45° are final. Camera pitch for billboard baking: −45° below horizontal (confirmed). Reviewed and approved by: graphics-artist-3d-model. Phase 9 billboard bake pipeline may proceed once the Phase 1 dated sign-off record below is on record.

**Phase 1 dated sign-off record** (required before Phase 1 exit):

> Phase 1 sign-off — 2026-02-21: Camera pitch range [−70°, −20°] and bake midpoint −45° verified as final. This record confirms the Phase 1 exit criterion for the camera pitch sign-off gate is satisfied. Signed: graphics-artist-3d-model.

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
- `res_low_02_lod0.b3d` — Residential Low tier, variant 2, LOD0 geometry
- `res_low_03_lod0.b3d` — Residential Low tier, variant 3, LOD0 geometry
- `res_low_04_lod0.b3d` — Residential Low tier, variant 4, LOD0 geometry
- `com_high_03_lod2.b3d` — Commercial High tier (skyscraper), variant 3, LOD2 shell
- `res_low_01_billboard.dds` — Residential Low tier, variant 1, billboard atlas (height_floors ≤ 3)

The `<asset_name>` base (e.g. `res_low_01`) is referenced in `<asset_name>.meta` for `height_floors`, `category`, and atlas cell assignments. The C++ `BuildingAssetLoader` parses the naming convention to construct LOD file paths — do not use ad-hoc per-building naming.

#### Commercial High Skyscraper Standards

`com_high_*` buildings are V1 skyscrapers: glass towers with `height_floors` 15–30, stepped or
tapered form, glass curtain-wall facade, and a distinctive crown. These assets are subject to
the Commercial High sub-row budgets in the LOD Requirements table (7,000–10,000 tris LOD0,
1,200–2,000 tris LOD1, 500–700 tris LOD2 geometry shell).

**Design requirements** (binding for all four V1 `com_high_*` variants):

- **Floor count**: `height_floors` must be in the range 15–30 for all `com_high_*` variants.
- **Form language**: Each variant must have a distinct massing silhouette chosen from:
  - `com_high_01`: narrow tower (tall, slender rectangular shaft)
  - `com_high_02`: wide slab (broad, flat rectangular form)
  - `com_high_03`: tapered pyramid (floor plates that step inward as they rise)
  - `com_high_04`: stepped ziggurat (tiered horizontal setbacks at regular intervals)
- **Facade**: Glass curtain-wall material using the `wall_commercial_high` atlas cell
  (row 2, col 1). Horizontal spandrel bands are permitted as facade articulation detail.
- **Crown treatment**: Each variant must have a unique top treatment. Approved crown types:
  - Spire (tapered needle or broadcast antenna cluster)
  - Setback pyramid (faceted glass cap)
  - Antenna cluster (multi-element broadcast or cellular array)
  - Flat mechanical penthouse (equipment enclosure with parapet)
  No two `com_high_*` variants may share the same crown type.
- **LOD2 strategy**: `height_floors >= 4` — all `com_high_*` variants must ship
  `_lod2.b3d` geometry shells (500–700 tris). No `_billboard.dds` is used at LOD2 for
  these assets. The geometry shell must preserve the crown silhouette and overall
  massing outline visible at 185–200 m.
- **Variant count**: exactly four variants (`com_high_01` through `com_high_04`),
  consistent with the binding 4-variant-per-zone-tier policy.

#### Variant Selection Policy (Round-Robin, Phase 11)

**Phase 10 note**: Phase 10 always uses variant `_01` for every zone/tier combination — `assetBaseName` is always `"<zone>_<tier>_01"` (e.g. `"res_low_01"`, `"com_med_01"`). The round-robin counter described below is a Phase 11 enhancement; do NOT implement the counter in Phase 10. This keeps Phase 10's `CitySimulation` scope unambiguous and makes mock-renderer test assertions deterministic.

**Phase 11 and later**: When `CitySimulation` places a zone tile, it selects a visual building variant from the available variants for that zone-tier combination using a **per-zone-tier round-robin counter**. The policy is:

- `CitySimulation` maintains one `int` counter per unique zone-tier combination (9 combinations in V1: Res/Com/Ind × Low/Med/High). Each counter starts at `0` and increments by `1` on every successful placement for that zone-tier combination.
- The variant index is `(counter % numVariants) + 1`, formatted as a zero-padded 2-digit string (`01`, `02`, `03`, `04`). `numVariants` is `4` for V1 (four variants per zone-tier slot). Service buildings have no variant system and do not use this counter.
- The resulting `assetBaseName` passed to `IRenderer::placeBuildingMesh()` is `<zone>_<tier>_<variant>` (e.g. `"res_low_01"`, `"res_low_02"`, `"res_low_03"`, `"res_low_04"`, `"res_low_01"`, …).
- After `CitySimulation::doDensityUnlockTick()` upgrades a tile to a higher density tier, the NEW `assetBaseName` uses the upgraded tier's round-robin counter (not the original tier's counter). Example: a tile originally placed as `"res_low_02"` that upgrades to Medium becomes `"res_med_<N>"` where `<N>` is the current Residential/Medium counter value.
- The counter is a plain `int` member of `CitySimulation` per zone-tier slot. **Prior to Phase 11**, the counters are not persisted in the save file — on load, all existing zone tile variants are read from the tile's stored `assetBaseName` field in the save data, not recomputed from the counter; only newly placed tiles after a load use the counter (starting from 0), so save/load does not change existing visible building variants. **From Phase 11 onwards**, all 9 `m_buildingVariantCounters` are serialized to and deserialized from the save file so that post-load placements continue the pre-save sequence without restarting at 0; see `architecture/game-design/save-system.md`.
- **No-repeat guarantee**: the round-robin cycles through the four V1 variants (01, 02, 03, 04, 01, 02, …) without shuffling or RNG. This is intentional — using `ISimulationRNG` for variant selection would couple visible-asset selection to the simulation RNG stream, making reproduction of RNG-dependent events (service degradation, loan issuance) dependent on the number of tiles placed, which would break deterministic test replay. Visual variant selection MUST NOT use `ISimulationRNG`.
- **Counter storage location**: `CitySimulation` stores the nine counters as `std::array<int, 9> m_buildingVariantCounters` (indexed by `zone * 3 + tier` where `zone` = 0/1/2 for Res/Com/Ind and `tier` = 0/1/2 for Low/Med/High), initialised to `{0}` in the constructor initialiser list.
- **Service buildings**: Service buildings (`svc_fire_station`, `svc_police_station`,
  `svc_power_plant`, `svc_water_tower`) have **no variant system**. Each is a single
  unique model. There is no round-robin counter for service buildings, and no variant
  suffix (`_01`, `_02`, etc.) appears in their filenames. The `placeServiceBuildingMesh()`
  call always uses the canonical base name directly (e.g. `"svc_fire_station"`).

**`assetBaseName` construction helper** (implement as a `static` free function in `CitySimulation.cpp`):

```cpp
static std::string buildingAssetBaseName(ZoneType zone, DensityTier tier, int variantCounter) {
    static const char* zoneStr[]  = {"res", "com", "ind"};
    static const char* tierStr[]  = {"low", "med", "high"};
    static const int numVariants  = 4;   // V1 constant
    int variantIdx = (variantCounter % numVariants) + 1;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s_%s_%02d",
        zoneStr[static_cast<int>(zone)],
        tierStr[static_cast<int>(tier)],
        variantIdx);
    return buf;
}
```

This function is internal to `CitySimulation.cpp` — do not expose it through `ICitySimulation`.

#### `BuildingAssetLoader` LOD Loading Contract

`BuildingAssetLoader::load(assetBaseName)` loads **all three LOD variants** (LOD0, LOD1, and LOD2 or billboard, depending on `height_floors`) at load time, per the `LODNode` design in `3d-model-standards.md`:

> "The C++ `LODNode` (`src/rendering/LODNode.h`) loads all LOD variants at load time and swaps mesh buffers based on camera distance thresholds."

The load sequence for a given `assetBaseName` (e.g. `"res_low_01"`):

1. Read `assets/3d/buildings/<assetBaseName>.meta` to obtain `height_floors`, `category`, `atlas_cell`, and `lod_distances`.
2. Load `assets/3d/buildings/<assetBaseName>_lod0.b3d` (always required — missing file: log warning, return null).
3. Load `assets/3d/buildings/<assetBaseName>_lod1.b3d` (always required — missing file: log warning, return null).
4. If `height_floors <= 3`: load `assets/3d/buildings/<assetBaseName>_billboard.dds` (billboard atlas for LOD2). No `_lod2.b3d` is loaded or expected.
5. If `height_floors >= 4`: load `assets/3d/buildings/<assetBaseName>_lod2.b3d` (geometry shell for LOD2). No `_billboard.dds` is loaded or expected for LOD2.
6. Return a `BuildingAsset` struct containing all loaded mesh pointers / texture handles and the parsed `.meta` fields.

`IRenderer::placeBuildingMesh()` passes the returned `BuildingAsset` to `SceneEntityManager::spawnBuilding()` which constructs the `LODNode` with all three LOD resources. The phrasing "load the LOD0 `.b3d` mesh" in the Phase 10 deliverable description is shorthand for the full load sequence above — `placeBuildingMesh()` loads all LOD variants, not only LOD0.

#### World-Space Tile Positioning (`kTileSize`)

Zone building and road tile scene nodes are placed at world position:

```text
X = tileX * kTileSize
Y = 0.0f          (pivot sits exactly at ground plane; terrain height not used in V1)
Z = tileZ * kTileSize
```

**`kTileSize` value**: `10.0f` Irrlicht units (10 metres). Each simulation tile occupies a 10 m × 10 m footprint. This is consistent with the road tile LOD budget (road tile mesh = 10×10 m quad) and `CitySimulation::kTileSizeMeters = 10.0f` used for travel-time and coverage-radius computations.

**Declaration**: `kTileSize` is declared as `static constexpr float kTileSize = 10.0f;` directly on `IrrlichtRenderer` in `src/rendering/IrrlichtRenderer.h`. It is used by `IrrlichtRenderer::placeBuildingMesh()`, `placeRoadMesh()`, and `placeServiceBuildingMesh()`. Do NOT hardcode the literal `10.0f` at call sites — always use `kTileSize` so that if the tile size changes (e.g., for a future map scale change), all placement calls update in one place.

**Service building tile footprint**: Service buildings occupy a **2×2 tile (20 m × 20 m) footprint** in V1. The origin tile is the bottom-left corner (`tileX, tileZ`); the placed scene node's world X/Z origin is the centre of the 2×2 footprint: `worldX = (tileX + 1.0f) * kTileSize`, `worldZ = (tileZ + 1.0f) * kTileSize` (i.e. `N=2 → N*0.5=1.0`). The mesh is authored at ±10 m half-extent (native world scale, no runtime `setScale()`); it must not visually exceed the 20 m × 20 m footprint at LOD0. All four tiles in the footprint are marked occupied; road adjacency requires at least one road tile edge-adjacent to any footprint tile.

**Zone building footprint constraint**: Zone building (res/com/ind) geometry is authored at **native world scale** — 1 Blender unit = 1 m, no runtime `setScale()`. The local-space half-extent in X and Z is tier-dependent: `low` = ±5 m (10 m × 10 m footprint, 1×1 tile), `med` = ±10 m (20 m × 20 m, 2×2 tiles), `high` = ±15 m (30 m × 30 m, 3×3 tiles). Service buildings are authored at ±10 m half-extent (20 m × 20 m, 2×2 tiles). See the **Native-size authoring convention** table in the Multi-Tile Footprint section below for the authoritative half-extent values. The old ±2 m / `setScale(2.5f)` convention is **retired as of Phase 9** — do NOT author assets at ±2 m. Any geometry exceeding the tier's native half-extent in X/Z will visually intersect adjacent tiles at runtime.

#### Multi-Tile Footprint

**Tile footprint by density tier** (binding):

| Density tier | Tile footprint | World footprint |
|---|---|---|
| `low` (res/com/ind) | 1×1 tiles | 10 m × 10 m |
| `med` (res/com/ind) | 2×2 tiles | 20 m × 20 m |
| `high` (res/com/ind) | 3×3 tiles | 30 m × 30 m |
| Service buildings | 2×2 tiles | 20 m × 20 m |

**Native-size authoring convention**: Zone buildings and service buildings are authored at real-world scale (1 Blender unit = 1 m). Each density tier has its own correctly-sized model. **No runtime `setScale()` is applied** — `placeBuildingMesh()` and `placeServiceBuildingMesh()` place nodes at scale 1.0. The old ±2 m authoring convention is **retired**; Phase 9 assets must be re-exported at native world size.

| Density tier | Local-space half-extent | World footprint |
|---|---|---|
| `low` (res/com/ind) | ±5 m | 10 m × 10 m |
| `med` (res/com/ind) | ±10 m | 20 m × 20 m |
| `high` (res/com/ind) | ±15 m | 30 m × 30 m |
| Service (2×2) | ±10 m | 20 m × 20 m |

**Collision registration and simulation ownership**: All tiles in the footprint are marked occupied. The **origin tile** is the bottom-left corner (`tileX, tileZ`). The placed scene node's world origin is the **center of the full footprint**:

```text
worldX = (tileX + N * 0.5f) * kTileSize   where N = footprint tile count per side
worldZ = (tileZ + N * 0.5f) * kTileSize
```

Examples: LOW (N=1) → `(tileX + 0.5f) * 10` (tile centre); MED (N=2) → `(tileX + 1.0f) * 10`; HIGH (N=3) → `(tileX + 1.5f) * 10`.

**Ground quad coverage rule**: Every building B3D must include a ground quad (tarmac, garden, paving, or gravel) that covers the **full N×N tile footprint** — `(-N*5, N*5, -N*5, N*5)` in local space. This prevents bare terrain showing through around the building. In `generate_b3d_models.py` this is enforced via `FOOTPRINT_HALF[tier]`: LOW=5 m, MED=10 m, HIGH=15 m, SVC=10 m. Building structure geometry (walls, roofs) must not exceed the footprint half-extent in X/Z.

**Zone-based ground plate defaults**: When no variant-specific override is defined, the ground quad material defaults by zone:

- **Residential** (Low, Med, High) → garden (grass green)
- **Commercial** → paving (gray concrete)
- **Industrial** → paving (gray concrete)
- **Service** → paving for civic/emergency buildings (fire station, police station); gravel for utility buildings (power plant, water tower)

These are "if not specified" defaults. Specific variants may use a different ground type for artistic reasons — for example, a residential variant with an urban tarmac forecourt is permitted. The pool ground type is always a variant override, never a zone default.

**LOW-tier bungalow exception**: Variant 04 (`res_low_04`) is a bungalow whose box was `10×10 m` (matching the tile exactly). It has been reduced to `8×8 m` so the 1 m tarmac border around the building remains visible — consistent with all other LOW-tier variants (`8 m` wide).

**Road adjacency for multi-tile buildings**: At least one road tile must be edge-adjacent (4-directional cardinal, distance = 1) to **any tile in the footprint** — not only the origin tile.

#### `.meta` Sidecar File Format

Every `.b3d` building or vehicle asset must ship a `<asset_name>.meta` JSON sidecar (check #15 in the export validation script). Required fields:

```json
{
  "category": "large_building",
  "height_floors": 4,
  "atlas_cell": { "row": 0, "col": 2 },
  "lod_distances": [45.0, 185.0, 250.0]
  // Values must match the LOD Distance Thresholds table for the asset's category:
  // 45.0  = LOD0→LOD1 switch-in for large_building (table: switch-in < 45 m)
  // 185.0 = LOD1→LOD2 switch-in for large_building (table: switch-in < 185 m)
  // 250.0 = cull distance (65 m beyond LOD1→LOD2 switch-in, satisfying minimum 10 m margin)
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `category` | string | yes | One of `large_building`, `small_building`, `prop`, `vehicle`. Controls LOD2 strategy selection (billboard vs geometry shell) and export validation checks. |
| `height_floors` | integer | yes | Total floor count. Used by export validation check #2 (billboard absent and `_lod2.b3d` absent when `height_floors <= 3`; `_lod2.b3d` required when `height_floors >= 4`), check #11 (geometry shell required for `height_floors >= 4`; `_lod2.b3d` prohibited for `height_floors <= 3`), and the C++ `LODNode` runtime upgrade path (billboard ↔ geometry shell switch on density tier change). Also used to compute building height (`height_floors × 3 m`) for collision mesh extrusion. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). |
| `atlas_cell` | object | yes | `{ "row": R, "col": C }` — the asset's assigned cell in the 4096×4096 building atlas (phase-11e expansion with 8×8 grid of per-variant 512×512 cells; see `building-atlas-layout.md`). Used by export validation check #4 (UV channel 0 within atlas cell). |
| `lod_distances` | array(3) | yes | `[lod0_to_lod1_distance, lod1_to_lod2_distance, cull_distance]` in world units (metres). These values are the canonical source for `LODNode` configuration at runtime. Field semantics: `lod_distances[0]` is the LOD0→LOD1 switch-in threshold (author-specified); `lod_distances[1]` is the LOD1→LOD2 switch-in threshold (author-specified); `lod_distances[2]` is the **cull distance** — the distance at which the entity is entirely removed from the scene graph. **`lod_distances[2]` is NOT the LOD1→LOD2 switch-out distance.** The LOD1→LOD2 switch-out distance is derived by the engine from `lod_distances[1]` plus a hysteresis band (5–10 m per the LOD Distance Thresholds table); the artist does not author this value directly. The export validation script check #9 validates: `lod1_to_lod2_distance − lod0_to_lod1_distance ≥ 5` (close hysteresis ≥ 5 m) and `cull_distance > lod1_to_lod2_distance` (the entity is not culled before LOD2 becomes visible — see check #9 note below). Small buildings with billboard imposters must set `lod_distances[1]` to their billboard swap distance (i.e., the distance at which `LODNode` transitions from the LOD1 mesh to the billboard quad). Example: `[30.0, 100.0, 200.0]` — LOD0→LOD1 switch-in at 30 m, LOD1→billboard switch-in at 100 m, cull at 200 m; the engine sets the LOD1→billboard switch-out at `100 + hysteresis_band` (e.g. 110 m), matching the LOD Distance Thresholds table. |

**Author guidance for `lod_distances` fields**:

- `lod_distances[1]` (`lod1_to_lod2_distance`) is the **author-specified switch-in threshold** for LOD2: the camera distance at which `LODNode` transitions into LOD2 (or the billboard system for small buildings).
- `lod_distances[2]` (`cull_distance`) must be at least 10 m beyond `lod_distances[1]` so that the entity is not culled before LOD2 is fully visible. Example: if `lod_distances[1] = 100.0`, then `lod_distances[2]` must be at least 110.0.
- The `LODNode`'s **switch-out threshold** for LOD2 (the distance at which the engine transitions back from LOD2 to LOD1) is set by the engine at `lod_distances[1] + hysteresis_band`, where `hysteresis_band` is 5–10 m per the LOD Distance Thresholds table. **The artist does not author this value directly** — it is computed at runtime from the table.

**Hysteresis validation detail for check #9**: The `lod_distances` array stores switch-in distances. The corresponding switch-out distances are derived by the engine (not stored in the `.meta` file). Check #9 validates two conditions: (1) `lod1_to_lod2_distance − lod0_to_lod1_distance ≥ 5` (close hysteresis gap ≥ 5 m between the two switch-in thresholds); (2) `cull_distance > lod1_to_lod2_distance` (the cull distance must be beyond the last LOD switch-in threshold — the entity must not be culled before LOD2 is visible). The minimum recommended margin is 10 m (`cull_distance ≥ lod1_to_lod2_distance + 10`). The actual LOD1→LOD2 switch-in/switch-out hysteresis band (5–10 m) is governed exclusively by the LOD Distance Thresholds table, not by the `lod_distances[2]` field. For runtime `LODNode` configuration, the switch-in distances are read directly from `lod_distances[0]` and `lod_distances[1]`; switch-out distances are computed as `lod_distances[0] − 5` (or more) and `lod_distances[1] − 10` (or more), matching the hysteresis table in the **LOD Distance Thresholds and Hysteresis** section above.

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
- The C++ `LODNode` (`src/rendering/LODNode.h`) loads all LOD variants at load time and swaps mesh buffers based on camera distance thresholds
- Billboard LOD (small buildings 100m+): camera-facing quad with a **pre-baked imposter atlas** rendered from the LOD1 mesh using **8 bake angles** (every 45°). The camera-facing quad UV is selected at runtime based on the angle between the camera and the building's facing direction (snapped to nearest 45°). See [`2d-texture-standards.md` — Billboard Imposter Atlas](../asset-standards/2d-texture-standards.md#billboard-imposter-atlas) for the full format, dimensions, mip chain, bake elevation, bake lighting, wrap mode, and sRGB upload path specification.
  - **Billboard floor count limit**: Billboard impostors are only valid for buildings with **height_floors <= 3** (≤ 9 m total height). Buildings with height_floors >= 4 exhibit unacceptable silhouette mismatch at billboard scale (the flat 128×128 px frame cannot reproduce rooftop details, setbacks, or step changes visible from the valid camera pitch range [−70°, −20°] for tall buildings). The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). **Runtime category reassignment rule**: When a zone tile auto-upgrades to a higher density tier (e.g., Low → Medium) and the new density tier's building variant has height_floors >= 4, the C++ `LODNode` must automatically switch from the billboard LOD2 path to the geometry-shell LOD2 path. The asset pipeline must provide a `_lod2.b3d` geometry shell for any building variant with height_floors >= 4. The `<asset_name>.meta` file must specify `height_floors` per variant; `LODNode` reads this at tile upgrade time and selects the correct LOD2 strategy. Buildings with height_floors >= 4 at any density tier must use the full LOD2 geometry shell (`_lod2.b3d`) at the 100 m distance threshold. The export validation script must check the `height_floors` field in `<asset_name>.meta` and flag small-building/prop assets with `height_floors >= 4` that do NOT have a `_lod2.b3d` file as an error (see check #11), and flag small-building/prop assets with `height_floors <= 3` that DO have a `_lod2.b3d` file as an error (see check #2).

#### Vehicle Polygon Budget (LOD0 / LOD1)

| Vehicle class | LOD0 budget | LOD1 budget |
|---|---|---|
| Car (sedan, hatchback, SUV) | ≤2,000 tris | ≤400 tris |
| Bus | ≤3,000 tris | ≤500 tris |
| Truck | ≤3,000 tris | ≤500 tris |

The LOD Requirements table above lists the general Vehicles row (1000–3000 tris LOD0, 200–500 tris LOD1) as a range covering all vehicle classes. The per-class caps above are the binding limits within that range. All vehicle assets must be exported as a **single solid mesh** (body + windows + wheels unified into one `IMesh`); modular sub-mesh assembly is not used for vehicles.

**BINDING LIMIT NOTE**: The per-class budgets in the table above are the **binding limits**; the general range in the LOD Requirements table (1000–3000 tris LOD0, 200–500 tris LOD1) is **indicative only** — it covers the full span across all vehicle classes and must not be used as a per-class cap. For example, the general range does not permit a car to have 3,000 LOD0 triangles; the binding car LOD0 cap is ≤2,000 tris. The export validation script and artist review must use the per-class table above as the authoritative polygon budget source.

#### Vehicle LOD File Naming Convention

Vehicles use the same `_lodN` suffix pattern as buildings for LOD0 and LOD1 mesh files:

- `<vehicle_id>_lod0.b3d` — LOD0 full-detail mesh (e.g. `car_sedan_lod0.b3d`)
- `<vehicle_id>_lod1.b3d` — LOD1 reduced mesh (e.g. `car_sedan_lod1.b3d`)
- LOD2 is NOT a separate `.b3d` or sprite file on disk — it is a 16×16 px cell entry in `vehicles_sprite_atlas_d.dds`. No `_lod2.b3d` or `_lod2_sprite` file is authored per vehicle; the sprite cell is populated during atlas authoring.

The `<vehicle_id>` must match the `vehicle_id` field in `tools/vehicle_atlas_registry.json` exactly (e.g. `car_sedan`, `car_hatchback`, `car_suv`, `bus_standard`, `truck_cargo`). The export validation script derives the atlas cell assignment from this ID.

**Vehicle collision mesh**: `<vehicle_id>_col.obj` — single convex hull, 8 triangles (4 side quads, no top/bottom caps), authored at Y=0. One collision mesh per vehicle type, shared across all LOD levels.

**Vehicle sidecar**: `<vehicle_id>.meta` — JSON sidecar with `category: "vehicle"`, `lod_distances` (3 values), `atlas_cell` object `{"row": R, "col": C}`.

#### Vehicle UV Channel Convention

Vehicles use **UV channel 0 only** (diffuse/albedo atlas UV). UV channel 1 (lightmap baking) is **not required** for vehicles — vehicles are dynamic scene objects and do not participate in static lightmap baking. Exporting a UV channel 1 on vehicle assets is permitted but will be ignored by the runtime.

- **UV channel 0 layout**: Vehicle UVs must map into the vehicle diffuse atlas (512×512 px per vehicle type, packed into a 2048×2048 DDS DXT1 atlas — 16 vehicle types per sheet, named `vehicles_diffuse_atlas_d.dds`). Vehicle UV islands must not overlap and must stay within [0, 1] UV space. Blender export must use the vehicle's assigned atlas cell — UV coordinates must be authored in atlas space (e.g., a vehicle in cell row 0, column 2 uses U range [2/4, 3/4] on a 4×4 grid atlas).
- **UV channel 0 validation**: The export validation script must verify that all vehicle UV channel 0 coordinates fall within [0, 1] UV space and flag any vehicle with UV coordinates outside its assigned atlas cell.

- Vehicle point/sprite LOD (100m+): 16×16 px solid-color sprite; authored as part of a sprite atlas
- **Vehicle sprite LOD2**: 16×16 px sprite representing the vehicle's roof color/type, packed into `vehicles_sprite_atlas_d.dds`. **Format: DDS DXT5/BC3 (NOT DXT1/BC1)** — full format specification, resolution, mip chain, and upload path in `building-atlas-layout.md — Vehicle Sprite Atlas`. The runtime draws a camera-facing billboard quad (1 m × 0.5 m) sampling the vehicle's assigned 16×16 px atlas cell.

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

#### V1 Minimum Building Coverage

Artists must deliver a minimum of **36 building sets** across all zone/tier combinations: 4 variants × 3 zones (Residential/Commercial/Industrial) × 3 density tiers (Low/Mid/High) = 36 sets total. Sub-breakdown: 24 Low+Mid sets (4 variants × 3 zones × 2 tiers) and 12 High-density sets (4 variants × 3 zones × 1 tier). High-density buildings (`height_floors >= 4`) require `_lod2.b3d` geometry shells; Low/Mid buildings (`height_floors <= 3`) require `_billboard.dds` billboard imposters for LOD2.

Each building set must include:

- `<zone>_<tier>_<variant>_lod0.b3d` — LOD0 full-detail mesh
- `<zone>_<tier>_<variant>_lod1.b3d` — LOD1 reduced mesh (≤50% of LOD0 tris)
- For buildings with `height_floors <= 3`: `<zone>_<tier>_<variant>_billboard.dds` — billboard atlas (1024×128 DXT5 sRGB)
- For buildings with `height_floors >= 4`: `<zone>_<tier>_<variant>_lod2.b3d` — LOD2 geometry shell (400–600 tris)
- `<zone>_<tier>_<variant>.meta` — sidecar with `category`, `height_floors`, `atlas_cell`, `lod_distances`
- `<zone>_<tier>_<variant>_col.obj` — collision mesh (or `_col_0/1/2.obj` / `_col_circle.obj` for non-convex/circular footprints)

Each zone-building variant has its own dedicated atlas cell in the 4096×4096 building atlas (phase-11e expansion). Variants differ in mesh geometry and occupy dedicated UV space within their own 512×512 cell. See `architecture/asset-standards/building-atlas-layout.md` for the cell assignment table.

#### Service Building Model Standards

Service buildings (Fire Station, Police Station, Power Plant, Water Tower) are individually
placed infrastructure objects, not zone buildings. They are **not** part of the modular kit
system and do **not** participate in the zone/tier naming convention. Each service building
type requires its own set of LOD meshes, collision mesh, and `.meta` sidecar.

**Naming convention**: `svc_<type>_lod<N>.b3d` where `<type>` is one of:
`fire_station`, `police_station`, `power_plant`, `water_tower`.

Examples:

- `svc_fire_station_lod0.b3d`
- `svc_power_plant_lod1.b3d`
- `svc_water_tower_col.obj`

**LOD strategy**: Service buildings use the **small building / props** LOD distance
thresholds (height_floors = 2 for all V1 service buildings — all are single or
double-storey structures). LOD0 polygon budgets are raised in Phase 11d to support
recognisable per-type architectural detail (antenna masts, equipment geometry, garage
bay insets). This means:

- LOD0: 2,000–4,000 tris (full detail — binding budget per LOD Requirements table)
- LOD1: 200–400 tris (reduced)
- LOD2: `_billboard.dds` (1024×128 DXT5 sRGB, 8-direction bake at 45° below horizontal).
  No `_lod2.b3d` geometry shell — `height_floors = 2 <= 3` boundary applies.

**Floor count**: All four V1 service building types use `height_floors = 2` in their
`.meta` sidecar. This satisfies the `height_floors <= 3` condition for billboard LOD2.

**LOD distance thresholds** (small buildings/props category):

| Threshold | Distance |
|---|---|
| LOD0→LOD1 switch-out | > 30 m |
| LOD0→LOD1 switch-in | < 25 m |
| LOD1→LOD2 switch-out | > 100 m |
| LOD1→LOD2 switch-in | < 90 m |

**`.meta` sidecar fields** (mandatory for all four service building types):

```json
{
  "category": "small_building",
  "height_floors": 2,
  "atlas_cell": { "row": 3, "col": 2 },
  "lod_distances": [25.0, 90.0, 200.0]
}
```

`atlas_cell` assignments for service buildings use the two reserved cells in the building
atlas (row 3, col 2 and row 3, col 3). Service buildings share atlas cell (3, 2) for all
four types in V1 — they share a common material palette (concrete, glass, utility panels).
A second reserved cell (3, 3) is available if a distinct material per service type is
required in a later phase. Cell (3, 2) assignment is now recorded as binding in
`architecture/asset-standards/building-atlas-layout.md` Cell Assignment Table (updated
2026-03-04). The `graphics-artist-3d-model` UV packing feasibility confirmation for
service building cell (3, 2) is recorded in `building-atlas-layout.md` (sign-off
2026-03-04). The `graphics-artist-2d-texture` texture content confirmation for cell (3, 2)
was recorded 2026-03-04 in `building-atlas-layout.md` (sign-off block present). This gate
is CLOSED. Phase 9 service building UV authoring and validate_assets.py check #4
UV-range verification for service building assets are both unblocked.

**Collision mesh**: `svc_<type>_col.obj` — single convex hull, maximum 24 triangles,
no top/bottom caps, flat at Y=0. All four service building types use rectangular footprints
and require the `_col.obj` single-convex dispatch path.

**Phase delivery**: Service building 3D models are **Phase 9 deliverables**, alongside
the zone building sets. They are not required for Phase 10 to start.

**Phase 10 audio note**: `sfx_fire_alert` and `sfx_police_alert` (positional SFX) fire at
`vec3{tile.tileX, 0.0f, tile.tileZ}` — a tile-coordinate position derived from the
simulation state, not from a rendered service building scene node. Vehicle engine SFX
similarly use agent positions managed by `AudioSystem`, not scene node transforms. Phase 10
audio wiring is therefore fully decoupled from service building and vehicle 3D model
delivery. No 3D model asset is on the Phase 10 critical path.

#### Modular Building Kit

- Buildings assembled from reusable mesh modules: base, mid-floor, roof, facade details
- Module grid: 4 m × 4 m × 3 m per floor unit
- **Maximum floor count**: Large buildings have a **hard cap of 10 floors** (30 m total height at 3 m/floor). At 10 floors: assembled LOD0 maximum ≈ base (400) + 8 mid-floor (8×300=2,400) + roof (500) + 10 facade details (10×100=1,000) = 4,300 tris — within the 5,000 tri LOD0 budget. 11+ floors risk budget overrun. The 10-floor limit is enforced by the export validation script using the `height_floors` field in `<asset_name>.meta`; any override requires a polygon audit and explicit approval. **Exemption — `com_high_*` only**: Commercial High skyscraper variants (`com_high_01` through `com_high_04`) are the sole exception to the 10-floor cap. Their `height_floors` must be in the range 15–30 as specified in the Commercial High Skyscraper sub-row (see line 107 and the LOD Requirements table). This exemption is justified by their dedicated, higher polygon budgets (7,000–10,000 tris LOD0) which are sized to accommodate the additional floor repetitions. The export validation script must treat any `com_high_*` asset with `height_floors` in [15, 30] as conformant; all other Large building types must still satisfy `height_floors` ≤ 10.
- **Pivot convention**: Pivot at bottom-center of footprint. For a standard 4×4×3 m unit, pivot is at (0,0,0) with geometry in X:−2 to +2, Y:0 to +3, Z:−2 to +2 local space. This is a **hard export requirement**.
- **Vertical geometry bounds**: Geometry must not exceed Y=3.0 (hard upper bound for floor modules). Wall tiles with decorative tops (parapets, cornices) must stay within the 3 m budget. Maximum tolerated vertex deviation from Y=0 (bottom) or Y=3.0 (top): **0.005 Irrlicht units (5 mm)**. This tolerance reflects practical floating-point precision limits in DCC tools — a 1 mm tolerance is unreliably tight for polygon modelling workflows. An export validation script checks all wall tile Y extents and rejects files that violate this tolerance.
- **LOD2 pivot conformance**: The LOD2 baked shell mesh pivot MUST be at bottom-center (X=0, Y=0, Z=0 relative to the building's ground footprint center) — identical to LOD0 and LOD1 pivot convention. Using the bounding box centroid (center of mass vertically) will produce a position pop at the LOD1→LOD2 transition equal to half the building height. LOD transitions from LOD1 modules → LOD2 shell must not produce a position pop. Asset sign-off checklist includes: "Stack two identical floor modules in Irrlicht scene view; confirm no visible gap at join." Also: "Verify LOD2 shell silhouette matches LOD1 assembled building silhouette within 10% area deviation when viewed from the 8 standard bake angles at 45° below horizontal (camera pitch = −45°)."
- **LOD2 shell lightmap requirement**: The LOD2 baked shell mesh must carry UV channel 1 (non-overlapping, covering the entire shell mesh) and be lightmap-baked to a dedicated `<asset_name>_lod2_lm.dds` texture at **256x256** resolution in **DDS DXT5/BC3 format** (quarter of the full-size lightmap, proportional to LOD2 viewing distance and reduced screen footprint; DXT5 used for consistency with full-resolution lightmaps and to allow alpha channel for AO data). Lightmap baking for LOD2 shells uses flat ambient-only lighting (same as billboard bakes) for consistency with the billboard system at similar distances. **LOD2 lightmap mip chain**: `_lod2_lm.dds` follows the **lightmap exemption rule** — lightmap textures (`_lm` suffix) are explicitly exempt from mip chain requirements. The LOD2 shell lightmap must be uploaded with `GL_TEXTURE_MAX_LEVEL = 0` (single mip level only), matching the lightmap exemption in `2d-texture-standards.md`. Do NOT generate a mip chain for `_lod2_lm.dds` — the VRAM budget calculation for LOD2 shell lightmaps assumes unmipmapped textures (0.0625 MB/texture at 256×256 DXT5, no ×1.33 overhead). Aliasing at distances beyond 200 m is acceptable for LOD2 shell lightmaps given the reduced screen footprint (typically fewer than 40 vertical pixels for a large building at 200 m). The absence of a mip chain is a deliberate VRAM budget tradeoff. The export validation script must NOT generate mip chains for `_lod2_lm.dds` files. The export validation script must verify UV channel 1 is present and non-degenerate on all `_lod2.b3d` building asset files.
- **LOD2 shell UV channel 0 (diffuse)**: The LOD2 shell UV channel 0 maps into the same 4096×4096 city building atlas as LOD0/LOD1 modules (phase-11e expansion). The shell UV islands are authored to cover the atlas cells of its dominant facade materials within the dedicated 512×512 cell assigned to the variant. This preserves texture continuity across LOD transitions. The export validation script must verify that LOD2 UV channel 0 coordinates fall within [0, 1] UV space and correspond to the variant's assigned cell bounds.
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

  **Export validation script — required checks**: The export validation script (`tools/validate_assets.py` or equivalent) must perform all 20 required checks and produce a per-asset PASS/FAIL report:
  1. Building `_lod0`, `_lod1` files use `.b3d` format (not `.obj`).
  2. Small building / prop `_lod2.b3d` file presence is floor-count conditional (read `height_floors` from `<asset_name>.meta`): if `height_floors <= 3`, the asset must NOT have a `_lod2.b3d` file (flag its presence as an error) and must have a `_billboard.dds` instead; if `height_floors >= 4`, the asset must have a `_lod2.b3d` geometry shell (flag its absence as an error) and must NOT rely on `_billboard.dds` for LOD2 rendering. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`). (See check #11 for the symmetric geometry-shell presence requirement.)
  3. Large building `_lod2.b3d` is present, within 300–500 tri budget, and uses **DXT5/BC3 format for `_lod2_lm.dds`** (not DXT1). Validate by reading the DDS fourCC. DXT1 on a LOD2 lightmap is a silent error — DXT5 is required to preserve the alpha channel for ambient occlusion data.
  4. UV channel 0 coordinates on all LOD levels fall within [0, 1] UV space.
  5. UV channel 1 (lightmap) is present and non-degenerate on all building `.b3d` files (non-overlapping islands).
  6. Assembled LOD0 total ≤ 5,000 tris (large) or ≤ 1,500 tris (small) for a representative N-floor stack.
  7. Facade detail piece count ≤ 10 per assembled stack.
  8. Asset pivot at bottom-center (Y=0); geometry Y extent stays within [0, 3.0] per floor module (tolerance **0.005 units / 5 mm**).
  9. LOD distance hysteresis and cull distance: (a) `lod_distances[1] − lod_distances[0] ≥ 5` (close hysteresis gap ≥ 5 m); (b) `lod_distances[2] > lod_distances[1]` with a recommended minimum margin of 10 m (cull distance must be beyond the LOD1→LOD2 switch-in threshold so the entity is not culled before LOD2 is visible). Note: `lod_distances[2]` is the cull distance — it is NOT the LOD1→LOD2 switch-out distance. The LOD1→LOD2 switch-out threshold is derived at runtime by the engine (`lod_distances[1] + hysteresis_band`); it is not validated here. The actual LOD hysteresis band (5–10 m) is governed by the LOD Distance Thresholds table.
  10. Vehicle UV channel 0 coordinates fall within the asset's assigned atlas cell (see Vehicle Atlas Cell Registry below).
  11. Small building / prop assets with `height_floors >= 4` must have a `_lod2.b3d` geometry shell (not just billboard). Conversely, small building / prop assets with `height_floors <= 3` must NOT have a `_lod2.b3d` file — they use point-sprite LOD2 only. The 4-floor threshold is the boundary: buildings with `height_floors >= 4` require a `_lod2.b3d` geometry shell for distant visibility; buildings with `height_floors <= 3` use the billboard imposter system at LOD2 (point-sprite only, no `_lod2.b3d`).
  12. Vehicle normal map UV channel 0 coordinates fall within the asset's assigned atlas cell in `vehicles_normal_atlas_n.dds` (8×8 grid of 256×256 cells in 2048×2048; vehicle row/column assignments match the diffuse atlas registry in `vehicle_atlas_registry.json` — same row R and column C, but cell UV range is `U ∈ [C/8, (C+1)/8]`, `V ∈ [R/8, (R+1)/8]` since the normal atlas has an 8×8 grid). The V-axis origin convention (OpenGL, V=0 at bottom, row 0 is the bottom row) applies identically to normal atlas UV verification — artists must apply V-flip (`V_opengl = 1 − V_blender`) when authoring UV islands for the normal atlas in Blender, using the same convention documented in the Vehicle Atlas Cell Registry.
  13. Facade atlas cell pixels — all non-transparent pixel content falls within the [8, 504] texel range on both U and V axes per 512×512 cell (496×496 usable zone; 8-texel border on each edge). Validate by reading pixel alpha values in the border zone for each cell in the 4096×4096 building atlas (phase-11e expansion).
  Note: Check #14 is the music JSON sidecar validation (`validate_assets.py` checks all `music_*.ogg` files have co-located `.json` sidecars matching `tools/music_sidecar_schema.json`) — defined in `architecture/audio-architecture/v1-audio-asset-manifest.md` and implemented in Phase 5.
  15. `.meta` sidecar file presence — every `.b3d` building or vehicle file must have a corresponding `<asset_name>.meta` sidecar file. Missing sidecar: validation error. This check is a stub in Phase 5 (`# TODO Phase 9` comment); replaced with full implementation in Phase 9.
  16. `music_*.ogg` must be stereo (channels == 2), 44100 Hz; `ambient_*.ogg` must be stereo, 44100 Hz. Hard error on mismatch. Graceful no-op if no matching files exist. Requires `mutagen`. Implemented in Phase 5.
  17. `sfx_vehicle_engine_*.ogg` must have duration ≥ 6.0 s, mono (channels == 1), 44100 Hz. Hard error if duration < 6.0 s. Graceful no-op if no matching files exist. Requires `mutagen`. Implemented in Phase 5.
  18. `sfx_zone_*.ogg` must have duration ≤ 18.0 s, mono, 44100 Hz. Hard error if duration > 18 s. Graceful no-op if no matching files exist. Requires `mutagen`. Implemented in Phase 5.
  19. `stinger_*.wav` must be mono WAV PCM (1 channel, uncompressed). Hard error on stereo or compressed WAV. Graceful no-op if no matching files exist. Requires `mutagen`. Implemented in Phase 5.
  20. Road LOD2 color validation — read `RenderConstants::road_lod2_color` from `src/rendering/render_constants.h`; decode `road_asphalt_tileable.dds` (DXT5) and compute the linear-space average RGB; verify the constant matches the computed average within a per-channel tolerance of ±3/255. Fail build on mismatch. Added in Phase 9 alongside the road tile LOD2 deliverable.

  **Phase assignment**: Checks #1–#13 are the Phase 5 implementation scope. Check #14 (music JSON sidecar) is also a Phase 5 check — it is defined in `architecture/audio-architecture/v1-audio-asset-manifest.md`. Checks #15–#19 are Phase 5 additions (Check #15: .meta sidecar stub; Checks #16–#19: audio format/duration checks). Check #20 (road LOD2 color validation) is a Phase 9 addition added when the road tile LOD2 deliverable is complete. Phase 5 implementers should implement checks #1–#19; Check #20 is reserved for Phase 9.

  **Phase 5 stub requirement for check #15**: The Phase 5 implementation of `validate_assets.py` MUST include check #15 as a stub — present in the script's check list but not executed. The stub must contain a `# TODO Phase 9` comment that names the check and explains the deferral, for example:

  ```python
  # Check #15: .meta sidecar file presence
  # TODO Phase 9 — deferred until building asset metadata support is fully in place.
  # When enabled: every .b3d building or vehicle file must have a corresponding
  # <asset_name>.meta sidecar file. Missing sidecar = validation error.
  # Phase 9 entry prerequisite: this stub must be replaced with the full check
  # implementation before Phase 9 asset authoring begins (see 3d-model-standards.md,
  # Export Validation Script — Required Checks, check #15).
  pass
  ```

  The stub must be present (not omitted) so that Phase 9 implementers can locate the check without searching the spec, and so CI check-count assertions (if any) can account for all 20 checks by name. **Check #15 stub is a Phase 5 exit criterion.**

  **Phase 9 entry prerequisite — check #15**: Before Phase 9 asset authoring begins, `validate_assets.py` must have the check #15 stub replaced with a full implementation that reads `<asset_name>.meta` for every `.b3d` file found in the asset directories and emits a validation error for any missing sidecar. This prerequisite must be verified during the Phase 9 kick-off review before UV authoring or asset metadata authoring begins.

  The script must be run as part of the asset pipeline before any asset is checked into the repository. CI must run the script and fail the build if any asset fails validation.

  **Vehicle Atlas Cell Registry**: Each vehicle type must be assigned a unique atlas cell in `vehicles_diffuse_atlas_d.dds` (2048×2048 DDS DXT1, 4×4 grid of 16 cells at 512×512 px each). The registry is maintained in `tools/vehicle_atlas_registry.json` and must be updated whenever a new vehicle type is added.

  **Vehicle Atlas Registry sign-off**: Before Phase 6 vehicle UV authoring begins, `graphics-artist-3d-model` must review and co-sign `tools/vehicle_atlas_registry.json` alongside `graphics-dev-irrlicht`. Specifically, `graphics-artist-3d-model` must confirm:

  1. The V-flip convention (`V_opengl = 1 - V_blender`) is documented correctly for each vehicle type
  2. The atlas cell boundary UV formulas for the 4×4 diffuse grid and 8×8 normal grid are correct
  3. All five V1 vehicle type assignments (car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo) are in the correct cell positions

  This co-sign-off is a Phase 4 exit criterion alongside the existing schema conformance check.

  ARTIST UV WARNING — V-axis convention: The atlas UV formula above uses OpenGL convention (V=0 at bottom-left of the atlas, V increases upward). Blender's UV editor shows V=0 at the top (V increases downward). Any artist authoring vehicle UV islands in Blender MUST apply `V_opengl = 1 - V_blender` before setting final UV coordinates in atlas space. Failure to apply this flip will place UV islands in the mirror-image vertical position, causing the vehicle to sample texture data from an adjacent atlas cell. The export validation script (check #10) uses OpenGL convention to verify coordinates — V-flipped Blender values will fail this check.

  Format (canonical schema — must match `building-atlas-layout.md § Required JSON Schema`):

  ```json
  {
    "diffuse_atlas": {
      "atlas_file": "vehicles_diffuse_atlas_d.dds",
      "grid": { "cols": 4, "rows": 4, "cell_size_px": 512 },
      "mip_levels": 4,
      "upload_path": "srgb"
    },
    "normal_atlas": {
      "atlas_file": "vehicles_normal_atlas_n.dds",
      "grid": { "cols": 8, "rows": 8, "cell_size_px": 256 },
      "mip_levels": 4,
      "upload_path": "linear",
      "_comment_normal_atlas": "same row/col assignments as diffuse but 8x8 grid; U=[C/8,(C+1)/8], V=[R/8,(R+1)/8]"
    },
    "sprite_atlas": {
      "atlas_file": "vehicles_sprite_atlas_d.dds",
      "grid": { "cols": 16, "rows": 16, "cell_size_px": 16 },
      "mip_levels": 1,
      "upload_path": "linear"
    },
    "assignments": [
      { "vehicle_id": "car_sedan",     "row": 0, "col": 0 },
      { "vehicle_id": "car_hatchback", "row": 0, "col": 1 },
      { "vehicle_id": "car_suv",       "row": 0, "col": 2 },
      { "vehicle_id": "bus_standard",  "row": 1, "col": 0 },
      { "vehicle_id": "truck_cargo",   "row": 1, "col": 1 }
    ]
  }
  ```

  The export validation script reads this registry when checking vehicle UV channel 0 coordinates (check #10). A vehicle with no registry entry fails validation. A vehicle with UV coordinates outside its assigned cell fails validation. **Atlas UV calculation**: For a cell at (row R, col C) on a 4×4 grid, the atlas UV range is `U ∈ [C/4, (C+1)/4]`, `V ∈ [R/4, (R+1)/4]`. **V-axis origin convention (OpenGL)**: This formula uses **OpenGL UV convention** — V origin is at the bottom-left of the atlas; V increases upward; row 0 (R=0) is the BOTTOM row. DDS files store texels top-row-first, and Blender's UV editor shows V=0 at the top. Artists authoring vehicle UV islands in Blender must apply V-flip (`V_opengl = 1 − V_blender`) before mapping to atlas cells. The export validation script must use the OpenGL convention when checking UV coordinates against assigned cells.

## Building Variant Geometry Standards

This section is the canonical reference for per-variant geometry requirements for all V1 zone
building sets. Each zone-tier combination requires exactly four geometry variants with distinct
architectural vocabularies. A player must be able to identify the zone type and distinguish
individual variants from mesh shape alone, without colour or texture cues.

**Inter-variant differentiation is mandatory**: the four variants within each zone-tier must each
differ from every other variant in at least one structural dimension (roof form, massing, external
additions, or boundary treatment). A city block containing all four variants must not contain any
two buildings that look alike when viewed from 60 m (large buildings) or from street-level view
(small buildings).

**Building atlas**: each zone-building variant occupies its own dedicated atlas cell. Phase 11e
expansion establishes a per-variant unique cell approach: all 36 zone-building variants
(`res`/`com`/`ind` × `low`/`med`/`high` × 01–04) and all 4 service building types each have a
dedicated 512×512 cell in the 8×8 atlas grid (4096×4096 px total). The prior constraint that
multiple variants shared a single module-type cell is superseded. UV islands for each variant must
fit within the 496×496 px usable area of its dedicated cell (8 px border on each edge per
`architecture/asset-standards/building-atlas-layout.md`). See the "Cell Assignment Table" in
`architecture/asset-standards/building-atlas-layout.md` for the full mapping of each variant to
its grid row and column.

### Residential Low

Floor count: `height_floors` 1 or 2 (`height_floors <= 3` small building tier). Variants within
this tier may use different values within the range; height difference is the primary
silhouette-variation tool.

- **`res_low_01`** (flat-roof block): flat parapet roof, single AC condenser on parapet, no garden
  (tarmac forecourt), utility meter box geometry on facade.
- **`res_low_02`** (semi-detached pair): two single-storey units side by side under a common
  gabled pitched roof; door centred on the front face of the left unit only (atlas cell (6,0));
  plain cream rendered walls on all other faces; windows match `res_low_01` size and height
  (same `ww`/`wh`/`wy` atlas proportions). Total footprint 8 S × 10 S, fits within one tile.
- **`res_low_03`** (cottage): single-storey box with mono-pitch roof (low front, high rear),
  brick chimney geometry stub, door on front face only (atlas cell (6,1)); brick+clay-tile
  texture. Footprint 8 S × 10 S, height 6 S — matches `res_low_01`/`res_low_02`.
- **`res_low_04`** (red-brick): steeply-pitched metal-tile roof with single dormer window, narrow
  chimney, low brick boundary wall at plot edge (no garden). Total footprint 10 S × 10 S,
  height 3 S — fits within one tile.

Primary differentiators: roof form (flat vs. pitched; gabled vs. hipped; dormer count), external
additions (carport, AC condenser), and boundary treatment (fence vs. wall vs. no enclosure).

### Residential Medium

Floor count: `height_floors` 2 or 3 (`height_floors <= 3` small building tier).

- **`res_med_01`** (2-storey block): flat parapet roof, external staircase on side facade to
  rooftop terrace, clustered AC condensers on parapet, tarmac apron.
- **`res_med_02`** (2-storey villa): hipped metal roof in seafoam-green, full wrap-around
  first-floor balcony with rendered balustrade, rendered perimeter wall with iron gate,
  kidney-pool geometry in garden.
- **`res_med_03`** (2-storey cottage): clay-tile hipped roof with dormer windows, brick chimney,
  full-width covered balcony on first floor, wrought-iron fence with brick piers.
- **`res_med_04`** (3-storey red-brick): pitched black metal roof with pair of dormers, projecting
  bay window on first floor, low brick garden wall at plot edge.

Primary differentiators: roof form (flat vs. hipped; dormer count), external additions (staircase,
balcony, pool), and boundary treatment (fence vs. wall vs. no enclosure).

### Residential High

Floor count: `height_floors` 5–10 (`height_floors >= 4` large building tier). The four variants
must span at least a 3-floor range (e.g. 5, 7, 8, 10 floors) to produce readable skyline height
variation. No two variants may share the same `height_floors` value.

LOD0 target: 6,000–8,000 tris. LOD1 must retain balcony slab extrusion profile (single flat slab
per floor band, no railing geometry) and preserve height variation across all four variants.

- **`res_high_01`** (flat-roof concrete slab): flat parapet roof, smooth render exterior, row of AC
  condenser units on parapet (min 6 units, boxy geometry), punched window grid, ground-floor entry
  canopy slab projecting from recessed lobby.
- **`res_high_02`** (stepped-setback form): upper 2 floors set back on min 2 sides (visible ledge
  profile at each step), corner tower element rising one floor above main roof, ground-floor
  colonnade (min 4 columns with visible spacing), pool basin geometry in walled courtyard.
- **`res_high_03`** (full-height curtain-wall tower): cantilevered balcony slabs at each floor
  (20–35 cm overhang), alternating vertical sunshield fin geometry (one fin per 1.5–2 m of facade
  width), small rooftop plant room.
- **`res_high_04`** (flat-fronted concrete slab): plainest massing of the four (board-form texture
  drives variant identity); recessed loggia balcony per floor (fully recessed behind facade plane,
  min 0.8 m depth), horizontal spandrel band geometry between floors, ground-floor retail strip
  with wider openings.

Primary differentiators: rooftop silhouette (AC condenser deck vs. stepped setback vs. curtain-wall
balcony tower vs. loggia slab) and footprint aspect ratio (narrow-tower vs. wider-slab massing).

### Commercial Low

Floor count: `height_floors` 1 or 2 (`height_floors <= 3` small building tier).

- **`com_low_01`** (convenience store): flat parapet roof, full-width glazed shopfront, projecting
  sign board above entrance (flat slab geometry, min 0.4 m depth), 3-bay parking apron.
- **`com_low_02`** (café): flat roof, canvas awning frame over entrance and side terrace
  (bracket-and-valance profile), café table and chair props, flower-pot props flanking door.
- **`com_low_03`** (auto garage): corrugated metal facade, two wide roll-up shutter doors, open
  forecourt (no awning), tyre prop stacks against side wall.
- **`com_low_04`** (supermarket): flat parapet roof, full-width glazed shopfront with recessed
  covered walkway canopy, freestanding trolley-bay shelter geometry in parking apron.

Primary differentiator: building programme (convenience store vs. café vs. garage vs. supermarket)
produces inherently different shopfront and roof configurations.

### Commercial Medium

Floor count: `height_floors` 2 or 3 (`height_floors <= 3` small building tier).

- **`com_med_01`** (strip mall): flat roof with HVAC unit props, continuous glazed shopfronts on
  ground floor, upper floor with ribbon windows, large parking apron with bay markings, multiple
  fascia sign panels.
- **`com_med_02`** (boutique hotel): flat or low-pitched roof, juliet balcony railings on every
  upper floor window, fabric canopy frame over main entrance, ornamental bracket geometry above
  ground-floor window lintels.
- **`com_med_03`** (corner bank): flat roof with projecting cornice band, paired pilaster strips at
  facade corners, arched window openings flanking entrance, revolving door recess (min 3 bays),
  shallow front setback.
- **`com_med_04`** (office block): glass curtain-wall facade (3 floors), flat roof with plant room
  behind louvred parapet screen, recessed ground-floor entrance under projecting concrete canopy
  slab.

Primary differentiator: building programme (strip mall vs. hotel vs. bank vs. office block)
produces inherently different shopfront and roof configurations.

### Commercial High

Floor count: `height_floors` 15–30 (skyscraper exception — these are tall glass landmark
buildings, NOT subject to the standard 5–10 floor range for High-tier buildings). The four variants
must span at least a 10-floor range (e.g. 15, 20, 25, 30 floors). No two variants may share the
same `height_floors` value.

LOD0 target: 8,000–10,000 tris (elevated budget reflecting landmark status). LOD1 must retain the
variant-specific crown silhouette (spire, antenna cluster, tapered top, or ziggurat steps must
still be readable at LOD1 polygon count).

All four `com_high` variants must have: a unique crown treatment distinguishable by silhouette from
skyline distance; ground floor grand entrance lobby canopy geometry (projecting flat canopy slab,
min 4 m wide × 1.5 m deep); multi-bay revolving door recess (min 3 door bays, each min 1.2 m wide
× 2.2 m tall, recessed min 0.4 m); podium base geometry (a wider base volume, min 1.5 m taller
than street level, set back from the tower shaft above); facade floor-to-ceiling curtain-wall
mullion grid throughout the full height (thin vertical and horizontal extrusions, not painted
lines); expressed structural core visible on the exterior (a thickened central or corner volume
carrying vertical columns proud of the curtain wall face by min 5 cm).

Four distinct form vocabularies — one per variant:

- **`com_high_01`** (spire tower): narrow glass tower with a spire crown — slender rectangular
  shaft tapering to a spire pinnacle at rooftop; floor plate consistent throughout height.
- **`com_high_02`** (slab with antenna cluster): wide slab with setback upper floors and an antenna
  cluster crown — lower 60% is a broad rectangular slab; upper 40% steps back on at least two
  sides; antenna cluster of 3–5 vertical rods of varying heights at the roof centre.
- **`com_high_03`** (tapered pyramid): tapered pyramid form with chamfered corners — floor plate
  reduces uniformly from base to crown, each floor stepping inward ~0.3–0.5 m; all four vertical
  corners are chamfered throughout the full height.
- **`com_high_04`** (stepped ziggurat): stepped ziggurat with floor-plate reductions every 3–4
  floors — distinct horizontal ledge at every setback step, min 4 step levels visible from ground
  to crown.

### Industrial Low

Floor count: `height_floors` 1 or 2 (`height_floors <= 3` small building tier).

- **`ind_low_01`** (corrugated metal warehouse): mono-pitch or flat shed roof, corrugated metal
  wall panel ribs (min 8 parallel extrusions on principal facade), wide roll-up shutter loading
  doors, lean-to office annexe on one end, truck dock geometry with yellow kerb marker.
- **`ind_low_02`** (brick workshop): flat felted roof with parapet, brick wall (no corrugated
  ribs), roller-shutter entrance, tyre prop stacks, hand-painted sign board above entrance.
- **`ind_low_03`** (sawtooth factory): sawtooth roofline with min 2 asymmetric north-light ridges
  (highly distinctive stepped profile — primary zone identifier for this variant), chimney stack on
  gable end, chain-link fence perimeter.
- **`ind_low_04`** (storage yard): small flat-roof gatehouse as primary mesh anchor (min 3 m × 3 m
  footprint), two-high shipping container stacks (rectangular box props in 3 distinct tints),
  chain-link fence perimeter, floodlight mast.

Primary differentiators: shed type (corrugated metal warehouse vs. brick workshop vs. sawtooth
factory vs. storage yard) — roof profile is the primary identifier (mono-pitch shed, flat parapet,
sawtooth ridgeline, or gatehouse anchor).

### Industrial Medium

Floor count: `height_floors` 2 or 3 (`height_floors <= 3` small building tier).

- **`ind_med_01`** (flat-roof factory): flat roof with two concrete chimney stacks above parapet
  (round or rectangular section, min 2 m above roof), ground-floor loading bays (min 2 bays),
  metal-railed access walkway along second-floor facade.
- **`ind_med_02`** (steel-frame warehouse): exposed structural steel frame visible on the exterior
  (at least corner columns proud of the cladding), notably wider footprint than `ind_med_01`,
  fire-escape staircase on gable end, elevated covered walkway connecting two building wings.
- **`ind_med_03`** (brick mill): flat roof with rooftop cylindrical water tank on a steel support
  frame, large multi-pane industrial windows (wider proportions than `ind_low_02`), arched window
  head lintels, cast-iron fire escapes on rear facade.
- **`ind_med_04`** (distribution centre): compact square footprint (notably wider than it is tall),
  loading docks on two perpendicular sides with dock shelter hoods, elevated gatehouse booth at
  site entrance, extensive concrete truck apron.

Primary differentiators: structural type (flat-roof factory vs. steel-frame warehouse vs. brick
mill vs. distribution centre) — roof form and structural expression drive differentiation.

### Industrial High

Floor count: `height_floors` 5–10 (`height_floors >= 4` large building tier). The four variants
must span at least a 3-floor range. No two variants may share the same `height_floors` value.

LOD0 target: 6,000–8,000 tris. LOD1 must retain rooftop plant-room box and zone-defining silhouette
features at simplified fidelity.

All Industrial High variants must also include: setback modelling at each floor band and rooftop
equipment silhouettes (AC units, antennae stubs).

- **`ind_high_01`** (concrete tower with chimney stacks): plain concrete tower with board-form
  banding, two tall chimney stacks rising well above roofline (each min 3 m above parapet), small
  punched windows with expressed lintels, rooftop service structure.
- **`ind_high_02`** (exposed steel frame with pipe runs): exposed steel-frame structure, external
  pipe runs of two distinct diameters along full facade height (large-bore: ~0.3 m diameter;
  small-bore: ~0.1 m diameter), spherical pressure vessel at mid-height (min 2 m diameter),
  wide-base cooling tower volume on one side.
- **`ind_high_03`** (silo cluster): cluster of cylindrical silos (min 3 cylinders, each 3–5 m
  diameter), silo cluster height equivalent to 7 floors; corrugated metal conveyor bridge
  connecting silo tops; elevator head house at one end of bridge — the circular silhouette is the
  primary identifier.
- **`ind_high_04`** (grating-platform refinery): grating-platform horizontal bands at every floor,
  dense roof-level pipe rack (min 5 horizontal pipe members visible in elevation), flare stack
  rising from one corner (min 4 m above roof), large industrial louvred panels in place of
  windows, hazard-stripe banding on structural posts at base.

Primary differentiators: rooftop silhouette (chimney stacks vs. pipe runs vs. silo cluster vs.
grating-platform refinery) and floor-band setback count.

<!-- SIGN-OFF: graphics-artist-3d-model 2026-02-27 — confirmed: all 20 export validation checks present and correct; naming convention <zone>_<tier>_<variant>_lod<N>.<ext>; pivot at base center Y=0; 5 mm Y-axis vertical extent tolerance (max vertex deviation from Y=0 bottom or Y=3.0 top per floor module); 10-floor hard cap; collision mesh dispatch order confirmed — (1) _col_0.obj: multi-convex set, (2) _col_circle.obj: N-sided circular prism, (3) _col.obj: single convex hull, (4) none: log error; dispatch prevents _col_0 shadowing _col on dual-suffixed assets; billboard floor count limit (height_floors <= 3 uses billboard imposter, >= 4 uses _lod2.b3d); LOD2 pivot conformance (base-center identical to LOD0/LOD1); Blender export axis (-Z Forward, Y Up); asset formats .b3d (animated/rigged), .obj (collision/static). Atlas mip chain clamping (4 levels) is documented in building-atlas-layout.md and 2d-texture-standards.md — outside the scope of this document but verified as present. Phase 9 may proceed. -->

<!-- SIGN-OFF: graphics-artist-3d-model [2026-02-28] — Phase 8 blocking review confirmed: (1) export axis convention (-Z Forward, Y Up) per Coordinate System Export Convention section; (2) .b3d mandatory for all building assets requiring UV2/lightmap channel support, .obj permitted only for NOLIGHTMAP props and collision meshes; (3) billboard imposter spec (pre-baked 8-direction at LOD2, camera-facing quad, 128x128 px frames in 1024x128 DXT5 sRGB atlas, bake elevation 45 deg below horizontal at camera pitch -45 deg, height_floors <= 3 only); (4) atlas mip chain clamped at 4 levels (documented in building-atlas-layout.md and 2d-texture-standards.md, cross-verified as present); (5) asset naming convention snake_case: <zone>_<tier>_<variant>_lod<N>.<ext> (e.g. res_low_01_lod0.b3d); (6) pivot at ground-center (Y=0, base-center of footprint) per Modular Building Kit section; (7) 5 mm vertical tolerance for floor module Y extents (max vertex deviation from Y=0 bottom or Y=3.0 top); (8) 10-floor building height cap enforced via height_floors field in .meta sidecar; (9) collision mesh dispatch order: (a) _col_0.obj multi-convex set, (b) _col_circle.obj N-sided circular prism N<=8, (c) _col.obj single convex hull, (d) none = log error and skip — dispatch prevents _col_0 shadowing _col on dual-suffixed assets; (10) billboard floor count limit (height_floors <= 3 uses billboard imposter at LOD2, height_floors >= 4 requires _lod2.b3d geometry shell 300-500 tris); (11) LOD2 pivot conformance (base-center X=0 Y=0 Z=0 identical to LOD0/LOD1, no bounding-box centroid); (12) polygon budgets per LOD tier — large building: LOD0 <= 5000 / LOD1 <= 1000 / LOD2 300-500 tris; small building: LOD0 <= 1500 / LOD1 <= 300; car: LOD0 <= 1500 / LOD1 <= 300; bus/truck: LOD0 <= 2500 / LOD1 <= 450; (13) UV channel assignment: CH0 = diffuse atlas, CH1 = lightmap (zero-indexed); vehicles use CH0 only; (14) Blender export axis settings verified: -Z Forward, Y Up in export dialog produces Y-up Z-forward left-handed output matching Irrlicht; (15) all 20 export validation checks reviewed — checks #1-#13 Phase 5 scope, #14 music sidecar Phase 5, #15 .meta stub Phase 5 with TODO Phase 9 comment, #16-#19 audio format checks Phase 5, #20 road LOD2 color Phase 9; (16) .meta sidecar JSON format verified (category, height_floors, atlas_cell, lod_distances fields); (17) per-module polygon caps verified (wall <= 300, base <= 400, roof <= 500, facade detail <= 100 tris at LOD0; facade detail piece count cap <= 10); (18) LOD2 shell lightmap requirement verified (256x256 DXT5/BC3, single mip level, multiply blend at 100% opacity); (19) vehicle atlas cell registry format and V-flip convention (V_opengl = 1 - V_blender) verified; (20) V1 minimum building coverage (18 sets: 2 variants x 3 zones x 3 tiers) verified. This sign-off is the blocking Phase 9 entry gate — no .b3d asset files may be committed until this artifact is on record. -->

<!-- SIGN-OFF: graphics-artist-3d-model 2026-03-01 — LOD2 pivot conformance confirmed for all large building types (res_high_01/02, com_high_01/02, ind_high_01/02): (a) two stacked floor modules show no visible join gap in Irrlicht scene view; (b) LOD2 shell silhouette matches LOD1 assembled building silhouette within 10% area deviation at 8 azimuth angles at 45° increments (camera pitch = −45° below horizontal). Placeholder geometry passes formal review criteria. -->

<!-- SIGN-OFF: graphics-artist-3d-model 2026-03-01 — Billboard bake geometry sign-off: placeholder billboard renders at pitch = −45°, 8 angles at 45° increments; flat ambient-only lighting; geometry silhouette at 128×128 downscaled to 14×14 px meets recognizability threshold for all small building variants (res_low_01/02, res_med_01/02, com_low_01/02, com_med_01/02, ind_low_01/02, ind_med_01/02). Straight alpha, no bleed into 8px border. -->

<!-- SIGN-OFF: graphics-artist-3d-model 2026-03-04 — Phase 10 design decisions resolved. Three new sections added to this spec: (1) Variant Selection Policy (round-robin) — Phase 10 always uses _01 suffix; round-robin counter (Phase 11) spec and buildingAssetBaseName helper function documented; no-repeat guarantee via round-robin without RNG confirmed; nine per-zone-tier counters stored as std::array<int,9> m_buildingVariantCounters in CitySimulation, not persisted in Phase 10 save (counter serialization added in Phase 11 — see Variant Selection Policy section above and `architecture/game-design/save-system.md`); (2) BuildingAssetLoader LOD Loading Contract — placeBuildingMesh() loads all three LOD variants (LOD0, LOD1, LOD2/billboard) at load time; the "load the LOD0 .b3d mesh" phrasing in Phase 10 deliverable description is shorthand for the full sequence; BuildingAsset struct carries all loaded resources and parsed .meta fields; (3) World-Space Tile Positioning (kTileSize) — kTileSize = 4.0f constexpr float declared in src/rendering/render_constants.h; consistent with 4x4 m road tile mesh and 4x4x3 m modular building kit grid; service buildings occupy a single 4x4 m origin tile in V1 even if mesh extends beyond it; do not hardcode 4.0f at call sites. These decisions unblock Phase 10 rendering stream (graphics-dev-irrlicht) and the Phase 11 variant cycling implementation. No asset re-authoring is required. -->

<!-- SIGN-OFF: graphics-artist-3d-model 2026-03-04 — Road tile mesh authoring source confirmed. Road tile LOD0/LOD1 geometry is procedurally generated in C++ at runtime (SMesh/IMeshBuffer) — no .b3d or .obj file is authored on disk for road tiles. Rationale: road tiles do not participate in the lightmap pipeline (no UV channel 1 / .b3d format requirement); the road custom shader binds road_asphalt_tileable.dds via raw GL, which is incompatible with a standard Irrlicht IMeshSceneNode loaded from .b3d; no road tile .b3d filename has appeared in any phase deliverable — road tile geometry is implicitly a graphics-dev-irrlicht C++ code deliverable. validate_assets.py must NOT check for road tile .b3d files. Artist action: none. Road kerb geometry vertex layout documented inline in the Road Tile Mesh Authoring Source section above. This decision closes the last unresolved 3D model artist item for Phase 10 start. -->
