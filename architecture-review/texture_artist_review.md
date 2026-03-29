# Texture Artist Spec Review — AI Town

**Reviewer**: Senior 2D Texture Artist (`graphics-artist-2d-texture`)
**Date**: 2026-03-29
**Files reviewed**:
- `/workspace/architecture/asset-standards/2d-texture-standards.md`
- `/workspace/architecture/asset-standards/building-atlas-layout.md`
- `/workspace/architecture/graphics-architecture/texture-cache.md`
- `/workspace/architecture/graphics-architecture/shader-loading.md`

---

## Summary

The texture specifications are unusually thorough and well-integrated across files. The majority of
runtime formats, upload paths, sRGB correctness, and mip chain rules are precisely specified. The
issues below are genuine gaps, contradictions, or missing content that would block production or
cause silent rendering errors if not resolved. Issues are ordered by severity within each file
grouping.

---

## Issues

---

### A. `2d-texture-standards.md`

---

**[PROBLEM] A-1 — CRITICAL: Resolution Matrix building facade cell description contradicts atlas layout**

The Resolution Matrix (§Resolution Matrix, line 234) states:

> `Building facade atlas cell | 512×512 per module face (wall tile); up to 4 module faces packed per 1024×1024 city building atlas cell`

This contradicts the current atlas architecture. The primary atlas is 4096×4096 with an 8×8 cell
grid at 512×512 px per cell — there is no 1024×1024 city building atlas cell. The parenthetical
"up to 4 module faces packed per 1024×1024 city building atlas cell" is a leftover from the
pre-phase-11e 2048×2048 or 4×4 grid design. The current atlas gives each variant its own 512×512
cell; no 1024×1024 composite cell exists anywhere in the spec.

**Proposed resolution**: Remove the "up to 4 module faces packed per 1024×1024 city building atlas
cell" clause entirely. Replace with: "One 512×512 cell per building variant in the 4096×4096
primary atlas (8×8 cell grid); 256×256 per variant in the 2048×2048 fallback atlas."

---

**[PROBLEM] A-2 — HIGH: `_tileable` suffix is not in the naming convention table but is used as a dispatch key**

The naming convention table (§Naming convention, line 214–226) defines exactly six valid suffixes:
`_d`, `_n`, `_s`, `_sp`, `_lm`, `_billboard`. The `_tileable` suffix is NOT listed. However,
`texture-cache.md` uses `_tileable` as a distinct `loadSRGB()` dispatch suffix row for
`road_asphalt_tileable.dds` (texture-cache.md line 83). The spec also states:
"validate_assets.py must reject any DDS file whose name does not end with one of these six
suffixes" — which means `road_asphalt_tileable.dds` would be rejected by its own validator since
`_tileable` is not in the approved suffix list.

**Proposed resolution**: Either (a) add `_tileable` as a seventh valid suffix to the naming
convention table with its dispatch rule documented, or (b) rename `road_asphalt_tileable.dds` to
`road_asphalt_d.dds` and add a named-exception dispatch rule for it similar to the
`vehicles_sprite_atlas_d.dds` exception. Option (a) is preferred because `_tileable` communicates
authoring intent (seamless repeat) that `_d` does not.

---

**[GAP] A-3 — HIGH: No canonical filename specified for the road marking atlas**

The road marking atlas (1024×1024 DXT5/BC3, 4×4 grid of 16 road decal cells) is fully spec'd
in terms of format, resolution, upload path, and mip chain. However, no canonical DDS filename is
ever stated anywhere in either `2d-texture-standards.md` or `building-atlas-layout.md`.
`validate_assets.py`, the shader binding, and artists all require a known filename. The
`road_asphalt_tileable.dds` road surface texture does have a filename, but the separate road
marking overlay atlas does not.

**Proposed resolution**: Assign and document an explicit filename. Suggested:
`road_markings_d.dds`. Add this to the naming convention and validator checks alongside
`road_asphalt_tileable.dds`.

---

**[PROBLEM] A-4 — HIGH: Road marking atlas upload path contradicts its own texture classification**

The road marking atlas upload path is specified as "linear pool (`IVideoDriver::getTexture()`)"
because "the atlas encodes a decal mask (alpha channel = blending opacity), not diffuse color data"
(§UV & Atlas Strategy). However, `building-atlas-layout.md` §Road Marking Atlas states the format
as "DDS DXT5/BC3 (alpha = decal mask)" and the upload path as "linear (NOT sRGB — decal mask data,
not diffuse color)". While both files agree on linear upload, the road marking atlas also contains
visible road surface color data in its RGB channels (white lane marking color, yellow center lines,
crosswalk stripes). These are perceptual colors that will appear gamma-incorrect if not sRGB-decoded
— identical to the billboard atlas problem the spec explicitly warns about. The current spec treats
road marking color data as "mask data" to justify linear upload, but artists author these RGB values
in a perceptual working space.

