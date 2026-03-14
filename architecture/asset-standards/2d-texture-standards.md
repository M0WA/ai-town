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
- **`IVideoDriver::getTexture()` cannot load DDS files** — Irrlicht 1.8.5 ships with its DDS
  image loader disabled by default (`_IRR_COMPILE_WITH_DDS_LOADER_` is commented out in
  `IrrCompileConfig.h`). Any call to `IVideoDriver::getTexture()` on a `.dds` path returns null.
  There is a secondary structural bug: `ddsBuffer` in `CImageLoaderDDS.h` contains a `void*
  surface` field (8 bytes on x86\_64), shifting `pixelFormat.fourCC` to file offset 88 instead
  of the DDS-spec offset 84. This would cause Irrlicht to misidentify DXT1 files as ARGB8888 on
  64-bit systems even if the loader were re-enabled. **Consequence**: textures loaded via
  `IVideoDriver::getTexture()` (the `TextureCache` linear pool, `BuildingAssetLoader`, and any
  other Irrlicht-path code) must use a format the built-in Irrlicht loaders support — PNG is the
  standard choice. The raw-GL `TextureCache::loadSRGB()` path (`glCompressedTexImage2D`) is NOT
  affected by this constraint because it reads the DDS file directly with a bespoke header parser
  and bypasses the Irrlicht image loader entirely.
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
| UI sprite sheet (RGBA8 uncompressed, no mip) | nvcompress | `nvcompress -rgb -nomips input.png output.dds` |
| UI sprite sheet (RGBA8 uncompressed, no mip) | Compressonator | `compressonatorcli -fd ARGB_8888 -nomipmap input.png output.dds` |

**IMPORTANT — nvcompress mip chain behaviour**: `nvcompress` does NOT support a `-miplevels N` (or `-mips N`) CLI parameter — no such flag exists in NVTT 2.x or NVTT 3.x. `nvcompress` always generates a **full mip chain** down to 1×1 (or 1×N for non-square textures) by default. For building diffuse, normal maps, and specular packed textures this is acceptable — the GPU sampler uses all levels and performance is optimal. For the **billboard atlas** (1024×128, 8-frame strip), a full chain produces 11 mip levels; the runtime sets `GL_TEXTURE_MAX_LEVEL = 3` to cap GPU reads at 4 levels, so in-game rendering is correct, but the DDS file on disk will contain extra levels. **If the Phase 5 validator enforces `mip_count == 4` exactly** (e.g. via `struct.unpack('<I', d[28:32])[0] == 4`), billboard atlases authored with `nvcompress` will fail that check. For billboard atlases requiring exactly 4 mip levels **in the DDS file**, use **Compressonator** with `-miplevels 4`.

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

### DDS Mip Chain Integrity

**Every mip level announced in `dwMipMapCount` MUST have its pixel data present in the DDS
file.** The `dwMipMapCount` field at byte offset 28 of the standard DDS header declares how many
mip levels exist. A DDS file whose `dwMipMapCount` is set to 4 but whose byte stream contains
data only for mip level 0 is a truncated file — it is invalid and must not be committed as a
runtime asset.

**Truncated DDS files cause a silent failure in `TextureCache::loadSRGB()`**: the bespoke header
parser reads `dwMipMapCount` and iterates that many mip levels; when it reaches a mip level
whose expected data is absent (or shorter than the declared size), it passes a dangling or
zero-length pointer to `glCompressedTexImage2D`. The GL driver silently accepts the call and
produces a **black surface** for the affected mip level. Depending on which mip level is
truncated, this manifests as:

- A black atlas across the entire city at the affected LOD distance (if mip 0 is absent).
- Normal rendering at close range but a black surface beyond a certain camera distance (if mip
  1–3 are absent), which is the most common symptom when only the base level was written.

There is no GL error, no assertion, and no log message for this failure mode. It must be caught
at asset-authoring time, not at runtime.

**Reference byte sizes** — for validation, the total file size of a correctly generated DDS
stub (header + all mip data) is:

