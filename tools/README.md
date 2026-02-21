# AI Town Tools — Asset Pipeline Documentation

This file documents the asset pipeline constraints for 2D textures and 3D models. It is reviewed and approved by `graphics-artist-2d-texture` AND `graphics-artist-3d-model` before Phase 0 exit — both must approve before Phase 0 is declared complete.

Reference: `architecture/asset-standards/2d-texture-standards.md`, `architecture/asset-standards/3d-model-standards.md`

---

## 2D Texture Pipeline Constraints

### Upload Categories and Formats

| Category | Format | Upload Method | Notes |
|---|---|---|---|
| Diffuse sRGB opaque | DXT1/BC1 | Raw-GL sRGB: `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | — |
| Diffuse sRGB with alpha | DXT5/BC3 | Raw-GL sRGB: `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` | — |
| Normal maps | DXT5nm (BC3) | Linear pool: `IVideoDriver::getTexture()` — DO NOT use `glCompressedTexImage2D` for normal maps (bypasses TextureCache linear pool tracking, causes texture leaks on eviction) | DDS export pipeline: (1) Y-convention correction, (2) DXT5nm swizzle, (3) compress |
| Specular/roughness | BC1 or BC3 | Linear pool: `IVideoDriver::getTexture()` | — |
| Splat maps | RGBA8 UNORM uncompressed | `glTexImage2D(GL_RGBA8)` — NEVER `glCompressedTexImage2D` | Using `glCompressedTexImage2D` on RGBA8 buffer triggers `GL_INVALID_OPERATION` |
| UI sprite sheet | RGBA8 UNORM | `glTexImage2D(GL_RGBA8)` | No mip chain |
| Billboard atlas | DXT5 sRGB | Raw-GL sRGB: `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` | 1024x128, 1x8 strip, 4-level mip, straight alpha, `GL_CLAMP_TO_EDGE` on both S and T axes |
| Road asphalt tileable | DXT5/BC3 | Raw-GL sRGB: `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` | Raw GLuint — must use custom GLSL material; `SMaterial::setTexture()` CANNOT be used |
| Road markings atlas | DXT5/BC3 | Linear pool: `IVideoDriver::getTexture()` | Encodes decal mask (NOT diffuse); sRGB path causes incorrect gamma; anisotropy disabled |

### DDS Export Pipeline for Normal Maps (must run in order)

1. **Y-convention correction**: If sourcing from a DirectX-convention baker (e.g. Substance Painter with DirectX normals enabled), invert the green channel. Skip if baker is already set to OpenGL convention. Normal maps must use OpenGL convention (Y-up: green channel points toward +Y in tangent space). Validate with a sphere test: a light from above-left must produce a highlight on the upper-left of bumps.
2. **DXT5nm swizzle**: Copy X (red channel) into A (alpha channel), keep Y in green channel, discard Z (blue). Export the swizzled result as an intermediate PNG (`input_swizzled.png`) before the next step. The shader samples `.a` for X and `.g` for Y.
3. **Compress**: Compress the Step 2 swizzled PNG to BC3/DXT5 DDS with 4 mip levels: `nvcompress -normal -bc3 -mips 4 input_swizzled.png output_n.dds` (or `compressonatorcli -fd BC3 -miplevels 4 input_swizzled.png output_n.dds`).

**Warning**: Skipping Step 1 before Step 2 produces silently inverted normals on every normal-mapped surface. Compressing the un-swizzled PNG (skipping Step 2's intermediate export) produces silently wrong normals. All three steps must run in order on every normal map asset.

### Billboard Atlas Requirements

- **Format**: 1024x128 DDS DXT5 sRGB (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), 1x8 horizontal strip (8 frames of 128x128 px each)
- **Alpha**: Straight (non-premultiplied) — premultiplied alpha causes color fringing at mip level boundaries
- **Wrap mode**: `GL_CLAMP_TO_EDGE` on both S and T axes. Default `GL_REPEAT` causes ghost frames from the opposite end of the 1x8 strip when UV samples near the horizontal edge
- **Mip chain**: 4 levels mandatory (`GL_TEXTURE_MAX_LEVEL = 3`)
- **Bake angle**: 8-direction bakes at 45 degrees below horizontal (camera pitch = -45 degrees, midpoint of [-70, -20] camera pitch range)
- **Usable content area**: 112x112 px per frame (8 px border on each of the 4 edges of each 128x128 frame). All building silhouette, windows, and geometry must fit within this 112x112 zone. Art bleeding into the 8 px border causes mip-level bleed artifacts at far distances.
- **Mip-3 legibility check MANDATORY**: At mip level 3, each frame is 16x16 px (14x14 px usable after border scaling). Artists must verify the building silhouette remains recognizable at 14x14 px in a DCC tool before approving the billboard atlas asset.

### Mip Chain Rules by Category

| Category | Mip levels | Notes |
|---|---|---|
| Terrain base textures (2048x2048 DXT1/BC1 sRGB) | 4-level cap (`GL_TEXTURE_MAX_LEVEL = 3`) | DXT5 must NOT be used for terrain base textures — opaque, no alpha channel needed; DXT5 wastes VRAM |
| Normal maps (DXT5nm) | Minimum 4 mip levels | Without mipmaps, normal map sampling at distance produces specular aliasing |
| Specular/roughness (BC1/BC3) | Minimum 4 mip levels | — |
| Building atlas (2048x2048) | 4-level cap (`GL_TEXTURE_MAX_LEVEL = 3`) | Default DXT1 sRGB; switch to DXT5 sRGB if any atlas cell requires alpha transparency (glass windows, facade cutouts) |
| Billboard atlas (1024x128 DXT5 sRGB) | 4-level cap (`GL_TEXTURE_MAX_LEVEL = 3`) | — |
| UI sprite sheet (RGBA8) | `GL_TEXTURE_MAX_LEVEL = 0` (no mips) | UI is pixel-exact; mips cause blur on crisp elements |
| Splat maps (RGBA8) | `GL_TEXTURE_MAX_LEVEL = 0` (no mips) | — |
| Lightmaps (`_lm` suffix) | `GL_TEXTURE_MAX_LEVEL = 0` (exempt) | Lightmap data is baked to match specific geometry UVs; generating mips would blend baked lighting values across texels |
| Road textures | 4-level mip chain (capped at `GL_TEXTURE_MAX_LEVEL = 3`) | 1024 -> 512 -> 256 -> 128 |

### Anisotropy Summary

| Category | Anisotropy |
|---|---|
| Terrain base textures | Minimum 8x (`GL_TEXTURE_MAX_ANISOTROPY_EXT = 8.0f`) |
| Building facade atlas | Minimum 8x |
| Road asphalt tileable | Minimum 4x |
| Road markings atlas | Disabled (1.0f) — decal masks do not benefit from anisotropic filtering |
| Normal maps (`_n` suffix) | Minimum 4x (`GL_TEXTURE_MAX_ANISOTROPY_EXT = 4.0f`) |
| Specular/roughness maps (`_s`, `_sp` suffixes) | Minimum 4x |
| All other categories | Use GPU-default or material setting |

### Resolution Matrix

| Asset category | Resolution |
|---|---|
| Terrain base textures | 2048x2048 |
| Building facade atlas cell | 512x512 per module face; up to 4 module faces packed per 1024x1024 city building atlas cell |
| Roads (tileable) | 1024x1024 |
| Props / street furniture | 256x256 or 512x512 |
| UI sprite sheet | 2048x2048 (atlas) |
| Lightmap (_lm) — small/medium buildings | 512x512 |
| Lightmap (_lm) — large buildings | 1024x1024 |
| Lightmap (_lm) — LOD2 shell | 256x256 |
| Specular/roughness (_s, _sp) — building facades | 512x512 (matches facade atlas cell) |
| Specular/roughness (_s, _sp) — terrain | 1024x1024 |
| Specular/roughness (_s, _sp) — props | 256x256 |
| Normal maps (_n) — all categories | Same resolution as specular/roughness for that category |
| Vehicle diffuse atlas | 512x512 per vehicle type, packed into 2048x2048 DDS DXT1 atlas (16 vehicle types per sheet) |
| Vehicle normal map atlas | 256x256 per vehicle type, packed into 2048x2048 DDS DXT5nm atlas (`vehicles_normal_atlas_n.dds`) |

All textures must be power-of-two dimensions (256, 512, 1024, 2048). Non-POT textures will fail validation.

### Building Atlas Usable Content Area

The city building atlas is a **2048x2048** texture divided into a 4x4 grid of **512x512 px** cells (16 cells total). Each cell has an 8 px border on each of the 4 edges. The **usable content area per cell is 496x496 px** (512 - 8 - 8 = 496 px per axis). All facade art — window detail, surface materials, trim lines — must be authored within this 496x496 usable zone. Content bleeding into the 8 px border zone will exhibit mip-level bleed from adjacent cells at distant views.

**Variant sharing (binding decision)**: Building variants within the same zone-tier combination share wall module atlas cells. For example, `res_low_01` and `res_low_02` both reference `wall_residential_low` at cell (0,0). Do NOT author a new atlas cell for a variant unless it introduces a genuinely new module type. See `architecture/asset-standards/building-atlas-layout.md` for the full cell assignment table.

A 4096x4096 atlas may be used only if a runtime `GL_MAX_TEXTURE_SIZE` check at device creation confirms support >= 4096; otherwise the pipeline falls back to 2048x2048. Do not hard-code atlas dimensions.

### Road Marking Atlas Usable Content Area

The road marking atlas is a **1024x1024** texture divided into a 4x4 grid of **256x256 px** cells (16 cells total). Each cell has an 8 px border on each of the 4 edges. The **usable content area per cell is 240x240 px** (256 - 8 - 8 = 240 px per axis). The atlas is near-capacity for V1 (12-16 unique decal types). Do not add new decal types beyond the 16-cell capacity; a second 2048x2048 atlas is the post-V1 expansion path.

### Terrain Texturing and Splat Maps

Terrain uses a multi-layer blend system. A splat map encodes four material blend weights in RGBA channels.

**Splat map format**: RGBA8 UNORM uncompressed — never DXT/BC compressed. DXT compression corrupts the smooth 0–255 blend weight gradients. Upload via `glTexImage2D` with `GL_RGBA8` internal format. No mip chain (`GL_TEXTURE_MAX_LEVEL = 0`). See the Upload Categories table above for the full upload path.

**UV tiling frequency**: Terrain base textures tile at **4x4 repeats per 64x64 m LOD0 chunk** (16 px/m effective density at 2048 px resolution). This value is fixed — inconsistent tiling produces visible density discontinuities at chunk borders.

**Splat map channel-to-material assignment (fixed — must not change after texture production begins):**

| Channel | Terrain material | Texture unit | Constant |
|---|---|---|---|
| R (red) | Grass | 5 | `kTexUnitTerrainLayer0` |
| G (green) | Asphalt | 6 | `kTexUnitTerrainLayer1` |
| B (blue) | Soil | 7 | `kTexUnitTerrainLayer2` |
| A (alpha) | Concrete | 8 | `kTexUnitTerrainLayer3` |

**Authoring rule**: Initialize the R channel (grass) to 255 across the entire splat map before painting begins. This ensures the blend-weight normalization divisor is never zero on unpainted tiles.

**Terrain normal map intensity by surface type:**
- Hard surfaces (concrete, brick, asphalt, stone): Z-scale 1.0–1.5 in the DCC baker. High-frequency sharp detail is appropriate — pavement cracks, mortar lines, stone facets.
- Soft surfaces (grass, soil, dirt, sand): Z-scale 0.3–0.7. Over-strong normals on soft terrain produce a plastic appearance. Subtle micro-undulation is the correct target.

Validate with a sphere lit from a single directional light: hard-surface normals produce sharp specular highlights; soft-surface normals produce low-contrast diffuse shading with no sharp specular peaks.

Coordinate layer count with `graphics-dev-irrlicht` before authoring terrain materials. The shader architecture supports exactly 4 terrain detail layers (V1) via texture units 5–8.

### Shader Texture Unit Assignment

All shaders use the following fixed texture unit assignments, defined in `src/rendering/shader_constants.h`. These are the binding contract between `TextureCache` and GLSL shaders.

| Texture unit | Constant | Usage |
|---|---|---|
| 0 | `kTexUnitDiffuse` | Diffuse/albedo (sRGB DXT1/DXT5 raw-GL path) |
| 1 | `kTexUnitNormal` | Normal map (DXT5nm, linear-pool `ITexture*`) |
| 2 | `kTexUnitSpecular` | Specular/roughness (BC1 or BC3 packed, linear-pool) |
| 3 | `kTexUnitLightmap` | Lightmap bake (DXT5/BC3, linear-pool) |
| 4 | `kTexUnitSplatMap` | Terrain splat/blend map (RGBA8 uncompressed, splat-map pool) |
| 5 | `kTexUnitTerrainLayer0` | Terrain detail layer 0 — Grass (DXT1 sRGB, raw-GL) |
| 6 | `kTexUnitTerrainLayer1` | Terrain detail layer 1 — Asphalt (DXT1 sRGB, raw-GL) |
| 7 | `kTexUnitTerrainLayer2` | Terrain detail layer 2 — Soil (DXT1 sRGB, raw-GL) |
| 8 | `kTexUnitTerrainLayer3` | Terrain detail layer 3 — Concrete (DXT1 sRGB, raw-GL) |
| 9 | `kTexUnitBillboard` | Billboard imposter atlas (DXT5 sRGB, raw-GL) |

Units 5–8 map 1-to-1 with splat-map channels R, G, B, A respectively. The splat map (unit 4) encodes 4 blend weights, one per layer. Combining two layers into one unit is incorrect and breaks the terrain blend shader.

### sRGB Color Space and Extension Requirement

Diffuse textures must be authored in sRGB space (standard Photoshop/Substance Painter workflow — no manual gamma adjustment needed). The engine decodes sRGB at load time via `GL_EXT_texture_sRGB` extension. Extension presence is checked once at `RenderSystem::init()` after `createDevice()`.

If `GL_EXT_texture_sRGB` is unavailable (rare on modern desktop GPUs), the engine falls back to shader-side gamma correction: `pow(color.rgb, vec3(2.2))` at the start of the terrain and building fragment shaders. Artists do not need to change their workflow for this fallback — the authoring target remains sRGB space in both paths.

Non-diffuse textures (normal maps, roughness, specular, splat maps) are always authored in **linear space**.

### DDS Export Command Reference

Use one of the two approved CLI tools per workstation. Do not use Photoshop DDS plug-ins or GIMP DDS plug-ins as sole export path — they do not support the sRGB internal-format flags required by this pipeline.

- **NVTT** (`nvcompress`) — preferred on Linux
- **Compressonator CLI** (`compressonatorcli`) — preferred on Windows

| Category | Tool | Command |
|---|---|---|
| Diffuse sRGB opaque (DXT1/BC1, 4-mip) | nvcompress | `nvcompress -color -bc1 -mips 4 input.png output_d.dds` |
| Diffuse sRGB opaque (DXT1/BC1, 4-mip) | Compressonator | `compressonatorcli -fd BC1 -miplevels 4 input.png output_d.dds` |
| Diffuse sRGB with alpha (DXT5/BC3, 4-mip) | nvcompress | `nvcompress -color -bc3 -mips 4 input.png output_d.dds` |
| Diffuse sRGB with alpha (DXT5/BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input.png output_d.dds` |
| Normal map DXT5nm (BC3, 4-mip) | nvcompress | `nvcompress -normal -bc3 -mips 4 input_swizzled.png output_n.dds` |
| Normal map DXT5nm (BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input_swizzled.png output_n.dds` |
| UI sprite sheet (RGBA8, no mip) | nvcompress | `nvcompress -rgb -nomips input.png output.dds` |
| UI sprite sheet (RGBA8, no mip) | Compressonator | `compressonatorcli -fd ARGB_8888 -nomipmap input.png output.dds` |

From Phase 2 onward, use `tools/export_textures.py` as the canonical entry point. Direct CLI invocation remains valid for individual asset iteration. CI must call only `export_textures.py`.

**Valid DXT formats**: `DXT1` and `DXT5` only. `DXT3`/BC2 must never be used — it will be flagged as an invalid format error by `validate_assets.py`.

### Vehicle Atlases

Two distinct vehicle atlases exist with separate purposes. They are not interchangeable.

**Vehicle Diffuse Atlas** (`vehicles_diffuse_atlas_d.dds`):
- Format: DDS DXT1 sRGB
- Resolution: 2048x2048 px — 4x4 grid of 512x512 px cells (16 vehicle type slots)
- Purpose: Diffuse/albedo color data for LOD0 and LOD1 vehicle meshes
- Mip chain: 4-level mandatory (`GL_TEXTURE_MAX_LEVEL = 3`)
- Upload path: raw-GL sRGB (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`)

