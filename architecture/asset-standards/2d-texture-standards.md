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

All DDS textures must be produced via a validated command-line pipeline rather than DCC-tool GUI exporters. The canonical pipeline entry point is `tools/export_textures.py` (to be created in Phase 9 when full atlas textures are produced). Direct invocation of the tools below is permitted for individual asset iteration, but CI must call only `export_textures.py`.

**Recommended export tools (choose one per workstation):**

- **NVTT** (`nvcompress` CLI) — preferred on Linux; ships with NVIDIA Texture Tools Exporter.
- **Compressonator CLI** (`compressonatorcli`) — preferred on Windows; AMD Compressonator open-source release.

Both tools produce standards-compliant DDS files and are CI-verified. Do not use Photoshop DDS plug-ins or GIMP DDS plug-ins as the sole export path — they do not support the sRGB internal-format flags required by this pipeline.

**Command-line invocation patterns by texture category:**

| Category | Tool | Command |
|---|---|---|
| Diffuse sRGB opaque (DXT1 BC1, 4-mip) | nvcompress | `nvcompress -color -bc1 input.png output.dds` |
| Diffuse sRGB opaque (DXT1 BC1, 4-mip) | Compressonator | `compressonatorcli -fd BC1 -miplevels 4 input.png output.dds` |
| Diffuse sRGB with alpha (DXT5 BC3, 4-mip) | nvcompress | `nvcompress -color -bc3 input.png output.dds` |
| Diffuse sRGB with alpha (DXT5 BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input.png output.dds` |
| Normal map DXT5nm (BC3, 4-mip) | nvcompress | `nvcompress -normal -bc3 input_swizzled.png output.dds` |
| Normal map DXT5nm (BC3, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input_swizzled.png output.dds` |
| Specular packed (BC3, linear, 4-mip) | nvcompress | `nvcompress -bc3 input.png output_sp.dds` |
| Specular packed (BC3, linear, 4-mip) | Compressonator | `compressonatorcli -fd BC3 -miplevels 4 input.png output_sp.dds` |
| UI sprite sheet (RGBA8 uncompressed, no mip) | nvcompress **only** | `nvcompress -rgba -nomips input.png output.dds` |

**UI sprite sheet — Compressonator is PROHIBITED**: `compressonatorcli -fd ARGB_8888` must NOT be used to author `_ui.dds`. Compressonator's ARGB_8888 format stores pixel data in **BGRA byte order** on little-endian platforms (all x86/x86-64 desktops). `IrrlichtUIBackend::loadTexture()` uploads the UI sprite sheet via `glTexImage2D(..., GL_RGBA, GL_UNSIGNED_BYTE, data)`, which tells the driver to interpret the incoming bytes as RGBA. A Compressonator-produced BGRA buffer passed through a `GL_RGBA` source format causes **silent red-blue channel swap** across all UI elements — icons and HUD graphics will appear with swapped color channels in-engine. `nvcompress -rgba -nomips` guarantees true RGBA byte order on all platforms and is the sole permitted export tool for the UI sprite sheet. There is no runtime workaround; the byte-order mismatch is invisible to the renderer and produces no GL error. The only fix is correct authoring.

**IMPORTANT — nvcompress mip chain behaviour**: `nvcompress` does NOT support a `-miplevels N` (or `-mips N`) CLI parameter — no such flag exists in NVTT 2.x or NVTT 3.x. `nvcompress` always generates a **full mip chain** down to 1×1 (or 1×N for non-square textures) by default. For building diffuse, normal maps, and specular packed textures this is acceptable — the GPU sampler uses all levels and performance is optimal. For the **billboard atlas** (1024×128, 8-frame strip), a full chain produces 11 mip levels; the runtime sets `GL_TEXTURE_MAX_LEVEL = 3` to cap GPU reads at 4 levels, so in-game rendering is correct, but the DDS file on disk will contain extra levels. **The `validate_assets.py` billboard mip count check MUST use `mip_count >= 4` (NOT `== 4`).** This accepts both nvcompress outputs (which generate a full mip chain, 11+ levels, capped at runtime via `GL_TEXTURE_MAX_LEVEL = 3`) and Compressonator outputs (which generate exactly 4 levels). Rejecting atlases with more than 4 mip levels is incorrect behaviour in the validator — a strict `== 4` equality check would incorrectly reject valid nvcompress-authored atlases and is prohibited. For billboard atlases requiring exactly 4 mip levels **in the DDS file**, use **Compressonator** with `-miplevels 4`.

### Validating sRGB DDS Output (mandatory before committing any diffuse atlas DDS)

1. **Verify sRGB ICC profile in source PNG before compression.** `nvcompress -color` only produces a sRGB-tagged DDS if the source PNG has an embedded sRGB ICC profile. Confirm the profile is present before running the compressor:

   ```bash
   exiftool input.png | grep -i 'color space'
   ```

   The output must show `sRGB` (or equivalent). A source PNG without an embedded sRGB profile will produce a DDS with a linear internal format even when `-color` is passed, causing incorrect color rendering. If no sRGB profile is reported, re-export the PNG from the DCC tool with the sRGB ICC profile embed option enabled.

2. **Validate the DDS header for sRGB encoding after compression.** The FourCC field at byte offset 84 identifies the block-compression format family, but it cannot distinguish sRGB from linear for DXT1/BC1:

   ```bash
   python3 -c "import struct; d=open('out.dds','rb').read(); print(hex(struct.unpack('<I',d[84:88])[0]))"
   ```

   **IMPORTANT — FourCC alone cannot identify sRGB DXT1.** The FourCC value `0x31545844` ("DXT1" in ASCII little-endian) is identical for both linear DXT1 and sRGB DXT1. A linear DXT1 file and an sRGB DXT1 file both produce `0x31545844` at byte offset 84. Do NOT use the FourCC field to determine whether a DXT1 file is sRGB-encoded; it is the wrong field to check and will always match regardless of color space.

   To verify sRGB intent on a DXT1/BC1 diffuse texture, validators must parse the DX10 extension header and check the DXGI_FORMAT field:
   - A file with a DX10 extended header is identified by a FourCC of `0x30315844` ("DX10") at byte offset 84.
   - The DXGI_FORMAT field follows the standard DDS header and the DDS_HEADER_DXT10 structure's first 4 bytes.
   - BC1_UNORM_SRGB = DXGI_FORMAT value **72** (sRGB DXT1/BC1 — correct for diffuse textures).
   - BC3_UNORM_SRGB = DXGI_FORMAT value **78** (sRGB DXT5/BC3 — correct for sRGB alpha diffuse and billboard atlases).
   - Any other DXGI_FORMAT value indicates a non-sRGB (linear) encoding — do not commit that file as a diffuse atlas.

   If `nvddsinfo` or `dxinfo` are available on the workstation, these may also be used to inspect the DDS header and confirm the sRGB DXGI_FORMAT field. A file produced without DX10 header support (no "DX10" FourCC at offset 84) cannot encode sRGB intent in a machine-readable way and must be rejected.

   For DXT5 (BC3) billboard atlases: the standard DDS FourCC field contains `0x35545844` (`DXT5` in ASCII little-endian), which does NOT encode the sRGB flag. The traditional FourCC inspection step alone cannot reliably detect sRGB intent for DXT5 files. Use one of the following approaches to validate sRGB intent on DXT5 billboard atlases:
   - **DX10 extended header (recommended)**: Check the DX10 DXGI format field (`DXGI_FORMAT_BC3_UNORM_SRGB`, value 78). Files authored via `nvcompress -color` produce a DX10 extended header with this format value.
   - **Source PNG sRGB ICC profile + authoring flag**: Verify the source PNG carries an sRGB ICC profile and was compressed with `nvcompress -color` (the `-color` flag signals sRGB intent to the compressor and sets the DX10 DXGI sRGB format).
   Note: `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` requires the uploaded data to be authored in sRGB — verifying the DX10 header or source provenance before upload prevents silent gamma errors in the billboard atlas rendering.

3. **Automation note.** `tools/export_textures.py` (Phase 9 deliverable) handles sRGB tagging automatically for all diffuse atlas DDS outputs. Until that script exists, manual validation per steps 1–2 above is required for every diffuse atlas DDS before committing to the asset directory.

4. **Consequence of skipping validation.** A DDS file with a linear (non-sRGB) internal format header will be uploaded via `glCompressedTexImage2D` using `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` (or `_DXT5_EXT`) with the wrong GL internal format — the driver silently accepts the call without a GL error, but color rendering will be incorrect (gamma-incorrect diffuse colors) across all surfaces using that atlas. There is no runtime diagnostic for this class of error; it must be caught at authoring time.

**Splat map (RGBA8 UNORM — NOT DDS)**:
Author as a plain RGBA PNG with R channel filled to 255, G/B/A channels filled to 0 (initial state: 100% grass). Splat maps are uploaded at runtime via `glTexImage2D` with `GL_RGBA8` internal format — they are never compressed to DDS. Do NOT run `export_textures.py` or `nvcompress` on splat map source files. The `TextureCache` splat map pool (third pool, distinct from the linear and sRGB pools) loads these RGBA PNGs directly. The `--validate-only` flag in `export_textures.py` does not apply to splat maps.

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

**Note:** `tools/export_textures.py` will automate steps 1–7 for all texture categories and is the canonical pipeline entry point from Phase 9 onward. Until that script exists, artists must follow the manual procedure above exactly and have their exported DDS files reviewed against the naming convention and format table before committing to the asset directory.

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
  | `_d` | Diffuse/albedo. Upload path: **sRGB raw-GL path** (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` / `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`) for all photographic/perceptual diffuse textures. **Exception: vehicle sprite atlas (`vehicles_sprite_atlas_d.dds`) uses `_d` suffix but LINEAR upload path** — palette swatches are not photographic diffuse data and must not be sRGB-decoded. The sprite atlas is uploaded via `IVideoDriver::getTexture()` (linear pool), NOT the sRGB raw-GL path. See `architecture/asset-standards/building-atlas-layout.md` sprite atlas section. |
  | `_n` | Normal map (DXT5nm) |
  | `_s` | Specular (grayscale) |
  | `_sp` | Specular packed (multi-channel roughness/metallic/AO). Upload path: **linear pool** (standard `IVideoDriver::getTexture()`) — packed roughness/metallic/AO data is linear; sRGB decode would corrupt channel values. |
  | `_lm` | Lightmap bake (UV channel 1) |
  | `_billboard` | Billboard imposter atlas (1024×128 DXT5 sRGB, 1×8 horizontal strip) |
  | `_ui` | UI sprite sheet atlas (2048×2048 RGBA8 UNORM, uncompressed, no mip chain). **Canonical base filename: `hud_sprites_ui`** (authoring source: `hud_sprites_ui.dds` produced by `nvcompress -rgba -nomips`; runtime asset: `hud_sprites_ui.png` converted from the DDS for loading via `IVideoDriver::getTexture()`). Upload path: **`IVideoDriver::getTexture()`**; `IrrlichtUIBackend` MUST call `m_driver->setTextureCreationFlag(ETCF_CREATE_MIP_MAPS, false)` before calling `getTexture()` to prevent Irrlicht from generating a ~21 MB mip chain that would violate the 16 MB UI texture budget. The mip-generation flag MUST be re-enabled after loading (`ETCF_CREATE_MIP_MAPS, true`) so that subsequent `getTexture()` calls for other textures are unaffected. Must NOT use the sRGB upload path. |

  All suffixes are lowercase. No other suffix patterns are valid. `validate_assets.py` must reject any DDS file whose name does not end with one of these seven suffixes (`_d`, `_n`, `_s`, `_sp`, `_lm`, `_billboard`, `_ui`). The `_billboard` suffix applies exclusively to LOD2 imposter atlases — small building and prop assets that ship a `_billboard.dds` must NOT also ship a `_lod2.b3d` mesh.

  **Road texture suffix exception**: The two V1 road textures (`road_asphalt_tileable.dds` and `road_markings_atlas.dds`) are the sole DDS files in the V1 asset set that carry no recognized suffix. These files MUST be validated by canonical full-filename match in `validate_assets.py`, not by suffix. Any DDS file not matching these two canonical names AND not ending with one of the seven recognized suffixes MUST be rejected. This exception must be explicitly coded as an allowlist in `validate_assets.py` — the two filenames are not considered to 'have no suffix for historical reasons'; they are deliberate exception entries.

  **UI sprite sheet canonical filename enforcement**: The `_ui` suffix has exactly one V1 instance: `hud_sprites_ui` (authoring DDS source: `hud_sprites_ui.dds`; runtime PNG: `hud_sprites_ui.png`). The Phase 9 export step (`tools/export_textures.py`) and `validate_assets.py` MUST enforce this canonical base filename — a `_ui`-suffixed DDS file with any other base name (e.g. `ui_atlas_ui.dds`) MUST be rejected. This enforcement is separate from suffix validation: the DDS source file must end with `_ui` AND the full DDS filename must be exactly `hud_sprites_ui.dds`; the shipped runtime PNG must be exactly `hud_sprites_ui.png`.

**Upload path determination**: suffix `_d` alone does NOT determine the upload path. Diffuse textures that contain photographic color must use the sRGB raw-GL path. Diffuse textures that encode stylised data (e.g. vehicle sprite atlas roof swatches) must use the linear path. The `TextureCache` upload path is determined by the texture category, not the suffix alone. See `architecture/graphics-architecture/texture-cache.md` for the dispatch table.

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

**Building atlas usable content area per cell**: With 8 texels per-cell border on each of the 4 edges, the usable content area per 512×512 facade atlas cell is **496×496 px** (512 − 8 − 8 = 496 px per axis). All facade art — window detail, surface materials, trim lines — must be authored to fit within this 496×496 usable zone. Content that bleeds into the 8 px border zone will exhibit mip-level bleed at distant views (the border pixels from adjacent cells become visible). The export validation script must verify that all non-transparent atlas cell pixels fall within the [8, 504) UV texel range on both axes (i.e., valid texel indices 8–503 inclusive, giving 496 usable texels per axis).

**Building variants within the same zone-tier combination share wall module atlas cells** — only distinct module types (wall, base, roof, facade detail) require separate cells. See `architecture/asset-standards/building-atlas-layout.md` for the confirmed binding cell-sharing decision, sign-off requirements, and the full example of `res_low_01` / `res_low_02` variant sharing. Do NOT author a new atlas cell for a building variant unless it introduces a genuinely new module type not covered by any existing cell.

**Road tile diffuse texture — custom shader requirement**: The road surface texture (`road_asphalt_tileable.dds`) is an sRGB raw-GL texture (raw `GLuint` in `TextureCache::m_srgbTextures`, not an `ITexture*`). Irrlicht's built-in material system cannot reference raw-GL textures. **Road tile mesh must use a custom GLSL material** (registered via `addHighLevelShaderMaterialFromFiles`) that manually binds the road texture to unit 0 (`glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, roadTexId)`) before each draw call. Do NOT assign the road texture via `SMaterial::setTexture()` — that API requires an `ITexture*`, which is unavailable for sRGB textures. The custom road shader is responsible for sampling unit 0 as the diffuse color and blending alpha-channel road markings.

#### UV & Atlas Strategy

- **2 UV channels per mesh**: Channel 0 = diffuse/albedo atlas; Channel 1 = lightmap baking. **Lightmap strategy**: Per-asset lightmaps (one `_lm` texture per building asset) for V1 — atlased lightmaps are preferred for VRAM efficiency post-V1. UV channel 1 unwrap must be non-overlapping across the entire mesh for correct lightmap baking.
- **KNOWN V1 LIMITATION — Lightmap UV repacking**: Per-asset lightmap UVs (UV channel 1) will require repacking when transitioning to atlased lightmaps post-V1. This is expected and planned rework. Artists must author UV channel 1 with uniform padding and non-rotated islands to ease atlas-friendliness. Do not optimize per-asset UV packing in ways that would require manual re-unwrap for atlasing. Post-V1 atlased `_lm` files will use a separate naming convention defined at that milestone.
- **City building atlas**: Default = **2048×2048** (safe for all OpenGL desktop hardware). A 4096×4096 atlas may be used only if a runtime `GL_MAX_TEXTURE_SIZE` check confirms support ≥ 4096; otherwise fall back to 2048×2048.
- Road markings and decals packed into a single **1024×1024 atlas** (DDS DXT5/BC3 to support painted lane-marking alpha transparency). Road marking decals are a finite set (approximately 12–16 unique cells: straight markings, intersection markings, crosswalks, turn arrows), fitting comfortably in a 1024×1024 layout with 8-texel per-cell padding. **⚠️ ARTIST WARNING — ATLAS IS NEAR CAPACITY**: The road marking atlas is a 4×4 grid of 16 cells. 12–16 unique decals fills this atlas to 75–100%. Before authoring any new road decal types, check the current cell count against the 16-cell maximum. If the 16-cell limit is reached, do NOT overpack — a 2048×2048 second atlas is the correct expansion path (post-V1 scope). Overflowing the 1024×1024 atlas by squeezing more cells in will violate the 8-texel padding requirement and cause mip-level bleed artifacts. A 2048×2048 second atlas is post-V1 scope if additional road decal variety is required. **Road marking atlas mip chain**: 4-level mip chain (1024→512→256→128); clamp at 4 levels via `GL_TEXTURE_MAX_LEVEL = 3`. **Atlas cell padding**: 8 texels per-cell border on each edge (same as building atlas). Each decal cell occupies **256×256 px total** in the atlas (240×240 px usable area + 8-texel border on each of the 4 edges: 8+240+8 = 256 px per cell). Four cells of 256 px each fit exactly in 1024 px: a **4×4 grid of 16 cells** on the 1024×1024 atlas. Math verification: 4 × 256 = 1024 — no overflow. The 16-texel total gutter between adjacent cells (8 px from each neighbouring cell's border) prevents mip-level bleed across cell boundaries. **Road markings atlas upload path**: linear pool (`IVideoDriver::getTexture()`) — NOT the raw-GL sRGB path. The road markings atlas encodes a decal mask (alpha channel = blending opacity), not diffuse color data. Uploading it via the sRGB path would cause incorrect gamma brightening of the mask values, altering decal opacity at UV boundaries. Anisotropy: disabled (`GL_TEXTURE_MAX_ANISOTROPY_EXT = 1.0f`) — decal masks do not benefit from anisotropic filtering.
- UI icons and HUD elements atlased into a single sprite sheet. **Runtime format for UI sprite sheet**: **RGBA8 UNORM (uncompressed)** — DXT5 compression is prohibited for UI atlases because DXT5's 4×4-pixel block boundaries produce visible compression artifacts on sharp 1-pixel icon outlines and text glyphs, and UI elements are rendered at exact screen pixel resolution (no LOD sampling) where compression quality loss is most visible. RGBA8 at 2048×2048 = 16 MB VRAM (2048 × 2048 × 4 bytes). This is a deliberate budget allocation for UI sharpness. **UI sprite sheet upload path**: the UI sprite sheet must be uploaded via `glTexImage2D` with `GL_RGBA8` internal format (not DXT). The `glTexImage2D` source format and type must be `GL_RGBA, GL_UNSIGNED_BYTE` — `loadTexture()` may hard-code these values because `nvcompress -rgba` (the sole permitted export tool for `_ui.dds`) guarantees true RGBA byte order in the DDS pixel data on all platforms. Do NOT use `GL_BGRA` as the source format. Using `GL_BGRA` would be necessary only if the DDS were authored with Compressonator's ARGB_8888 format, which stores pixels in BGRA byte order on little-endian platforms; Compressonator is prohibited precisely to avoid this platform-dependent source-format selection at upload time. Mipmapping must be disabled: call `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0)` before `glTexImage2D` to prevent driver-side mip generation. Failure to set `GL_TEXTURE_MAX_LEVEL = 0` may cause the driver to generate a full mip chain (16 MB × 1.33 ≈ 21 MB), violating the 16 MB budget assumption. The `glTexImage2D` call must NOT be followed by `glGenerateMipmap`. `GL_TEXTURE_MIN_FILTER` must be set to `GL_LINEAR` (not `GL_LINEAR_MIPMAP_LINEAR`) since there is only one mip level.
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

`shader_constants.h` must include the following compile-time range guard. The bound is 15 (not 31) because the per-stage (fragment shader) minimum of `GL_MAX_TEXTURE_IMAGE_UNITS` is 16 in OpenGL 3.3 core — texture units 0–15 are guaranteed available per stage. The combined minimum (`GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS`) is 80 for OpenGL 3.x and is the wrong limit to check here; shaders bind textures per stage, so the per-stage minimum governs the safe upper bound:

```cpp
static_assert(kTexUnitBillboard <= 15,
    "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)");
