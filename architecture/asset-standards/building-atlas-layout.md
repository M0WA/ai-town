# Building Atlas Layout

**Status**: ACTIVE — schema and atlas structure are established. Phase 9 UV authoring is complete. All required sign-offs are recorded in this file: `graphics-artist-2d-texture` (general atlas layout 2026-02-25; service building cell (3,2) texture content 2026-03-04), `graphics-dev-irrlicht` (2026-02-26), and `graphics-artist-3d-model` (multiple sign-offs through 2026-03-04). No open sign-off gates remain.

## City Building Atlas (2048×2048)

- Format: DDS DXT1 sRGB (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`); use DXT5 sRGB (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`) if any atlas cell requires alpha
- Upload path: raw-GL sRGB path (`glGenTextures` → `glBindTexture` → `glCompressedTexImage2D` with sRGB internal format) — diffuse color data requires sRGB decode. Do NOT use `IVideoDriver::getTexture()` for the building atlas.

> **V1 implementation exception — PNG format via `IVideoDriver::getTexture()`**: Irrlicht 1.8.5's
> DDS image loader (`CImageLoaderDDS`) is **disabled by default** — `_IRR_COMPILE_WITH_DDS_LOADER_`
> is commented out in `IrrCompileConfig.h`, making `IVideoDriver::getTexture()` unable to load any
> DDS file. Additionally, the `ddsBuffer` struct contains a `void* surface` field (8 bytes on
> 64-bit systems) that shifts `pixelFormat.fourCC` to file offset 88 instead of the DDS-spec
> offset 84, so even enabling the loader would silently misidentify DXT1 as ARGB8888 on x86\_64.
>
> **Current V1 workaround**: the on-disk atlas file is `buildings_atlas_d.png` (PNG, not DDS).
> `BuildingAssetLoader::load()` calls `IVideoDriver::getTexture()` with the full PNG path and
> explicitly assigns the result to every material slot via `node->getMaterial(m).setTexture(0, atlas)`.
> This bypasses the failed B3D-embedded DDS reference entirely.
>
> **Production target (Phase 11+)**: migrate to `buildings_atlas_d.dds` (DXT1 sRGB) uploaded via
> `TextureCache::loadSRGB()` (raw-GL `glCompressedTexImage2D` path), which bypasses the Irrlicht
> image loader entirely and therefore is not affected by the disabled DDS loader or the
> `ddsBuffer` struct alignment issue.

- Resolution: 2048×2048 pixels
- Cell grid: 4×4 cells at 512×512 px each (16 cells total for V1 module variants)
- Mip chain: 4-level mandatory (`GL_TEXTURE_MAX_LEVEL = 3`; 2048→1024→512→256). All four mip
  levels MUST be present as data in the DDS file — a file whose `dwMipMapCount` field declares
  4 mips but contains only mip 0 data is truncated and causes `TextureCache::loadSRGB()` to
  render a black atlas. Regenerate stubs with `python3 tools/generate_dds_stubs.py` from the
  repo root. See `architecture/asset-standards/2d-texture-standards.md` §DDS Mip Chain
  Integrity for reference byte sizes and the truncation failure mode.
- Per-cell usable area: 496×496 px (8 px border on each edge, per 2d-texture-standards.md)
- All building LOD0/LOD1 UV channel 0 maps into this atlas

### Cell Assignment Table

