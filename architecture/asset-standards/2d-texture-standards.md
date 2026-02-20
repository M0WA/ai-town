# 2D Texture Standards

- **Runtime formats**:
  - Diffuse/albedo (opaque): DDS DXT1/BC1
  - Diffuse/albedo (alpha): DDS DXT5/BC3
  - **Normal maps**: **DDS BC3 (DXT5nm packing) is the mandatory default**. BC3's RGB channels have lower precision than the alpha channel (4-bit block encoding vs 8-bit interpolated blocks). Standard XYZ-in-RGB storage produces visible banding on smooth curved surfaces. Use **DXT5nm packing** instead:
    - **DDS export pipeline (apply in order)**:
      - Step 1 — Y-convention correction (if sourcing from DirectX-convention baker such as Substance Painter with DirectX normals enabled): invert the green channel. Skip this step if the baker is already set to OpenGL convention.
      - Step 2 — DXT5nm swizzle: copy X→alpha, keep Y in green, discard Z (blue = 0).
      - Step 3 — Compress to BC3/DXT5.
    - Pack X (tangent-space right) into the **alpha channel** (highest precision in BC3)
    - Pack Y (tangent-space up) into the **green channel** (best 4-bit color precision in DXT)
    - Do **NOT** store Z in the blue channel at runtime
    - Shader must reconstruct Z: `float nz = sqrt(max(0.0, 1.0 - nx*nx - ny*ny));`
    - Full shader unpack:

      ```glsl
      float nx = texture(normalMap, uv).a * 2.0 - 1.0;
      float ny = texture(normalMap, uv).g * 2.0 - 1.0;
      float nz = sqrt(max(0.0, 1.0 - nx*nx - ny*ny));
      vec3 normal = normalize(vec3(nx, ny, nz));
      ```

    - Source PNG stores full XYZ for reference; DDS export must swizzle X→alpha, Y→green before BC3 compression.
    - BC5/ATI2 migration (post-V1): BC5/ATI2 is not confirmed to have a load path in Irrlicht's standard DDS loader and must not be used in production until explicitly verified.
  - Specular/roughness (grayscale): DDS BC1; (packed multi-channel): DDS BC3
- **Source format**: PNG (source files only — never shipped as runtime textures)
- **Color space / sRGB**: Irrlicht's OpenGL backend does not automatically apply sRGB decode on texture sample. **Chosen approach: sRGB decode at load time** (preferred) **with shader fallback**:
  - **Extension check required** (perform once at `RenderSystem::init()` after `createDevice()`):

    ```cpp
    bool m_srgbTextureSupported = glewIsExtensionSupported("GL_EXT_texture_sRGB");
    // GL_EXT_texture_sRGB covers GL_COMPRESSED_SRGB_S3TC_DXT1_EXT and GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT.
    // "GL_EXT_texture_compression_s3tc_srgb" is NOT a valid OpenGL extension name and must NOT be used.
    // GL_EXT_texture_sRGB alone is the correct extension to query for sRGB DXT support.
    // Expose via: bool RenderSystem::isSRGBTextureSupported() const
    ```

    If `GL_EXT_texture_sRGB` is unavailable (rare on modern desktop GPUs but must be handled), **fall back to shader gamma correction**: load diffuse textures as standard linear DXT1/DXT5, and add a `pow(color.rgb, vec3(2.2))` gamma decode at the start of the terrain and building fragment shaders. This fallback is less accurate (no per-channel hardware decode) but avoids undefined behavior from using unsupported extension formats.
  - **Primary path (GL_EXT_texture_sRGB present)**: Load diffuse/albedo textures with sRGB-aware internal formats:
    - Opaque diffuse (DXT1/BC1): `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`
    - Alpha diffuse (DXT5/BC3): `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`
  - `TextureCache` must upload diffuse textures via a **fully raw OpenGL path** — **do not use `IVideoDriver::addTexture()` + `ITexture::lock()` + `glCompressedTexImage2D`**. `addTexture()` creates an `ECF_A8R8G8B8` texture object already committed to a linear GL internal format; calling `glCompressedTexImage2D` on that already-allocated object produces `GL_INVALID_OPERATION`. Additionally, `ITexture::lock()` returns **null** for DXT compressed-format textures (Irrlicht does not maintain a lockable CPU buffer for block-compressed textures); dereferencing the null pointer will crash. The `addTexture` + `COpenGLTexture::getOpenGLTextureName()` approach is equally invalid for the same root cause. See [`texture-cache.md`](../graphics-architecture/texture-cache.md) for the authoritative upload sequence (`glGenTextures` → `glBindTexture` → `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` or `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`). **Also**: never call `ITexture::lock()` on a DXT-format texture anywhere in the codebase — always check the return value for null before dereference: `void* data = tex->lock(); if (!data) { logError(...); return; }`.
  - Non-diffuse textures (normal maps, roughness) remain linear and use standard `GL_COMPRESSED_RGB_S3TC_DXT1_EXT` / `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`. Splat maps are a separate upload category (RGBA8 UNORM via `glTexImage2D`, third raw-GL pool in `TextureCache`) — they are NOT uploaded with any DXT format; see the Terrain Texturing & Splat Maps section below.
  - This decision is **final and binding** before any texture production begins. `graphics-dev-irrlicht` implements the `TextureCache` upload path; `graphics-artist-2d-texture` authors diffuse textures in sRGB space (standard Photoshop/Substance workflow).