```

These unit assignments are **fixed across all shaders** — terrain, building, vehicle, and billboard shaders all use the same unit-to-role mapping. A shader that needs only a subset (e.g., buildings don't use splat maps) simply does not declare the unused `uniform sampler2D` slot; the unused unit binding is harmless. Deviating from this table requires updating `shader_constants.h` AND all GLSL files that reference the affected unit — document any deviation explicitly.

**Important: 4 terrain detail layers, 4 texture units (5–8).** The splat map (unit 4) encodes 4 blend weights (RGBA channels), one per layer. Each layer corresponds to one terrain detail texture at a dedicated texture unit (5, 6, 7, 8). Units 5–8 must map 1-to-1 with splat-map channels R, G, B, A respectively. Combining two layers into one unit (e.g., assigning layers 0–3 to units 5–7) is incorrect and will break the terrain shader blend logic. All 4 terrain layer constants (`kTexUnitTerrainLayer0` through `kTexUnitTerrainLayer3`) must be defined in `shader_constants.h` as consecutive values 5, 6, 7, 8.

**Billboard atlas is sRGB** (unit 9): The billboard imposter atlas contains pre-rendered diffuse color data and must be uploaded via the raw-GL sRGB path (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), consistent with all other diffuse-color textures. Uploading the billboard atlas as linear DXT5 (`GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`) produces washed-out color at the billboard LOD transition (linear math on gamma-encoded data).

- Atlas layout must be designed before modeling begins, as a joint responsibility of `graphics-artist-2d-texture` and `graphics-dev-irrlicht`

#### Billboard Imposter Atlas

Small buildings and props use a pre-baked imposter atlas at LOD2 (beyond 100 m). The atlas is authored by the 2D texture artist from renders of the LOD1 mesh:

- **Atlas format**: DDS DXT5 **sRGB** (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), **1024×128 px**, 8 frames of 128×128 px each arranged in a 1×8 horizontal strip. Uploaded via the raw-GL sRGB path (same as diffuse building textures): `glGenTextures` → `glBindTexture` → `glCompressedTexImage2D` with `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`. **Never** use Irrlicht's `addTexture()` path for the billboard atlas — it produces a linear internal format. Alpha channel encodes non-rectangular silhouette cutouts. **Alpha convention: straight (unassociated) alpha** — RGB channels store the full-intensity billboard render (flat ambient-only), and alpha stores the cutout mask independently. Do NOT premultiply alpha: premultiplied alpha would darken billboard edges during DXT5 compression block rounding, producing a dark fringe on the silhouette. The billboard shader must use `vec4 texel = texture(billboardAtlas, uv); if (texel.a < 0.5) discard;` for cutout rendering (alpha test at 0.5 threshold, not alpha blending). This avoids transparency sorting issues with billboards rendered at varying camera angles.
- **Bake angles**: 8 directions every 45° around the vertical axis (0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°). The runtime `LODNode` selects the nearest 45° frame based on the angle between the camera and the building's facing direction.
- **Bake elevation**: **45° below horizontal (camera pitch = −45°)** — the midpoint of the [−70°, −20°] camera pitch operating range. This minimises average mismatch error across all valid camera pitches. "Above horizon" wording is incorrect for this convention; the camera is always looking downward in city view. Sign-off status: CONFIRMED — bake midpoint −45° and camera pitch operating range [−70°, −20°] are final per `architecture/asset-standards/3d-model-standards.md` Camera Pitch Range section. Phase 9 billboard authoring may proceed on this basis.
- **Bake lighting**: flat ambient-only (no directional light). This prevents sun-angle mismatch artifacts at different times of day. The runtime shader does not apply directional lighting to billboard quads (V1 scope).
- **Mip chain**: 4-level mandatory (1024×128 → 512×64 → 256×32 → 128×16); set `GL_TEXTURE_MAX_LEVEL = 3`. Validators checking billboard mip count must read the `dwMipMapCount` field from the standard DDS header: `dwMipMapCount` is at **byte offset 28** in the DDS header. The value must be >= 4 to satisfy the required 4-level mip chain. A `dwMipMapCount` of 0 or 1 indicates no mip chain was generated — do not commit that file.
- **Texture wrap mode**: Set `GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE` and `GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE` on billboard atlas textures. The default `GL_REPEAT` wrap mode causes the atlas strip to tile at the strip boundary, producing a ghost frame from the opposite end of the 1×8 strip when the UV quad samples near the horizontal edge. Clamp-to-edge eliminates this artifact.
- **Cell padding**: 8 texels per-cell border on each edge of each 128×128 frame — frames are packed edge-to-edge within the 1024×128 strip so the effective gutter between frames equals 16 texels (8 from each neighbour). **Border halves per mip level**: the 8-texel border at mip 0 halves at each successive mip: **8 px (mip 0) → 4 px (mip 1) → 2 px (mip 2) → 1 px (mip 3)**. At mip 3 the 1-texel border is the minimum safe margin to prevent bleed between adjacent frames in the strip. This is why a 4-level mip chain (clamped at `GL_TEXTURE_MAX_LEVEL = 3`) is the maximum safe chain for an 8-texel border — adding a 5th mip level would produce a sub-texel (0.5 px) border, which is insufficient to prevent bleed.
- **Usable content area per frame**: **112×112 px** at mip level 0 (128 px minus 8 px border on each of the 4 sides: 128 − 8 − 8 = 112). The building silhouette, windows, and all visible geometry must be authored to fit within the 112×112 usable area of each frame. Content that bleeds into the 8 px border zone will be clipped by mip-level bleeding or the clamp-to-edge wrap mode at far distances. **Mip level progression (authoring minimum legibility requirement)**:
  - Mip 0: 1024×128 — 8 × 128×128, usable **112×112 px** per frame
  - Mip 1: 512×64 — 8 × 64×64, usable **56×56 px** per frame
  - Mip 2: 256×32 — 8 × 32×32, usable **28×28 px** per frame
  - Mip 3: 128×16 — 8 × 16×16, usable **14×14 px** per frame (border shrinks to 1 px/side at this mip level; effective content is 14×14 px)
  At mip level 3 (used beyond ~400 m view distance), each building imposter is represented by 14×14 usable pixels. **Authoring requirement**: The primary silhouette of the building must be recognisable at 14×14 px — verified by downscaling a single frame to 14×14 in the DCC tool before finalising LOD2 billboard art. Fine details (windows, ledges) are not required to be legible at mip 3; gross building form (tower vs low-rise, vertical vs horizontal dominant shape) must remain distinguishable.
- **Naming**: `<asset_name>_billboard.dds`. Small building / prop assets must NOT ship a `_lod2.b3d` file — the billboard DDS is the LOD2 asset for these categories.

**Phase 1 dated sign-off record** (required before Phase 1 exit):

> Phase 1 sign-off — 2026-02-21
>
> Billboard imposter atlas format accepted for Phase 1:
>
> - Format: 1024×128 DXT5 sRGB (`GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT`), uploaded via
>   raw-GL path (`glGenTextures` → `glBindTexture` → `glCompressedTexImage2D`).
> - Frames: 8 frames of 128×128 px in a 1×8 horizontal strip.
> - Usable content area: 112×112 px per frame (8-texel border on all four sides of each frame).
> - Mip chain: 4 levels mandatory (1024×128 → 512×64 → 256×32 → 128×16);
>   `GL_TEXTURE_MAX_LEVEL = 3` set at runtime.
> - Alpha convention: straight (unassociated) alpha; RGB stores full-intensity ambient render,
>   alpha stores cutout mask; do NOT premultiply.
> - DX10 extended header required: DDS files for the billboard atlas must carry a DX10
>   extended header (FourCC `DX10` at byte offset 84) with DXGI_FORMAT set to
>   `DXGI_FORMAT_BC3_UNORM_SRGB` (value 78). A file lacking a DX10 header cannot encode
>   sRGB intent in a machine-readable form and must be rejected by the Phase 5 validator.
>
> Mip-generation tool differences (authoring note):
>
> - `Compressonator -miplevels 4`: generates exactly 4 mip levels in the DDS file on disk.
>   The `dwMipMapCount` field in the DDS header will be 4. This satisfies the `>= 4` check.
> - `nvcompress -color -bc3`: generates a full mip chain down to 1×N (11 levels for
>   1024×128). The `dwMipMapCount` field will be > 4. GPU reads are capped at 4 levels via
>   `GL_TEXTURE_MAX_LEVEL = 3` at runtime, so in-game rendering is correct. This also
>   satisfies the `>= 4` check.
>
> Phase 5 validator MUST use a unified `>= 4` mip count threshold regardless of authoring
> tool. Compressonator produces exactly 4 levels (`mip_count == 4`, passes `>= 4` check);
> nvcompress generates a full mip chain (`mip_count > 4`, also passes `>= 4` check).
> Tool-conditional `== 4` logic is prohibited — a strict equality check would incorrectly
> reject valid nvcompress-authored atlases.
>
> Preferred authoring tool: Compressonator (`compressonatorcli -fd BC3 -miplevels 4`) for
> exact 4-level DDS output. nvcompress (`nvcompress -color -bc3`) is also acceptable; both
> pass the unified `>= 4` validator check.
>
> Phase 9 billboard asset authoring may proceed on this basis.
>
> Signed: graphics-artist-2d-texture — 2026-02-21.

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

  **Per-chunk splat map pixel dimensions**: 1 texel per terrain tile. Formula: `(chunk_size_m / tile_size_m)² px`.

  | Map configuration | Chunk size | Tile size | Splat map per chunk |
  |---|---|---|---|
  | V1 (default) | 64 m | 4 m | **16×16 px** |
  | (future) 128 m chunk | 128 m | 4 m | 32×32 px |

  **V1 deliverable**: author and commit one `terrain_splat_test.png` (16×16 RGBA PNG) as the terrain
  test fixture. Channels: R=0.8, G=0.1, B=0.05, A=0.05 (grassland-dominant blend). This fixture is
  used by `TextureCache::loadSplatMap()` unit tests.
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
  | R (red) | Base (biome-specific) | 5 | `kTexUnitTerrainLayer0` | 255 across entire map |
  | G (green) | Asphalt | 6 | `kTexUnitTerrainLayer1` | 0 |
  | B (blue) | Soil | 7 | `kTexUnitTerrainLayer2` | 0 |
  | A (alpha) | Concrete | 8 | `kTexUnitTerrainLayer3` | 0 |

  **"Base" channel semantics**: the R channel holds the primary ground cover for the current biome.
  In V1 (grassland biome): R = grass texture. In future desert biome: R = sand texture. The channel
  is labeled "base" rather than "grass" because the splat map architecture is biome-agnostic — only
  the texture asset assigned to R changes between biomes; the splat map layout, UV coordinates, and
  shader binding are identical across all biomes. V1 ships one biome (grassland). **Artist rule**:
  when authoring a new biome variant, swap only the R-channel texture asset file; do NOT modify the
  splat map or shader.

  This assignment is **fixed before texture production begins** and must match the texture unit binding order in the terrain shader (`kTexUnitTerrainLayer0` through `kTexUnitTerrainLayer3`). Any mismatch between the splat map channel order and the texture unit binding order causes incorrect material blending across the entire terrain surface with no compile-time or runtime error. **The R channel (base terrain layer) is initialized to 255 across the entire map** so that unpainted tiles default to the base material and the blend-weight normalization divisor is always non-zero.

- **Blend shader**: Sample splat map at tile UV; use R/G/B/A as blend weights for four detail textures; normalize weights to sum to 1.0; output weighted sum of sampled detail texture colors. **Division-by-zero protection**: If all four RGBA channels are simultaneously 0.0, the normalization divisor is 0. Shader must guard: `if (sum < 0.001) { weights = vec4(1,0,0,0); }` (fall back to base layer R). **Authoring rule**: The R channel (base terrain layer) must be initialized to 255 across the entire splat map before any painting begins, ensuring the divisor is always non-zero on unpainted tiles.
- **Initial splat map state**: the default splat map PNG for a new terrain chunk must have R channel = 255, G = 0, B = 0, A = 0 across the entire image — this initialises all terrain to the base material layer (R channel; grass in V1 grassland biome). Before committing any splat map asset, the artist or export script must verify this initialization (e.g., via `python -c "from PIL import Image; img = Image.open('splat.png'); assert img.getextrema() == ((255,255),(0,0),(0,0),(0,0))"` or equivalent). A splat map with incorrect initial state causes the terrain shader to render black or undefined material blends at runtime.
- **Terrain normal map intensity — hard vs soft surface distinction**: Terrain normal maps must be authored with surface-appropriate intensity to avoid plastic or unnatural shading. Apply to all terrain `_n` textures:
  - **Hard surfaces** (concrete, brick, asphalt, stone): strong normal intensity; use Z-scale **1.0–1.5** in the DCC tool normal baker. High-frequency sharp detail is appropriate — pavement cracks, mortar lines, stone facets.
  - **Soft surfaces** (grass, soil, dirt, sand): gentle normal intensity; use Z-scale **0.3–0.7**. Over-strong normals on soft terrain surfaces produce a plastic or moulded appearance and must be avoided. Subtle micro-undulation (gentle height variation, soil clumping) is the correct authoring target.
  - Validate by rendering a sphere lit from a single directional light: hard-surface normals should produce sharp specular highlights along surface features; soft-surface normals should produce a diffuse, low-contrast shading variation with no sharp specular peaks.
- **CI validation requirement (`validate_assets.py`)**: terrain normal map DDS files (`*_n.dds`
  matching terrain naming pattern) must have `dwMipMapCount >= 4`. The `validate_assets.py` script
  must check this field in the DDS header (bytes 28–31 of the DDS file, little-endian uint32). A
  normal map with `dwMipMapCount = 1` (driver-generated mips) is a hard asset error — at lower mips,
  driver-generated bilinear downsampling denormalizes the tangent-space normal vectors, producing
  incorrect lighting at medium and far distances. The bicubic pre-bake preserves vector normalization
  across the mip chain. **Normal map mip authoring requirement**: terrain normal maps must be authored
  with a bicubic-downsampled mip chain (using the DCC tool's normal-map-aware mip baker, not
  driver-generated bilinear mips) to ensure tangent-space normal vectors remain unit-length at every
  mip level.
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

### UI Sprite Sheet Cell Layout (`hud_sprites_ui.dds` → `hud_sprites_ui.png`)

**Authoring format**: `hud_sprites_ui.dds` — 2048×2048 RGBA8 UNORM, no mip chain, authored
with `nvcompress -rgba -nomips`. This is the source/authoring format; it is converted to PNG
before shipping (see Runtime asset path below). Linear color space — do NOT author in sRGB.

**nvcompress byte-order verification (mandatory before first DDS commit)**: `nvcompress
-rgba -nomips` guarantees RGBA byte order on NVTT 2.x and confirmed NVTT 3.x builds. Before
committing any `hud_sprites_ui.dds`, run the byte-order spike: create a 2×2 PNG with
`R=255, G=0, B=0, A=255`, convert it with `nvcompress -rgba -nomips`, then decode the
resulting DDS and verify the first pixel reads R=255, G=0, B=0. If it reads B=255 (BGRA
output), that nvcompress build is non-standard and must NOT be used for this asset.
**Approved fallback when nvcompress byte-order check fails**: export `hud_sprites_ui.png`
directly from the DCC tool as a lossless 32-bit RGBA PNG and skip the DDS intermediate
step entirely. The PNG is pixel-identical to a correct RGBA8 UNORM DDS for uncompressed
data, and Irrlicht loads it correctly via `getTexture()`. The DDS intermediate step exists
only to validate RGBA byte order; when that validation fails the direct-PNG export is the
canonical alternative. Do NOT use Compressonator as a substitute under any circumstances.

**Runtime format**: `hud_sprites_ui.png` — the DDS converted to PNG for loading via Irrlicht's
`IVideoDriver::getTexture()`. `IrrlichtUIBackend` stores the result as `irr::video::ITexture*`
and uses `IGUIButton::setImage()` for per-button icon display.

**DDS→PNG conversion**: Convert the authored `hud_sprites_ui.dds` to `hud_sprites_ui.png`
using any standard image-processing tool. Acceptable tools:

- **GIMP**: File → Export As → select PNG, disable all gamma/ICC-profile options on export
- **ImageMagick** (modern `magick` CLI, ImageMagick 7+):
  `magick hud_sprites_ui.dds hud_sprites_ui.png`
- **ImageMagick** (legacy `convert` CLI, ImageMagick 6 and below):
  `convert hud_sprites_ui.dds hud_sprites_ui.png`
- Any other tool that preserves RGBA8 pixels without colour-space conversion

Do NOT use `nvcompress` for DDS→PNG conversion — `nvcompress` is a DDS compressor and cannot
decode DDS back to PNG. Do NOT apply gamma correction or sRGB encoding during conversion —
the source file is in linear colour space and the PNG must remain in linear colour space.

**Verification after conversion**: Confirm the output PNG is 2048×2048 RGBA8 with no mip
chain before committing. Use `python3` to spot-check pixel values:

```bash
python3 -c "
from PIL import Image
img = Image.open('hud_sprites_ui.png')
print('Size:', img.size)          # must be (2048, 2048)
print('Mode:', img.mode)          # must be 'RGBA'
# Spot-check: sample a known-coloured cell (e.g. top-left pixel of cell 0,0 — should be
# transparent background if icon is centered in cell)
print('Pixel 0,0:', img.getpixel((0, 0)))
"
```

If Pillow is not installed, use `identify -verbose hud_sprites_ui.png | grep -E 'Geometry|Type|Colorspace'`
(ImageMagick `identify` command) and verify the output reports 2048x2048 and TrueColorAlpha.
The reported colorspace value may read "sRGB" as an ImageMagick display convention even when
no ICC profile is embedded — this is harmless. What matters is that the PNG contains no
embedded ICC profile and that the pixel data has not been gamma-corrected during export.
Confirm with `identify -verbose hud_sprites_ui.png | grep -i profile` — the output must be
empty (no embedded profiles), meaning the file carries no colour management metadata.

**DDS FourCC field for UI sprite sheet (authoring note)**: When verifying the DDS output
from `nvcompress -rgba -nomips`, the FourCC field at byte offset 84 may read `b'DX10'`
(NVTT 3.x writes a DX10 extended header) or a legacy uncompressed FourCC value. Both
outcomes are acceptable for a linear RGBA8 UI texture. The DX10 header is NOT an sRGB
indicator by itself — the DXGI format field within the DX10 header determines sRGB. The
only prohibited outcome is `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` (format value 29). When a
DX10 header is present (FourCC = `b'DX10'`), verify the DXGI format: the value at byte
offset 128 must be 28 (`DXGI_FORMAT_R8G8B8A8_UNORM`, linear) and must NOT be 29 (sRGB).
A file produced by `nvcompress -rgba -nomips` from a source PNG with no embedded ICC
profile will always carry format 28 and never 29; this check is a belt-and-suspenders
verification step, not an expected failure path.

**Format recap**: 2048×2048 RGBA8, no mip chain, linear color space. Full cell layout in the
sections below.

**Cell grid**: 32×32 uniform cells at **64×64 px each** (2048 / 64 = 32 columns; 32 rows).
No inter-cell padding is required — mipmapping is disabled for the UI sprite sheet, so
there is no mip-bleed between adjacent cells. Each icon is drawn within the full 64×64 px
cell area. Icons that are smaller than 64×64 px (e.g. the 48×48 toolbar icons) must be
centered within the cell with transparent fill on all four sides.

**Cell coordinate system**: `(col, row)` zero-indexed from the top-left corner.
UV bottom-left origin (OpenGL convention): `U = col / 32`, `V = (31 - row) / 32`.
UV top-left origin (DDS / direct pixel indexing): pixel `(col * 64, row * 64)`.

**Sprite handle encoding**: The integer sprite handle passed to `IUIBackend::setElementImage()`
is encoded as `col + row * 32`. This packs both coordinates into a single integer that
`IrrlichtUIBackend` decodes as `col = handle % 32`, `row = handle / 32`. The encoding
produces handles 0–1023 for the 32×32 grid. No handle value exceeds 1023 in V1.

**Runtime asset path (authoritative)**: `assets/textures/ui/hud_sprites_ui.png`

`IrrlichtUIBackend` loads this file internally via `m_driver->getTexture("assets/textures/ui/hud_sprites_ui.png")` during its own initialization (before any `UIManager` panel code runs).
This is an `IrrlichtUIBackend`-internal operation — `UIManager` does NOT call
`IUIBackend::loadTexture("assets/textures/ui/hud_sprites_ui.png")` and does NOT store a
sprite-sheet texture handle. `UIManager` only calls `setElementImage(buttonHandle, kSpriteXxx)`,
where `kSpriteXxx` is a `constexpr uint32_t` cell-index from `src/ui/hud_sprite_ids.h`.
`IrrlichtUIBackend::setElementImage()` decodes the second argument as `col = handle % 32;
row = handle / 32;` and applies the corresponding UV sub-rect of the already-loaded sprite
sheet to the button's image region.

The `kSprite*` integer constants are NOT separate per-icon file paths — they are UV cell-index
values. `IUIBackend::loadTexture()` is NOT called for sprite-sheet icon swaps. It may be
called for other purposes (e.g. non-sprite-sheet textures in future phases) but Phase 9b
`UIManager` code does not call it.

**`MockUIBackend` test contract**: In unit tests, `MockUIBackend::setElementImage()` is
called with the actual `kSprite*` integer constants as the second argument (e.g.
`kSpriteZoneResLowInactive = 96`, `kSpriteZoneResLowActive = 64`). Tests verify sprite-swap
behaviour by asserting `EXPECT_CALL(backend_, setElementImage(zone_btn[i], kSpriteZoneResLowInactive + col + row * 3))`.
`MockUIBackend::loadTexture()` is NOT expected to be called during Phase 9b UIManager
construction for sprite-sheet icons.

Named constants for all V1 sprite handles are defined in
`src/ui/hud_sprite_ids.h` (a non-class constants header, snake\_case filename per CLAUDE.md).
Every `IUIBackend::setElementImage()` call in production code MUST use a named constant from
this header — raw integer literals are prohibited. The header is auto-generated from this
table by `tools/export_textures.py` (Phase 9 deliverable); until then it is hand-maintained.

#### Cell Assignment Table — Row 0: Toolbar Tool-Mode Icons (active state)

These are the filled, accent-bordered versions displayed on the toolbar button when the
tool is active. Each is a 48×48 px icon centered within a 64×64 px cell.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 0 | 0 | 0 | `kSpriteToolZoneActive` | Zone tool — active state (filled, accent border) |
| 1 | 1 | 0 | `kSpriteToolRoadActive` | Road tool — active state |
| 2 | 2 | 0 | `kSpriteToolUtilitiesActive` | Utilities tool — active state |
| 3 | 3 | 0 | `kSpriteToolDemolishActive` | Demolish tool — active state |
| 4 | 4 | 0 | `kSpriteToolQueryActive` | Query tool — active state |

#### Cell Assignment Table — Row 1: Toolbar Tool-Mode Icons (inactive state)

These are the outline-only versions displayed on the toolbar button when the tool is
not selected.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 32 | 0 | 1 | `kSpriteToolZoneInactive` | Zone tool — inactive state (outline, no border) |
| 33 | 1 | 1 | `kSpriteToolRoadInactive` | Road tool — inactive state |
| 34 | 2 | 1 | `kSpriteToolUtilitiesInactive` | Utilities tool — inactive state |
| 35 | 3 | 1 | `kSpriteToolDemolishInactive` | Demolish tool — inactive state |
| 36 | 4 | 1 | `kSpriteToolQueryInactive` | Query tool — inactive state |

#### Cell Assignment Table — Row 2: Zone Sub-Panel Button Icons (active / selected state)

These are the filled, accent-bordered versions displayed on a zone sub-panel button when
that zone type + density tier is the current selection. 9 icons for 3 zone types ×
3 density tiers. Each is a 56×40 px icon (matches the 64×40 px button minus 4 px margin
per side on width; full height used) centered within a 64×64 px cell.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 64 | 0 | 2 | `kSpriteZoneResLowActive` | Residential Low density — active/selected |
| 65 | 1 | 2 | `kSpriteZoneComLowActive` | Commercial Low density — active/selected |
| 66 | 2 | 2 | `kSpriteZoneIndLowActive` | Industrial Low density — active/selected |
| 67 | 3 | 2 | `kSpriteZoneResMedActive` | Residential Medium density — active/selected |
| 68 | 4 | 2 | `kSpriteZoneComMedActive` | Commercial Medium density — active/selected |
| 69 | 5 | 2 | `kSpriteZoneIndMedActive` | Industrial Medium density — active/selected |
| 70 | 6 | 2 | `kSpriteZoneResHighActive` | Residential High density — active/selected |
| 71 | 7 | 2 | `kSpriteZoneComHighActive` | Commercial High density — active/selected |
| 72 | 8 | 2 | `kSpriteZoneIndHighActive` | Industrial High density — active/selected |

#### Cell Assignment Table — Row 3: Zone Sub-Panel Button Icons (inactive / outline state)

Outline-only versions for unselected zone sub-panel buttons.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 96 | 0 | 3 | `kSpriteZoneResLowInactive` | Residential Low density — inactive/outline |
| 97 | 1 | 3 | `kSpriteZoneComLowInactive` | Commercial Low density — inactive/outline |
| 98 | 2 | 3 | `kSpriteZoneIndLowInactive` | Industrial Low density — inactive/outline |
| 99 | 3 | 3 | `kSpriteZoneResMedInactive` | Residential Medium density — inactive/outline |
| 100 | 4 | 3 | `kSpriteZoneComMedInactive` | Commercial Medium density — inactive/outline |
| 101 | 5 | 3 | `kSpriteZoneIndMedInactive` | Industrial Medium density — inactive/outline |
| 102 | 6 | 3 | `kSpriteZoneResHighInactive` | Residential High density — inactive/outline |
| 103 | 7 | 3 | `kSpriteZoneComHighInactive` | Commercial High density — inactive/outline |
| 104 | 8 | 3 | `kSpriteZoneIndHighInactive` | Industrial High density — inactive/outline |

#### Cell Assignment Table — Row 4: Utilities Sub-Panel Button Icons (active / selected state)

4 icons for 4 service building types, one per column. Each is sized to fill most of the
64×64 px cell (icon art at 56×40 px, centered). Column order matches the 2×2 sub-panel
grid: cols 0–3 map to (col0 row0)=PowerPlant, (col1 row0)=WaterTower,
(col0 row1)=FireStation, (col1 row1)=PoliceStation.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 128 | 0 | 4 | `kSpriteUtilPowerActive` | Power Plant — active/selected |
| 129 | 1 | 4 | `kSpriteUtilWaterActive` | Water Tower — active/selected |
| 130 | 2 | 4 | `kSpriteUtilFireActive` | Fire Station — active/selected |
| 131 | 3 | 4 | `kSpriteUtilPoliceActive` | Police Station — active/selected |

#### Cell Assignment Table — Row 5: Utilities Sub-Panel Button Icons (inactive / outline state)

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 160 | 0 | 5 | `kSpriteUtilPowerInactive` | Power Plant — inactive/outline |
| 161 | 1 | 5 | `kSpriteUtilWaterInactive` | Water Tower — inactive/outline |
| 162 | 2 | 5 | `kSpriteUtilFireInactive` | Fire Station — inactive/outline |
| 163 | 3 | 5 | `kSpriteUtilPoliceInactive` | Police Station — inactive/outline |

#### Cell Assignment Table — Row 6: Active Tool Indicator Icons

These are the 32×32 px icons displayed in the active tool badge (virtual x: 8–72 px,
y: 752–784 px) immediately below the demand bar. One icon per tool mode plus a "no tool"
placeholder.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 192 | 0 | 6 | `kSpriteIndicatorNone` | No tool active — neutral / camera icon |
| 193 | 1 | 6 | `kSpriteIndicatorZone` | Zone tool active indicator |
| 194 | 2 | 6 | `kSpriteIndicatorRoad` | Road tool active indicator |
| 195 | 3 | 6 | `kSpriteIndicatorUtilities` | Utilities tool active indicator |
| 196 | 4 | 6 | `kSpriteIndicatorDemolish` | Demolish tool active indicator |
| 197 | 5 | 6 | `kSpriteIndicatorQuery` | Query tool active indicator |

#### Cell Assignment Table — Row 7: Cursor-Shape Icons (deferred — post-Phase 10)

OS-level cursor shape changes require `IUIBackend::setMouseCursor()`, which does not exist
in the V1 `IUIBackend` interface and is not a Phase 10 deliverable. These cells are reserved
stubs. **Phase 10 artists must leave all row 7 cells fully transparent (RGBA = 0,0,0,0) in
the committed `hud_sprites_ui.png`.** The `kSpriteCursor*` constants in `hud_sprite_ids.h`
are defined so that future phases can fill the cells without renumbering any existing handle.
No cursor icon art is required until `setMouseCursor()` is added to the `IUIBackend` interface
in a future phase.

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 224 | 0 | 7 | `kSpriteCursorDefault` | Default arrow cursor |
| 225 | 1 | 7 | `kSpriteCursorZone` | Zone crosshair with zone-color tint |
| 226 | 2 | 7 | `kSpriteCursorRoad` | Road-segment cursor icon |
| 227 | 3 | 7 | `kSpriteCursorUtilities` | Wrench cursor icon |
| 228 | 4 | 7 | `kSpriteCursorDemolish` | X-marker cursor icon |
| 229 | 5 | 7 | `kSpriteCursorQuery` | Magnifying glass cursor icon |

#### Cell Assignment Table — Row 8: Minimap Overlay Toggle Icons (active state)

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 256 | 0 | 8 | `kSpriteOverlayServiceCoverageActive` | Service coverage overlay — active (filled, accent border) |

#### Cell Assignment Table — Row 9: Minimap Overlay Toggle Icons (inactive state)

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 288 | 0 | 9 | `kSpriteOverlayServiceCoverageInactive` | Service coverage overlay — inactive (outline) |

#### Cell Assignment Table — Row 10: Notification / HUD Miscellaneous Icons

| Handle | Col | Row | Constant name | Description |
|---|---|---|---|---|
| 320 | 0 | 10 | `kSpriteNotificationBell` | Notification bell (badge-eligible) |
| 321 | 1 | 10 | `kSpriteClockIcon` | Clock icon for grace period indicator |
| 322 | 2 | 10 | `kSpriteUnsavedDot` | Unsaved-changes amber dot (16×16 px icon, centered in cell) |
| 323 | 3 | 10 | `kSpriteUndoIcon` | Undo button icon (↩) |

#### Reserved Rows 11–31

Rows 11–31 (handles 352–1023) are reserved for future phases. Do not assign cells outside
the ranges above without updating this table and the `hud_sprite_ids.h` header in the same
commit.

#### Authoring Notes for Phase 9b Icons

**Zone sub-panel icon visual convention** (applies to rows 2 and 3):

- **Active state** (rows 2): Filled solid icon using the zone's canonical colour
  (Residential = green `#33BB44`; Commercial = blue `#3366CC`; Industrial = yellow-orange
  `#CCAA22`). A 2 px accent-colour border surrounds the icon area within the 64×64 cell.
  Density tier visual differentiation:
  - Low: single-storey building silhouette (1 floor, wide footprint)
  - Medium: mid-rise building silhouette (3–4 floors)
  - High: tall building silhouette (6+ floors, narrow footprint)
