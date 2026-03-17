# Building Atlas Layout

**Status**: ACTIVE — schema and atlas structure are established. Phase 9 UV authoring is complete.
Phase-11e expanded atlas to 4096×4096 (8×8 grid, per-variant unique cells). All required sign-offs
are recorded in this file: `graphics-artist-2d-texture` (general atlas layout 2026-02-25; service
building texture content 2026-03-04), `graphics-dev-irrlicht` (2026-02-26), and
`graphics-artist-3d-model` (multiple sign-offs through 2026-03-04). No open sign-off gates remain.

## City Building Atlas (4096×4096)

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

- Resolution: 4096×4096 pixels
- Cell grid: 8×8 cells at 512×512 px each (64 cells total; rows 0–4 assigned, rows 5–7 RESERVED)
- Mip chain: 5-level mandatory (`GL_TEXTURE_MAX_LEVEL = 4`; 4096→2048→1024→512→256). All five mip
  levels MUST be present as data in the DDS file — a file whose `dwMipMapCount` field declares
  5 mips but contains only mip 0 data is truncated and causes `TextureCache::loadSRGB()` to
  render a black atlas. Regenerate stubs with `python3 tools/generate_dds_stubs.py` from the
  repo root. See `architecture/asset-standards/2d-texture-standards.md` §DDS Mip Chain
  Integrity for reference byte sizes and the truncation failure mode.
- Per-cell usable area: 496×496 px (8 px border on each edge, per 2d-texture-standards.md)
- All building LOD0/LOD1 UV channel 0 maps into this atlas

### Cell Assignment Table

Phase-11e expansion: the 8×8 grid provides one unique 512×512 cell per variant. Each of the 40
assigned cells (rows 0–4, cols 0–7) is exclusive to a single building variant or service building
type. Rows 5–7 (24 cells) are RESERVED for future expansion.