- **`GL_MAX_TEXTURE_SIZE` query timing**: `glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size)` can only be called after an OpenGL context is current. `RenderSystem` must perform this query **immediately after `createDevice()` returns**, store the result as `u32 m_maxTextureSize`, and expose it via `getMaxTextureSize()`. No other class may call `glGetIntegerv` directly. Atlas dimensions must not be resolved at static initialization time.
- **EDT_NULL guard for raw OpenGL calls**: `RenderSystem::init()` must check `device->getVideoDriver()->getDriverType() == video::EDT_NULL` before any raw OpenGL call. Under `EDT_NULL` (used in integration tests), there is no OpenGL context; `glGenTextures`, `glCompressedTexImage2D`, and `glGetIntegerv` all invoke undefined behavior. Required guards:
  - `m_maxTextureSize`: initialize to 2048 as a safe default when `driverType == EDT_NULL`; skip the `glGetIntegerv` query.
  - `m_maxAnisotropy`: initialize to `1.0f` as a safe default when `driverType == EDT_NULL`; skip the `glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, ...)` query and skip the `GL_EXT_texture_filter_anisotropic` extension check.
  - `TextureCache` upload path: skip `glGenTextures`/`glCompressedTexImage2D` and return a sentinel handle when `driverType == EDT_NULL`.
  - Document that `TextureCache` sRGB upload correctness is NOT verified by `EDT_NULL` integration tests; a separate test category using `EDT_OPENGL` under `xvfb-run` is required for texture upload verification.
- **TextureCache release paths** — two distinct pool families require two distinct release methods (`releaseLinear()` for linear-pool `ITexture*` textures, `releaseSRGB(filename)` for sRGB raw-GL textures). There is no `releaseByITexture()` method — this name must not appear in code or documentation. See [`texture-cache.md`](../graphics-architecture/texture-cache.md) for the full release method signatures, ref_count semantics, `evictUnreferenced()` design, and the authoritative sRGB eviction sequence.
- Normal maps, roughness, specular, and splat maps are always authored in **linear space**.
- **Normal map Y-channel convention**: Normal maps must use **OpenGL convention** (Y-up: green channel points toward +Y in tangent space). If sourcing from Substance Painter or other DirectX-convention bakers, flip the green channel on export. Validate with a sphere test: a light from above-left must produce a highlight on the upper-left surface of bumps (not lower-right). If the convention is wrong, lighting appears inverted on all affected surfaces — there is no runtime error. Note: when using DXT5nm packing, apply Y-flip (this step) before the DXT5nm swizzle — see the DDS export pipeline in the DXT5nm packing section above.

## DDS Authoring Pipeline

All DDS textures must be produced via a validated command-line pipeline rather than DCC-tool GUI exporters. The canonical pipeline entry point is `tools/export_textures.py` (to be created in Phase 2 alongside `validate_assets.py`). Direct invocation of the tools below is permitted for individual asset iteration, but CI must call only `export_textures.py`.

**Recommended export tools (choose one per workstation):**

- **NVTT** (`nvcompress` CLI) — preferred on Linux; ships with NVIDIA Texture Tools Exporter.
- **Compressonator CLI** (`compressonatorcli`) — preferred on Windows; AMD Compressonator open-source release.

Both tools produce standards-compliant DDS files and are CI-verified. Do not use Photoshop DDS plug-ins or GIMP DDS plug-ins as the sole export path — they do not support the sRGB internal-format flags required by this pipeline.

**Command-line invocation patterns by texture category:**