- **Inactive state** (row 3): Outline-only version of the same silhouette in neutral grey
  `#888888`. No accent border. Fill is fully transparent (alpha = 0) inside the outline.

**Utilities sub-panel icon visual convention** (applies to rows 4 and 5):

- **Active state** (row 4): Filled icon using neutral white `#DDDDDD` on a mid-grey
  `#444444` background fill within the cell. 2 px accent-colour border.
  - Power Plant: lightning-bolt symbol
  - Water Tower: cylindrical tower silhouette
  - Fire Station: fire/flame symbol
  - Police Station: badge/shield symbol
- **Inactive state** (row 5): Outline-only in neutral grey `#888888`. No accent border.
  Transparent fill.

**Toolbar tool-mode icon visual convention** (applies to rows 0 and 1):

- Icons are 48×48 px, centered within the 64×64 px cell (8 px transparent margin on all sides).
- **Active state** (row 0): Filled icon, accent-colour border drawn within the 48×48 area.
  Each tool has a unique shape:
  - Zone: zoning grid square (four quadrants — R/C/I colour-coded)
  - Road: single road segment (straight horizontal line with lane markings)
  - Utilities: wrench silhouette
  - Demolish: X mark (two crossing diagonal lines)
  - Query: magnifying glass
- **Inactive state** (row 1): Same shapes, outline-only, neutral grey `#888888`.