**Proposed resolution**: Clarify authoring intent. If road marking RGB channels encode alpha-blend
masks only (R=G=B, grayscale mask intensity), linear upload is correct. If RGB channels encode
actual visible colors (white, yellow), the atlas needs the sRGB upload path. The spec must state
explicitly which is required and add a corresponding `validate_assets.py` check.

---

**[GAP] A-5 — HIGH: No texture specification for building facade normal maps**

The Resolution Matrix specifies `Specular/roughness (_s,_sp) — building facades | 512×512` and
`Normal maps (_n) — all categories | Same resolution as specular/roughness for that category`,
implying building facade normal maps at 512×512. However:

- No building facade normal atlas is specified anywhere. Are building facade normals per-asset
  standalone files or packed into an atlas?
- If standalone per-asset, no naming convention example is given (e.g.,
  `res_low_01_n.dds`).
- If atlased, the building atlas layout file contains zero mention of a normal atlas — it covers
  only the diffuse atlas.
- The `building-atlas-layout.md` sign-off records reference "normal at DXT5nm 256×256 per island"
  (sign-off, line 330) which contradicts the Resolution Matrix's 512×512 for building facade
  normals.
- No `GL_TEXTURE_MAX_LEVEL` dispatch row exists in `texture-cache.md` for building facade normals.
- No VRAM budget line exists for building facade normals.

**Proposed resolution**: Explicitly state whether building facade normals are (a) per-cell within a
separate normal atlas (e.g., `buildings_atlas_n.dds`) or (b) standalone per-variant files. If (a),
add the atlas to `building-atlas-layout.md`. If (b), add a naming convention example and VRAM
budget entry. The sign-off mention of "256×256 per island" must be reconciled with the Resolution
Matrix's "same resolution as specular/roughness" (512×512).

---

**[GAP] A-6 — HIGH: `loading_screen.png` has no texture specification**

`irrlicht-device-lifecycle.md` (line 130) references `assets/textures/ui/loading_screen.png` as a
loading screen asset rendered during startup. An untracked PNG file at this path already exists in
the working tree (listed in git status: `?? assets/textures/ui/loading_screen.png`). No texture
specification exists in any architecture file for this asset:

- No format specified (PNG at what resolution?)
- No color space specified
- No upload path specified (is it loaded via `IVideoDriver::getTexture()`? Direct pixel blit?)
- No mip requirement stated
- No runtime format (should it be PNG, or converted to RGBA8 DDS like the UI sprite sheet?)
- No VRAM budget entry

**Proposed resolution**: Add a `loading_screen.png` specification to `2d-texture-standards.md`
(probably under the UI section). State: resolution (e.g., full target window size or fixed 1920×1080),
color space (sRGB for visible imagery), upload path (PNG via `IVideoDriver::getTexture()` is
simplest), no mip required, and VRAM budget (at 1920×1080 RGBA8 ≈ 8 MB).

---

**[INCONSISTENCY] A-7 — MEDIUM: Normal map checklist uses BC3_UNORM (DXGI 77) but context is ambiguous**

The normal map authoring quality checklist (line 636) states:

> `DDS FourCC: 0x35545844 (DXT5) or DX10 header with BC3_UNORM (DXGI 77)`

The value 77 is correct for `DXGI_FORMAT_BC3_UNORM` (linear BC3). However, earlier in the same
file at line 122, the spec lists `BC3_UNORM_SRGB = DXGI_FORMAT value 78` (for sRGB usage). The
checklist item is technically correct — normal maps should NOT be sRGB, so DXGI 77 is right — but
the checklist does not explain why 77 is correct, leaving artists who read the sRGB validation
section first with potential confusion (they might try to use 78 for all DXT5 files). The `BC3_UNORM
(DXGI 77)` label also does not match the DXT5nm encoding — DXT5nm is a swizzle convention, not a
DXGI format variant. A file produced with Compressonator `-fd BC3` without the sRGB flag will
produce DXGI_FORMAT_BC3_UNORM (77), which is correct, but this is never stated.

**Proposed resolution**: Add an inline note to the checklist item: "DXGI 77 = BC3_UNORM (linear)
— correct for normal maps (not sRGB). Do not use DXGI 78 (BC3_UNORM_SRGB) for normal maps —
sRGB decode corrupts the encoded direction vectors."

---

**[PROBLEM] A-8 — MEDIUM: Terrain normal maps loaded via `loadLinear()` (`IVideoDriver::getTexture()`) cannot have `GL_TEXTURE_MAX_LEVEL` set, but spec requires pre-baked mip chain**