| Cell Row | Cell Col | Module Type | Zone | Tier | Notes |
|---|---|---|---|---|---|
| 0 | 0 | wall_residential_low | Residential | Low density | Phase 9 UV authoring complete. Variants `res_low_01` and `res_low_02` share this cell. |
| 0 | 1 | wall_commercial_low | Commercial | Low density | Phase 9 UV authoring complete. Variants `com_low_01` and `com_low_02` share this cell. |
| 0 | 2 | wall_industrial_low | Industrial | Low density | Phase 9 UV authoring complete. Variants `ind_low_01` and `ind_low_02` share this cell. |
| 0 | 3 | base_shared_low | Residential / Commercial / Industrial | Low density | Phase 9 UV authoring complete. Shared base module for all zone types at Low density (per binding decision below). |
| 1 | 0 | wall_residential_med | Residential | Med density | Phase 9 UV authoring complete. Variants `res_med_01` and `res_med_02` share this cell. |
| 1 | 1 | wall_commercial_med | Commercial | Med density | Phase 9 UV authoring complete. Variants `com_med_01` and `com_med_02` share this cell. |
| 1 | 2 | wall_industrial_med | Industrial | Med density | Phase 9 UV authoring complete. Variants `ind_med_01` and `ind_med_02` share this cell. |
| 1 | 3 | base_shared_med | Residential / Commercial / Industrial | Med / High density | Phase 9 UV authoring complete. Shared base module for all zone types at Med density; High-density buildings also reuse this cell (binding decision below). |
| 2 | 0 | wall_residential_high | Residential | High density | Phase 9 UV authoring complete. Variants `res_high_01` and `res_high_02` share this cell. |
| 2 | 1 | wall_commercial_high | Commercial | High density | Phase 9 UV authoring complete. Variants `com_high_01` and `com_high_02` share this cell. |
| 2 | 2 | wall_industrial_high | Industrial | High density | Phase 9 UV authoring complete. Variants `ind_high_01` and `ind_high_02` share this cell. |
| 2 | 3 | roof_shared | All zones | All tiers | Phase 9 UV authoring complete. All V1 zone types and density tiers share this rooftop cell. |
| 3 | 0 | facade_detail_balcony | All zones | Med/High | Phase 9 UV authoring complete. Balcony and window-bay facade detail pieces for Med and High density buildings. |
| 3 | 1 | facade_detail_pilaster | All zones | Med/High | Phase 9 UV authoring complete. Pilaster and cornice facade detail pieces for Med and High density buildings. |
| 3 | 2 | service_buildings | All service types (fire_station, police_station, power_plant, water_tower) | N/A — infrastructure | Assigned per `architecture/asset-standards/3d-model-standards.md` Service Building Model Standards (2026-03-04). Shared palette: concrete, glass, utility panels. `graphics-artist-3d-model` UV packing feasibility confirmed 2026-03-04. `graphics-artist-2d-texture` texture content confirmed 2026-03-04 (see sign-off block below). Phase 9 service building UV authoring gate is CLOSED. |
| 3 | 3 | RESERVED | — | — | Available for Phase 9+ service building material variants. Do not assign in V1. |

**Note**: All zone building module cells (0,0)–(3,1) have Phase 9 UV authoring complete. Cell (3,2) is assigned to service buildings with UV authoring complete (geometry); the `graphics-artist-2d-texture` texture content quality sign-off was recorded 2026-03-04 (see sign-off block below) — this gate is CLOSED. Cell (3,3) remains reserved. The service building cell (3,2) assignment is binding and must not be reallocated.

**IMPORTANT — Variant sharing of wall cells (binding decision)**: Building variants within the same zone-tier combination share the same wall module atlas cells — only distinct module types (wall, base, roof, facade detail) require separate cells. For example, `res_low_01` and `res_low_02` are two variants of Low-density Residential; both reference the same `wall_residential_low` atlas cell with different mesh geometry configurations. Unique cells are NOT required per variant, only per module type. This keeps the 4×4 (16-cell) atlas within capacity for all V1 building module types. This decision is binding and confirmed here before UV authoring begins. `graphics-artist-2d-texture` and `graphics-dev-irrlicht` must both sign off that all V1 variant UVs map into the correct shared module-type cell before Phase 9 UV authoring begins.

**BINDING DECISION — Shared base module and roof cells across all zone types**: The single `roof_shared` cell (row 2, col 3) is intentional — all V1 zone types and density tiers share a common rooftop texture. Similarly, Commercial and Industrial buildings share the residential base module cells (`base_residential_low` at row 0, col 3 and `base_residential_med` at row 1, col 3) for ground-floor geometry. Any zone-specific ground-floor character is encoded via facade detail pieces (row 3, cols 0–1), not separate base module cells.

**High-density base module (binding)**: There is no separate `base_*_high` cell in the V1 atlas. High-density (high-rise) buildings reuse `base_residential_med` (row 1, col 3) for their ground-floor base module. Any zone-specific High-density lobby character is handled via facade-detail pieces at row 3, cols 0–1, identical to Med density. Phase 9 UV artists must map High-density building base modules to row 1, col 3. A distinct High-density base cell would require one of the two reserved slots (row 3, col 2 or row 3, col 3) and is Phase 9+ scope only.