| Category | Tool | Command |
|---|---|---|
| Diffuse sRGB opaque (DXT1 BC1, 4-mip) | nvcompress | `nvcompress -color -bc1 -mips 4 input.png output.dds` |
| Diffuse sRGB opaque (DXT1 BC1, 4-mip) | Compressonator | `compressonatorcli -fd BC1 -miplevels 4 input.png output.dds` |
| Diffuse sRGB with alpha (DXT5 BC3, 4-mip) | nvcompress | `nvcompress -color -bc3 -mips 4 input.png output.dds` |
| Diffuse sRGB with alpha (DXT5 BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input.png output.dds` |
| Normal map DXT5nm (BC3, 4-mip) | nvcompress | `nvcompress -normal -bc3 -mips 4 input_swizzled.png output.dds` |
| Normal map DXT5nm (BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input_swizzled.png output.dds` |
| UI sprite sheet (RGBA8 uncompressed, no mip) | nvcompress | `nvcompress -rgb -nomips input.png output.dds` |
| UI sprite sheet (RGBA8 uncompressed, no mip) | Compressonator | `compressonatorcli -fd ARGB_8888 -nomipmap input.png output.dds` |

**DXT5nm swizzle procedure (normal maps only — must be applied in DCC tool before compression):**

Normal map source PNGs store full XYZ in RGB. Before invoking the compressor, the artist must remap channels in Photoshop, Substance Painter, or equivalent:

1. Open the normal map PNG in the DCC tool.
2. If sourcing from a DirectX-convention baker (e.g. Substance Painter with DirectX normals enabled): invert the green channel (Y-flip). Skip this step if the baker outputs OpenGL convention.
3. Copy the red channel (X component) into the alpha channel.
4. Keep the green channel (Y component) unchanged.
5. Set the blue channel (Z component) to 0 (it is reconstructed in the shader at runtime — storing it wastes compression bits).
6. Export the remapped image as a new PNG (e.g. `input_swizzled.png`).
7. Pass the swizzled PNG to the compressor using the normal-map command above.

The final DDS contains: alpha = X, green = Y, red = 0, blue = 0. The shader reconstructs Z via `sqrt(max(0.0, 1.0 - nx*nx - ny*ny))` — see the DXT5nm packing section above for the full unpack GLSL.

**Note:** `tools/export_textures.py` will automate steps 1–7 for all texture categories and is the canonical pipeline entry point from Phase 2 onward. Until that script exists, artists must follow the manual procedure above exactly and have their exported DDS files reviewed against the naming convention and format table before committing to the asset directory.

- **Anisotropic filtering** (mandatory for in-world textures):
  - Terrain base textures and building facades: minimum **8× anisotropy**
  - Road tileable textures and props: minimum **4× anisotropy**
  - Normal maps and roughness maps: minimum **4× anisotropy** (same as roads/props; oblique surface detail is also affected by anisotropy)
  - **Specular/roughness maps**: minimum **4× anisotropy** (same tier as normal maps — specular highlights are as sensitive to oblique sampling artifacts as normal maps; omitting anisotropy on specular maps produces shimmering at grazing angles)
  - UI sprite sheet: anisotropic filtering disabled (UI elements rendered at screen pixels)
  - **Splat maps**: anisotropic filtering **disabled** (GL_LINEAR only) — blend weights are low-frequency per-tile data (1 texel per tile at 64×64 px per chunk) and do not benefit from anisotropic filtering. The splat map GPU upload code (see Terrain Texturing & Splat Maps section) must NOT add a `GL_TEXTURE_MAX_ANISOTROPY_EXT` call.
  - **Raw GL path** (sRGB diffuse textures): after the `glTexParameteri` filter calls, add:

    ```cpp
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(m_maxAnisotropy, requestedAnisotropy));
    ```

  - **Linear pool path** (normal maps, roughness loaded via `IVideoDriver::getTexture()`): after loading, use Irrlicht's material anisotropy setting. Irrlicht exposes per-material anisotropy via `SMaterial::AnisotropicFilter = requestedAnisotropy`. The scene node's material must set this field after texture assignment (e.g., `node->getMaterial(m).AnisotropicFilter = 8u;` for 8× anisotropy). **Do NOT call `IVideoDriver::makeColorKeyTexture()` for anisotropy setup** — that API makes a specific color channel transparent for alpha-keying and is completely unrelated to anisotropic filtering. Calling it on normal maps, roughness maps, or any non-keyed texture will destructively corrupt the texture data.
  - Query `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` immediately after context creation (same timing as `GL_MAX_TEXTURE_SIZE`); store as `m_maxAnisotropy` in `RenderSystem`. Check `GL_EXT_texture_filter_anisotropic` extension presence; if absent, log warning and skip (graceful degradation to trilinear).
  - Player-configurable in Settings > Graphics tab as an optional quality setting.
- **All textures must be power-of-two dimensions** (e.g. 256, 512, 1024, 2048)
- **Mipmap generation**: Mandatory for all in-world textures **except lightmaps**. Lightmap textures (`_lm` suffix) are explicitly exempt: they are sampled at a consistent scale close to camera (no perspective mip benefit) and the VRAM budget calculation assumes unmipmapped lightmaps (`ceil(1024/4)^2 × 16 = 1.0 MB/texture` at DXT5, no ×1.33 mip overhead). Adding mip chains to lightmaps would increase VRAM by ~33% and violate the budget assumption. Lightmap uploads must explicitly set `GL_TEXTURE_MAX_LEVEL = 0` (single mip level only) to prevent driver-side mip generation.
- **Naming convention**:

  | Suffix | Usage |
  |---|---|
  | `_d` | Diffuse/albedo |
  | `_n` | Normal map (DXT5nm) |
  | `_s` | Specular (grayscale) |
  | `_sp` | Specular packed (multi-channel roughness/metallic/AO) |
  | `_lm` | Lightmap bake (UV channel 1) |
  | `_billboard` | Billboard imposter atlas (1024×128 DXT5 sRGB, 1×8 horizontal strip) |

  All suffixes are lowercase. No other suffix patterns are valid. `validate_assets.py` must reject any DDS file whose name does not end with one of these six suffixes. The `_billboard` suffix applies exclusively to LOD2 imposter atlases — small building and prop assets that ship a `_billboard.dds` must NOT also ship a `_lod2.b3d` mesh.

### Resolution Matrix

| Asset category | Resolution |
|---|---|
| Terrain base textures | 2048×2048 |
| Building facade atlas cell | 512×512 per module face (wall tile); up to 4 module faces packed per 1024×1024 city building atlas cell |
| Roads (tileable) | 1024×1024 |
| Props / street furniture | 256×256 or 512×512 |
| UI sprite sheet | 2048×2048 (atlas) |
| Lightmap bake (_lm) — small/medium buildings | 512×512 |
| Lightmap bake (_lm) — large buildings | 1024×1024 |
| Lightmap bake (_lm) — LOD2 shell | 256×256 |
| Specular/roughness (_s,_sp) — building facades | 512×512 (matches facade atlas cell resolution) |
| Specular/roughness (_s,_sp) — terrain | 1024×1024 (half of diffuse, sufficient for smooth terrain materials) |
| Specular/roughness (_s,_sp) — props | 256×256 (half of prop diffuse maximum) |
| Normal maps (_n) — all categories | Same resolution as specular/roughness for that category |
| Vehicle diffuse atlas | 512×512 per vehicle type, packed into 2048×2048 DDS DXT1 atlas (16 vehicle types per sheet) |
| Vehicle normal map | 256×256 per vehicle type, packed into a separate 2048×2048 DDS DXT5nm atlas (8×8 grid of 256×256 cells, 64 slots; same vehicle registry assignments as diffuse atlas rows/columns; named `vehicles_normal_atlas_n.dds`) |

**Facade texture policy**: All building facade diffuse textures must use atlas cells — no standalone per-building non-atlas textures for facade diffuse. Each unique wall module variant occupies one 512×512 cell in the 2048×2048 city building atlas (16 unique wall module textures per atlas sheet). Effective density at 512×512 for a 4×3 m module face: ~128 px/m at LOD0 near-camera distance.

**Building atlas usable content area per cell**: With 8 texels per-cell border on each of the 4 edges, the usable content area per 512×512 facade atlas cell is **496×496 px** (512 − 8 − 8 = 496 px per axis). All facade art — window detail, surface materials, trim lines — must be authored to fit within this 496×496 usable zone. Content that bleeds into the 8 px border zone will exhibit mip-level bleed at distant views (the border pixels from adjacent cells become visible). The export validation script must verify that all non-transparent atlas cell pixels fall within the [8, 504] UV texel range on both axes.

**Building variants within the same zone-tier combination share wall module atlas cells** — only distinct module types (wall, base, roof, facade detail) require separate cells. See `architecture/asset-standards/building-atlas-layout.md` for the confirmed binding cell-sharing decision, sign-off requirements, and the full example of `res_low_01` / `res_low_02` variant sharing. Do NOT author a new atlas cell for a building variant unless it introduces a genuinely new module type not covered by any existing cell.

**Road tile diffuse texture — custom shader requirement**: The road surface texture (`road_asphalt_tileable.dds`) is an sRGB raw-GL texture (raw `GLuint` in `TextureCache::m_srgbTextures`, not an `ITexture*`). Irrlicht's built-in material system cannot reference raw-GL textures. **Road tile mesh must use a custom GLSL material** (registered via `addHighLevelShaderMaterialFromFiles`) that manually binds the road texture to unit 0 (`glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, roadTexId)`) before each draw call. Do NOT assign the road texture via `SMaterial::setTexture()` — that API requires an `ITexture*`, which is unavailable for sRGB textures. The custom road shader is responsible for sampling unit 0 as the diffuse color and blending alpha-channel road markings.

#### UV & Atlas Strategy

- **2 UV channels per mesh**: Channel 0 = diffuse/albedo atlas; Channel 1 = lightmap baking. **Lightmap strategy**: Per-asset lightmaps (one `_lm` texture per building asset) for V1 — atlased lightmaps are preferred for VRAM efficiency post-V1. UV channel 1 unwrap must be non-overlapping across the entire mesh for correct lightmap baking.
- **KNOWN V1 LIMITATION — Lightmap UV repacking**: Per-asset lightmap UVs (UV channel 1) will require repacking when transitioning to atlased lightmaps post-V1. This is expected and planned rework. Artists must author UV channel 1 with uniform padding and non-rotated islands to ease atlas-friendliness. Do not optimize per-asset UV packing in ways that would require manual re-unwrap for atlasing. Post-V1 atlased `_lm` files will use a separate naming convention defined at that milestone.
- **City building atlas**: Default = **2048×2048** (safe for all OpenGL desktop hardware). A 4096×4096 atlas may be used only if a runtime `GL_MAX_TEXTURE_SIZE` check confirms support ≥ 4096; otherwise fall back to 2048×2048.
- Road markings and decals packed into a single **1024×1024 atlas** (DDS DXT5/BC3 to support painted lane-marking alpha transparency). Road marking decals are a finite set (approximately 12–16 unique cells: straight markings, intersection markings, crosswalks, turn arrows), fitting comfortably in a 1024×1024 layout with 8-texel per-cell padding. **⚠️ ARTIST WARNING — ATLAS IS NEAR CAPACITY**: The road marking atlas is a 4×4 grid of 16 cells. 12–16 unique decals fills this atlas to 75–100%. Before authoring any new road decal types, check the current cell count against the 16-cell maximum. If the 16-cell limit is reached, do NOT overpack — a 2048×2048 second atlas is the correct expansion path (post-V1 scope). Overflowing the 1024×1024 atlas by squeezing more cells in will violate the 8-texel padding requirement and cause mip-level bleed artifacts. A 2048×2048 second atlas is post-V1 scope if additional road decal variety is required. **Road marking atlas mip chain**: 4-level mip chain (1024→512→256→128); clamp at 4 levels via `GL_TEXTURE_MAX_LEVEL = 3`. **Atlas cell padding**: 8 texels per-cell border on each edge (same as building atlas). Each decal cell occupies **256×256 px total** in the atlas (240×240 px usable area + 8-texel border on each of the 4 edges: 8+240+8 = 256 px per cell). Four cells of 256 px each fit exactly in 1024 px: a **4×4 grid of 16 cells** on the 1024×1024 atlas. Math verification: 4 × 256 = 1024 — no overflow. The 16-texel total gutter between adjacent cells (8 px from each neighbouring cell's border) prevents mip-level bleed across cell boundaries. **Road markings atlas upload path**: linear pool (`IVideoDriver::getTexture()`) — NOT the raw-GL sRGB path. The road markings atlas encodes a decal mask (alpha channel = blending opacity), not diffuse color data. Uploading it via the sRGB path would cause incorrect gamma brightening of the mask values, altering decal opacity at UV boundaries. Anisotropy: disabled (`GL_TEXTURE_MAX_ANISOTROPY_EXT = 1.0f`) — decal masks do not benefit from anisotropic filtering.
- UI icons and HUD elements atlased into a single sprite sheet. **Runtime format for UI sprite sheet**: **RGBA8 UNORM (uncompressed)** — DXT5 compression is prohibited for UI atlases because DXT5's 4×4-pixel block boundaries produce visible compression artifacts on sharp 1-pixel icon outlines and text glyphs, and UI elements are rendered at exact screen pixel resolution (no LOD sampling) where compression quality loss is most visible. RGBA8 at 2048×2048 = 16 MB VRAM (2048 × 2048 × 4 bytes). This is a deliberate budget allocation for UI sharpness. **UI sprite sheet upload path**: the UI sprite sheet must be uploaded via `glTexImage2D` with `GL_RGBA8` internal format (not DXT). Mipmapping must be disabled: call `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0)` before `glTexImage2D` to prevent driver-side mip generation. Failure to set `GL_TEXTURE_MAX_LEVEL = 0` may cause the driver to generate a full mip chain (16 MB × 1.33 ≈ 21 MB), violating the 16 MB budget assumption. The `glTexImage2D` call must NOT be followed by `glGenerateMipmap`. `GL_TEXTURE_MIN_FILTER` must be set to `GL_LINEAR` (not `GL_LINEAR_MIPMAP_LINEAR`) since there is only one mip level.
- **Atlas padding**: Per-cell border must be `2^(num_mip_levels − 1)` texels added to **each edge of each cell independently** (top, bottom, left, right — not total gutter). This ensures that at every mip level the border shrinks to exactly 1 texel minimum, preventing color bleeding between adjacent cells. Concretely: "8 texels per-cell border" means 8 texels on EACH side of a cell, producing a 16-texel total gutter between two adjacent cells.
  - 4-level mip chain (e.g. billboard atlas): **8 texels per-cell border on each edge** (16 texels total gutter between adjacent cells)
  - 8-level mip chain (e.g. 2048 building atlas with mipmaps to 8×8): **128 texels per-cell border** — impractical, so the building atlas mip chain must be **clamped at 4 levels** (2048→1024→512→256, stopping there). Configure this in the TextureCache upload call (e.g., `GL_TEXTURE_MAX_LEVEL = 3`). Building atlas uses **8 texels per-cell border** (safe to mip level 3).
  - UI sprite sheet: mipmapping must be **disabled** at load time. No padding constraint applies.
  - Padding pixels must be filled with a copy of the nearest border pixel — not transparent or black.

#### Shader Texture Unit Assignment Table

All terrain and building shaders must use the following fixed texture unit assignments. These assignments are the authoritative binding contract between `TextureCache` (which binds textures) and GLSL shaders (which declare `uniform sampler2D` slots). Defined in `src/rendering/shader_constants.h`:

| Texture unit | Constant name | Usage |
|---|---|---|
| 0 | `kTexUnitDiffuse` | Diffuse/albedo (sRGB DXT1/DXT5 raw-GL path) |
| 1 | `kTexUnitNormal` | Normal map (DXT5nm, linear-pool `ITexture*`) |
| 2 | `kTexUnitSpecular` | Specular/roughness (BC1 or BC3 packed, linear-pool) |
| 3 | `kTexUnitLightmap` | Lightmap bake (DXT5/BC3, linear-pool) |
| 4 | `kTexUnitSplatMap` | Terrain splat/blend map (RGBA8 uncompressed, splat-map pool — raw `glTexImage2D`, NOT linear-pool) |
| 5 | `kTexUnitTerrainLayer0` | Terrain detail layer 0 (DXT1 sRGB, raw-GL) |
| 6 | `kTexUnitTerrainLayer1` | Terrain detail layer 1 (DXT1 sRGB, raw-GL) |
| 7 | `kTexUnitTerrainLayer2` | Terrain detail layer 2 (DXT1 sRGB, raw-GL) |
| 8 | `kTexUnitTerrainLayer3` | Terrain detail layer 3 (DXT1 sRGB, raw-GL) |
| 9 | `kTexUnitBillboard` | Billboard imposter atlas (DXT5 sRGB, raw-GL) |

These unit assignments are **fixed across all shaders** — terrain, building, vehicle, and billboard shaders all use the same unit-to-role mapping. A shader that needs only a subset (e.g., buildings don't use splat maps) simply does not declare the unused `uniform sampler2D` slot; the unused unit binding is harmless. Deviating from this table requires updating `shader_constants.h` AND all GLSL files that reference the affected unit — document any deviation explicitly.

**Important: 4 terrain detail layers, 4 texture units (5–8).** The splat map (unit 4) encodes 4 blend weights (RGBA channels), one per layer. Each layer corresponds to one terrain detail texture at a dedicated texture unit (5, 6, 7, 8). Units 5–8 must map 1-to-1 with splat-map channels R, G, B, A respectively. Combining two layers into one unit (e.g., assigning layers 0–3 to units 5–7) is incorrect and will break the terrain shader blend logic. All 4 terrain layer constants (`kTexUnitTerrainLayer0` through `kTexUnitTerrainLayer3`) must be defined in `shader_constants.h` as consecutive values 5, 6, 7, 8.

**Billboard atlas is sRGB** (unit 9): The billboard imposter atlas contains pre-rendered diffuse color data and must be uploaded via the raw-GL sRGB path (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), consistent with all other diffuse-color textures. Uploading the billboard atlas as linear DXT5 (`GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`) produces washed-out color at the billboard LOD transition (linear math on gamma-encoded data).

- Atlas layout must be designed before modeling begins, as a joint responsibility of `graphics-artist-2d-texture` and `graphics-dev-irrlicht`

#### Billboard Imposter Atlas

Small buildings and props use a pre-baked imposter atlas at LOD2 (beyond 100 m). The atlas is authored by the 2D texture artist from renders of the LOD1 mesh:

- **Atlas format**: DDS DXT5 **sRGB** (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), **1024×128 px**, 8 frames of 128×128 px each arranged in a 1×8 horizontal strip. Uploaded via the raw-GL sRGB path (same as diffuse building textures): `glGenTextures` → `glBindTexture` → `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`. **Never** use Irrlicht's `addTexture()` path for the billboard atlas — it produces a linear internal format. Alpha channel encodes non-rectangular silhouette cutouts. **Alpha convention: straight (unassociated) alpha** — RGB channels store the full-intensity billboard render (flat ambient-only), and alpha stores the cutout mask independently. Do NOT premultiply alpha: premultiplied alpha would darken billboard edges during DXT5 compression block rounding, producing a dark fringe on the silhouette. The billboard shader must use `vec4 texel = texture(billboardAtlas, uv); if (texel.a < 0.5) discard;` for cutout rendering (alpha test at 0.5 threshold, not alpha blending). This avoids transparency sorting issues with billboards rendered at varying camera angles.
- **Bake angles**: 8 directions every 45° around the vertical axis (0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°). The runtime `LODNode` selects the nearest 45° frame based on the angle between the camera and the building's facing direction.
- **Bake elevation**: **45° below horizontal (camera pitch = −45°)** — the midpoint of the [−70°, −20°] camera pitch operating range. This minimises average mismatch error across all valid camera pitches. "Above horizon" wording is incorrect for this convention; the camera is always looking downward in city view.
- **Bake lighting**: flat ambient-only (no directional light). This prevents sun-angle mismatch artifacts at different times of day. The runtime shader does not apply directional lighting to billboard quads (V1 scope).
- **Mip chain**: 4-level mandatory (1024×128 → 512×64 → 256×32 → 128×16); set `GL_TEXTURE_MAX_LEVEL = 3`.
- **Texture wrap mode**: Set `GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE` and `GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE` on billboard atlas textures. The default `GL_REPEAT` wrap mode causes the atlas strip to tile at the strip boundary, producing a ghost frame from the opposite end of the 1×8 strip when the UV quad samples near the horizontal edge. Clamp-to-edge eliminates this artifact.
- **Cell padding**: 8 texels per-cell border on each edge of each 128×128 frame — frames are packed edge-to-edge within the 1024×128 strip so the effective gutter between frames equals 16 texels (8 from each neighbour). **Border halves per mip level**: the 8-texel border at mip 0 halves at each successive mip: **8 px (mip 0) → 4 px (mip 1) → 2 px (mip 2) → 1 px (mip 3)**. At mip 3 the 1-texel border is the minimum safe margin to prevent bleed between adjacent frames in the strip. This is why a 4-level mip chain (clamped at `GL_TEXTURE_MAX_LEVEL = 3`) is the maximum safe chain for an 8-texel border — adding a 5th mip level would produce a sub-texel (0.5 px) border, which is insufficient to prevent bleed.
- **Usable content area per frame**: **112×112 px** at mip level 0 (128 px minus 8 px border on each of the 4 sides: 128 − 8 − 8 = 112). The building silhouette, windows, and all visible geometry must be authored to fit within the 112×112 usable area of each frame. Content that bleeds into the 8 px border zone will be clipped by mip-level bleeding or the clamp-to-edge wrap mode at far distances. **Mip level progression (authoring minimum legibility requirement)**:
  - Mip 0: 1024×128 — 8 × 128×128, usable **112×112 px** per frame
  - Mip 1: 512×64 — 8 × 64×64, usable **56×56 px** per frame
  - Mip 2: 256×32 — 8 × 32×32, usable **28×28 px** per frame
  - Mip 3: 128×16 — 8 × 16×16, usable **14×14 px** per frame (border shrinks to 1 px/side at this mip level; effective content is 14×14 px)
  At mip level 3 (used beyond ~400 m view distance), each building imposter is represented by 14×14 usable pixels. **Authoring requirement**: The primary silhouette of the building must be recognisable at 14×14 px — verified by downscaling a single frame to 14×14 in the DCC tool before finalising LOD2 billboard art. Fine details (windows, ledges) are not required to be legible at mip 3; gross building form (tower vs low-rise, vertical vs horizontal dominant shape) must remain distinguishable.
- **Naming**: `<asset_name>_billboard.dds`. Small building / prop assets must NOT ship a `_lod2.b3d` file — the billboard DDS is the LOD2 asset for these categories.

#### Road Tileable Texture

Road surfaces use a dedicated tileable texture separate from terrain and building atlases:

- **Format**: DDS DXT5 sRGB (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`); uploaded via the raw-GL sRGB path (same as diffuse building textures). DXT5 is required (not DXT1) because road markings (lane lines, crosswalk stripes) use the alpha channel for blending road marks onto the road surface via the terrain shader's blend weight mechanism.
- **Dimensions**: 1024×1024 px (power-of-two; aspect ratio 1:1 for seamless tiling on orthogonal road quads)
- **Color space**: sRGB (contains diffuse road surface color and lane marking color — visual data requiring gamma-correct sampling)
- **Mip chain**: 4 levels (1024→512→256→128); `GL_TEXTURE_MAX_LEVEL = 3`. Beyond 4 levels, road lane markings become indistinguishable at mip 4+ (64×64 px), so clamping at mip 3 (128×128 usable) is the minimum for legibility at standard play distances.
- **Shader texture unit**: `kTexUnitDiffuse` (unit 0) — road surface shader binds the road tileable texture to unit 0, same as building diffuse. Road tiles do not use the splat map path; they use a simpler per-quad material assignment.
- **UV tiling**: Road texture tiles at **2×2 per road tile** (each road quad tiles the texture twice in each axis). This gives ~512 px/m effective density at 1024 px resolution, which is appropriate for road surfaces viewed from standard city-builder camera distances (20–200 m altitude).
- **Content**: Full 1024×1024 contains base road surface (asphalt) with embedded lane markings (dashed/solid white lines, centerline). Alpha channel is used for compositing road markings over the base surface in the road shader. Authors must NOT include direction arrows or intersection-specific markings — generic lane marking only; intersections are handled separately.
- **Named**: `road_asphalt_tileable.dds`. Only one road tileable texture in V1 — no separate textures for different road widths or materials.
- **VRAM budget**: DXT5 1024×1024 with 4-level mip: `ceil(1024/4)² × 16 × 1.33`. **Correct calculation**: `ceil(1024/4) = 256` blocks per side; `256 × 256 × 16 = 1,048,576` bytes for mip 0; with 4-level mip overhead × 1.33: `1,048,576 × 1.33 ≈ 1,394,688 bytes ≈ 1.33 MB`. This is within the road atlas budget entry of ≤2 MB in the VRAM table. **Note**: an earlier draft of this spec incorrectly stated 2.1 MB — the correct value is ~1.33 MB. The VRAM table has been updated to reflect this.

#### Terrain Texturing & Splat Maps

- Seamless tileable base textures required (grass, asphalt, soil, concrete) — no visible seams
- **UV tiling frequency**: Terrain base textures tile at **4×4 repeats per 64×64 m LOD0 chunk** (16 px/m effective density at 2048 px resolution). All terrain texture artists must use this exact tiling frequency — inconsistent values produce visible density discontinuities at chunk borders. This value is fixed before terrain texture production begins.
- **Splat/blend map format**: **DDS RGBA8 UNORM (uncompressed) — never DXT/BC compressed**. DXT compression introduces block quantization artifacts that corrupt the smooth blend weight gradients (R/G/B/A channels encode continuous 0–255 weights). At 1024×1024 uncompressed RGBA8 = 4 MB VRAM — acceptable. **1 texel per terrain tile** (e.g. 64×64 px per 64×64 m LOD0 chunk; full map: 1024×1024 px for a 1024×1024 tile map); each RGBA channel = blend weight for one terrain material layer (4 layers per RGBA map). **Splat maps must be power-of-two dimensions** — the per-chunk resolution (1 texel/tile) already satisfies POT for standard chunk sizes (64×64, 128×128), but non-standard chunk dimensions must be padded to the next POT before upload.
- **Splat map GPU upload**: Splat maps must be uploaded as uncompressed `GL_RGBA8` via `glTexImage2D` — **never via `glCompressedTexImage2D`**. The correct upload sequence (texture object must be explicitly created and bound first — the sRGB raw-GL path requires the same discipline):

  ```cpp
  GLuint splatTexId = 0;
  glGenTextures(1, &splatTexId);
  glBindTexture(GL_TEXTURE_2D, splatTexId);
  // Splat maps: NO mip chain — single mip level only.
  // glGenerateMipmap would inflate VRAM from 4 MB to ~5.3 MB per map (×1.33 factor),
  // exceeding the ≤8 MB two-map budget. Set GL_TEXTURE_MAX_LEVEL = 0 to suppress
  // driver-side mip generation and enforce single-level storage.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);       // MUST appear before glTexImage2D
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // no mipmaps → GL_LINEAR, not GL_LINEAR_MIPMAP_LINEAR
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  // DO NOT call glGenerateMipmap here — it contradicts GL_TEXTURE_MAX_LEVEL = 0 and
  // inflates VRAM beyond the budget. Splat maps at 64×64 or 1024×1024 texels do not
  // benefit from mipmapping (they are sampled at a near-uniform screen scale per terrain chunk).
  glBindTexture(GL_TEXTURE_2D, 0);
  ```

  Using `glCompressedTexImage2D` for splat maps is a critical error — it corrupts the smooth 0–255 blend weight gradients that require full 8-bit per-channel precision. The sRGB raw-GL path (`m_srgbTextures`, DXT-format) must not be used for splat maps. The `IVideoDriver::getTexture()` path must also not be used. `TextureCache` must handle splat maps as a distinct upload category separate from both the sRGB pool and the linear DXT pool.
- Second splat map required if >4 terrain material layers needed (multi-pass blend strategy)
- **Splat map channel-to-material assignment (fixed, must not be changed after texture production begins):**

  | Splat map channel | Terrain material | Texture unit | Constant name | Initial value |
  |---|---|---|---|---|
  | R (red) | Grass | 5 | `kTexUnitTerrainLayer0` | 255 across entire map |
  | G (green) | Asphalt | 6 | `kTexUnitTerrainLayer1` | 0 |
  | B (blue) | Soil | 7 | `kTexUnitTerrainLayer2` | 0 |
  | A (alpha) | Concrete | 8 | `kTexUnitTerrainLayer3` | 0 |

  This assignment is **fixed before texture production begins** and must match the texture unit binding order in the terrain shader (`kTexUnitTerrainLayer0` through `kTexUnitTerrainLayer3`). Any mismatch between the splat map channel order and the texture unit binding order causes incorrect material blending across the entire terrain surface with no compile-time or runtime error. **The R channel (grass) is initialized to 255 across the entire map** so that unpainted tiles default to grass and the blend-weight normalization divisor is always non-zero.

- **Blend shader**: Sample splat map at tile UV; use R/G/B/A as blend weights for four detail textures; normalize weights to sum to 1.0; output weighted sum of sampled detail texture colors. **Division-by-zero protection**: If all four RGBA channels are simultaneously 0.0, the normalization divisor is 0. Shader must guard: `if (sum < 0.001) { weights = vec4(1,0,0,0); }` (fall back to base layer R). **Authoring rule**: The R channel (base terrain layer, e.g. grass) must be initialized to 255 across the entire splat map before any painting begins, ensuring the divisor is always non-zero on unpainted tiles.
- **Terrain normal map intensity — hard vs soft surface distinction**: Terrain normal maps must be authored with surface-appropriate intensity to avoid plastic or unnatural shading. Apply to all terrain `_n` textures:
  - **Hard surfaces** (concrete, brick, asphalt, stone): strong normal intensity; use Z-scale **1.0–1.5** in the DCC tool normal baker. High-frequency sharp detail is appropriate — pavement cracks, mortar lines, stone facets.
  - **Soft surfaces** (grass, soil, dirt, sand): gentle normal intensity; use Z-scale **0.3–0.7**. Over-strong normals on soft terrain surfaces produce a plastic or moulded appearance and must be avoided. Subtle micro-undulation (gentle height variation, soil clumping) is the correct authoring target.
  - Validate by rendering a sphere lit from a single directional light: hard-surface normals should produce sharp specular highlights along surface features; soft-surface normals should produce a diffuse, low-contrast shading variation with no sharp specular peaks.
- Coordinate layer count with `graphics-dev-irrlicht` before authoring terrain materials
- `graphics-dev-irrlicht` implements the splatting shader/multi-texture blend via Irrlicht's material system

### Scene VRAM Budget (V1 targets, mid-range desktop GPU with 4 GB VRAM)

Total texture VRAM for all simultaneously-resident assets must not exceed **1.0 GB** (leaving ~1 GB+ headroom for OS/driver/geometry/framebuffers on 4 GB hardware):

| Category | Budget |
|---|---|
| Terrain base textures (up to **4 layers**, 2048x2048 DXT1 with 4-level mip) | ≤11 MB (`ceil(2048/4)^2 × 8 × 1.33 ≈ 2.66 MB/layer × 4 layers`). **4 layers matches the shader texture unit table** — texture units 5–8 (`kTexUnitTerrainLayer0` through `kTexUnitTerrainLayer3`) provide exactly 4 terrain detail layer slots. Claiming 6 layers here is inconsistent with the 4-slot architecture. A second splat map (described in Terrain Texturing & Splat Maps) adds 4 more layers post-V1 only. V1 VRAM budget assumes 4 layers. |
| Splat maps (up to 2 x 1024x1024 RGBA8 UNORM, GL_TEXTURE_MAX_LEVEL=0, no mip chain) | ≤8 MB (2 × 1024 × 1024 × 4 bytes = 8 MB exact; no ×1.33 mip overhead) |
| City building atlas (2048x2048 DXT1, 4-level mip) | ≤6 MB |
| Per-asset lightmaps (50 active buildings x 1024x1024 DXT5/BC3, no mip chain) | ≤50 MB (corrected: ceil(1024/4)^2 *16 = 1.0 MB/texture* 50; DXT5 used for lightmaps because the alpha channel can encode ambient occlusion or shadow density independently of RGB luminance; lightmap textures do not require mip chains — sampled at consistent scale close to camera) |
| LOD2 shell lightmaps (50 active buildings × 256×256 DXT5/BC3, no mip chain) | ≤4 MB (ceil(256/4)^2 × 16 = 0.0625 MB/texture × 50; no mip chain per lightmap exemption rule) |
| Road atlas (1024x1024 DXT5, 4-level mip) | ≤1.5 MB (~1.33 MB exact; see Road Tileable Texture VRAM budget note) |
| UI sprite sheet (2048x2048 RGBA8, no mips) | ≤16 MB |
| Vehicle diffuse atlas (2048×2048 DXT1, 4-level mip) | ≤6 MB |
| Vehicle sprite atlas (256×256 DXT5, no mip) | ≤1 MB (DXT5 required — see 3D Model Standards; alpha channel needed for non-rectangular vehicle roof silhouettes; actual size ≈ 64 KB) |
| Billboard imposter atlases (≤50 unique small building/prop types × 1024×128 DXT5, 4-level mip) | ≤9 MB (≤170 KB per atlas × 50 types) |
| Miscellaneous (normal maps, roughness, props, per-type vehicle textures) | ≤48 MB |
| **Total** | **≤170 MB** (well within 1 GB ceiling) |

**Draw call ceiling**: ≤2,000 draw calls per frame (all LODs combined). Buildings sharing the same atlas texture and material can be batched or instanced into a single draw call. This drives the atlas-first design requirement.
**Unique mesh variant cap**: ≤50 unique LOD0/LOD1/LOD2 building mesh variants simultaneously loaded. Building types exceeding 50 unique meshes must share atlas space and be explicitly approved.
