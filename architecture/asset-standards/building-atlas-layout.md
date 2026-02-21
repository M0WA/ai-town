# Building Atlas Layout

**Status**: DRAFT — schema and atlas structure are established. Placeholder cells in the Cell Assignment Table are reserved by design for future building types and will be finalised during Phase 9. Full sign-off by `graphics-artist-2d-texture` and `graphics-dev-irrlicht` remains a Phase 5 exit criterion before UV authoring begins.

## City Building Atlas (2048×2048)

- Format: DDS DXT1 sRGB (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`); use DXT5 sRGB (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`) if any atlas cell requires alpha
- Upload path: raw-GL sRGB path (`glGenTextures` → `glBindTexture` → `glCompressedTexImage2D` with sRGB internal format) — diffuse color data requires sRGB decode. Do NOT use `IVideoDriver::getTexture()` for the building atlas.
- Resolution: 2048×2048 pixels
- Cell grid: 4×4 cells at 512×512 px each (16 cells total for V1 module variants)
- Mip chain: 4-level mandatory (`GL_TEXTURE_MAX_LEVEL = 3`; 2048→1024→512→256)
- Per-cell usable area: 496×496 px (8 px border on each edge, per 2d-texture-standards.md)
- All building LOD0/LOD1 UV channel 0 maps into this atlas

### Cell Assignment Table

| Cell Row | Cell Col | Module Type | Zone | Tier | Notes |
|---|---|---|---|---|---|
| 0 | 0 | PLACEHOLDER — wall_residential_low | Residential | Low density | Assign in Phase 9 |
| 0 | 1 | PLACEHOLDER — wall_commercial_low | Commercial | Low density | Assign in Phase 9 |
| 0 | 2 | PLACEHOLDER — wall_industrial_low | Industrial | Low density | Assign in Phase 9 |
| 0 | 3 | PLACEHOLDER — base_residential_low | Residential / Commercial / Industrial | Low density | Assign in Phase 9. Shared with Commercial and Industrial — see binding decision below. |
| 1 | 0 | PLACEHOLDER — wall_residential_med | Residential | Med density | Assign in Phase 9 |
| 1 | 1 | PLACEHOLDER — wall_commercial_med | Commercial | Med density | Assign in Phase 9 |
| 1 | 2 | PLACEHOLDER — wall_industrial_med | Industrial | Med density | Assign in Phase 9 |
| 1 | 3 | PLACEHOLDER — base_residential_med | Residential / Commercial / Industrial | Med density | Assign in Phase 9. Shared with Commercial and Industrial — see binding decision below. |
| 2 | 0 | PLACEHOLDER — wall_residential_high | Residential | High density | Assign in Phase 9 |
| 2 | 1 | PLACEHOLDER — wall_commercial_high | Commercial | High density | Assign in Phase 9 |
| 2 | 2 | PLACEHOLDER — wall_industrial_high | Industrial | High density | Assign in Phase 9 |
| 2 | 3 | PLACEHOLDER — roof_shared | All zones | All tiers | Assign in Phase 9 |
| 3 | 0 | PLACEHOLDER — facade_detail_balcony | All zones | Med/High | Assign in Phase 9 |
| 3 | 1 | PLACEHOLDER — facade_detail_pilaster | All zones | Med/High | Assign in Phase 9 |
| 3 | 2 | PLACEHOLDER — reserved | — | — | Reserved for Phase 9+ |
| 3 | 3 | PLACEHOLDER — reserved | — | — | Reserved for Phase 9+ |

**Note**: Cell assignments are PLACEHOLDERS. The exact per-module-variant assignments must be determined during Phase 9 based on the minimum V1 building set (minimum 2 variants per zone-tier combination). Update this table before Phase 9 UV authoring begins.

**IMPORTANT — Variant sharing of wall cells (binding decision)**: Building variants within the same zone-tier combination share the same wall module atlas cells — only distinct module types (wall, base, roof, facade detail) require separate cells. For example, `res_low_01` and `res_low_02` are two variants of Low-density Residential; both reference the same `wall_residential_low` atlas cell with different mesh geometry configurations. Unique cells are NOT required per variant, only per module type. This keeps the 4×4 (16-cell) atlas within capacity for all V1 building module types. This decision is binding and confirmed here before UV authoring begins. `graphics-artist-2d-texture` and `graphics-dev-irrlicht` must both sign off that all V1 variant UVs map into the correct shared module-type cell before Phase 9 UV authoring begins.

**BINDING DECISION — Shared base module and roof cells across all zone types**: The single `roof_shared` cell (row 2, col 3) is intentional — all V1 zone types and density tiers share a common rooftop texture. Similarly, Commercial and Industrial buildings share the residential base module cells (`base_residential_low` at row 0, col 3 and `base_residential_med` at row 1, col 3) for ground-floor geometry. Any zone-specific ground-floor character is encoded via facade detail pieces (row 3, cols 0–1), not separate base module cells. This decision keeps the 4×4 (16-cell) atlas within capacity for all V1 module types. The two reserved cells (row 3, col 2 and row 3, col 3) remain available for Phase 9+ use precisely because no additional base or roof cells are required in V1. **This is a binding decision — UV authoring for Phase 9 must not require additional base or roof cells.**

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