The spec states terrain normal maps require "4 mip levels pre-baked via bicubic downsample" and
that the "driver must not generate additional levels beyond level 3." The `texture-cache.md`
dispatch table confirms `_n` textures use `loadLinear()` with `GL_TEXTURE_MAX_LEVEL = (driver
default)` — meaning `GL_TEXTURE_MAX_LEVEL` cannot be set. The V1 workaround for vehicle normal
atlases (pre-bake exactly 4 mip levels so the driver loads no more than authored levels) is
documented in `building-atlas-layout.md`, but the same constraint for terrain normal maps
(`terrain_grass_n.dds` etc.) is never explicitly stated or pointed to.

**Proposed resolution**: Add a cross-reference note in the terrain normal map section:
"Terrain normal maps use the same mip-capping workaround as the vehicle normal atlas — pre-bake
exactly 4 mip levels into the DDS file header so the driver reads no more than 4 levels. See
`building-atlas-layout.md` §Vehicle Normal Atlas for the V1 workaround rationale."

---

**[GAP] A-9 — MEDIUM: No prop/street-furniture texture authoring spec beyond resolution**

The Resolution Matrix states props/street furniture use 256×256 or 512×512. Beyond this, there is
no authoring spec for prop textures:

- No color palette / style direction
- No named prop texture files
- No spec for whether props use standalone diffuse files or are atlased
- No spec for whether props require normal maps or specular maps
- No upload path stated for prop diffuse (are they sRGB `loadSRGB()` or linear `loadLinear()`?)
- The "Miscellaneous (normal maps, roughness, props, per-type vehicle textures)" VRAM budget line
  allocates 48 MB as a catch-all but provides no per-asset breakdown

**Proposed resolution**: Add a "Props & Street Furniture Textures" subsection. State: standalone
DDS files per prop, sRGB upload path for diffuse, optional normal map, max 256×256 per prop
diffuse, naming convention example.

---

**[INCONSISTENCY] A-10 — MEDIUM: Mip chain description for terrain base textures is internally ambiguous**

The spec states terrain base textures use "4 mip levels (2048→1024→512→256)" in the diffuse
texture spec, but the VRAM budget entry (line 700–701) states "up to 4 layers, 2048×2048 DXT1
with 4-level mip" and the per-file VRAM table (line 682) states "DXT1/BC1, 4 mip ~ 2.8 MB." The
VRAM calculation `ceil(2048/4)² × 8 × 1.33 ≈ 2.8 MB` applies the 1.33× mip overhead factor,
which accounts for a full mip chain — but a full mip chain on a 2048×2048 texture has 12 levels
(down to 1×1), not 4. The 1.33× factor is correct for a full pyramid but the spec caps at 4
levels in the DDS file for `nvcompress` output (when `GL_TEXTURE_MAX_LEVEL = 3` is set at
runtime) and 4 levels exactly for Compressonator output. There is a subtle discrepancy: if the
DDS file contains 12 mip levels (nvcompress output) but only 4 are sampled at runtime, the VRAM
consumed is the full 1.33× overhead (GPU driver loads the full DDS content), not just the 4
active levels. The spec's 2.8 MB figure is therefore correct for nvcompress output (full chain
uploaded, 1.33× overhead), but not for Compressonator output with exactly 4 levels (overhead is
smaller: ~1.25× of mip 0 size). This is a minor budget inconsistency but could mislead artists
computing VRAM per asset.

**Proposed resolution**: Add a note: "VRAM budget uses 1.33× overhead for nvcompress-generated
full-chain DDS files. For Compressonator-generated 4-level files, actual VRAM overhead is
approximately 1.25×, which is slightly under the budget figure — this is acceptable."

---

**[GAP] A-11 — MEDIUM: No specular/roughness texture art direction or content spec**

The spec defines resolution and format for `_s` (grayscale specular) and `_sp` (packed
roughness/metallic/AO) textures but provides zero authoring guidance:

- No channel packing convention for `_sp` (which channel = roughness? metallic? AO?)
- No target roughness ranges per material type
- No metallic flags (is concrete metallic? Is glass metallic?)
- No authoring style direction
- No shader documentation for how `_sp` channels map to lighting model inputs

Without a channel packing convention, multiple artists will produce incompatible files that break
in the lighting shader silently.

**Proposed resolution**: Add a "Specular/Roughness (_sp) Channel Packing Convention" subsection.
Specify: R = roughness, G = metallic, B = AO (or whatever the project chooses). State target ranges
per material category (e.g., concrete: roughness 0.85, metallic 0.0, AO from bake). Cross-reference
the shader that reads these channels.

---

**[PROBLEM] A-12 — MEDIUM: Mip chain byte-size reference table contains incorrect calculation for DXT5 1024×1024**

The DDS Mip Chain Integrity reference table (line 163) states:

> `DXT5/BC3 | 1024×1024 | 4 | 1,392,768 bytes (128 header + (262144 + 65536 + 16384 + 4096) × 1 byte-per-raw)`

The annotation "× 1 byte-per-raw" is misleading/incorrect. DXT5 uses 16 bytes per 4×4 block.