| Format | Resolution | Mip levels | Total file size |
|---|---|---|---|
| DXT5/BC3 | 1024×1024 | 4 | **1,392,768 bytes** (128 header + `(262144 + 65536 + 16384 + 4096)` × 1 byte-per-raw) |
| DXT1/BC1 | 2048×2048 | 4 | **1,398,272 bytes** (128 header + `(524288 + 131072 + 32768 + 8192)` × 1 byte-per-raw) |
| DXT5/BC3 | 2048×2048 | 4 | **2,796,544 bytes** (128 header + `(1048576 + 262144 + 65536 + 16384)` × 1 byte-per-raw) |
| DXT5/BC3 | 1024×128 | 4 | **192,640 bytes** (128 header + `(131072 + 32768 + 8192 + 2048)` × 1 byte-per-raw) |

DXT1 block size: 8 bytes per 4×4 pixel block. DXT5 block size: 16 bytes per 4×4 pixel block.
Mip N dimensions: `max(1, floor(W / 2^N)) × max(1, floor(H / 2^N))`, rounded up to the
nearest 4-pixel block boundary.

**Regenerating all DDS stubs** — after any change to `tools/generate_dds_stubs.py` (or if
stubs are suspected truncated), regenerate all assets from the repository root:

```bash
python3 tools/generate_dds_stubs.py
```

This command overwrites every stub DDS file tracked by the script. Commit the regenerated files
to ensure CI and other contributors receive complete mip chains. **Do not manually edit DDS
binary files** — always regenerate via the script.

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

  All suffixes are lowercase. No other suffix patterns are valid. `validate_assets.py` must reject any DDS file whose name does not end with one of these six suffixes. The `_billboard` suffix applies exclusively to LOD2 imposter atlases — small building and prop assets that ship a `_billboard.dds` must NOT also ship a `_lod2.b3d` mesh.

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
| Sky cloud texture (`clouds.png`) | 1024×1024 RGBA PNG (Phase 10b). No `_d`/`_n`/`_s` suffix — this is a transparency-mask cloud texture, not a diffuse surface texture. Upload path: linear pool via `IVideoDriver::getTexture()` (PNG, not DDS — Irrlicht DDS loader disabled). Located at `assets/textures/sky/clouds.png`. No mip chain (tiled at UV scale; mips would blur the density mask edges). |

**Facade texture policy**: All building facade diffuse textures must use atlas cells — no standalone per-building non-atlas textures for facade diffuse. Each unique wall module variant occupies one 512×512 cell in the 2048×2048 city building atlas (16 unique wall module textures per atlas sheet). Effective density at 512×512 for a 4×3 m module face: ~128 px/m at LOD0 near-camera distance.

**Building atlas usable content area per cell**: With 8 texels per-cell border on each of the 4 edges, the usable content area per 512×512 facade atlas cell is **496×496 px** (512 − 8 − 8 = 496 px per axis). All facade art — window detail, surface materials, trim lines — must be authored to fit within this 496×496 usable zone. Content that bleeds into the 8 px border zone will exhibit mip-level bleed at distant views (the border pixels from adjacent cells become visible). The export validation script must verify that all non-transparent atlas cell pixels fall within the [8, 504) UV texel range on both axes (i.e., valid texel indices 8–503 inclusive, giving 496 usable texels per axis).

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

##### Terrain Texture Art Style Direction

**Realistic-stylized**: matching the visual register of Cities: Skylines (2015) and SimCity 4
(2003) — richer saturation than raw photography, simplified micro-texture, strong material
read at all zoom levels. Not photorealistic. Not cartoony.

**Color temperature guideline**: warm sunlit palette overall. Grass is golden-green, soil is
warm ochre, asphalt is cool blue-grey, concrete is warm light grey. These temperature
contrasts aid material identification when the splat map blends between zones at road edges.

**Zone overlay avoidance**: The game renders colored zone overlays (residential = green tint,
commercial = blue tint, industrial = yellow tint). Avoid heavy use of pure green (`#00FF00`
family) in grass or pure yellow in soil — these hues will merge with zone overlays and confuse
zone-type readability. Shift grass toward olive-gold and soil toward rust-ochre.

