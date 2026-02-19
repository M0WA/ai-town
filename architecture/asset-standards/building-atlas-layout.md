# Building Atlas Layout

**Status**: TEMPLATE — to be populated and signed off jointly by `graphics-artist-2d-texture` and `graphics-dev-irrlicht` as a Phase 2 exit criterion. No building mesh UV channel 0 authoring may begin until this document is approved.

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
| 0 | 0 | PLACEHOLDER — wall_residential_low | Residential | Low density | Assign in Phase 2 |
| 0 | 1 | PLACEHOLDER — wall_commercial_low | Commercial | Low density | Assign in Phase 2 |
| 0 | 2 | PLACEHOLDER — wall_industrial_low | Industrial | Low density | Assign in Phase 2 |
| 0 | 3 | PLACEHOLDER — base_residential_low | Residential | Low density | Assign in Phase 2 |
| 1 | 0 | PLACEHOLDER — wall_residential_med | Residential | Med density | Assign in Phase 2 |
| 1 | 1 | PLACEHOLDER — wall_commercial_med | Commercial | Med density | Assign in Phase 2 |
| 1 | 2 | PLACEHOLDER — wall_industrial_med | Industrial | Med density | Assign in Phase 2 |
| 1 | 3 | PLACEHOLDER — base_residential_med | Residential | Med density | Assign in Phase 2 |
| 2 | 0 | PLACEHOLDER — wall_residential_high | Residential | High density | Assign in Phase 2 |
| 2 | 1 | PLACEHOLDER — wall_commercial_high | Commercial | High density | Assign in Phase 2 |
| 2 | 2 | PLACEHOLDER — wall_industrial_high | Industrial | High density | Assign in Phase 2 |
| 2 | 3 | PLACEHOLDER — roof_shared | All zones | All tiers | Assign in Phase 2 |
| 3 | 0 | PLACEHOLDER — facade_detail_balcony | All zones | Med/High | Assign in Phase 2 |
| 3 | 1 | PLACEHOLDER — facade_detail_pilaster | All zones | Med/High | Assign in Phase 2 |
| 3 | 2 | PLACEHOLDER — reserved | — | — | Reserved for Phase 6+ |
| 3 | 3 | PLACEHOLDER — reserved | — | — | Reserved for Phase 6+ |

**Note**: Cell assignments are PLACEHOLDERS. The exact per-module-variant assignments must be determined during Phase 2 based on the minimum V1 building set (minimum 2 variants per zone-tier combination). Update this table before Phase 6 UV authoring begins.

**IMPORTANT — Variant sharing of wall cells (binding decision)**: Building variants within the same zone-tier combination share the same wall module atlas cells — only distinct module types (wall, base, roof, facade detail) require separate cells. For example, `res_low_01` and `res_low_02` are two variants of Low-density Residential; both reference the same `wall_residential_low` atlas cell with different mesh geometry configurations. Unique cells are NOT required per variant, only per module type. This keeps the 4×4 (16-cell) atlas within capacity for all V1 building module types. This decision is binding and confirmed here before UV authoring begins. `graphics-artist-2d-texture` and `graphics-dev-irrlicht` must both sign off that all V1 variant UVs map into the correct shared module-type cell before Phase 6 UV authoring begins.

## Road Marking Atlas (1024×1024)

- Format: DDS DXT5/BC3 (alpha = decal mask)
- Resolution: 1024×1024 pixels
- Cell grid: 4×4 cells at 256×256 px each (16 cells)
- Per-cell usable area: 240×240 px (8 px border per 2d-texture-standards.md)
- Upload path: linear (NOT sRGB — decal mask data, not diffuse color)

### Cell Assignment Table

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
| (2,0–3,3) | RESERVED | For Phase 6+ additional decal types |

## Vehicle Atlas Registry Stubs

See `tools/vehicle_atlas_registry.json` (stub created at **Phase 0** with the V1 vehicle type assignments and schema from `architecture/asset-standards/3d-model-standards.md` — Vehicle Atlas Cell Registry; Phase 6 completes the full registry during validate_assets.py integration).

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
- Format: DDS DXT5 (DXT5 required — alpha channel encodes the silhouette mask for non-rectangular vehicle roof shapes; DXT1 only supports 1-bit alpha, which is insufficient for smooth vehicle silhouettes)
- Resolution: 256×256 px
- Cell grid: 16×16 cells at 16×16 px each (256 sprite slots)
- Purpose: Point/sprite LOD2 representation at distances beyond 100 m — each sprite is a 16×16 px roof-color/type identifier rendered as a camera-facing billboard quad (1 m × 0.5 m)
- Mip chain: none (`GL_TEXTURE_MAX_LEVEL = 0`) — sprites are rendered at near-fixed screen size; mip filtering would blur 16×16 px cells into unrecognisable blobs
- Upload path: linear pool (`IVideoDriver::getTexture()`) — sprite atlas encodes stylised roof color swatches, not photographic diffuse data; sRGB path is not required

These two atlases are **not interchangeable**. The diffuse atlas feeds the mesh material pipeline (UV channel 0 of LOD0/LOD1 vehicle meshes); the sprite atlas feeds the point-sprite LOD2 renderer. Do not conflate their formats, resolutions, or upload paths.

## Sign-Off Checklist

Before Phase 6 UV authoring begins, both reviewers must confirm:

- [ ] All V1 module variant types have a cell assignment (no unbounded "assign in Phase 2" placeholders remaining)
- [ ] Road marking atlas cell assignments cover all V1 decal types
- [ ] Vehicle atlas stub entries match the V1 vehicle type list
- [ ] Cell UV borders (8 px) respected in all assignments
- [ ] Document reviewed and approved by `graphics-artist-2d-texture`
- [ ] Document reviewed and approved by `graphics-dev-irrlicht`