**Active tool indicator convention** (row 6): All icons are 32×32 px, centered in 64×64 px cell.
Same design as the corresponding toolbar active-state icon but at 32×32 px output size —
use the same master artwork, scaled down for this position.

**sRGB authoring**: All icons must be authored in **linear color space** and exported as a
linear RGBA PNG. The shipped runtime file is `hud_sprites_ui.png` (PNG, linear RGBA8), loaded
by Irrlicht's `getTexture()`. The intermediate DDS source (`hud_sprites_ui.dds`) is produced by
running `nvcompress -rgba -nomips` on the linear PNG export. Icons authored in sRGB and left
uncorrected will appear over-darkened at runtime. The correct authoring workflow in Photoshop
or Krita: work in the file's native color space, disable color-space conversion on PNG export,
and ensure the DCC tool's working color space is set to linear
(no ICC profile embed needed for the UI sprite sheet — the upload path assumes linear).

**`hud_sprite_ids.h` header stub for Phase 9b** (required before Phase 9b implementation
begins — hand-authored until `tools/export_textures.py` generates it):

```cpp
// src/ui/hud_sprite_ids.h
// Sprite handle constants for hud_sprites_ui.dds (2048×2048, 32×32 cell grid, 64×64 px/cell).
// Handle encoding: col + row * 32. Authoritative table: architecture/asset-standards/2d-texture-standards.md
// DO NOT use raw integer literals in IUIBackend::setElementImage() calls.
#pragma once
#include <cstdint>

// Row 0 — Toolbar tool-mode icons (active state)
constexpr uint32_t kSpriteToolZoneActive       =  0;
constexpr uint32_t kSpriteToolRoadActive        =  1;
constexpr uint32_t kSpriteToolUtilitiesActive   =  2;
constexpr uint32_t kSpriteToolDemolishActive    =  3;
constexpr uint32_t kSpriteToolQueryActive       =  4;

// Row 1 — Toolbar tool-mode icons (inactive state)
constexpr uint32_t kSpriteToolZoneInactive      = 32;
constexpr uint32_t kSpriteToolRoadInactive      = 33;
constexpr uint32_t kSpriteToolUtilitiesInactive = 34;
constexpr uint32_t kSpriteToolDemolishInactive  = 35;
constexpr uint32_t kSpriteToolQueryInactive     = 36;

// Row 2 — Zone sub-panel button icons (active/selected state; order: col=zone R/C/I, row=density Low/Med/High)
constexpr uint32_t kSpriteZoneResLowActive      = 64;
constexpr uint32_t kSpriteZoneComLowActive      = 65;
constexpr uint32_t kSpriteZoneIndLowActive      = 66;
constexpr uint32_t kSpriteZoneResMedActive      = 67;
constexpr uint32_t kSpriteZoneComMedActive      = 68;
constexpr uint32_t kSpriteZoneIndMedActive      = 69;
constexpr uint32_t kSpriteZoneResHighActive     = 70;
constexpr uint32_t kSpriteZoneComHighActive     = 71;
constexpr uint32_t kSpriteZoneIndHighActive     = 72;

// Row 3 — Zone sub-panel button icons (inactive/outline state)
constexpr uint32_t kSpriteZoneResLowInactive    = 96;
constexpr uint32_t kSpriteZoneComLowInactive    = 97;
constexpr uint32_t kSpriteZoneIndLowInactive    = 98;
constexpr uint32_t kSpriteZoneResMedInactive    = 99;
constexpr uint32_t kSpriteZoneComMedInactive    = 100;
constexpr uint32_t kSpriteZoneIndMedInactive    = 101;
constexpr uint32_t kSpriteZoneResHighInactive   = 102;
constexpr uint32_t kSpriteZoneComHighInactive   = 103;
constexpr uint32_t kSpriteZoneIndHighInactive   = 104;

// Row 4 — Utilities sub-panel button icons (active/selected state)
constexpr uint32_t kSpriteUtilPowerActive       = 128;
constexpr uint32_t kSpriteUtilWaterActive       = 129;
constexpr uint32_t kSpriteUtilFireActive        = 130;
constexpr uint32_t kSpriteUtilPoliceActive      = 131;

// Row 5 — Utilities sub-panel button icons (inactive/outline state)
constexpr uint32_t kSpriteUtilPowerInactive     = 160;
constexpr uint32_t kSpriteUtilWaterInactive     = 161;
constexpr uint32_t kSpriteUtilFireInactive      = 162;
constexpr uint32_t kSpriteUtilPoliceInactive    = 163;

// Row 6 — Active tool indicator badge icons (32×32 px, centered in 64×64 px cell)
constexpr uint32_t kSpriteIndicatorNone         = 192;
constexpr uint32_t kSpriteIndicatorZone         = 193;
constexpr uint32_t kSpriteIndicatorRoad         = 194;
constexpr uint32_t kSpriteIndicatorUtilities    = 195;
constexpr uint32_t kSpriteIndicatorDemolish     = 196;
constexpr uint32_t kSpriteIndicatorQuery        = 197;

// Row 7 — Cursor-shape icons (reserved; Phase 12 — IUIBackend::setMouseCursor() added in Phase 12)
constexpr uint32_t kSpriteCursorDefault         = 224;
constexpr uint32_t kSpriteCursorZone            = 225;
constexpr uint32_t kSpriteCursorRoad            = 226;
constexpr uint32_t kSpriteCursorUtilities       = 227;
constexpr uint32_t kSpriteCursorDemolish        = 228;
constexpr uint32_t kSpriteCursorQuery           = 229;

// Row 8 — Minimap overlay toggle icons (active state)
constexpr uint32_t kSpriteOverlayServiceCoverageActive   = 256;

// Row 9 — Minimap overlay toggle icons (inactive state)
constexpr uint32_t kSpriteOverlayServiceCoverageInactive = 288;

// Row 10 — Notification / HUD miscellaneous
constexpr uint32_t kSpriteNotificationBell      = 320;
constexpr uint32_t kSpriteClockIcon             = 321;
constexpr uint32_t kSpriteUnsavedDot            = 322;
constexpr uint32_t kSpriteUndoIcon              = 323;
```