This decision keeps the 4×4 (16-cell) atlas within capacity for all V1 module types. The two reserved cells (row 3, col 2 and row 3, col 3) remain available for Phase 9+ use precisely because no additional base or roof cells are required in V1. **This is a binding decision — UV authoring for Phase 9 must not require additional base or roof cells.**

## Road Marking Atlas (1024×1024)

- Format: DDS DXT5/BC3 (alpha = decal mask)
- Resolution: 1024×1024 pixels
- Cell grid: 4×4 cells at 256×256 px each (16 cells)
- Per-cell usable area: 240×240 px (8 px border per 2d-texture-standards.md)
- Upload path: linear (NOT sRGB — decal mask data, not diffuse color)

### Road Decal Cell Assignment Table

| Cell | Decal Type | Notes |
|---|---|---|
| (0,0) | Straight lane marking | Double solid white line |
| (0,1) | Dashed lane marking | Broken white line |
| (0,2) | Crosswalk stripe | Zebra crossing segment |
| (0,3) | Turn arrow — left | Left turn indicator |
| (1,0) | Turn arrow — right | Right turn indicator |
| (1,1) | Turn arrow — straight | Through traffic indicator |
| (1,2) | Stop line | Thick white bar |
| (1,3) | Intersection center | Optional road center marking |
| (2,0–3,3) | RESERVED | For Phase 9+ additional decal types |

## Vehicle Atlas Registry Stubs

See `tools/vehicle_atlas_registry.json` (stub created at **Phase 4** with the V1 vehicle type assignments and schema from `architecture/asset-standards/3d-model-standards.md` — Vehicle Atlas Cell Registry; Phase 9 completes the full registry during validate_assets.py integration).

V1 minimum vehicle types:

- `car_sedan` — standard passenger car
- `car_hatchback` — compact car
- `car_suv` — larger passenger vehicle
- `bus_standard` — city bus
- `truck_cargo` — delivery/cargo truck

Two distinct vehicle atlases exist with separate purposes:

**Vehicle Diffuse Atlas** (`vehicles_diffuse_atlas_d.dds`):