##### Tiling and Camera Context

- Terrain chunk size: 128 m × 128 m world space; LOD0 quad grid: 32 × 32 cells at 4 m per cell
- Texture repeat frequency: **one full tile per 4 m quad cell** at LOD0
- At 2048 px resolution, 1 texel ≈ 1.95 mm world-space
- Camera pitch range −70° to −20° from vertical: high-frequency micro-detail that reads
  top-down is more valuable than low-angle surface silhouette detail
- At mip level 3 (256 px, representing a 4 m area), each mip texel covers approximately
  16 mm world-space — macro-scale color and value variation must read correctly at this resolution
- All 8 textures must tile seamlessly with themselves in all directions (`GL_TEXTURE_WRAP_S/T = GL_REPEAT`)

##### Diffuse Texture Specifications (Grassland Biome)

All four diffuse textures are 2048 × 2048 px, DXT1/BC1, sRGB, 4 mip levels (2048→1024→512→256),
uploaded via `TextureCache::loadSRGB()` with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`.

###### terrain\_grass\_d.dds

**Semantic**: Natural grass ground cover. Fills splat channel R. The dominant biome surface
for undeveloped and park tiles.

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone blade) | `#7A8C3E` | 122 | 140 | 62 | Muted olive-green; principal blade color |
| Secondary (shadow pocket) | `#4E5C28` | 78 | 92 | 40 | Dark gap between blade clusters |
| Tertiary (dry tip highlight) | `#A09848` | 160 | 152 | 72 | Slightly yellow-gold blade tip; sunlit |
| Soil show-through | `#7A6448` | 122 | 100 | 72 | Warm brown visible between sparse patches |
| Specular highlight catch | `#B4B068` | 180 | 176 | 104 | Hot highlight on near-vertical blade faces |

Luminance range: min 62 (shadow pocket) — max 180 (highlight catch). Do not compress the
range — the variance maintains read at DXT1 compression and at LOD3 (256 px mip).

Surface: **isotropic stochastic**, high roughness. Use a stochastic Voronoi or fBm base with
point-seeded blade clusters. Approximately 400–600 blade impressions at 2048 px. No
directional bias. Approximately 15% soil show-through between sparse patches.

Key constraints:

- No pure-green pixels: max green channel value after sRGB encoding must be below 200/255
- Blade minimum width 2 px in source (narrower aliases under DXT1 4×4 block compression)
- DXT1 mitigation: keep blade colors within a 60-unit luminance spread within any 4×4 block;
  use 0.5 px Gaussian blur on source PNG before compression to soften hard blade edges

###### terrain\_asphalt\_d.dds

**Semantic**: Road and paved driveway surface. Fills splat channel G.

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone aggregate) | `#3C3C3C` | 60 | 60 | 60 | Dark grey aggregate body |
| Worn highlight (oxidized surface) | `#585858` | 88 | 88 | 88 | Lighter grey on worn areas |
| Deep crack / joint gap | `#1C1C1C` | 28 | 28 | 28 | Near-black in crack recesses |
| Aggregate specks (light) | `#6A6A6A` | 106 | 106 | 106 | Quartz aggregate pebbles |
| Aged tar stain | `#2A2820` | 42 | 40 | 32 | Slight warm-dark for tar pools |

Luminance range: min 28 — max 106 (spread 78 units). Author primary aggregate with faint
blue bias: R ≤ G = B by approximately 2–4 units (e.g. `#3A3C3C`) to distinguish from the
warm-grey concrete.

Surface: **isotropic fine granular** with superimposed macro-crack network (Voronoi-edge,
cell size 80–150 px, crack width 1–2 px, ≈3–5% of surface area). Road markings are NOT
authored into this texture — they are handled by the road marking atlas.

DXT1 mitigation: ensure no 4×4 block contains both a 106-luminance speck and a 28-luminance
crack shadow without at least one intermediate-luminance texel between them.

###### terrain\_soil\_d.dds