**Zone sub-panel button array mapping for Phase 9b init()**: The 9 zone sub-panel buttons
are created at init() in (zone, density) order. The active and outline handles for a given
(zone, density) pair are computed as:

```cpp
// ZoneType::Residential=0, Commercial=1, Industrial=2
// DensityTier::Low=0, Medium=1, High=2
// zone_col = static_cast<int>(zoneType)      // 0, 1, or 2
// density_row = static_cast<int>(densityTier) // 0, 1, or 2
// Active handle:   kSpriteZoneResLowActive + zone_col + density_row * 3
// Inactive handle: kSpriteZoneResLowInactive + zone_col + density_row * 3
```

This formula works because the zone sub-panel icons are laid out as 3 columns (R/C/I) ×
3 rows (Low/Med/High) in the sprite sheet, matching the sub-panel's visual 3×3 grid. The
formula is valid only for the active/inactive zone-type rows (rows 2 and 3) where all 9
cells are contiguous in a 3-per-density-tier pattern.

**Utilities sub-panel button array mapping for Phase 9b init()**:

```cpp
// ServiceBuildingType::PowerPlant=0, WaterTower=1, FireStation=2, PoliceStation=3
// Active handle:   kSpriteUtilPowerActive   + static_cast<int>(type)
// Inactive handle: kSpriteUtilPowerInactive + static_cast<int>(type)
```