**Vehicle Sprite Atlas** (`vehicles_sprite_atlas_d.dds`):

- Format: DDS DXT5 (BC3). **The vehicle sprite atlas (`vehicles_sprite_atlas_d.dds`) MUST use DXT5 (BC3) compression — NOT DXT1 (BC1). DXT1 has no per-pixel alpha channel and cannot represent the roof-silhouette alpha required for transparent vehicle tops. DXT5 stores 4-bit alpha per pixel. This mandate applies to all LOD levels of the vehicle atlas. Using DXT1 for the vehicle atlas is a HARD ERROR — the validate-assets CI step (Phase 5) will reject any vehicle atlas with FourCC `DXT1`.** DXT1 only supports 1-bit alpha, which is insufficient for smooth vehicle silhouettes; DXT5 (BC3) is the only valid compression format for this atlas.
- Resolution: 256×256 px
- Cell grid: 16×16 cells at 16×16 px each (256 sprite slots)
- Purpose: Point/sprite LOD2 representation at distances beyond 100 m — each sprite is a 16×16 px roof-color/type identifier rendered as a camera-facing billboard quad (1 m × 0.5 m)
- Mip chain: none (`GL_TEXTURE_MAX_LEVEL = 0`) — sprites are rendered at near-fixed screen size; mip filtering would blur 16×16 px cells into unrecognisable blobs
- Upload path: linear pool (`IVideoDriver::getTexture()`) — sprite atlas encodes stylised roof color swatches, not photographic diffuse data; sRGB path is not required

These two atlases are **not interchangeable**. The diffuse atlas feeds the mesh material pipeline (UV channel 0 of LOD0/LOD1 vehicle meshes); the sprite atlas feeds the point-sprite LOD2 renderer. Do not conflate their formats, resolutions, or upload paths.

**Vehicle Normal Atlas** (`vehicles_normal_atlas_n.dds`):

- Format: DDS DXT5/BC3 (linear — normal map data must not be sRGB-decoded)
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
      "vehicle_type": "car_sedan",
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

Before Phase 9 UV authoring begins, all three reviewers must confirm:

- [ ] All V1 module variant types have a cell assignment (no unbounded "assign in Phase 9" placeholders remaining)
- [ ] Road marking atlas cell assignments cover all V1 decal types
- [ ] Vehicle atlas stub entries match the V1 vehicle type list
- [ ] Cell UV borders (8 px) respected in all assignments
- [ ] Document reviewed and approved by `graphics-artist-2d-texture`
- [ ] Document reviewed and approved by `graphics-dev-irrlicht`
- [ ] Document reviewed and approved by `graphics-artist-3d-model`: (a) the shared atlas cell variant approach (multiple mesh variants referencing one module-type cell) is compatible with modular kit UV authoring workflows; (b) per-module UV islands can be fully authored within the 496×496 px usable area per 512×512 cell without requiring bleed into the 8 px border; (c) the 4×4 cell grid and 16-cell capacity correctly covers the V1 minimum building module set, including all zone-tier wall, base, roof, and facade-detail types across Residential, Commercial, and Industrial zones

**Phase 1 dated sign-off record — `graphics-artist-3d-model` atlas compatibility** (required before Phase 1 exit):

> Phase 1 sign-off — 2026-02-21: The three shared atlas cell variant compatibility checks for `graphics-artist-3d-model` are confirmed as follows: (a) the shared atlas cell variant approach — multiple mesh variants of the same zone-tier type referencing a single module-type cell — is compatible with modular kit UV authoring workflows in Blender; per-variant UV islands can be placed within the same atlas cell without conflicting UV shells by isolating each variant's islands to a distinct sub-region of the cell; (b) per-module UV islands can be fully authored within the 496×496 px usable area of each 512×512 cell without requiring bleed into the 8 px border; (c) the 4×4 cell grid with 16 cells correctly covers the V1 minimum building module set, including all zone-tier wall, base, roof, and facade-detail types across Residential, Commercial, and Industrial zones without exceeding atlas capacity. This record confirms the Phase 1 exit criterion for the building atlas `graphics-artist-3d-model` sign-off gate is satisfied. Signed: graphics-artist-3d-model.

- [ ] **Lightmap atlas (`_lm` suffix) upload path** — Phase 2 exit criterion: the `TextureCache` Phase 2 skeleton must include a stub comment inside `loadLinear()` explicitly stating that all textures with the `_lm` suffix (e.g., `buildings_atlas_lm.dds`, `vehicles_diffuse_atlas_lm.dds`) MUST be loaded via `loadLinear()` and must NOT be routed through `loadSRGB()`. The stub comment must cross-reference the `GL_TEXTURE_MAX_LEVEL` dispatch table in `architecture/graphics-architecture/texture-cache.md` (the `_lm` row) and note that the lightmap upload path must be confirmed as a passing check before Phase 5 lightmapping work begins. Lightmap data encodes pre-baked irradiance, which is already in linear light units and must not undergo sRGB gamma expansion; using `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` for a lightmap atlas would incorrectly gamma-expand the stored radiance values and produce blown-out, physically incorrect lighting. The presence of this stub comment in the Phase 2 `TextureCache` skeleton is the Phase 2 deliverable that replaces the former "N/A" status. This item must be verified as present and correct in the Phase 2 review before Phase 5 lightmapping work begins.