- Mip 0: 1024×1024 = 65536 blocks × 16 bytes = 1,048,576 bytes
- Mip 1: 512×512 = 16384 blocks × 16 bytes = 262,144 bytes
- Mip 2: 256×256 = 4096 blocks × 16 bytes = 65,536 bytes
- Mip 3: 128×128 = 1024 blocks × 16 bytes = 16,384 bytes

Total data: 1,048,576 + 262,144 + 65,536 + 16,384 = 1,392,640 bytes + 128 header = 1,392,768 bytes.

The final total is correct but the per-mip breakdown parenthetical `(262144 + 65536 + 16384 + 4096)`
sums to 348,160 (not 1,392,640). The individual values are wrong — they appear to be DXT1 block
counts (not byte sizes). The correct individual mip byte sizes for DXT5 1024×1024 are
1,048,576 + 262,144 + 65,536 + 16,384 = 1,392,640. The label "× 1 byte-per-raw" is meaningless.

**Proposed resolution**: Fix the per-mip breakdown for the DXT5 1024×1024 row to show correct byte
sizes: `(1,048,576 + 262,144 + 65,536 + 16,384)`. Remove the incorrect "× 1 byte-per-raw"
annotation. Also verify that the DXT1 2048×2048 and DXT5 2048×2048 rows are similarly correct.

---

**[DUPLICATE] A-13 — LOW: DXT5nm swizzle procedure is described three times in the same file**

The DXT5nm packing procedure appears in full or near-full detail in:
1. The Runtime Formats preamble (lines 6–25): full shader unpack GLSL and swizzle steps
2. The DDS Authoring Pipeline section (lines 180–194): 7-step swizzle procedure
3. The Normal Map Y-Channel Convention note (line 66): partial re-statement with Y-flip ordering

Each appearance adds small details the others omit (the Y-flip ordering note in item 3, the 7-step
numbered procedure in item 2). However, this structure creates three sources of truth that can
diverge during edits.

**Proposed resolution**: Keep the full 7-step procedure in §DDS Authoring Pipeline as the canonical
reference. In the Runtime Formats section, replace the detailed repeat with a single-sentence
cross-reference ("For the DDS export pipeline, see §DDS Authoring Pipeline — DXT5nm swizzle
procedure"). In the Y-channel convention note, keep only the Y-flip ordering point (the detail that
is unique there) with a pointer to the canonical procedure.

---

**[GAP] A-14 — LOW: No VRAM budget entry for terrain normal maps in the Scene VRAM Budget table**

The Scene VRAM Budget table (lines 699–713) includes terrain base textures but lists only the
diffuse layer budget (≤11 MB for up to 4 layers). The per-file VRAM table earlier in the file
(lines 678–693) correctly shows terrain normal maps at ~5.6 MB each (4 files × 5.6 MB = 22.4 MB),
and the running total "Total terrain detail texture VRAM: 33.6 MB" is stated below that table.
However, the Scene VRAM Budget table does not have a distinct row for terrain normal maps — the
33.6 MB total is not captured anywhere in the budget table, and the "Miscellaneous" catch-all
(≤48 MB) would need to absorb it. This makes the budget table's total of ≤180 MB unverifiable
against the per-file totals.

**Proposed resolution**: Add a "Terrain normal maps (4 × 2048×2048 DXT5, 4-level mip)" row to the
Scene VRAM Budget table with ≤23 MB budget. Update the running total accordingly. Confirm the grand
total remains within the ≤180 MB ceiling.

---

### B. `building-atlas-layout.md`

---

**[INCONSISTENCY] B-1 — HIGH: Phase 5 sign-off text references obsolete 4×4 grid cell dimensions**

The Phase 5 sign-off block (lines 318–342) states:

> "diffuse at DXT1 256×256 effective per island, normal at DXT5nm 256×256 per island"

This was authored when cells were 512×512 (sub-divided into four 256×256 island slots per the
original shared-cell design). After the phase-11e 8×8 expansion, each variant now occupies a full
512×512 cell with 496×496 usable area. The "256×256 effective per island" figure is now stale.
Artists referencing this sign-off record might incorrectly constrain their UV islands to a quarter
of the available cell space.

**Proposed resolution**: Add a correction note directly under the Phase 5 sign-off block: "UPDATED
phase-11e: effective resolution per UV island is now 496×496 px (full usable area of each 512×512
dedicated cell). The '256×256 effective per island' figure in this sign-off record reflects the
pre-phase-11e shared-cell design and is superseded." Alternatively, strike through the outdated
figure.

---

**[GAP] B-2 — HIGH: Road marking atlas upload path `GL_TEXTURE_MAX_LEVEL` dispatch not specified in `texture-cache.md`**