**Semantic**: Bare earth, earthworks, and unpaved areas. Fills splat channel B.

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (damp mid-tone) | `#7A5C3C` | 122 | 92 | 60 | Warm rust-brown; main soil body |
| Dry surface highlight | `#A07850` | 160 | 120 | 80 | Lighter, slightly desaturated dry crust |
| Wet shadow (compact) | `#4E3820` | 78 | 56 | 32 | Dark compact soil in shadow zones |
| Aggregate grit (coarse) | `#8A7060` | 138 | 112 | 96 | Small pebbles / coarse sand grains |
| Clay layer show-through | `#6A4830` | 106 | 72 | 48 | Darker orange-clay layer in deep ruts |

Luminance range: min 56 — max 160 (spread 104 units — most visual texture of the four types).
Hue must read as **rust-ochre-brown** (R/G ratio ≈ 1.3–1.4). No green tones: G must always
be less than R throughout. Avoid hues near `#B4922C` (conflicts with industrial zone overlays).

Surface: **isotropic granular** with desiccation crack network (cell size 120–200 px, crack
width 2–4 px, ≈6–10% surface area) and 3–5 wheel rut impressions per 512 px area (8–16 px
wide, 50–80 px long, random angles — do not align parallel to texture edges).

###### terrain\_concrete\_d.dds

**Semantic**: Paved plazas, sidewalks, parking lots, and industrial pads. Fills splat channel A.

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone slab) | `#C0B8A8` | 192 | 184 | 168 | Warm light grey; main slab color |
| Fresh pour highlight | `#D8D4CC` | 216 | 212 | 204 | Lighter, slightly blue-grey new pour |
| Shadow / expansion joint | `#7A7870` | 122 | 120 | 112 | Dark joint line between slabs |
| Aggregate exposed (coarse) | `#A09888` | 160 | 152 | 136 | Aggregate show-through on worn surface |
| Staining (oil / water) | `#A4A090` | 164 | 160 | 144 | Slightly darker slab with stain |

Luminance range: min 116 — max 216 (brightest of the four types). **Warm grey bias**: R ≥ G > B
by approximately 8–16 units. No strong hue variation — monochromatic warm grey only.

Surface: **regular orthogonal grid (expansion joints) with isotropic aggregate fill**.
Recommended joint period: 512 px (4×4 slabs per 2048 px tile). Joint lines: 2–3 px wide,
luminance 116, with 1 px bright bevel (luminance 180) on each edge. Aggregate: 500–800
granule impressions per 512 px area, ±8 luminance units contrast (low — much subtler than soil).
Each slab face must be individually varied in luminance (±8 units) so adjacent slabs are
distinguishable without joint lines.

##### Normal Map Per-Texture Specifications

The DXT5nm encoding pipeline and shader unpack are defined in the Runtime Formats section
above. All four terrain normal maps are 2048 × 2048 px, DXT5/BC3, linear, 4 mip levels
pre-baked via bicubic downsample (not driver-generated — bilinear mip generation does not
preserve tangent-space normal vector normalization).

Mip pre-bake procedure: bicubic downsample at each level → renormalize vector field →
DXT5nm encode per mip level → composite into one DDS.

###### terrain\_grass\_n.dds

**Normal intensity**: moderate (|nx| or |ny| in range 0.15–0.40). Do not use high-intensity
normals for grass — the matte surface means strong normals produce unrealistic specular
highlights from the reconstructed Z.

**Primary features**: blade stroke normals 8–24 px long, 1–3 px wide (matching diffuse blade
impressions). Each blade in the diffuse must have a paired normal deflection at the same UV.

**Low-frequency base**: gentle Perlin undulation (period 512–1024 px, amplitude ±0.10 nx/ny)
representing ground microtopography beneath the grass.

**Mip behavior**: at mip 2 (512 px), individual blade normals merge into a broad directional
field; at mip 3 (256 px), only large-scale undulations remain. This is correct behavior.

###### terrain\_asphalt\_n.dds

**Normal intensity**: subtle (max tangential component ±0.25). The surface is macroscopically
flat — heavy normals would produce harsh specular highlights on a predominantly diffuse material.