| Cell Row | Cell Col | Variant / Asset | Notes |
|---|---|---|---|
| 0 | 0 | `res_low_01` | Residential Low density variant 01. Phase 9 UV authoring complete. |
| 0 | 1 | `res_low_02` | Residential Low density variant 02. Phase 9 UV authoring complete. |
| 0 | 2 | `res_low_03` | Residential Low density variant 03. Phase 9 UV authoring complete. |
| 0 | 3 | `res_low_04` | Residential Low density variant 04. Phase 9 UV authoring complete. |
| 0 | 4 | `res_med_01` | Residential Med density variant 01. Phase 9 UV authoring complete. |
| 0 | 5 | `res_med_02` | Residential Med density variant 02. Phase 9 UV authoring complete. |
| 0 | 6 | `res_med_03` | Residential Med density variant 03. Phase 9 UV authoring complete. |
| 0 | 7 | `res_med_04` | Residential Med density variant 04. Phase 9 UV authoring complete. |
| 1 | 0 | `res_high_01` | Residential High density variant 01. Phase 9 UV authoring complete. |
| 1 | 1 | `res_high_02` | Residential High density variant 02. Phase 9 UV authoring complete. |
| 1 | 2 | `res_high_03` | Residential High density variant 03. Phase 9 UV authoring complete. |
| 1 | 3 | `res_high_04` | Residential High density variant 04. Phase 9 UV authoring complete. |
| 1 | 4 | `com_low_01` | Commercial Low density variant 01. Phase 9 UV authoring complete. |
| 1 | 5 | `com_low_02` | Commercial Low density variant 02. Phase 9 UV authoring complete. |
| 1 | 6 | `com_low_03` | Commercial Low density variant 03. Phase 9 UV authoring complete. |
| 1 | 7 | `com_low_04` | Commercial Low density variant 04. Phase 9 UV authoring complete. |
| 2 | 0 | `com_med_01` | Commercial Med density variant 01. Phase 9 UV authoring complete. |
| 2 | 1 | `com_med_02` | Commercial Med density variant 02. Phase 9 UV authoring complete. |
| 2 | 2 | `com_med_03` | Commercial Med density variant 03. Phase 9 UV authoring complete. |
| 2 | 3 | `com_med_04` | Commercial Med density variant 04. Phase 9 UV authoring complete. |
| 2 | 4 | `com_high_01` | Commercial High density variant 01. Phase 9 UV authoring complete. |
| 2 | 5 | `com_high_02` | Commercial High density variant 02. Phase 9 UV authoring complete. |
| 2 | 6 | `com_high_03` | Commercial High density variant 03. Phase 9 UV authoring complete. |
| 2 | 7 | `com_high_04` | Commercial High density variant 04. Phase 9 UV authoring complete. |
| 3 | 0 | `ind_low_01` | Industrial Low density variant 01. Phase 9 UV authoring complete. |
| 3 | 1 | `ind_low_02` | Industrial Low density variant 02. Phase 9 UV authoring complete. |
| 3 | 2 | `ind_low_03` | Industrial Low density variant 03. Phase 9 UV authoring complete. |
| 3 | 3 | `ind_low_04` | Industrial Low density variant 04. Phase 9 UV authoring complete. |
| 3 | 4 | `ind_med_01` | Industrial Med density variant 01. Phase 9 UV authoring complete. |
| 3 | 5 | `ind_med_02` | Industrial Med density variant 02. Phase 9 UV authoring complete. |
| 3 | 6 | `ind_med_03` | Industrial Med density variant 03. Phase 9 UV authoring complete. |
| 3 | 7 | `ind_med_04` | Industrial Med density variant 04. Phase 9 UV authoring complete. |
| 4 | 0 | `ind_high_01` | Industrial High density variant 01. Phase 9 UV authoring complete. |
| 4 | 1 | `ind_high_02` | Industrial High density variant 02. Phase 9 UV authoring complete. |
| 4 | 2 | `ind_high_03` | Industrial High density variant 03. Phase 9 UV authoring complete. |
| 4 | 3 | `ind_high_04` | Industrial High density variant 04. Phase 9 UV authoring complete. |
| 4 | 4 | `svc_fire_station` | Service: fire station. UV authoring complete; concrete/glass/utility palette confirmed 2026-03-04. |
| 4 | 5 | `svc_police_station` | Service: police station. UV authoring complete; concrete/glass/utility palette confirmed 2026-03-04. |
| 4 | 6 | `svc_power_plant` | Service: power plant. UV authoring complete; concrete/glass/utility palette confirmed 2026-03-04. |
| 4 | 7 | `svc_water_tower` | Service: water tower. UV authoring complete; concrete/glass/utility palette confirmed 2026-03-04. |
| 5–7 | 0–7 | RESERVED | 24 cells reserved for future expansion. Do not assign in V1. |

**Phase-11e per-variant unique cell assignment**: The 8×8 atlas expansion (phase-11e) allows and
implements a unique 512×512 cell per variant. Each of the 36 zone-building variants
(`res`/`com`/`ind` × `low`/`med`/`high` × 01–04) and all 4 service building types has its own
dedicated atlas cell. The previous shared-cell approach (multiple variants mapped to one
module-type cell) is superseded; per-variant UV islands now occupy the full 496×496 px usable
area of their own cell without sub-region partitioning constraints.

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

**Phase 9 UV authoring gate status (as of Phase 10 start, 2026-03-04; updated phase-11e)**: Phase 9
is complete. All items below are confirmed. The `graphics-artist-2d-texture` service building
texture content sign-off was completed 2026-03-04 (see sign-off comment block below). The
phase-11e 8×8 atlas expansion assigns each service building type its own dedicated cell (row 4,
cols 4–7). No open sign-off gates remain.

Before Phase 9 UV authoring begins, all three reviewers must confirm:

- [x] All V1 module variant types have a cell assignment (no unbounded "assign in Phase 9" placeholders remaining; phase-11e per-variant cells assigned for all 36 zone-building variants and 4 service building types). Confirmed: Phase 9 UV authoring complete; Cell Assignment Table updated 2026-03-04 and expanded phase-11e by `graphics-artist-3d-model`.
- [x] Road marking atlas cell assignments cover all V1 decal types. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Vehicle atlas stub entries match the V1 vehicle type list. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Cell UV borders (8 px) respected in all assignments. Confirmed by `graphics-artist-2d-texture` 2026-02-25 sign-off.
- [x] Document reviewed and approved by `graphics-artist-2d-texture`; service building texture content confirmed 2026-03-04: concrete, glass, and utility panel materials for all four V1 service building types fit within the 496×496 px usable area of their respective dedicated cells (phase-11e: row 4, cols 4–7) (see sign-off block below). All sign-off items complete.
- [x] Document reviewed and approved by `graphics-dev-irrlicht`. Confirmed 2026-02-26 sign-off.
- [x] Document reviewed and approved by `graphics-artist-3d-model`: (a) the phase-11e 8×8 per-variant unique cell assignment is compatible with modular kit UV authoring workflows — each variant has its own 512×512 cell and may use the full 496×496 px usable area without sub-region partitioning; (b) per-variant UV islands can be fully authored within the 496×496 px usable area per 512×512 cell without requiring bleed into the 8 px border; (c) the 8×8 cell grid and 64-cell capacity correctly covers all 36 V1 zone-building variants and all 4 service building types across Residential, Commercial, and Industrial zones, with 24 cells reserved for future expansion; (d) service building UV islands for all four V1 service building types each occupy a dedicated cell (row 4, cols 4–7) with the full 496×496 px usable area available. Confirmed by multiple sign-offs (2026-02-21, 2026-02-25, 2026-02-28, 2026-03-01, 2026-03-04).

**Service building atlas cell gate** (`graphics-artist-3d-model` decision, 2026-03-04; `graphics-artist-2d-texture` texture content confirmation, 2026-03-04; updated phase-11e): Each service building type has its own dedicated cell: `svc_fire_station` (4,4), `svc_police_station` (4,5), `svc_power_plant` (4,6), `svc_water_tower` (4,7). The previous single-cell shared-palette approach (all four service types in one cell) is superseded by the phase-11e per-variant expansion. Each service type now has the full 496×496 px usable area of its dedicated cell. The material palette (concrete, glass, utility panels) and UV feasibility confirmation from 2026-03-04 remain valid. Both packing feasibility (by `graphics-artist-3d-model`) and texture content quality (by `graphics-artist-2d-texture`) are confirmed. This gate is CLOSED.

**Phase 1 dated sign-off record — `graphics-artist-3d-model` atlas compatibility** (required before Phase 1 exit):

> Phase 1 sign-off — 2026-02-21 (historical; superseded by phase-11e 8×8 expansion): The original
> shared atlas cell variant compatibility checks for `graphics-artist-3d-model` were confirmed as
> follows: (a) the shared atlas cell variant approach — multiple mesh variants of the same zone-tier
> type referencing a single module-type cell — is compatible with modular kit UV authoring workflows
> in Blender; (b) per-module UV islands can be fully authored within the 496×496 px usable area of
> each 512×512 cell without requiring bleed into the 8 px border; (c) the then-current 4×4 cell
> grid with 16 cells covered the V1 minimum building module set at the time of sign-off. The
> phase-11e expansion to an 8×8 grid supersedes item (c) — each variant now has a unique dedicated
> cell. Items (a) and (b) remain valid. This record confirms the Phase 1 exit criterion for the
> building atlas `graphics-artist-3d-model` sign-off gate is satisfied. Signed:
> graphics-artist-3d-model.

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

1. **Per-variant unique cell approach (phase-11e)**: The 8×8 atlas expansion provides a dedicated
   512×512 cell for each of the 36 zone-building variants and 4 service building types. Each
   variant's UV islands occupy the full 496×496 px usable area of its own cell without sub-region
   partitioning. This supersedes the earlier shared-cell approach and is fully compatible with
   modular kit UV authoring workflows.

2. **Per-variant UV islands within 496x496 px usable area**: With a 512x512 px cell and 8 px border
   on all sides, the 496x496 px usable area is sufficient for all V1 building variant types at the
   planned resolution (diffuse at DXT1 256x256 effective per island, normal at DXT5nm 256x256 per
   island). No variant requires bleed into the 8 px border.