The road marking atlas (1024×1024 DXT5/BC3, linear upload) has no row in the
`texture-cache.md` `GL_TEXTURE_MAX_LEVEL` dispatch table. `building-atlas-layout.md` states it
uses a 4-level mip chain ("4-level mip chain (1024→512→256→128); clamp at 4 levels via
`GL_TEXTURE_MAX_LEVEL = 3`"), but `texture-cache.md`'s dispatch table does not include a row for
road marking atlases. The linear pool path (`IVideoDriver::getTexture()`) cannot set
`GL_TEXTURE_MAX_LEVEL` — the same constraint that affects `_n` and `_lm` textures applies here.
If the road marking atlas is loaded via the linear pool, the 4-level mip clamp specified in
`building-atlas-layout.md` cannot be enforced at runtime.

**Proposed resolution**: Either (a) add the road marking atlas to the `loadSRGB()` path (resolves
the `GL_TEXTURE_MAX_LEVEL` enforcement gap and is appropriate if RGB channels encode visible color),
or (b) explicitly document that linear-pool upload means the mip clamp is not enforced and the
driver may sample additional mip levels beyond level 3, then confirm this is visually acceptable.
Add a row to the `texture-cache.md` dispatch table for road marking atlas.

---

**[GAP] B-3 — MEDIUM: No normal or specular atlas specification for the building facade pipeline**

`building-atlas-layout.md` is entirely dedicated to the diffuse atlas. The spec mentions building
facade normals in passing (sign-off records reference "normal at DXT5nm 256×256 per island") but
there is no:

- Normal atlas filename
- Normal atlas resolution (4096×4096? Same as diffuse? Separate file?)
- Normal atlas cell layout
- Normal atlas upload path or `GL_TEXTURE_MAX_LEVEL` specification
- Specular/roughness atlas for building facades

If building facade normals and specular maps exist as production assets, they need equivalent
atlas specification. If they are deferred post-V1, that deferral must be explicitly stated in
the file.

**Proposed resolution**: Add a "Building Facade Normal Atlas (V1 status)" section. Either specify
the atlas layout (filename, resolution, cell grid — likely matching the diffuse atlas grid) or
explicitly state "Building facade normal and specular maps are out of scope for V1 — facades use
flat normals (no per-fragment normal mapping) in V1." If deferred, document the future atlas
filename convention so artists know what to produce in later phases.

---

**[INCONSISTENCY] B-4 — MEDIUM: Road marking atlas sRGB vs linear classification conflicts with visual content**

`building-atlas-layout.md` §Road Marking Atlas states upload path is "linear (NOT sRGB — decal
mask data, not diffuse color)" (line 133). The format is DXT5/BC3 with "alpha = decal mask."
However, the road markings spec in `2d-texture-standards.md` describes road marking content as
"white lane markings, crosswalk stripes, turn arrows" — these are visually authored colors
requiring perceptual correctness. Lane marking white and crosswalk white are perceptual colors
that will appear visually darker in linear space than authored in a DCC tool (because DCC tools
work in sRGB by default). This is the same gamma-correctness issue the spec explicitly documents
for billboard atlases.

Note: This is the same root problem as Issue A-4 above, appearing in a different file. Tracking
separately because both files need to be updated.

**Proposed resolution**: Same as A-4 — decide whether road marking RGB channels are grayscale mask
values (linear upload is fine) or perceptual colors (sRGB upload required). Add a clarifying
statement and update upload path consistently in both files.

---

**[MISSING] B-5 — MEDIUM: No specification for the minimap texture or procedural terrain chunk overlay textures**

The minimap system (`architecture/ui-ux/minimap.md`) renders a top-down view of the city.
No texture specification exists for:

- Minimap render target format (what GL texture is the minimap drawn into?)
- Minimap texture dimensions and update frequency
- Whether the minimap texture is a separate atlas or a framebuffer attachment
- Zone color overlay textures (residential green, commercial blue, industrial yellow tints applied
  to the minimap or world view)

These are texture pipeline concerns with VRAM, format, and upload path implications.

**Proposed resolution**: Add a "Minimap Render Target" entry to `2d-texture-standards.md` under
the UI textures section, even if the answer is "512×512 RGBA8 render texture, updated once per
simulation tick, no mip chain, linear upload, not in VRAM budget table (transient framebuffer
attachment)."

---

**[GAP] B-6 — LOW: No cell assignment for road marking atlas content in V1 validator checks**