**Primary features**: aggregate dome normals 2–8 px diameter matching diffuse pebble sizes;
crack groove normals 2–4 px wide (slightly wider than the 1–2 px diffuse cracks for smooth
gradient). South hemisphere of each pebble dome points slightly toward camera (negative Y in
OpenGL tangent space) to produce aggregate sparkle under overhead directional light.

###### terrain\_soil\_n.dds

**Normal intensity**: strong (max tangential component ±0.50). Soil has the largest physical
height variation (granules 5–20 mm above base; deep desiccation crack troughs).

**Primary features** at 2048 × 2048:

- Coarse granules (6–8 px diffuse): dome normals 8–12 px, ±0.40–0.50
- Medium granules (3–5 px diffuse): dome normals 4–8 px, ±0.25–0.35
- Fine granules (1–2 px diffuse): perturbation ±0.10–0.15 (merges into uniform roughness at mip 1)
- Desiccation cracks (2–4 px wide diffuse): groove normals 4–6 px wide, beveled inward
  deflection ±0.35 at crack edges, falling to 0 at crack center
- Wheel ruts: concave trough normals, |ny| = 0.30–0.40 at edges (8–16 px wide, matching diffuse)

**Low-frequency base**: large-scale undulation (period 256–512 px, amplitude ±0.08) to prevent
the normal map from reading as a perfectly flat plane.

###### terrain\_concrete\_n.dds

**Normal intensity**: subtle (max tangential component ±0.18). Concrete is the flattest surface.
Slab face has effectively flat normals (|nx|, |ny| < 0.05).

**Primary features** at 2048 × 2048:

- Slab face aggregate (1–3 px diffuse): extremely subtle perturbation ±0.05–0.10
- Expansion joint edges (512 px grid): bevel normals 4 px wide on each side of the joint gap,
  ±0.15 pointing outward — produces highlight on slab edge under directional light
- Joint intersection corners: dome normal pointing slightly inward ±0.12 (raised filler plug)

**Mip behavior**: at mip 2 (512 px), joint bevel normals dominate and aggregate normals
disappear. At mip 3 (256 px), the joint grid may not be fully resolved — approaches flat
with only the broadest joint bevel gradients remaining. Acceptable for far-distance rendering.

##### Authoring Quality Checklist

Before committing any terrain DDS file, verify each item:

**Diffuse DDS**:

- [ ] Source PNG has embedded sRGB ICC profile (`exiftool | grep -i 'color space'`)
- [ ] DDS FourCC at byte offset 84 is `0x30315844` (DX10 extended header present)
- [ ] DX10 DXGI\_FORMAT field = 72 (`BC1_UNORM_SRGB`) for DXT1 diffuse
- [ ] DDS file contains exactly 4 mip levels (2048, 1024, 512, 256)
- [ ] Texture tiles seamlessly: composite 2×2 grid at 4096×4096 — no visible seam in any direction
- [ ] No pure-green pixels in grass texture (max G channel value 195 in sRGB)
- [ ] No green tones in soil texture (G < R throughout)
- [ ] Asphalt primary color has slight cool bias (R ≤ G ≤ B in primary aggregate)
- [ ] Concrete primary color has warm bias (R ≥ G ≥ B + 8)
- [ ] Luminance range within spec bounds stated above
- [ ] No visible DXT1 block grid artifacts at 1:1 zoom

**Normal map DDS**:

- [ ] Source swizzled PNG: alpha = X, green = Y, blue = 127, red = 0
- [ ] Flat-surface reference pixel: alpha = 128, green = 128 (decodes to nx=0, ny=0, nz=1)
- [ ] DDS FourCC: `0x35545844` (DXT5) or DX10 header with BC3\_UNORM (DXGI 77)
- [ ] DDS file contains exactly 4 mip levels pre-baked via bicubic downsample
- [ ] At mip 3 (256 px): decoded normals produce no NaN (no vec with |nx|+|ny| > 1)
- [ ] Normal intensity within the range stated per-texture above
- [ ] No sRGB ICC profile embedded in source swizzled PNG

##### Terrain Texture Export Commands