3. **8x8 grid, 64-cell capacity covers V1 variants plus future expansion**: The 40 assigned cells
   (rows 0–4, cols 0–7) cover all 36 zone-building variants (Residential/Commercial/Industrial ×
   Low/Med/High density × 01–04 variants) and 4 service building types. Road surface materials are
   NOT in this atlas — they belong in the separate Road Marking Atlas (1024×1024) and
   road_asphalt_tileable.dds. No civic zone exists in V1. The 24 reserved cells (rows 5–7) are
   available for future expansion without requiring an atlas resolution increase.

This sign-off satisfies the Phase 5 exit criterion for the `graphics-artist-3d-model` building
atlas sign-off gate. Phase 9 UV authoring for building mesh UV channel 0 may proceed once all
three required sign-off comment blocks are present in this file.

<!-- SIGN-OFF: graphics-artist-3d-model 2026-02-25 — confirmed: (a) shared atlas cell variant approach (multiple mesh variants referencing one module-type cell) is compatible with modular kit UV authoring workflows; (b) per-module UV islands fit within 496x496 px usable area per 512x512 cell without bleed into 8 px border; (c) 4x4 grid 16-cell capacity covers V1 minimum building module set: 9 zone-tier wall cells (Res/Com/Ind x Low/Med/High), 2 shared base cells (Low=row0col3, Med=row1col3; High-density buildings reuse Med base cell per binding decision), 1 shared roof cell (row2col3), 2 facade-detail cells (row3col0-1), 2 reserved. Road modules and civic zone are NOT covered here — they belong in separate atlases or do not exist in V1. [CORRECTED 2026-02-26: original sign-off text erroneously listed road modules and civic building facades as covered by this atlas; corrected per Phase 5 verification review] -->

<!-- SIGN-OFF: graphics-dev-irrlicht 2026-02-26 — confirmed: (1) building atlas upload path (buildings_atlas_d.dds) uses raw-GL sRGB path (glGenTextures + glBindTexture + glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT; DXT5 variant GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT used when alpha cells required) — correctly specified here and implemented in TextureCache::loadSRGB() (TextureCache.cpp); (2) building atlas (buildings_atlas_d.dds, 4096×4096): GL_TEXTURE_MAX_LEVEL = 4 (5-level mip chain: 4096→2048→1024→512→256); billboard imposter atlas and vehicles_diffuse_atlas_d.dds: GL_TEXTURE_MAX_LEVEL = 3 (4-level mip chain: 1024×128/2048×2048→...→256 or smaller); all values consistent with the TextureCache GL_TEXTURE_MAX_LEVEL dispatch table in texture-cache.md; lightmaps (_lm) correctly use driver default (single-mip, no raw-GL path); splat maps correctly use GL_TEXTURE_MAX_LEVEL = 0; no mip-level collisions found; (3) texture unit assignments kTexUnitDiffuse=0, kTexUnitNormal=1, kTexUnitBillboard=9, terrain layers 5-8 are finalized in src/rendering/shader_constants.h and are consistent with the shader-loading.md terrain splat shader 5-unit binding sequence and the 2d-texture-standards.md Shader Texture Unit Assignment Table — no unit collisions; (4) vehicle sprite atlas linear upload exception (vehicles_sprite_atlas_d.dds: DXT5 format, loadLinear() path, NOT sRGB) is correctly documented here and in texture-cache.md, and is correctly implemented via exact-filename dispatch in TextureCache::loadSRGB() routing to loadLinear() — palette swatches are not photographic diffuse, sRGB decode is incorrect for this atlas; (5) DXT5 mandate for vehicle sprite atlas (not DXT1) is correctly specified and enforceable via validate_assets.py DDS FourCC check — current check #10 covers registry/UV cell assignments; the DDS format enforcement for vehicles_sprite_atlas_d.dds is a Phase 9 validate_assets.py deliverable per the registry integration scope noted in this file; the spec mandate is clear and the enforcement path is defined -->