`validate_assets.py` checks are enumerated for building and vehicle atlas cells. The road marking
atlas (1024×1024, 16 cells) has content specified in the cell assignment table, but no
corresponding `validate_assets.py` check number or validation rule is referenced anywhere in either
atlas spec file. The building atlas cells have explicit check numbers (e.g., check #10 for registry
UV cell assignments, check #12 for vehicle normal atlas UV coordinates). Road marking atlas cells
have no analogous validator check.

**Proposed resolution**: Define a validate_assets.py check for road marking atlas cells (e.g.,
check #16: verify road_marking atlas DDS dimensions = 1024×1024, DXT5/BC3 format, mip count >= 4,
and that no cell assignment references a cell index outside 0–15). Add the check number to the road
marking atlas spec in `building-atlas-layout.md`.

---

### C. `texture-cache.md`

---

**[MISSING] C-1 — HIGH: `loadLinear()` file format constraint not consistently propagated to all `_n` and `_lm` users**

`texture-cache.md` states "`loadLinear()` only accepts formats that Irrlicht's active image loaders
support — PNG, JPG, TGA, BMP" because the Irrlicht DDS loader is disabled. All `_n` (normal map)
and `_lm` (lightmap) textures are loaded via `loadLinear()`. This means runtime `_n.dds` files
CANNOT be loaded through the linear pool path — only PNG, JPG, TGA, or BMP files can be.

However:
- The `2d-texture-standards.md` naming convention says `_n` files are DDS
- The Resolution Matrix references `_n.dds` files
- All terrain normal export commands output `.dds` files
- The GL dispatch table shows `_n | loadLinear() | GL_COMPRESSED_RGBA_S3TC_DXT5_EXT`

This is a fundamental contradiction: normal maps are specced as DDS but `loadLinear()` cannot
load DDS files. The spec resolves this implicitly by saying vehicle normals use the
"pre-bake exactly 4 mip levels" workaround, but that workaround address `GL_TEXTURE_MAX_LEVEL`
control only, not the format loading issue. There is no explicit statement that terrain or building
normal maps must be stored as PNG (non-DDS) if they use the linear pool, or that they must use
a raw-GL path instead.

**Proposed resolution**: Explicitly state the V1 normal map file format. Options: (a) normal maps
are stored as PNG (lossy-free, no block compression) and loaded via `loadLinear()` — simple but
larger files and no DXT5nm compression; (b) normal maps use a third raw-GL upload path similar to
sRGB textures but without sRGB internal format — this requires `TextureCache` expansion. Document
whichever is chosen. If option (a), update the naming convention table to show `_n` files may be
either `.dds` or `.png` with clear guidance.

---

**[MISSING] C-2 — HIGH: No `getSRGBGLuint()` method documented in TextureCache public API**

`shader-loading.md` (line 46) references `textureCache->getSRGBGLuint(filename)` as a method that
"provides the raw `GLuint` for the sRGB texture to bind" in `OnSetConstants()`. This method is
also referenced in the terrain splat shader binding sequence (shader-loading.md lines 107, 108).
However, `texture-cache.md` does not document `getSRGBGLuint()` as part of the `TextureCache`
public API. Only `getSplatMapGLuint()` is documented as an external accessor (texture-cache.md
line 162). The `getSRGBGLuint()` method is effectively undocumented in the canonical TextureCache
spec file.

**Proposed resolution**: Add `getSRGBGLuint(const std::string& filename) const` to the
`texture-cache.md` API section with its contract: "Returns the raw `GLuint` for a loaded sRGB
texture, for binding to a GLSL `sampler2D` uniform in building or terrain shaders. Returns `0`
if the path is not loaded. Must not be called from within `evictUnreferenced()` (eviction-during-
draw safety — same constraint as `evictUnreferenced()` itself)."

---

**[INCONSISTENCY] C-3 — MEDIUM: sRGB pool VRAM estimation applies ×1.33 mip overhead but building atlas has 5 mip levels, not the standard 4**

The VRAM estimation formula for DXT1/BC1 is `ceil(w/4) × ceil(h/4) × 8 × 1.33`. The 1.33 factor
represents a standard 4-level mip pyramid overhead (geometric series 1 + 0.25 + 0.0625 + ... ≈
1.333). The primary building atlas (4096×4096 DXT1) has 5 mip levels (4096→2048→1024→512→256),
not 4. The 5-level overhead factor is 1 + 0.25 + 0.0625 + 0.015625 + 0.00390625 ≈ 1.332, which
is nearly identical to the 4-level case (within 0.001%). So the formula produces the correct result
numerically. However, the spec says "×1.33 mip overhead" without explaining this near-equivalence,
which may cause confusion for developers checking the 5-level atlas calculation.

**Proposed resolution**: Add an inline note: "The 1.33× factor applies to both 4-level and 5-level
mip chains — the fifth level adds only ~0.4% of mip-0 size and is negligible for budget estimation."

---

**[INCONSISTENCY] C-4 — MEDIUM: Dispatch table row for road tileable texture uses `_tileable` suffix but naming convention table does not include it**

The `GL_TEXTURE_MAX_LEVEL` dispatch table in `texture-cache.md` (line 83) has a row:

> `Road tileable texture (sRGB DXT5) | _tileable | loadSRGB() | GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT | 3`

The suffix `_tileable` is used as a dispatch key here. As noted in Issue A-2, this suffix does not
appear in the `2d-texture-standards.md` naming convention table. The dispatch table's use of
`_tileable` as a distinct suffix implies the `TextureCache` is expected to identify this texture
by its suffix, but the naming convention table explicitly forbids any suffix other than the six
listed. These two specifications directly contradict each other.

**Proposed resolution**: Same as A-2. Add `_tileable` to the naming convention table, or document
that `road_asphalt_tileable.dds` is a named exception identified by exact filename (not suffix)
and update the dispatch table row comment accordingly.

---

**[GAP] C-5 — LOW: `loadSplatMap()` PNG decoder does not specify what to do if `createImageFromFile()` returns null**

`texture-cache.md` documents the splat map PNG decoder using `IVideoDriver::createImageFromFile()`.
The documentation shows `IImage* img = m_driver->createImageFromFile(path.c_str())` but does not
specify what `loadSplatMap()` must do if `createImageFromFile()` returns `nullptr` (file not
found, unsupported format, or corrupt PNG). Subsequent calls to `img->lock()` on a null pointer
would crash.

**Proposed resolution**: Add null-check documentation: "If `createImageFromFile()` returns null,
log an error, set `texId = 0`, and return `0`. Do not proceed to `lock()` or `glTexImage2D()`."

---

### D. `shader-loading.md`

---

**[GAP] D-1 — MEDIUM: No specification for building shader texture bindings**

`shader-loading.md` specifies the terrain splat shader's 5-unit binding sequence in detail. No
equivalent specification exists for the building shader — which texture units does the building
fragment shader bind? The building shader samples at minimum: diffuse (unit 0, sRGB raw-GL),
possibly normal map (unit 1), possibly specular/lightmap. Without an authoritative binding spec,
building shader authors may use arbitrary unit numbers that conflict with the terrain shader's
reserved units 4–8 or the billboard unit 9.

**Proposed resolution**: Add a "Building Shader Texture Binding Sequence" section specifying which
texture units the building fragment shader uses (at minimum: unit 0 = diffuse, unit 1 = normal,
unit 2 = specular, unit 3 = lightmap) and confirm these are consistent with `shader_constants.h`.
Note which units require `glActiveTexture`/`glBindTexture` raw binding (sRGB diffuse) vs. Irrlicht
material binding (normal, specular if linear-pool `ITexture*`).

---

**[GAP] D-2 — LOW: No shader file listed for the building/vehicle custom shader**

The GLSL Shader Files section (lines 182–191) lists six mandatory shader files: two for lighting,
two for terrain, two for billboard. No building diffuse/normal shader pair is listed. The road
shader (`road.vert` referenced by name on line 169) is also not in the mandatory shader files list.
The UI quad shaders are noted as Phase 8 additions. This means `validate_assets.py` / CI cannot
verify that building and road shader files exist.

**Proposed resolution**: Add the following to the mandatory GLSL shader files list:
`assets/shaders/building.vert` and `assets/shaders/building.frag`,
`assets/shaders/road.vert` and `assets/shaders/road.frag`,
and add a corresponding CI check (analogous to `ShaderLoadingTest::LightingShaderCompilesWithoutError`).

---

## Cross-File Inconsistencies

---

**[INCONSISTENCY] X-1 — CRITICAL: `2d-texture-standards.md` suffix table and `texture-cache.md` dispatch table have incompatible `_splat` treatment**

`2d-texture-standards.md` naming convention table (line 218–225) lists exactly six suffixes and
states "validate_assets.py must reject any DDS file whose name does not end with one of these six
suffixes." `_splat` is not in this list. However `texture-cache.md` dispatch table (line 85)
includes a `_splat` row as if it is a valid suffix. The `texture-cache.md` text below the dispatch
table clarifies: "`_splat` is NOT a DDS suffix — it is an internal documentation convention."

However, this clarification is only in `texture-cache.md`, not in `2d-texture-standards.md`. The
CLAUDE.md system prompt states the `_splat.png` suffix is valid (under the note "Splat maps: PNG
(exception)"), which implies `_splat.png` is a legitimate runtime file suffix. But:

- `2d-texture-standards.md` says `validate_assets.py` must reject files not ending in the six
  approved suffixes — yet `terrain_blend_splat.png` (or whatever the splat map is named) would
  need a suffix not in that table.
- If splat maps are named `terrain_blend.png` (no `_splat` in the name), the naming convention
  table is consistent. But no canonical splat map filename is stated in either spec.

**Proposed resolution**: (1) Add `_splat.png` (or `_splat`) as a seventh valid recognized suffix
in `2d-texture-standards.md`'s naming convention table, marked as "PNG only — not DDS." (2) State
the canonical splat map filename pattern (e.g., `chunk_<x>_<z>_splat.png` or `terrain_blend.png`).
(3) Confirm `validate_assets.py` will accept PNG files with `_splat` suffix and not reject them
for being PNG instead of DDS.

---

**[INCONSISTENCY] X-2 — HIGH: `building-atlas-layout.md` states primary atlas format is "DDS DXT1 sRGB" but also simultaneously documents a "V1 PNG workaround"**

The document opens with "Format: DDS DXT1 sRGB" (line 11) as the authoritative format, then
immediately below (lines 14–29) documents a "V1 implementation exception" where the on-disk file
is actually `buildings_atlas_d.png` loaded via `IVideoDriver::getTexture()`. The "Production target
(Phase 11+)" clause says DDS migration is pending. This means the stated format and the actual V1
runtime format differ:

- Stated format: DXT1 sRGB DDS
- Actual V1 runtime format: PNG loaded via `IVideoDriver::getTexture()` (linear, uncompressed)

`2d-texture-standards.md` does NOT mention this PNG workaround at all — it consistently describes
`buildings_atlas_d.dds` as the building atlas. A developer reading only the texture standards file
will implement DDS loading; a developer reading only the atlas layout file will see the PNG
exception. The two files are inconsistent on a critical implementation detail.

**Proposed resolution**: Add a matching "V1 PNG workaround" note in `2d-texture-standards.md` in
the building atlas section, cross-referencing the `building-atlas-layout.md` caveat. Mark the
production DDS path clearly as "Phase 11+" and the V1 PNG path as the current implementation.

---

**[INCONSISTENCY] X-3 — MEDIUM: VRAM budget for UI sprite sheet states 16 MB but format differs from system prompt**

`2d-texture-standards.md` §VRAM Budget (line 708) states the UI sprite sheet is "2048×2048 RGBA8,
no mips, ≤16 MB." The CLAUDE.md system prompt states "UI sprite sheet: 2048×2048 RGBA8 UNORM DDS
(exported via `export_textures.py --format rgba8`)." Both agree on format and resolution. However,
`2d-texture-standards.md` §UV & Atlas Strategy (line 263) states the UI sprite sheet "must be
uploaded via `glTexImage2D` with `GL_RGBA8` internal format (not DXT)." The `texture-cache.md`
dispatch table does not contain a row for the UI sprite sheet at all — neither `loadLinear()` nor
`loadSRGB()` nor `loadSplatMap()`. How the UI sprite sheet is loaded at runtime is unspecified in
`texture-cache.md`.

**Proposed resolution**: Add a `TextureCache` method documentation entry (or a note in
`texture-cache.md`) for the UI sprite sheet: "UI sprite sheet is NOT managed by `TextureCache`.
It is uploaded directly by `IrrlichtUIBackend` via a dedicated raw-GL `glTexImage2D` call during
UIManager initialization and is not subject to `TextureCache` eviction." If the UI sprite sheet IS
intended to go through `TextureCache`, add it to the dispatch table.

---

## Issues Not Found / Areas Confirmed Clean

The following areas were reviewed and found internally consistent:

- Billboard atlas format (DXT5 sRGB, 1024×128, 8 frames, mip chain, clamp-to-edge) — fully
  specified and consistent across both files
- Vehicle diffuse atlas, vehicle sprite atlas, vehicle normal atlas — format, resolution, upload
  path, and exception rationale are consistent across `building-atlas-layout.md` and
  `2d-texture-standards.md`
- sRGB upload path for diffuse textures — extension check, GL_EXT_texture_sRGB, fallback shader
  gamma correction — fully specified and consistent
- DXT5nm swizzle pipeline (modulo the duplication noted in A-13) — technically correct and complete
- Splat map upload as uncompressed RGBA8 via `glTexImage2D` — consistent across all three files
- `GL_TEXTURE_MAX_LEVEL` for sRGB pool textures (primary atlas = 4, fallback atlas = 3, billboard =
  3, road tileable = 3) — correctly specified and consistent
- anisotropic filtering tiers and extension check — complete and consistent
- Entity destroy / sRGB release / `evictUnreferenced()` sequence — complete and consistent
- LOD swap sRGB tracking policy — complete and consistent
- `EDT_NULL` guards — comprehensive and consistent across both files
- Normal map Y-convention (OpenGL, green = +Y) — correctly specified
- Power-of-two constraint — stated clearly
- Terrain splat channel-to-material assignment — consistent across spec and shader-loading.md

---

## Issue Count by Severity

| Severity | Count |
|---|---|
| CRITICAL | 2 (A-1, X-1) |
| HIGH | 7 (A-2, A-3, A-4, A-5, A-6, B-2, B-3, C-1, C-2, X-2) |
| MEDIUM | 8 (A-7, A-8, A-9, A-10, A-11, A-12, B-1, B-4, B-5, C-3, C-4, D-1, X-3) |
| LOW | 4 (A-13, A-14, B-6, C-5, D-2) |

*(Note: Issue count in table reflects actual issues listed above; per-row totals may differ from
heading counts due to cross-file issues appearing in both category counts.)*