```bash
# Diffuse — NVTT (Linux preferred):
nvcompress -color -bc1 terrain_grass_src.png terrain_grass_d.dds
nvcompress -color -bc1 terrain_asphalt_src.png terrain_asphalt_d.dds
nvcompress -color -bc1 terrain_soil_src.png terrain_soil_d.dds
nvcompress -color -bc1 terrain_concrete_src.png terrain_concrete_d.dds

# Diffuse — Compressonator (Windows preferred, produces exactly 4 mip levels):
compressonatorcli -fd BC1 -miplevels 4 terrain_grass_src.png terrain_grass_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_asphalt_src.png terrain_asphalt_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_soil_src.png terrain_soil_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_concrete_src.png terrain_concrete_d.dds

# Normal maps (after DXT5nm swizzle) — NVTT:
nvcompress -normal -bc3 terrain_grass_swizzled.png terrain_grass_n.dds
nvcompress -normal -bc3 terrain_asphalt_swizzled.png terrain_asphalt_n.dds
nvcompress -normal -bc3 terrain_soil_swizzled.png terrain_soil_n.dds
nvcompress -normal -bc3 terrain_concrete_swizzled.png terrain_concrete_n.dds

# Normal maps — Compressonator:
compressonatorcli -fd BC3 -miplevels 4 terrain_grass_swizzled.png terrain_grass_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_asphalt_swizzled.png terrain_asphalt_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_soil_swizzled.png terrain_soil_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_concrete_swizzled.png terrain_concrete_n.dds
```

Note: `nvcompress` generates a full mip pyramid (down to 1×1). The runtime sets
`GL_TEXTURE_MAX_LEVEL = 3` to cap GPU reads at 4 levels, so rendering is correct.
`validate_assets.py` must check `mip_count >= 4`, not `mip_count == 4`, so both
compressor outputs are accepted. Use Compressonator if you need exactly 4 mip levels in
the DDS file header.

Output files go to `assets/textures/terrain/`.

##### Terrain Texture VRAM Budget (per-file)

| File | Format | Approx. disk/VRAM |
|---|---|---|
| `terrain_grass_d.dds` | DXT1/BC1, 4 mip | ~2.8 MB |
| `terrain_asphalt_d.dds` | DXT1/BC1, 4 mip | ~2.8 MB |
| `terrain_soil_d.dds` | DXT1/BC1, 4 mip | ~2.8 MB |
| `terrain_concrete_d.dds` | DXT1/BC1, 4 mip | ~2.8 MB |
| `terrain_grass_n.dds` | DXT5/BC3, 4 mip | ~5.6 MB |
| `terrain_asphalt_n.dds` | DXT5/BC3, 4 mip | ~5.6 MB |
| `terrain_soil_n.dds` | DXT5/BC3, 4 mip | ~5.6 MB |
| `terrain_concrete_n.dds` | DXT5/BC3, 4 mip | ~5.6 MB |

DXT1 per texture: `ceil(2048/4)² × 8 × 1.33 ≈ 2.8 MB`.
DXT5 per texture: `ceil(2048/4)² × 16 × 1.33 ≈ 5.6 MB`.
Total terrain detail texture VRAM: 4 × 2.8 + 4 × 5.6 = **33.6 MB** (within VRAM budget above).

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

### UI Sprite Sheet Art Style — Frosted Glass

The sprite sheet uses a two-layer glass design.  A **milky/satinato glass
background panel** covers the used area (rows 0-10) to provide a soft,
diffused, semi-opaque warm white surface.  **Nearly transparent frosted-glass
icon cells** float on top, their rounded edges revealing the milky glass
beneath.  Icon symbols use vivid, bright colors with 3D gradient fills and
drop shadows.  Each 64x64 cell is rendered at 4x internal resolution
(256x256) and Lanczos-downsampled for clean anti-aliased edges.  The
generator script is `tools/generate_hud_sprites.py`.

**Milky glass background panel (behind rows 0-10):**

- Rounded rectangle covering all occupied icon rows + 8 px padding, corner
  radius ~16 px