**Vehicle Sprite Atlas** (`vehicles_sprite_atlas_d.dds`):
- Format: DDS DXT5 (DXT5 required — alpha channel encodes silhouette mask for non-rectangular vehicle roof shapes; DXT1's 1-bit alpha is insufficient)
- Resolution: 256x256 px — 16x16 grid of 16x16 px cells (256 sprite slots)
- Purpose: Point/sprite LOD2 representation at distances beyond 100 m; each sprite is a 16x16 px roof-color/type identifier rendered as a camera-facing billboard quad (1 m x 0.5 m)
- Mip chain: none (`GL_TEXTURE_MAX_LEVEL = 0`) — mip filtering would blur 16x16 px cells into unrecognisable blobs
- Upload path: linear pool (`IVideoDriver::getTexture()`) — sprite atlas encodes stylised roof color swatches, not photographic diffuse data

### Scene VRAM Budget (V1 targets)

All simultaneously-resident textures must not exceed **1.0 GB** total VRAM on a mid-range desktop GPU with 4 GB VRAM. Target is well under the ceiling at approximately 170 MB. Artist format and resolution choices directly impact this budget.

| Category | Budget |
|---|---|
| Terrain base textures (4 layers, 2048x2048 DXT1, 4-level mip) | <= 11 MB |
| Splat maps (up to 2 x 1024x1024 RGBA8, no mip) | <= 8 MB |
| City building atlas (2048x2048 DXT1, 4-level mip) | <= 6 MB |
| Per-asset lightmaps (50 active buildings x 1024x1024 DXT5, no mip) | <= 50 MB |
| LOD2 shell lightmaps (50 active buildings x 256x256 DXT5, no mip) | <= 4 MB |
| Road atlas (1024x1024 DXT5, 4-level mip) | <= 1.5 MB |
| UI sprite sheet (2048x2048 RGBA8, no mips) | <= 16 MB |
| Vehicle diffuse atlas (2048x2048 DXT1, 4-level mip) | <= 6 MB |
| Vehicle sprite atlas (256x256 DXT5, no mip) | <= 1 MB |
| Billboard imposter atlases (<= 50 types x 1024x128 DXT5, 4-level mip) | <= 9 MB |
| Miscellaneous (normal maps, roughness, props) | <= 48 MB |
| **Total** | **<= 170 MB** |

Exceeding these per-category budgets requires explicit sign-off from `graphics-dev-irrlicht`.

### Naming Convention

All DDS files must end with one of these six suffixes (enforced by `validate_assets.py`):

| Suffix | Usage |
|---|---|
| `_d` | Diffuse texture |
| `_n` | Normal map |
| `_s` | Specular map |
| `_sp` | Specular/PBR roughness map |
| `_lm` | Lightmap |
| `_billboard` | Billboard imposter atlas |

**Road texture exception**: `road_asphalt_tileable.dds` and `road_markings_atlas.dds` in `assets/textures/roads/` are exempt from the six-suffix check — they are matched by canonical full filename in `validate_assets.py`. These are the only DDS files in the pipeline with no standard suffix.

**Billboard placement**: Billboard atlas DDS files (`_billboard.dds`) are placed in `assets/textures/billboards/`. No DDS files should exist under `assets/models/` — any DDS file found there is a misplaced asset validation error.

---

## 3D Model Pipeline Constraints

### Format Requirements

- **`.b3d`** is mandatory for all building and vehicle assets (UV2/lightmap channel support)
- **`.obj`** is permitted only for simple props with no lightmap UV (marked `"lightmap_uv_channel": null` in sidecar)

### Blender Export Axis

**Use `-Z Forward, Y Up`** (NOT "Y Forward, Z Up").

- Irrlicht uses a **left-handed** coordinate system (+X right, +Y up, **+Z forward into screen**)
- This is opposite handedness from Blender's right-handed default
- Using "Y Forward, Z Up" produces Z-up assets that appear rotated 90 degrees in-engine
- Verify: the exported asset's front face must align with +Z in Irrlicht's scene view after import

### Unit Scale

1 Irrlicht unit = 1 meter. All geometry must be authored at real-world scale.

### Asset Directory Layout

| Asset type | Directory |
|---|---|
| Building `.b3d` meshes and `.meta` sidecars | `assets/models/buildings/` |
| Vehicle `.b3d` meshes and `.meta` sidecars | `assets/models/vehicles/` |
| Prop `.obj` / `.b3d` meshes and `.meta` sidecars | `assets/models/props/` |
| Collision meshes (`_col.obj` etc.) | Same directory as their parent asset |
| Billboard atlas DDS (`_billboard.dds`) | `assets/textures/billboards/` |

No DDS files should exist under `assets/models/` — any DDS file found there is a misplaced asset and will fail validation.

### LOD Polygon Budgets

| Asset category | LOD0 (near) | LOD1 (mid) | LOD2 (far) |
|---|---|---|---|
| Large buildings | 2000–5000 tris | 500–1000 tris | 300–500 tris (geometry shell) |
| Small buildings / props | 500–1500 tris | 100–300 tris | Billboard (no `_lod2.b3d`) |
| Vehicles | 1000–3000 tris | 200–500 tris | Point/sprite |
| Terrain chunk (64x64 m) | 32x32 quad grid | 16x16 quad grid | 8x8 quad grid |
| Road tile (4x4 m) | ≤48 tris | ≤16 tris | ≤8 tris (flat quad) |
| Infrastructure props (lamp posts, signs) | ≤300 tris | ≤75 tris | Billboard |

The LOD2 geometry shell for large buildings must be a single hand-authored baked mesh within the 300–500 tri budget (not assembled from modules). The 300–500 tri range is required to represent building silhouettes at the 185–200 m switch-in distance where tall buildings occupy 50–80 vertical pixels.

**Vehicle per-class polygon caps (binding, within the general range above):**

| Vehicle class | LOD0 budget | LOD1 budget |
|---|---|---|
| Car (sedan, hatchback, SUV) | ≤1,500 tris | ≤300 tris |
| Bus | ≤2,500 tris | ≤450 tris |
| Truck | ≤2,500 tris | ≤450 tris |

All vehicle assets must be exported as a **single solid mesh** (body + windows + wheels unified into one IMesh). Modular sub-mesh assembly is not used for vehicles.

**Modular building per-module polygon caps:**

| Module | LOD0 max triangles | LOD1 max triangles |
|---|---|---|
| Wall tile (mid-floor) | ≤300 | ≤75 |
| Base module (ground floor) | ≤400 | ≤100 |
| Roof module | ≤500 | ≤125 |
| Facade detail (snap-on piece) | ≤100 per piece | ≤25 per piece |

Maximum 10 facade detail pieces per assembled building stack (balconies, pilasters, window bays, etc. across all sides). Assembled building totals must not exceed: LOD0 5,000 tris / LOD1 1,000 tris (large building), or LOD0 1,500 tris / LOD1 300 tris (small building). The export validation script measures a representative N-floor assembled stack at each density tier and rejects combinations that exceed these limits.

### Modular Building Kit

- Module grid: 4 m x 4 m x 3 m per floor unit
- **Hard floor cap**: 10 floors maximum for large buildings (30 m total height). At 10 floors the assembled LOD0 maximum approaches the 5,000 tri budget. 11+ floors risk budget overrun. Enforced by `validate_assets.py` using the `height_floors` field in `.meta`.
- **Pivot convention (hard export requirement)**: Pivot at bottom-center of footprint at Y=0. For a standard 4x4x3 m unit: geometry in X: -2 to +2, Y: 0 to +3, Z: -2 to +2 in local space. Do NOT offset the pivot above or below Y=0.
- **Y bounds per floor module**: Geometry must not exceed Y=3.0 (hard upper bound per floor). Wall tiles with decorative tops (parapets, cornices) must stay within the 3 m budget. Maximum tolerated vertex deviation from Y=0 (bottom) or Y=3.0 (top): **0.005 Irrlicht units (5 mm)**. Export validation script rejects files that violate this tolerance.
- **LOD2 pivot conformance**: The LOD2 baked shell pivot must match the full assembled building's bottom-center at Y=0. No visible position pop should occur during the LOD1-to-LOD2 transition in-engine. Sign-off check: "Stack two identical floor modules in Irrlicht scene view; confirm no visible gap at join."

### LOD Distance Thresholds and Hysteresis

**Hysteresis bands are mandatory** to prevent LOD thrashing (mesh rebind stutter) when the camera sits near a threshold. Never use bare threshold comparisons. Minimum band: ≥5 m for close thresholds (LOD0/LOD1), ≥10 m for far thresholds (LOD1/LOD2).

| Asset category | LOD0 -> LOD1 switch-out | LOD0 -> LOD1 switch-in | LOD1 -> LOD2 switch-out | LOD1 -> LOD2 switch-in |
|---|---|---|---|---|
| Large buildings | > 50 m | < 45 m | > 200 m | < 185 m |
| Small buildings / props | > 30 m | < 25 m | > 100 m | < 90 m |
| Vehicles | > 40 m | < 35 m | > 100 m | < 90 m |
| Terrain chunk | > 100 m | < 92 m | > 300 m | < 285 m |

Switch-out distances in `.meta` `lod_distances` must satisfy: `lod_distances[1] - lod_distances[0] >= 5` and `lod_distances[2] - lod_distances[1] >= 10` (validated by check #9). Switch-in distances are computed at runtime as `switch_in = switch_out - hysteresis_gap` and are not stored in `.meta`.

### UV Channel Requirements

| Asset type | UV channel 0 | UV channel 1 |
|---|---|---|
| Building `.b3d` (LOD0, LOD1) | Diffuse atlas UV — maps into assigned cell in 2048x2048 building atlas | Lightmap UV — non-overlapping islands; required on all building `.b3d` files; validated by check #5 |
| Large building LOD2 geometry shell | Diffuse atlas UV — same atlas cell(s) as LOD0/LOD1 | Lightmap UV — required; baked to `<asset_name>_lod2_lm.dds` (256x256 DXT5/BC3) |
| Vehicle `.b3d` (LOD0, LOD1) | Diffuse atlas UV — maps into assigned cell in vehicle diffuse atlas | Not required; ignored by runtime if present |
| Prop `.obj` (NOLIGHTMAP) | Diffuse UV | Not present (`.obj` has no multi-UV support) |

**UV channel 0 authoring rules:**
- Building and vehicle UVs must stay within [0, 1] UV space and within the asset's assigned atlas cell (checks #4 and #10)
- Apply the OpenGL V-flip before mapping to atlas cells: `V_opengl = 1 - V_blender`. Blender's UV editor shows V=0 at the top; OpenGL uses V=0 at the bottom-left. Failing to flip means all atlas cell assignments will be mirrored vertically

**UV channel 1 (lightmap UV) authoring rules:**
- Islands must be non-overlapping across the entire mesh
- All islands must lie within [0, 1] UV space
- Lightmap baking for LOD2 shells uses flat ambient-only lighting (same as billboard bakes), for consistency at similar viewing distances
- **LOD2 shell lightmap file**: `<asset_name>_lod2_lm.dds`, 256x256 px, DXT5/BC3 (NOT DXT1 — DXT5 is required to preserve the alpha channel for ambient occlusion data). Upload with `GL_TEXTURE_MAX_LEVEL = 0` (no mip chain; lightmaps are exempt from mip requirements per the Mip Chain Rules table above)
- The LOD2 shell diffuse (UV channel 0) and lightmap (UV channel 1) are blended at runtime using multiply blend mode at 100% opacity: `finalColor = diffuseColor * lightmapColor`. No directional lighting is applied to LOD2 shells in V1

### Density-Tier Naming Convention

`<zone>_<tier>_<variant>_lod<N>.b3d`

Where:
- `<zone>`: `res` (Residential), `com` (Commercial), `ind` (Industrial)
- `<tier>`: `low`, `med`, `high`
- `<variant>`: 2-digit integer (`01`, `02`, ...) for visual variety within a tier
- `<N>`: LOD level (0, 1, 2)

Examples: `res_low_01_lod0.b3d`, `res_low_01_lod1.b3d`, `com_med_03_lod2.b3d`

- Variant numbering starts at `01`
- LOD suffix `_lod0`, `_lod1`, `_lod2` is mandatory
- All three LOD files must share the same base name — missing any LOD level is a validation error

### LOD2: Billboard vs. Geometry Shell

- **Buildings with `height_floors <= 3`**: `_lod2.b3d` must be ABSENT. Use `_billboard.dds` instead. Submitting `_lod2.b3d` for a small building is a validation error.
- **Buildings with `height_floors > 3`**: `_lod2.b3d` geometry shell must be PRESENT. Submitting only a `_billboard.dds` without `_lod2.b3d` for a tall building is also a validation error.
- Billboard naming: `<asset_name>_billboard.dds` (e.g., `res_low_01_billboard.dds`). Billboard DDS files go to `assets/textures/billboards/`.
- **Runtime upgrade rule**: When a zone tile auto-upgrades to a higher density tier and the new building variant exceeds 3 floors, the C++ LODNode automatically switches from the billboard LOD2 path to the geometry-shell LOD2 path. The asset pipeline must provide `_lod2.b3d` for any building variant exceeding 3 floors.
- **Billboard floor count limit**: Billboard imposters are only valid for buildings with `height_floors <= 3` (≤9 m total height). Buildings taller than 3 floors exhibit unacceptable silhouette mismatch at 128x128 px frame resolution — the flat frame cannot reproduce rooftop setbacks, step changes, or rooftop details visible across the valid camera pitch range [-70°, -20°].

### .meta Sidecar Required Fields

Only four required fields (filename encodes `asset_name`, `zone_type`, and `tier`):

| Field | Type | Description |
|---|---|---|
| `category` | string | `"large_building"`, `"small_building"`, `"prop"`, or `"vehicle"` |
| `height_floors` | integer | Required for LOD billboard-vs-geometry-shell switching |
| `lod_distances` | array[3] | `[lod0_to_lod1_m, lod1_to_lod2_m, cull_distance_m]` — three values, NOT four. Switch-in distances are computed at runtime as `switch_in = switch_out - hysteresis_gap` |
| `atlas_cell` | object | `{ "row": R, "col": C }` — JSON object with named keys (NOT array `[R, C]`) |

Example for large building: `"lod_distances": [40.0, 80.0, 200.0]`
Example for small building/prop: `"lod_distances": [30.0, 100.0, 200.0]`

Validation gap requirements (check #9): `lod_distances[1] - lod_distances[0] >= 5` and `lod_distances[2] - lod_distances[1] >= 10`.

### Collision Mesh Naming

Collision meshes must be separate files, never embedded in `.b3d`. Author all collision footprints as **flat meshes at Y=0** (ground plane) with **side faces only — no top or bottom caps**. The C++ physics system extrudes collision vertically at runtime using `height_floors x 3 m` from `.meta`. Do NOT add top/bottom face caps — they are ignored and waste triangle budget. Do NOT offset vertices below Y=0.

| Type | Naming | Max triangles |
|---|---|---|
| Single convex footprint | `<asset_name>_col.obj` | 24 |
| Non-convex multi-piece | `<asset_name>_col_0.obj`, `_col_1.obj`, `_col_2.obj` | 24 per piece |
| Curved/circular | `<asset_name>_col_circle.obj` (N-sided prism, N <= 8) | 16 |
| Vehicle | `<asset_name>_col.obj` | 8 (NOT 24) |
| Terrain decoration | `<asset_name>_col.obj` | 12 |
| Very small prop (footprint < 4 m) | `<asset_name>_col.obj` | 2 |

Additional authoring rules:
- Non-convex buildings: maximum 3 convex sub-meshes. Do NOT silently create a `_col_3.obj` — the C++ loader will not pick it up and the discrepancy goes undetected
- Curved/circular footprints: N-sided polygon prism where N <= 8. At N=8, side faces only = 16 triangles (within the 24-tri standard budget). Do not use a rectangular hull for circular buildings — it incorrectly blocks road tiles adjacent to the building's curved face
- Vehicle collision mesh: single `_col.obj` used across all LOD levels (LOD0, LOD1, and the point/sprite LOD2). No LOD-specific collision meshes are authored for vehicles
- **Loader dispatch order**: The C++ loader checks suffixes in this order: (1) `_col_0.obj` exists → load multi-piece non-convex set; (2) `_col_circle.obj` exists → load circular N-sided prism; (3) `_col.obj` exists → load single convex hull; (4) none found → log error and skip collision registration

**The suffix `_collision.obj` is INCORRECT and will not be loaded by the C++ asset loader.** Use `_col.obj` / `_col_0.obj` / `_col_circle.obj`.

### NOLIGHTMAP Flag

Props explicitly exempt from UV2/lightmap requirements:
- Mark `"lightmap_uv_channel": null` in their `.meta` sidecar
- Must be exported as `.obj` (not `.b3d`)
- `validate_assets.py` checks: if `lightmap_uv_channel` is null, file must use `.obj` format

---

## Vehicle Atlas Authoring Rules

Reference: `tools/vehicle_atlas_registry.json`

- **Atlas grid**: 4x4 cells at 512x512 px per cell in a 2048x2048 atlas
- **UV formula**: For cell at (row R, col C): `U in [C/4, (C+1)/4]`, `V in [R/4, (R+1)/4]`
- **OpenGL V-axis origin convention**: V origin is at bottom-left. Artists in Blender must apply `V_opengl = 1 - V_blender` before mapping to atlas cells. All vehicle UVs must be authored in OpenGL convention or check #10 will fail for all 5 V1 vehicle assets.

V1 vehicle type atlas cell assignments (see `vehicle_atlas_registry.json`):

| Vehicle ID | Row | Col |
|---|---|---|
| `car_sedan` | 0 | 0 |
| `car_hatchback` | 0 | 1 |
| `car_suv` | 0 | 2 |
| `bus_standard` | 1 | 0 |
| `truck_cargo` | 1 | 1 |

---

## Planned Scripts (Created in Phase 2)

- **`validate_assets.py`**: 14-check validation suite (naming conventions, DDS fourCC detection, LOD2 geometry shell triangle budget, POT dimension check, mip level count, sidecar presence and field validation, billboard atlas cross-check, collision mesh naming). All 14 checks are active from Phase 2 onward.
  - Valid DXT formats: `DXT1` and `DXT5` only. `DXT3`/BC2 must be flagged as an invalid format error. `_lod2_lm.dds` lightmap files must use DXT5/BC3 (NOT DXT1).
- **`export_textures.py`**: Canonical DDS export automation (DXT5nm swizzle for normal maps, sRGB DXT1/DXT5 for diffuse, RGBA8 for UI sprite sheets and splat maps). Calls NVTT or Compressonator based on platform detection.