- Format: DDS DXT1 sRGB
- Resolution: 2048×2048 px
- Cell grid: 4×4 cells at 512×512 px each (16 vehicle type slots)
- Purpose: Full-detail diffuse/albedo color data for LOD0 and LOD1 vehicle meshes
- Mip chain: 4-level mandatory (2048→1024→512→256); `GL_TEXTURE_MAX_LEVEL = 3`
- Upload path: raw-GL sRGB path (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`) — diffuse color data requires sRGB decode
- Cell assignments maintained in `tools/vehicle_atlas_registry.json`

> **V1 implementation exception — PNG format**: same constraint as the city building atlas above.
> On-disk file is `vehicles_diffuse_atlas_d.png`. `BuildingAssetLoader::load()` loads it via
> `IVideoDriver::getTexture()`. Production DXT1 sRGB DDS migration is Phase 11+.

**Vehicle Sprite Atlas** (`vehicles_sprite_atlas_d.dds`):

- Format: DDS DXT5 (BC3). **The vehicle sprite atlas (`vehicles_sprite_atlas_d.dds`) MUST use DXT5 (BC3) compression — NOT DXT1 (BC1). DXT1 has no per-pixel alpha channel and cannot represent the roof-silhouette alpha required for transparent vehicle tops. DXT5 stores 4-bit alpha per pixel. This mandate applies to all LOD levels of the vehicle atlas. Using DXT1 for the vehicle atlas is a HARD ERROR — the validate-assets CI step (Phase 5) will reject any vehicle atlas with FourCC `DXT1`.** DXT1 only supports 1-bit alpha, which is insufficient for smooth vehicle silhouettes; DXT5 (BC3) is the only valid compression format for this atlas.
- Resolution: 256×256 px
- Cell grid: 16×16 cells at 16×16 px each (256 sprite slots)
- Purpose: Point/sprite LOD2 representation at distances beyond 100 m — each sprite is a 16×16 px roof-color/type identifier rendered as a camera-facing billboard quad (1 m × 0.5 m)
- Mip chain: none (`GL_TEXTURE_MAX_LEVEL = 0`) — sprites are rendered at near-fixed screen size; mip filtering would blur 16×16 px cells into unrecognisable blobs
- Upload path: linear pool (`IVideoDriver::getTexture()`) — sprite atlas encodes stylised roof color swatches, not photographic diffuse data; sRGB path is not required

> **LINEAR UPLOAD EXCEPTION — Vehicle Sprite Atlas**: Although DXT5 (BC3) compression is used for this atlas (required for per-pixel alpha), the `upload_path` is intentionally `"linear"` — NOT `"srgb"`. This is NOT a contradiction with the sRGB diffuse rule. The sRGB upload requirement applies to photographic or painterly diffuse/albedo color data that was authored in a perceptual color space and must be gamma-expanded before lighting calculations. The vehicle sprite atlas contains only synthetic, palette-swatch roof colors (flat solid fills — typically 4–6 unique hues per vehicle type) authored directly in linear color space as design identifiers, not as photographic diffuse data. Applying sRGB gamma decode to palette swatches would incorrectly brighten the solid fills relative to their designed appearance. `TextureCache` routes this atlas through `loadLinear()` (`IVideoDriver::getTexture()`) and does NOT route it through `loadSRGB()` (the raw-GL sRGB path). This routing is intentional and must not be changed. If the sprite atlas content is ever replaced with photographic or painterly roof textures, this exception must be revisited and `upload_path` updated to `"srgb"` with a corresponding `TextureCache` routing change.

These two atlases are **not interchangeable**. The diffuse atlas feeds the mesh material pipeline (UV channel 0 of LOD0/LOD1 vehicle meshes); the sprite atlas feeds the point-sprite LOD2 renderer. Do not conflate their formats, resolutions, or upload paths.

**Vehicle Normal Atlas** (`vehicles_normal_atlas_n.dds`):

- Format: DDS DXT5nm/BC3 (X→alpha, Y→green, Z discarded; Y-flip before swizzle for OpenGL convention; linear — normal map data must not be sRGB-decoded; ref: `architecture/asset-standards/2d-texture-standards.md` DXT5nm section)
- Resolution: 2048×2048 px
- Cell grid: 8×8 cells at 256×256 px each (64 vehicle type slots)
- Purpose: Tangent-space normal map for LOD0 and LOD1 vehicle meshes, providing surface-detail lighting without additional geometry
- Upload path: linear pool (`IVideoDriver::getTexture()`) — normal vector data is not perceptual color; sRGB decode would corrupt the encoded direction vectors
- Row/column assignments: identical to the diffuse atlas (`vehicles_diffuse_atlas_d.dds`). A vehicle assigned to diffuse atlas cell (row R, col C) on the 4×4 grid maps to the same logical row R and column C in the normal atlas, but the cell UV range reflects the 8×8 grid: `U ∈ [C/8, (C+1)/8]`, `V ∈ [R/8, (R+1)/8]`. The `_comment_normal_atlas` field in `tools/vehicle_atlas_registry.json` documents this relationship explicitly.
- The export validation script check #12 verifies vehicle normal atlas UV channel 0 coordinates against this 8×8 grid using the same V-axis OpenGL convention (V=0 at bottom, `V_opengl = 1 − V_blender`) as the diffuse atlas.
- Mip chain: 4-level mandatory (levels 0–3; 2048→1024→512→256); generated from the full-resolution normal map layer via bicubic downsampling before DXT5nm compression. The mip chain must be authored (not driver-generated) to preserve normal vector normalization across levels. Because `vehicles_normal_atlas_n.dds` is uploaded via `IVideoDriver::getTexture()` (linear pool), `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3)` cannot be applied through the Irrlicht driver path. The V1 workaround: authors MUST pre-bake exactly 4 mip levels (levels 0–3) into the DDS file header so the driver loads no more than the authored levels. Do NOT rely on driver-side mip generation for this atlas — the driver will generate additional levels beyond level 3 from a 2048×2048 input using bilinear (not bicubic) sampling, corrupting the normal vector normalization at those levels. Storing exactly 4 authored mip levels in the DDS header is the V1 workaround. Phase 5+ may implement a raw-GL upload path to apply `GL_TEXTURE_MAX_LEVEL` explicitly.

### Required JSON Schema for `tools/vehicle_atlas_registry.json`

All mandatory top-level keys and their structure are defined below. The stub file created at Phase 4 must conform to this schema exactly. `validate_assets.py` (Phase 9) must reject any registry file that is missing a required top-level key or whose `upload_path` values deviate from those shown here.

```json
{
  "_comment": "...",
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
    "_comment_normal_atlas": "Same row/col as diffuse but 8x8 grid; U=[C/8,(C+1)/8], V=[R/8,(R+1)/8]"
  },
  "sprite_atlas": {
    "_comment": "256x256 DXT5 sprite atlas; 16x16 cell grid; 16x16 px per cell; GL_TEXTURE_MAX_LEVEL=0; upload_path: linear (roof color swatches, not photographic diffuse)",
    "atlas_file": "vehicles_sprite_atlas_d.dds",
    "grid": { "cols": 16, "rows": 16, "cell_size_px": 16 },
    "mip_levels": 1,
    "upload_path": "linear"
  },
  "assignments": [
    {
      "vehicle_id": "car_sedan",
      "row": 0, "col": 0,
      "comment": "Standard passenger car"
    }
  ]
}
```

**Critical upload path note**: The `sprite_atlas` upload_path MUST be `"linear"` (NOT `"srgb"`). The sprite atlas encodes stylised roof color swatches, not photographic diffuse data — sRGB gamma decode is incorrect for palette swatches. Using `"srgb"` would incorrectly gamma-expand the colors in the point-sprite LOD2 renderer.

## Billboard Imposter Atlas

LOD2 billboard textures for small buildings and props use a separate 1024×128 DXT5 sRGB atlas (not part of the city building atlas or vehicle atlases above). See `architecture/asset-standards/2d-texture-standards.md` "Billboard Imposter Atlas" section for the full authoring spec (bake angles, elevation, lighting, cell padding, usable content area, naming convention).

> **Billboard Imposter Atlas Mip Chain (Mandatory)**: LOD2 billboard textures (1024×128 DXT5 sRGB atlas) **require a 4-level mip chain** (`GL_TEXTURE_MAX_LEVEL = 3`; mips 0-3: 1024×128 → 512×64 → 256×32 → 128×16). The 8-texel per-frame border is sized to shrink to exactly 1 texel at mip level 3, which is the minimum safe margin to prevent bleed between adjacent frames in the strip. Do NOT set `GL_TEXTURE_MAX_LEVEL = 0` on billboard atlases. **Lightmap textures** (`_lm` suffix) are the category that uses `GL_TEXTURE_MAX_LEVEL = 0` (single mip level only) — lightmaps are sampled at a consistent scale close to camera and do not benefit from mip chains. See `architecture/asset-standards/2d-texture-standards.md` for the full lightmap exemption rationale.

## Sign-Off Checklist

**Sign-off recording requirement**: Each reviewer's confirmation MUST be recorded as a dated comment block appended to this file before Phase 9 UV authoring begins. Use the following format exactly — unsigned or undated confirmations are not traceable and will not satisfy the exit criterion:

```text
<!-- SIGN-OFF: [role] [YYYY-MM-DD] — confirmed [condition] -->
```

Example:

```text
<!-- SIGN-OFF: graphics-artist-2d-texture 2026-04-15 — confirmed all V1 cell assignments are within 496x496 px usable area and road marking cells cover all V1 decal types -->
```

All three sign-off comment blocks must be present in this file before Phase 9 UV authoring begins. A missing or undated block is a blocking exit criterion failure.

**Phase 9 UV authoring gate status (as of Phase 10 start, 2026-03-04)**: Phase 9 is complete. All items below are confirmed. The `graphics-artist-2d-texture` service building texture content sign-off for cell (3,2) was completed 2026-03-04 (see sign-off comment block below). No open sign-off gates remain.

Before Phase 9 UV authoring begins, all three reviewers must confirm:

- [x] All V1 module variant types have a cell assignment (no unbounded "assign in Phase 9" placeholders remaining for zone building module types — service building cell (3,2) is already assigned). Confirmed: Phase 9 UV authoring complete; Cell Assignment Table updated 2026-03-04 by `graphics-artist-3d-model`.
- [x] Road marking atlas cell assignments cover all V1 decal types. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Vehicle atlas stub entries match the V1 vehicle type list. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Cell UV borders (8 px) respected in all assignments. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Document reviewed and approved by `graphics-artist-2d-texture`; service building cell (3,2) texture content confirmed 2026-03-04: concrete, glass, and utility panel materials for all four V1 service building types fit within the 496×496 px usable area of cell (3,2); cell (3,3) confirmed not needed for V1 (see sign-off block below). All sign-off items complete.
- [x] Document reviewed and approved by `graphics-dev-irrlicht`. Confirmed 2026-02-26 sign-off.
- [x] Document reviewed and approved by `graphics-artist-3d-model`: (a) the shared atlas cell variant approach (multiple mesh variants referencing one module-type cell) is compatible with modular kit UV authoring workflows; (b) per-module UV islands can be fully authored within the 496×496 px usable area per 512×512 cell without requiring bleed into the 8 px border; (c) the 4×4 cell grid and 16-cell capacity correctly covers the V1 minimum building module set, including all zone-tier wall, base, roof, and facade-detail types across Residential, Commercial, and Industrial zones; (d) service building UV islands for all four V1 service building types can be packed within the 496×496 px usable area of cell (3,2) without mutual overlap. Confirmed by multiple sign-offs (2026-02-21, 2026-02-25, 2026-02-28, 2026-03-01, 2026-03-04).

**Service building atlas cell gate** (`graphics-artist-3d-model` decision, 2026-03-04; `graphics-artist-2d-texture` texture content confirmation, 2026-03-04): Cell (3,2) is assigned to service buildings with a shared palette. The UV packing constraint for four service building types within one 496×496 px cell is feasible because service buildings at `height_floors = 2` have a simple footprint and a limited material count (concrete, glass, utility panels). Each building type's UV islands may reference any sub-region of the cell; the shared-palette approach means all four types draw from the same texel data, so UV overlap between types is not only permitted but expected. The only constraint is that no single building type's UV islands extend outside the 496×496 px usable boundary. This is the same constraint already confirmed for zone building module types at this cell size. Both packing feasibility (by `graphics-artist-3d-model`) and texture content quality (by `graphics-artist-2d-texture`) are now confirmed. This gate is CLOSED.

**Phase 1 dated sign-off record — `graphics-artist-3d-model` atlas compatibility** (required before Phase 1 exit):

> Phase 1 sign-off — 2026-02-21: The three shared atlas cell variant compatibility checks for `graphics-artist-3d-model` are confirmed as follows: (a) the shared atlas cell variant approach — multiple mesh variants of the same zone-tier type referencing a single module-type cell — is compatible with modular kit UV authoring workflows in Blender; per-variant UV islands can be placed within the same atlas cell without conflicting UV shells by isolating each variant's islands to a distinct sub-region of the cell; (b) per-module UV islands can be fully authored within the 496×496 px usable area of each 512×512 cell without requiring bleed into the 8 px border; (c) the 4×4 cell grid with 16 cells correctly covers the V1 minimum building module set, including all zone-tier wall, base, roof, and facade-detail types across Residential, Commercial, and Industrial zones without exceeding atlas capacity. This record confirms the Phase 1 exit criterion for the building atlas `graphics-artist-3d-model` sign-off gate is satisfied. Signed: graphics-artist-3d-model.

<!-- SIGN-OFF: graphics-artist-2d-texture 2026-02-25 — confirmed all V1 cell assignments are within the 496×496 px usable area per cell; all required road marking cell types for V1 decals are covered; all V1 vehicle atlas stub rows are finalized and consistent with the diffuse/sprite/normal atlas split; texture unit assignments in shader_constants.h (kTexUnitDiffuse=0, kTexUnitNormal=1, kTexUnitBillboard=9, terrain layers 5–8) are finalized and will not change during Phase 5 texture production -->

**Phase 4 dated sign-off record — `graphics-artist-2d-texture` pre-alignment gate** (required before Phase 5 UV authoring):

> Phase 4 sign-off — 2026-02-25: The pre-alignment gate checks for `graphics-artist-2d-texture` are confirmed as follows: (a) all V1 cell assignments are within the 496×496 px usable area per 512×512 cell (8 px border respected on all sides); (b) all required road marking cell types for V1 decals are covered in the atlas layout; (c) all V1 vehicle atlas stub rows are finalized and consistent with the diffuse (4×4 grid, sRGB upload), normal (8×8 grid, linear upload), and sprite (16×16 grid, linear upload) atlas split. Texture unit assignments reviewed: kTexUnitDiffuse=0, kTexUnitNormal=1, kTexUnitBillboard=9, terrain detail layers 5–8 — these assignments are finalized and will not change during Phase 5 texture production, preventing artist rework from late renumbering. Signed: graphics-artist-2d-texture.

- [x] **Lightmap atlas (`_lm` suffix) upload path** — Phase 2 exit criterion: closed (2026-03-04). The `_lm` routing through `loadLinear()` is fully documented in `architecture/graphics-architecture/texture-cache.md` `GL_TEXTURE_MAX_LEVEL` dispatch table (`_lm` row: `loadLinear()`, driver-default single mip, `GL_TEXTURE_MAX_LEVEL = 0`). The `graphics-dev-irrlicht` sign-off dated 2026-02-26 below confirms implementation in `TextureCache.cpp`. Lightmap data encodes pre-baked irradiance in linear light units and must not be routed through `loadSRGB()` — this constraint is enforced by the dispatch table. Phase 5 lightmapping work was completed on this basis. No further action required.

<!-- SIGN-OFF: graphics-artist-2d-texture 2026-02-25 — confirmed splat channel lock:
     R=base (biome-specific: grassland=grass, desert=sand), G=asphalt, B=soil, A=concrete;
     content-only swap rule confirmed; per-chunk splat map 16×16 px for 64×64 m chunk confirmed -->

## Phase 5 Building Atlas Layout Sign-Off

**`graphics-artist-3d-model` — 2026-02-25**

Confirmed:

1. **Shared atlas cell variant approach**: Multiple mesh variants referencing one module-type cell
   (e.g., all residential low-rise variants UV-mapped to the same `wall_residential_low` cell) is
   fully compatible with modular kit UV authoring workflows. Artists author UV islands per module
   type, not per building variant — this is the standard modular kit approach. Per-variant UV
   islands are placed within distinct sub-regions of the shared cell without conflicting UV shells.

2. **Per-module UV islands within 496x496 px usable area**: With a 512x512 px cell and 8 px border
   on all sides, the 496x496 px usable area is sufficient for all V1 building module types at the
   planned resolution (diffuse at DXT1 256x256 effective per island, normal at DXT5nm 256x256 per
   island). No module type requires bleed into the 8 px border.

3. **4x4 grid, 16-cell capacity covers V1 minimum module set**: The 16 cells cover: 9 zone-tier
   wall module cells (Residential/Commercial/Industrial × Low/Med/High density — row 0–2, cols
   0–2); 2 base module cells shared across all zones (base_residential_low at row 0 col 3 for
   Low tier; base_residential_med at row 1 col 3 for Med tier — High-density buildings reuse
   base_residential_med per the binding decision in this file); 1 shared roof cell (row 2, col 3);
   2 facade-detail cells (balcony row 3 col 0; pilaster row 3 col 1). Road surface materials
   are NOT in this atlas — they belong in the separate Road Marking Atlas (1024×1024) and
   road_asphalt_tileable.dds. No civic zone exists in V1. 16 cells is sufficient for V1 scope with
   2 spare cells (row 3, col 2 and row 3, col 3) remaining available for Phase 9+ use without
   requiring an atlas resolution increase.

This sign-off satisfies the Phase 5 exit criterion for the `graphics-artist-3d-model` building
atlas sign-off gate. Phase 9 UV authoring for building mesh UV channel 0 may proceed once all
three required sign-off comment blocks are present in this file.

<!-- SIGN-OFF: graphics-artist-3d-model 2026-02-25 — confirmed: (a) shared atlas cell variant approach (multiple mesh variants referencing one module-type cell) is compatible with modular kit UV authoring workflows; (b) per-module UV islands fit within 496x496 px usable area per 512x512 cell without bleed into 8 px border; (c) 4x4 grid 16-cell capacity covers V1 minimum building module set: 9 zone-tier wall cells (Res/Com/Ind x Low/Med/High), 2 shared base cells (Low=row0col3, Med=row1col3; High-density buildings reuse Med base cell per binding decision), 1 shared roof cell (row2col3), 2 facade-detail cells (row3col0-1), 2 reserved. Road modules and civic zone are NOT covered here — they belong in separate atlases or do not exist in V1. [CORRECTED 2026-02-26: original sign-off text erroneously listed road modules and civic building facades as covered by this atlas; corrected per Phase 5 verification review] -->

<!-- SIGN-OFF: graphics-artist-3d-model 2026-03-04 — service building atlas cell assignment confirmed: cell (3,2) is assigned to all four V1 service building types (fire_station, police_station, power_plant, water_tower) sharing a common material palette (concrete, glass, utility panels). UV packing feasibility for all four service building types within the 496x496 px usable area of cell (3,2) confirmed: at height_floors=2 each service building has a simple rectangular footprint (LOD0 500-1500 tris) with a limited surface material count. All four types draw from the same texel data (shared palette approach), so UV overlap between types is permitted and expected — the only authoring constraint is that no single type's UV islands extend outside the 496x496 px usable boundary. This is identical to the zone building module-type cell constraint already confirmed in the 2026-02-25 sign-off. The cell (3,3) assignment remains RESERVED for Phase 9+ service building material variants. Cell (3,2) assignment is binding and must not be reallocated. The graphics-artist-2d-texture confirmation of texture content quality (concrete/glass/utility panels within 496x496 px) is separately required before Phase 9 service building UV authoring begins. -->

<!-- SIGN-OFF: graphics-artist-2d-texture 2026-03-04 — service building atlas cell (3,2) texture content confirmed: (a) a shared palette covering concrete, glass, and utility panel materials for all four V1 service building types (fire_station, police_station, power_plant, water_tower) fits comfortably within the 496x496 px usable area of cell (3,2). Texture content assessment: concrete = grey tonal base with surface micro-detail (cracks, aggregation lines); glass = tonal vertical bands with specular highlight strip; utility panels = rectilinear panel lines, bolt detail, hazard stripe for power_plant variant. All three material types are low-polygon-silhouette, low-colour-complexity surfaces that pack efficiently within a shared palette — each material type requires approximately 80x496 px of horizontal strip space or equivalent sub-region, leaving adequate breathing room within the 496x496 cell. At 512 px per atlas axis (496 usable), the texel density is approximately 256 px per metre for a nominal 2 m service building face — sufficient for the utilitarian industrial aesthetic at LOD0 through LOD1 viewing distances. (b) The reserved cell (3,3) is NOT needed for V1 service buildings. The shared-palette approach means all four types draw from identical texel data; no additional cell is required for material variety within V1 scope. Cell (3,3) remains RESERVED for Phase 9+ service building material variants (e.g. a second palette for future building types). This sign-off resolves the PENDING gate noted by graphics-artist-3d-model. Phase 9 service building UV authoring may proceed with the confirmed cell (3,2) layout. -->

<!-- SIGN-OFF: graphics-dev-irrlicht 2026-02-26 — confirmed: (1) building atlas upload path (buildings_atlas_d.dds) uses raw-GL sRGB path (glGenTextures + glBindTexture + glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT; DXT5 variant GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT used when alpha cells required) — correctly specified here and implemented in TextureCache::loadSRGB() (TextureCache.cpp); (2) GL_TEXTURE_MAX_LEVEL = 3 for all 4-level mip atlases (_d diffuse, _billboard imposter, vehicles_diffuse_atlas_d.dds) is consistent with the TextureCache GL_TEXTURE_MAX_LEVEL dispatch table in texture-cache.md and with the glTexParameteri call in TextureCache.cpp loadSRGB(); lightmaps (_lm) correctly use driver default (single-mip, no raw-GL path); splat maps correctly use GL_TEXTURE_MAX_LEVEL = 0; no mip-level collisions found; (3) texture unit assignments kTexUnitDiffuse=0, kTexUnitNormal=1, kTexUnitBillboard=9, terrain layers 5-8 are finalized in src/rendering/shader_constants.h and are consistent with the shader-loading.md terrain splat shader 5-unit binding sequence and the 2d-texture-standards.md Shader Texture Unit Assignment Table — no unit collisions; (4) vehicle sprite atlas linear upload exception (vehicles_sprite_atlas_d.dds: DXT5 format, loadLinear() path, NOT sRGB) is correctly documented here and in texture-cache.md, and is correctly implemented via exact-filename dispatch in TextureCache::loadSRGB() routing to loadLinear() — palette swatches are not photographic diffuse, sRGB decode is incorrect for this atlas; (5) DXT5 mandate for vehicle sprite atlas (not DXT1) is correctly specified and enforceable via validate_assets.py DDS FourCC check — current check #10 covers registry/UV cell assignments; the DDS format enforcement for vehicles_sprite_atlas_d.dds is a Phase 9 validate_assets.py deliverable per the registry integration scope noted in this file; the spec mandate is clear and the enforcement path is defined -->