- Base fill: `rgba(235, 238, 242, 140)` -- warm off-white, lighter/more see-through
- Gaussian noise (sigma ~1.5, amplitude ~8) for frosted/sandblasted
  micro-texture -- milky glass is not perfectly smooth
- Radial vignette: center slightly brighter `rgba(248, 250, 252)`, edges
  slightly more grey `rgba(220, 223, 228)` -- gives depth
- 1 px glass-edge rim: `rgba(255, 255, 255, 180)`
- Rows 11-31 remain fully transparent (no background panel)

**Cell background treatment (per icon cell):**

- Rounded rectangle with ~10 px corner radius (at 64 px scale), nearly
  transparent frosted tint: `rgba(255, 255, 255, 55)` (alpha ~55 out of 255)
- The cell is almost see-through; the game's own UI background shows
  through the rounded corners
- Rim / bevel is the primary visible framing: top/left bright
  `rgba(255, 255, 255, 160)`, bottom/right dark `rgba(180, 185, 195, 100)`,
  2-3 px width
- Gloss highlight arc in top 30% of cell: white ellipse, **alpha 55**,
  Gaussian-blurred -- this is the **strongest visual element** of the cell
  and the main "glass" cue
- Very subtle drop shadow below the chip: `rgba(0, 0, 0, 25)`
- Active state: very subtle cyan-teal tint overlay `rgba(0, 200, 220, 35)`
  (barely visible) + subtle outer glow (teal for toolbar tools;
  zone-specific color for zone buttons; red for demolish), blur
  radius ~12 px at 4x scale
- Inactive state: plain frosted `rgba(255, 255, 255, 45)`, no glow,
  no tint overlay

**Icon symbol treatment:**

- Vivid, bright colors (designed for any background) -- white crosshairs,
  bright greens/blues/ambers, full-saturation fills
- Top-left light source: gradient fills with highlights on top/left faces,
  shadows on bottom/right
- Drop shadow behind icon content: `rgba(0, 0, 0, 60)` offset (2,2) blur 3
  at final scale (8,8 blur 6 at 4x working resolution)
- Distinct silhouettes per semantic meaning (no shape reuse across
  different icon roles)

**Water drop icon (Utilities sub-panel, Water Tower):**

- Classic teardrop shape: narrow at top, rounded at bottom
- Transparent crystal-clear body: glass-blue tint `rgba(120, 200, 255, 180)`
  with lighter transparent center
- Bright white crescent highlight on upper-left (primary visual cue)
- Dark blue rim outline `rgba(0, 80, 150, 130)` defining the drop shape
- Tiny bright specular dot at top-left of the bulge
- Background: frosted glass cell with subtle blue tint overlay

**Color palette:**

- Base glass (active): `rgba(255, 255, 255, 55)` -- nearly transparent
- Base glass (inactive): `rgba(255, 255, 255, 45)` -- plain frosted
- Active tint overlay: `rgba(0, 200, 220, 35)` (toolbar tools)
- Zone Residential active tint: `rgba(60, 220, 90, 30)`
- Zone Commercial active tint: `rgba(60, 130, 240, 30)`
- Zone Industrial active tint: `rgba(220, 200, 50, 30)`
- Demolish active tint: `rgba(240, 70, 70, 30)`
- Badge/indicator/panel cells: `rgba(255, 255, 255, 50)` -- same frosted
- Fire badge shield body: `rgb(220, 70, 60)` -- bright fire-engine red
- Police badge shield body: `rgb(70, 130, 220)` -- clear sky blue
- Icon symbols: full-color per semantic role (vivid amber bolt,
  crystal-clear blue drop, bright-red shield, gold star on sky-blue shield,
  etc.) with white/cream highlights

### Phase 10 Sign-Off — UI Sprite Sheet (graphics-artist-2d-texture)