This formula is valid only if `ServiceBuildingType` enum values are sequential from 0.
Confirmed: the enum is defined in `src/interfaces/simulation_types.h` as
`{ PowerPlant, WaterTower, FireStation, PoliceStation }` with default sequential values.

**`IrrlichtUIBackend::setElementImage()` implementation contract**: The backend decodes
the sprite handle as `col = handle % 32; row = handle / 32;` and computes the UV rect:
`u0 = col / 32.0f; v0 = row / 32.0f; u1 = (col + 1) / 32.0f; v1 = (row + 1) / 32.0f;`
(top-left UV origin, matching the DDS pixel layout). The backend then applies the UV rect
to the button's image region. The `hud_sprites_ui.dds` texture must be bound to the UI
texture unit before any draw call that uses sprite-sheet icons.

### Sign-Off

This section holds required sign-off comment blocks for Phase gates. Each sign-off must be recorded
as a dated HTML comment appended here before the corresponding Phase work begins. Unsigned or
undated confirmations are not traceable and will not satisfy the blocking pre-condition.

**Phase 9 — Normal/Specular Map Sign-Off** (`graphics-artist-2d-texture`, required before any
LOD0/LOD1 UV authoring of building meshes begins):

<!-- SIGN-OFF: graphics-artist-2d-texture 2026-03-01 — confirmed all per-module normal and specular map source PNGs meet DXT5nm authoring spec -->

<!-- SIGN-OFF: graphics-artist-2d-texture 2026-03-01 — Billboard atlas format sign-off: confirmed DXT5 sRGB 1024×128 format (8 frames of 128×128), 4-level mip chain (GL_TEXTURE_MAX_LEVEL=3), 8-texel per-cell border (112×112 usable per frame), straight alpha, GL_CLAMP_TO_EDGE, uploaded via raw-GL sRGB path. All placeholder billboard DDS files use DX10 BC3_UNORM_SRGB (DXGI_FORMAT=78). -->