> Phase 10 sign-off — 2026-03-04
>
> `assets/textures/ui/hud_sprites_ui.png` delivered and validated:
>
> - Dimensions: 2048x2048 px (32 columns x 32 rows of 64x64 px cells = 1024 cells total)
> - Colour mode: RGBA (8 bits per channel, straight alpha)
> - Format: PNG (source/authoring format; DDS export via `export_textures.py --format rgba8`
>   is a Phase 9 deliverable by `graphics-dev-irrlicht`)
> - Colour space: **linear** (NOT sRGB) -- UI elements are not photographic diffuse; sRGB
>   decode must NOT be applied at upload time; `GL_TEXTURE_MAX_LEVEL=0` (mipmapping disabled)
> - Art style: **frosted-glass chips** -- nearly transparent glass cells (alpha ~55) with a
>   strong gloss highlight arc (alpha 55) as the main "glass" visual cue, bright rim/bevel
>   framing, and subtle outer glow (active state). Icons use vivid, bright colors with 3D
>   gradient fills and drop shadows. Water drop icon uses crystal-clear teardrop design.
>   Generator script: `tools/generate_hud_sprites.py` (4x supersampled, Lanczos downsample).
> - Rows 0-6 painted: Toolbar active/inactive, Zone active/inactive, Utilities
>   active/inactive, Active tool indicator badges
> - Row 7 painted: Cursor shape icons (reserved for Phase 12 `setMouseCursor()`)
> - Rows 8-9 painted: Minimap overlay toggle (active/inactive)
> - Row 10 painted: Notification bell, clock, unsaved dot, undo arrow
> - Rows 11-31: fully transparent (unused; reserved for future icon expansion)
> - 42 unique sprites total across rows 0-10 (no duplicated shapes between cells)
> - No mip chain; no DXT compression on PNG source; atlas padding not required
>   (mipmapping disabled removes the padding constraint per UV & Atlas Strategy section)
>
> Signed off by: `graphics-artist-2d-texture`

### Sprite ID Encoding and Row-Conflict Pitfall

Sprite IDs in `hud_sprite_ids.h` are encoded as `id = col + row * 32`. The pixel position of
cell (col, row) in the 2048×2048 sheet is `(col * 64, row * 64)` — **every row is 64 px tall
and every column is 64 px wide, with no exceptions.** `spriteRectForIndex()` in
`IrrlichtUIBackend.cpp` resolves all IDs via this single formula.  No per-ID special cases
exist or should ever be added — the generator (`tools/generate_hud_sprites.py`) places every
icon at exactly `(col * 64, row * 64)`.

**Row 1 (y = 64) is exclusively the toolbar-inactive row** (IDs 32–36, 5 icons, 64×64 px
each at x = 0, 64, 128, 192, 256). No other sprites may be placed at y = 64 in the PNG.

**Row 10 (y = 640)** holds the four notification/HUD-misc icons (IDs 320–323):

| ID | Constant | Col | x | y |
|----|----------|-----|---|---|
| 320 | `kSpriteNotificationBell` | 0 | 0 | 640 |
| 321 | `kSpriteClockIcon` | 1 | 64 | 640 |
| 322 | `kSpriteUnsavedDot` | 2 | 128 | 640 |
| 323 | `kSpriteUndoIcon` | 3 | 192 | 640 |

**Known past bugs — do not reintroduce:**

1. **Bell/undo/clock/dot (IDs 320–323)** — an old JSON listed `icon_bell` at `(56, 64)` and
   `icon_undo` at `(0, 64)`, placing both inside the toolbar-inactive row (y = 64).
   The resulting special cases in `spriteRectForIndex()` caused the notification bell button
   to render the road-inactive icon shifted 8 px left (bell rect `x:56–104` overlapped road
   inactive `x:64–128`).  Fixed by removing the special cases; the default grid resolves
   all four correctly to row 10 (y = 640).

2. **Utilities sub-panel icons (IDs 128–163, rows 4–5)** — an old JSON listed water, fire,
   and police with 72 px column spacing (`x = 72, 144, 216`) instead of 64 px.  A special
   case in `spriteRectForIndex()` used `xOffsets = {0, 72, 144, 216}`, reading Fire 16 px
   and Police 24 px past their real start positions, clipping their left edges and making
   them appear shifted left on the button.  Fixed by removing the special case; the
   generator always places all four icons on the standard 64 px grid (x = 0, 64, 128, 192).
