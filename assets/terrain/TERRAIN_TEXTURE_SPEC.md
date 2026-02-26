# Terrain Texture Authoring Specification — Phase 5

**Status**: AUTHORITATIVE — replaces placeholder notes in `TERRAIN_TEXTURES.md`
**Author role**: `graphics-artist-2d-texture`
**Date**: 2026-02-25
**Scope**: All 8 runtime DDS files required for the Phase 5 grassland biome terrain system.

---

## 1. Pipeline Summary

All 8 DDS files are deposited at `assets/terrain/`. The runtime reads them via `TextureCache`:

- Diffuse textures (`_d.dds`): uploaded via the **raw-GL sRGB path** (`glCompressedTexImage2D`
  with `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`). Author in sRGB. Embed sRGB ICC profile in source PNG.
- Normal maps (`_n.dds`): uploaded via `loadLinear()` (`IVideoDriver::getTexture()`). Linear data
  only — no sRGB profile in source PNG. DXT5nm packing mandatory.
- Splat maps: `terrain_chunk_splat.png` (already exists, not covered here).

```text
Source PNG (sRGB ICC)  →  DXT1 sRGB DDS  →  GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
Source PNG (linear)    →  DXT5nm DDS     →  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT (linear)
```

### Format table

| File | Compression | Upload path | GL internal format | Mip levels |
|---|---|---|---|---|
| `terrain_grass_d.dds` | DXT1/BC1 | sRGB raw-GL | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | 4 (2048→256) |
| `terrain_asphalt_d.dds` | DXT1/BC1 | sRGB raw-GL | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | 4 (2048→256) |
| `terrain_soil_d.dds` | DXT1/BC1 | sRGB raw-GL | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | 4 (2048→256) |
| `terrain_concrete_d.dds` | DXT1/BC1 | sRGB raw-GL | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | 4 (2048→256) |
| `terrain_grass_n.dds` | DXT5/BC3 | linear | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | 4 (2048→256) |
| `terrain_asphalt_n.dds` | DXT5/BC3 | linear | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | 4 (2048→256) |
| `terrain_soil_n.dds` | DXT5/BC3 | linear | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | 4 (2048→256) |
| `terrain_concrete_n.dds` | DXT5/BC3 | linear | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | 4 (2048→256) |

`GL_TEXTURE_MAX_LEVEL = 3` is set by `TextureCache` at upload time — do not author more than 4
mip levels into the DDS header for files destined for the raw-GL sRGB path. For normal maps
uploaded via `loadLinear()`, pre-bake exactly 4 levels (2048, 1024, 512, 256) into the DDS file
so the driver does not synthesise additional levels via bilinear mip generation.

---

## 2. Tiling Context

- Terrain chunk size: 128 m × 128 m world space
- LOD0 quad grid: 32 × 32 cells, each cell 4 m × 4 m
- Texture repeat frequency: **one full tile per 4 m quad cell** at LOD0
- At 2048 px resolution, 1 texel = 4 m / 2048 = approximately 1.95 mm world-space — extreme
  close-up detail is available but the camera pitch range (−70° to −20°) means the player
  never sees the surface from below 20° from vertical.
- All 8 textures must tile seamlessly with themselves (no visible seam in any direction).
- Viewed primarily from steep top-down angles. High-frequency micro-detail that reads well
  top-down is more valuable than low-angle surface silhouette detail.
- At mip level 3 (256 px, representing a 4 m area), each mip texel covers approximately 16 mm
  world-space. Macro-scale color and value variation must read correctly at this resolution.

---

## 3. Art Style Direction

**Realistic-stylized**: matching the visual register of Cities: Skylines (2015) and SimCity 4
(2003) — richer saturation than raw photography, simplified micro-texture, strong material
read at all zoom levels. Not photorealistic. Not cartoony.

**Color temperature guideline**: warm sunlit palette overall. Grass is golden-green, soil is
warm ochre, asphalt is cool blue-grey, concrete is warm light grey. These temperature
contrasts aid material identification when the splat map blends between zones at road edges.

**Zone overlay avoidance**: The game renders colored zone overlays (residential = green tint,
commercial = blue tint, industrial = yellow tint). Avoid heavy use of pure green (#00FF00
family) in grass or pure yellow in soil — these hues will merge with zone overlays and confuse
zone-type readability. Shift grass toward olive-gold and soil toward rust-ochre.

---

## 4. Diffuse Texture Specifications

### 4.1 terrain_grass_d.dds

**Semantic**: Natural grass ground cover. Fills splat channel R. The dominant biome surface
for undeveloped and park tiles.

#### Colors

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone blade) | `#7A8C3E` | 122 | 140 | 62 | Muted olive-green; principal blade color |
| Secondary (shadow pocket) | `#4E5C28` | 78 | 92 | 40 | Dark gap between blade clusters |
| Tertiary (dry tip highlight) | `#A09848` | 160 | 152 | 72 | Slightly yellow-gold blade tip; sunlit |
| Soil show-through | `#7A6448` | 122 | 100 | 72 | Warm brown visible between sparse patches |
| Specular highlight catch | `#B4B068` | 180 | 176 | 104 | Hot highlight on near-vertical blade faces |

Luminance range: **min 62 (shadow pocket) — max 180 (highlight catch)**, measured as sRGB
perceived luminance (Y component). Do not compress the range — the variance maintains
read at DXT1 compression and at LOD3 (256 px mip).

#### Surface description

Roughness: **high**. Grass blades are matte-diffuse; near-zero specular contribution. No
smooth transitions — micro-surface is highly irregular.

Tiling pattern: **isotropic stochastic**. No directional bias (grass grows in all directions;
a directional tile would reveal repeat as a linear pattern from top-down view). Use a
stochastic Voronoi or fractal Brownian motion (fBm) base with point-seeded blade clusters
scattered over it.

Detail frequency breakdown:

- **High frequency (512–2048 px scale)**: individual blade geometry impressions — thin
  elongated strokes 8–24 px long, 1–3 px wide, scattered at random orientations. Density:
  approximately 400–600 blade impressions visible at 2048 px. At DXT1 compression, blades
  narrower than 2 px will alias — keep minimum blade width 2 px in source.
- **Medium frequency (128–512 px scale)**: blade cluster groupings — patches of denser
  growth alternating with patches where soil shows through. Patch scale 60–120 px diameter.
  Use a low-frequency noise (Perlin or Worley) to drive density variation.
- **Low frequency (32–128 px scale)**: overall luminance variation — subtle darkening in
  hollow areas, brightening on ridge areas. Amplitude ±12 luminance units. This variation
  prevents the surface reading as flat and featureless at far distances (mip level 2–3).

Key visual features:

- Blade clusters: tight groups of 5–12 blade strokes radiating from a common root point.
  Groups are distributed stochastically, not on a grid (grid will alias into a pattern).
- Soil show-through: in the lower-density patches, soil color bleeds through between blade
  roots. Approximately 15% of total surface area should show soil-brown tones.
- Dry tip gradient: each blade impression should be slightly lighter at the tip end than
  at the root. Simulate this with a subtle radial gradient applied to each stroke, or
  achieved implicitly by the highlight catch color at the top of stroke shapes.
- No pure-green pixels: maximum green channel value after sRGB encoding must be kept
  below 200 (out of 255). Pure green (#00FF00 or nearby) conflicts with residential zone
  overlays and creates halation at DXT1 block boundaries.

DXT1 compression artifact mitigation:

DXT1 operates on 4×4 texel blocks. Thin single-pixel grass strokes at high contrast against
a dark background produce visible block boundaries. Mitigate by:

1. Using broader color variance (do not place a 255-luminance blade adjacent to a 40-luminance
   shadow with no intermediate tones in the same 4×4 block).
2. Keeping blade colors within a 60-unit luminance spread within any 4×4 region.
3. Using slight blur (0.5 px Gaussian) on the final source PNG before DXT1 compression —
   softens the hard blade edges just enough to reduce block-boundary contrast without
   losing the blade impression at normal view distances.

---

### 4.2 terrain_asphalt_d.dds

**Semantic**: Road and paved driveway surface. Fills splat channel G. Appears wherever road
tiles are painted over terrain.

#### Colors

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone aggregate) | `#3C3C3C` | 60 | 60 | 60 | Dark grey aggregate body |
| Worn highlight (oxidized surface) | `#585858` | 88 | 88 | 88 | Lighter grey on worn areas |
| Deep crack / joint gap | `#1C1C1C` | 28 | 28 | 28 | Near-black in crack recesses |
| Aggregate specks (light) | `#6A6A6A` | 106 | 106 | 106 | Quartz aggregate pebbles |
| Aged tar stain | `#2A2820` | 42 | 40 | 32 | Slight warm-dark for tar pools |

Luminance range: **min 28 (crack depth) — max 106 (light aggregate)**, sRGB. Keep the range
narrow (max spread 78 units) to match real asphalt's low-albedo, low-variance character.

Note on color temperature: asphalt reads as slightly cool blue-grey, not neutral grey, because
road surfaces are viewed under sky light reflected from the horizontal plane. Author the
primary aggregate with a faint blue bias: R ≤ G = B by approximately 2–4 units (e.g.
`#3A3C3C` rather than `#3C3C3C`). This is subtle but improves material distinctness from
concrete, which has a warm grey bias.

#### Surface description

Roughness: **medium-high**. Fresh asphalt has a fine granular texture; aged asphalt is
slightly smoother with macrocracking. This texture represents aged (2–5 year old) road
surface.

Tiling pattern: **isotropic fine granular** with superimposed macro-crack network. The
granular base tiles equally in all directions. Macro-cracks can have a slight directional
bias (road compaction stress produces cracks along the traffic direction) but should not be
so strongly directional that a seam is visible when tiling 45°-rotated instances.

Detail frequency breakdown:

- **High frequency (256–2048 px scale)**: fine aggregate texture — rounded pebble impressions
  1–6 px in diameter. Author 300–500 distinct aggregate specks per 512 px area. Use a
  stochastic point process for placement (not grid). Each speck should have a soft circular
  mask with 1 px feathering — avoids DXT1 block artifacts from sharp pebble edges.
- **Medium frequency (64–256 px scale)**: macrocrack network — hairline fissures 1–2 px
  wide, forming an irregular polygonal network (Voronoi-edge-based). Cell size 80–150 px.
  Crack luminance: 28 (near-black). Feather crack edges 1 px. Total crack network area:
  approximately 3–5% of surface.
- **Low frequency (16–64 px scale)**: surface age variation — subtle luminance patches
  (±8 units) representing differential weathering. Smooth, large-radius (30–60 px) Perlin
  noise. Prevents the surface from reading as a perfectly uniform grey from mid-zoom.

Key visual features:

- Aggregate pebbles: the most important micro-feature. Each pebble impression is a slightly
  lighter disc (106 grey) with a dark shadow crescent on the camera-facing underside
  (28 grey). At top-down view, shadow crescent should appear on the south (bottom) edge of
  each pebble to match default directional light coming from the upper-left.
- Crack network: a low-contrast web of fine cracks. Do not paint thick cracks (>3 px) —
  realistic asphalt cracking is hairline. The crack network should form closed polygons,
  not straight parallel lines (which would immediately read as a tiling grid).
- Tar patches: 2–4 dark blobs per 512 px area representing pothole repairs. Slightly
  smoother texture within the patch, 10% darker than primary aggregate.
- Road markings are NOT authored into this texture. Road lane markings are handled by the
  road marking atlas (1024×1024 DXT5, applied as decals). Keep this diffuse clean.

DXT1 compression considerations: asphalt is inherently low-contrast and compresses well with
DXT1. The only risk area is where light aggregate specks sit directly adjacent to dark crack
shadows within the same 4×4 block. Limit maximum local contrast: ensure no 4×4 block
contains both a 106-luminance speck and a 28-luminance crack shadow — maintain at least one
intermediate-luminance texel between them.

---

### 4.3 terrain_soil_d.dds

**Semantic**: Bare earth, earthworks, and unpaved areas. Fills splat channel B. Appears on
freshly graded terrain, undeveloped industrial zones, and earthwork tiles.

#### Colors

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (damp mid-tone) | `#7A5C3C` | 122 | 92 | 60 | Warm rust-brown; main soil body |
| Dry surface highlight | `#A07850` | 160 | 120 | 80 | Lighter, slightly desaturated dry crust |
| Wet shadow (compact) | `#4E3820` | 78 | 56 | 32 | Dark compact soil in shadow zones |
| Aggregate grit (coarse) | `#8A7060` | 138 | 112 | 96 | Small pebbles / coarse sand grains |
| Clay layer show-through | `#6A4830` | 106 | 72 | 48 | Darker orange-clay layer in deep ruts |

Luminance range: **min 56 (compact shadow) — max 160 (dry crust highlight)**, sRGB.
This range of 104 units gives soil the most visual texture of the four terrain types,
distinguishing it from the flatter asphalt and the greener grass at a glance.

Hue guidance: soil must read as **rust-ochre-brown**, not yellow-brown. Avoid hues near
`#B4922C` (pure yellow-ochre) — this conflicts with industrial zone overlays. Shift hue
toward red-brown (R > G > B, with R/G ratio approximately 1.3–1.4).

#### Surface description

Roughness: **very high**. Soil has the highest surface roughness of the four terrain types.
Micro-relief from individual granule stacking, wheel ruts, tool marks, and desiccation
cracking all contribute to the surface character.

Tiling pattern: **isotropic granular** with superimposed low-frequency directional variation.
At the granule scale (high frequency) the pattern is fully isotropic. At the medium
frequency scale, slight directionality is acceptable (mimicking compaction lines from
grading equipment) provided the pattern tiles seamlessly in all four cardinal directions.

Detail frequency breakdown:

- **High frequency (64–2048 px scale)**: individual granules — irregular rounded shapes
  1–8 px diameter, placed stochastically. Mix three size classes: fine (1–2 px, ~60% of
  count), medium (3–5 px, ~30%), coarse (6–8 px, ~10%). Each granule has a slight
  specular highlight on its top face. At 2048 px, a soil surface should have approximately
  1000–2000 visible granule impressions of mixed sizes.
- **Medium frequency (32–128 px scale)**: desiccation crack network — similar to asphalt
  cracks but larger cell size (120–200 px) and slightly wider cracks (2–4 px). Crack
  luminance: wet-dark (56). Cracks form closed polygon cells. Total crack area: 6–10% of
  surface (more visible than asphalt cracks).
- **Low frequency (16–64 px scale)**: moisture variation zones — large smooth gradients
  (60–100 px radius) shifting between damp (dark, primary 122) and dry (light, 160)
  surface states. Amplitude ±20 luminance units. This gives the soil surface a mottled
  appearance consistent with real disturbed earth.

Key visual features:

- Granule field: the defining character of soil texture. The granule mix of three size
  classes produces a natural-looking surface that avoids the uniform-sand appearance of
  a purely fine-granule field.
- Desiccation polygon network: large-cell crack pattern (120–200 px cells) in the medium
  frequency layer. More prominent than the asphalt crack network. Cracks must be wider
  (2–4 px) and have a slightly beveled profile (darker at crack center, lighter at edges
  where the polygon face rounds toward the crack) to give three-dimensional impression.
- Rut impressions: 3–5 subtle elongated depressions per 512 px area, simulating wheel
  tracks from construction vehicles. Each rut is a narrow trough (8–16 px wide, 50–80 px
  long) of darker, compacted soil material. Place at random angles — do not align ruts
  parallel to texture edges (would produce visible tile boundary).
- No green tones: soil must contain no green channel values exceeding the red channel.
  G must always be less than R in sRGB space throughout the texture.

---

### 4.4 terrain_concrete_d.dds

**Semantic**: Paved plazas, sidewalks, parking lots, and industrial pads. Fills splat
channel A. Appears wherever the player paints a concrete zone or places civic buildings.

#### Colors

| Role | Hex | R | G | B | Notes |
|---|---|---|---|---|---|
| Primary (mid-tone slab) | `#C0B8A8` | 192 | 184 | 168 | Warm light grey; main slab color |
| Fresh pour highlight | `#D8D4CC` | 216 | 212 | 204 | Lighter, slightly blue-grey new pour |
| Shadow / expansion joint | `#7A7870` | 122 | 120 | 112 | Dark joint line between slabs |
| Aggregate exposed (course) | `#A09888` | 160 | 152 | 136 | Aggregate show through worn surface |
| Staining (oil / water) | `#A4A090` | 164 | 160 | 144 | Slightly darker slab with stain |

Luminance range: **min 116 (joint shadow) — max 216 (fresh highlight)**, sRGB. Concrete
is the lightest of the four terrain types. The high luminance (brighter than grass and
much brighter than asphalt) gives it a distinct read at all zoom levels.

Warm grey bias: concrete must read as **warm grey** (R ≥ G > B by approximately 8–16 units).
This distinguishes it from the cool blue-grey asphalt at a glance. A concrete surface
that is pure neutral grey (#B0B0B0) will merge with asphalt in the splat-blended transition
zone and lose material distinctness.

#### Surface description

Roughness: **low-medium**. Concrete is the smoothest of the four terrain types. Surface
detail comes from the aggregate show-through (slightly grainy at high zoom) and the
expansion joint grid rather than from micro-roughness.

Tiling pattern: **regular grid (expansion joints) with isotropic aggregate fill**. Unlike
the other three textures which are purely stochastic, concrete has a structured element:
the expansion joint grid. Joint grid must tile seamlessly — use a joint period that is an
exact divisor of 2048 (e.g., 256, 512, or 342 px are reasonable slab sizes).

Recommended joint grid: 512 px period (i.e., 4 × 4 slabs per 2048 px tile). This gives
approximately 1 slab every 1 m of world space at the tiling frequency designed for 4 m
quad cells (2048 px per 4 m = 512 px per 1 m, so 512 px joint spacing = 1 m slab width,
which is narrower than a physical slab but reads well at city zoom levels).

Detail frequency breakdown:

- **High frequency (64–2048 px scale)**: aggregate show-through — fine granule impressions
  1–3 px diameter, low contrast against primary slab color (luminance difference ±8 units).
  Aggregate is isotropic, does not follow the joint grid. Approximately 500–800 visible
  granule impressions per 512 px area. Much lower contrast than soil granules.
- **Medium frequency (joint grid scale, 512 px period)**: expansion joint lines — 2–3 px
  wide, dark-filled (luminance 116), with a 1 px bright bevel on each edge (luminance 180)
  for a subtle 3D impression. Joints run in a perfect orthogonal grid aligned to texture
  UV axes. Joint intersections should have a small square widening (4 × 4 px) to simulate
  the filler plug at intersection corners.
- **Low frequency (128–512 px scale)**: slab age variation — smooth luminance patches
  (±10 units) representing differential weathering, staining, and moisture. One slightly
  dark stain patch (oil/water discoloration) per slab face on average, 80–120 px diameter,
  feathered at edges.

Key visual features:

- Expansion joint grid: the single most important structural feature. Each slab face should
  be individually varied in luminance (±8 units) so adjacent slabs are distinguishable
  without the joint lines — real concrete slabs cure and weather at different rates.
- Aggregate show-through: subtle but present. The aggregate granule field gives the surface
  texture when viewed at close range (high-zoom city view). At normal zoom, it contributes
  to perceived roughness without being individually resolved.
- Corner weathering: at expansion joint intersections, author a slight darkening gradient
  (radius 8–12 px) representing corner spalling. Corners weather faster than slab faces.
- No strong hue variation: concrete must be monochromatic (warm grey only). Occasional
  water stain can introduce a faint blue-grey desaturation (shift R/G down by 8 units
  relative to primary) but must never introduce green tones.

---

## 5. Normal Map Specifications

All four normal maps are 2048 × 2048 px, DXT5nm encoded. The shader reads them as:

```glsl
float nx = texture(u_normalMap, uv).a * 2.0 - 1.0;  // X from alpha channel
float ny = texture(u_normalMap, uv).g * 2.0 - 1.0;  // Y from green channel
float nz = sqrt(max(0.0, 1.0 - nx*nx - ny*ny));      // Z reconstructed
vec3 normal = normalize(vec3(nx, ny, nz));
```

### DXT5nm Export Pipeline (mandatory for all four normal maps)

Apply steps in order in the DCC tool before running the compressor:

1. Bake tangent-space normals at full resolution (2048 × 2048) in OpenGL convention
   (Y-up tangent space). If using Substance Painter with DirectX normals enabled, invert
   the green channel before proceeding.
2. Verify the baked normal is in OpenGL convention: a flat surface facing up must produce
   the base normal color `#8080FF` (R=128, G=128, B=255) — if the result is `#8080FF`,
   no Y-flip is needed.
3. Copy the red channel (X component) into the alpha channel of the export image.
4. Keep the green channel (Y component) unchanged.
5. Set the blue channel to exactly 127 (mid-grey, representing discarded Z).
6. Set the red channel to 0 (it is unused by the shader).
7. Export as `<name>_swizzled.png` (linear color space, no ICC profile).
8. Compress: `nvcompress -normal -bc3 <name>_swizzled.png <name>_n.dds`
   or: `compressonatorcli -fd BC3 -miplevels 4 <name>_swizzled.png <name>_n.dds`

Pre-bake all 4 mip levels (2048, 1024, 512, 256) via bicubic downsampling of the
full-resolution normal map before DXT5nm compression. Do not rely on driver-generated mips
for normal maps — bilinear mip generation does not preserve tangent-space normal vector
normalization. If the tool generates a full mip pyramid, pre-generate mip levels manually
(bicubic downsample → renormalize → DXT5nm encode per mip level) and composite into one DDS.

---

### 5.1 terrain_grass_n.dds

**Surface microdetail**: Grass blade normals. Each blade impression in the diffuse texture
has a corresponding normal deflection in the normal map. Blades deflect normals laterally
(away from blade centerline) and slightly upward (toward blade tip) — this produces the
characteristic sheen of a grass field at oblique light angles.

**Normal intensity**: **moderate**. Grass normals should deflect tangential components by
approximately ±0.4 (after decode from [0,1] to [−1,+1]). Flat reference value for a
perfectly horizontal surface is nx=0, ny=0, nz=1. Blade-deflected normals have |nx| or
|ny| in the range 0.15–0.40. Do not use high-intensity normals for grass — the matte surface
means strong normals produce unrealistic specular highlights from the reconstructed Z.

**Primary feature size at 2048 × 2048**: individual blade stroke normals are 8–24 px long,
1–3 px wide — matching the diffuse blade impressions. Each blade impression in the diffuse
must correspond to a paired normal deflection in the normal map at the same UV coordinates.

**Low-frequency base layer**: superimpose a gentle low-frequency undulation (large Perlin
noise, period 512–1024 px, amplitude ±0.10 nx/ny) representing the ground microtopography
beneath the grass. This ensures the normal map reads as a natural ground surface rather than
a perfectly flat plane with blades attached.

**Mip behavior**: at mip level 2 (512 px), individual blade normals must have merged into
a broad directional field representing the average blade orientation over each ~8 m patch.
At mip level 3 (256 px), the normal map approaches a flat base with only the large-scale
undulations remaining. This is correct behavior for a well-authored normal map mip chain.

---

### 5.2 terrain_asphalt_n.dds

**Surface microdetail**: Asphalt aggregate normals. Each aggregate pebble impression in the
diffuse has a matching dome-shaped normal in the normal map — the top of each pebble has a
near-flat normal (pointing up) and the sides have deflected normals (pointing outward).
Hairline cracks in the diffuse correspond to narrow groove normals in the normal map (inward
deflection, toward crack center).

**Normal intensity**: **subtle**. Asphalt is a predominantly flat surface. Maximum tangential
component magnitude: ±0.25. The low intensity is appropriate: the surface is macroscopically
flat (each aggregate pebble is less than 5 mm tall in world space) and heavy normals would
produce harsh specular highlights on a material that should be predominantly diffuse.

**Primary feature size at 2048 × 2048**: aggregate dome normals are 2–8 px diameter (matching
the diffuse pebble sizes). Crack groove normals are 2–4 px wide (slightly wider than the
1–2 px diffuse crack lines to ensure the gradient is smooth rather than a hard step).

**Directional note**: the aggregate pebbles have a slight asymmetric normal — the camera-
facing (south) hemisphere of each pebble is in specular highlight and the north hemisphere
is in shadow. Ensure the normal deflection on the south edge of each pebble dome points
slightly toward the camera (negative Y in OpenGL tangent space, representing a surface
normal tilted toward the viewer). This produces the characteristic sparkle of aggregate
asphalt under overhead directional light.

---

### 5.3 terrain_soil_n.dds

**Surface microdetail**: Soil granule normals combined with desiccation crack groove normals.
This normal map has the most complex multi-scale structure of the four terrain types.

**Normal intensity**: **strong**. Soil is the roughest surface. Maximum tangential component
magnitude: ±0.50. The high intensity is justified by the large physical height variation
in a granule field (granules project 5–20 mm above the compacted base in world space) and
by the deep desiccation crack troughs.

**Primary feature size at 2048 × 2048**:

- Coarse granules (6–8 px in diffuse): large dome normals, 8–12 px diameter, ±0.40–0.50
  tangential deflection.
- Medium granules (3–5 px in diffuse): medium dome normals, 4–8 px diameter, ±0.25–0.35.
- Fine granules (1–2 px in diffuse): fine perturbation, ±0.10–0.15. At mip level 1 (1024 px)
  and below, fine granule normals merge into a uniform surface roughness contribution.
- Desiccation cracks (2–4 px wide in diffuse): groove normals 4–6 px wide, with a beveled
  inward deflection of ±0.35 at crack edges, falling to 0 at crack center.

**Rut impressions**: the wheel ruts in the diffuse texture require matching normals. A rut is
a concave trough — the normal deflection should point inward (downward toward the trough
floor) at the rut edges, with a flat normal at the trough center. Rut trough normals:
|ny| = 0.30–0.40 at edges (in OpenGL convention, pointing toward the camera when the rut
runs east-west). Rut width: 8–16 px in normal map (matching diffuse rut width).

**Low-frequency base**: large-scale normal undulation (period 256–512 px, amplitude ±0.08)
representing the bulk microtopography of a graded surface. Prevents the normal map from
reading as a perfectly flat plane.

---

### 5.4 terrain_concrete_n.dds

**Surface microdetail**: Concrete aggregate show-through normals (very low relief) with
expansion joint edge normals (bevel profile along joint edges).

**Normal intensity**: **subtle**. Concrete is the flattest surface in the set. Maximum
tangential component magnitude: ±0.18 across all features. The smooth slab face has
effectively flat normals (|nx|, |ny| < 0.05) with only the joint edges and aggregate
contributing meaningful deflection.

**Primary feature size at 2048 × 2048**:

- Slab face aggregate (1–3 px in diffuse): extremely subtle perturbation, ±0.05–0.10.
  At mip level 1 and below, aggregate normals are effectively noise and average to flat.
- Expansion joint edges (joint grid at 512 px period): bevel normals along each joint
  edge. The bevel width is 4 px on each side of the 2–3 px joint gap. Normal deflection
  at the bevel edge: ±0.15 pointing outward from the joint line. This produces a visible
  highlight on the slab edge adjacent to each joint under directional light, enhancing
  the impression of concrete panel depth.
- Joint intersection corners: at the 4 × 4 px corner widening, use a dome normal
  (pointing slightly inward, ±0.12) to represent the slightly raised filler plug at
  intersection corners.

**Mip behavior**: at mip level 2 (512 px), the expansion joint bevel normals dominate and
individual aggregate normals disappear. At mip level 3 (256 px), the joint grid may not
be fully resolved — the normal map approaches flat with only the broadest joint bevel
gradients remaining. This is acceptable for far-distance rendering.

---

## 6. Authoring Quality Checklist

Before committing any DDS file, verify each item:

### Diffuse DDS

- [ ] Source PNG has embedded sRGB ICC profile (verified via `exiftool | grep -i 'color space'`)
- [ ] DDS FourCC at byte offset 84 is `0x30315844` ("DX10" extended header present)
- [ ] DX10 DXGI_FORMAT field = 72 (`BC1_UNORM_SRGB`) for DXT1 diffuse
- [ ] DDS file contains exactly 4 mip levels (2048, 1024, 512, 256)
- [ ] Texture tiles seamlessly: open in Photoshop, set canvas to 2× the texture size, tile 2×2
  — no visible seam in any direction
- [ ] No pure-green pixels in grass texture (max G channel value 195 in sRGB)
- [ ] No green tones in soil texture (G < R throughout)
- [ ] Asphalt primary color has slight cool bias (R ≤ G ≤ B in primary aggregate)
- [ ] Concrete primary color has warm bias (R ≥ G ≥ B + 8)
- [ ] Luminance range falls within spec bounds stated in section 4.x above
- [ ] DXT1 block artifacts assessed at 1:1 zoom — no visible block grid in any region

### Normal Map DDS

- [ ] Source swizzled PNG verified: alpha = X, green = Y, blue = 127, red = 0
- [ ] Flat-surface reference pixel: alpha = 128, green = 128 (decodes to nx=0, ny=0, nz=1)
- [ ] DDS FourCC: `0x35545844` ("DXT5") or DX10 extended header with BC3_UNORM (DXGI 77)
- [ ] DDS file contains exactly 4 mip levels (2048, 1024, 512, 256)
- [ ] Mip levels pre-baked via bicubic downsample (not driver-generated)
- [ ] At mip level 3 (256 px): decoded normal vectors produce no NaN (no vec with |nx|+|ny| > 1)
- [ ] Normal intensity within the range stated for each texture in section 5.x above
- [ ] No sRGB ICC profile embedded in source swizzled PNG (normal data is linear)

### Seam test procedure

Export a 4096 × 4096 composited tile image by placing four copies of the 2048 × 2048
texture in a 2 × 2 grid (top-left, top-right, bottom-left, bottom-right, all at 0°
rotation). Zoom in on each of the four seam lines (top horizontal, bottom horizontal,
left vertical, right vertical). No visible discontinuity of color, pattern, or direction
must be present at any seam.

---

## 7. Export Commands (Reference)

### Diffuse textures (all four `_d.dds` files)

```bash
# NVTT (Linux preferred):
nvcompress -color -bc1 terrain_grass_src.png terrain_grass_d.dds
nvcompress -color -bc1 terrain_asphalt_src.png terrain_asphalt_d.dds
nvcompress -color -bc1 terrain_soil_src.png terrain_soil_d.dds
nvcompress -color -bc1 terrain_concrete_src.png terrain_concrete_d.dds

# Compressonator (Windows preferred, produces exactly 4 mip levels):
compressonatorcli -fd BC1 -miplevels 4 terrain_grass_src.png terrain_grass_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_asphalt_src.png terrain_asphalt_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_soil_src.png terrain_soil_d.dds
compressonatorcli -fd BC1 -miplevels 4 terrain_concrete_src.png terrain_concrete_d.dds
```

Note: `nvcompress` generates a full mip pyramid (down to 1×1). The runtime sets
`GL_TEXTURE_MAX_LEVEL = 3` to cap GPU reads at 4 levels, so rendering is correct. However,
`validate_assets.py` must check `mip_count >= 4`, not `mip_count == 4`, so both outputs are
accepted by the validator. Use Compressonator if you need exactly 4 mip levels in the DDS
file header.

### Normal maps (all four `_n.dds` files — after DXT5nm swizzle)

```bash
# NVTT:
nvcompress -normal -bc3 terrain_grass_swizzled.png terrain_grass_n.dds
nvcompress -normal -bc3 terrain_asphalt_swizzled.png terrain_asphalt_n.dds
nvcompress -normal -bc3 terrain_soil_swizzled.png terrain_soil_n.dds
nvcompress -normal -bc3 terrain_concrete_swizzled.png terrain_concrete_n.dds

# Compressonator:
compressonatorcli -fd BC3 -miplevels 4 terrain_grass_swizzled.png terrain_grass_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_asphalt_swizzled.png terrain_asphalt_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_soil_swizzled.png terrain_soil_n.dds
compressonatorcli -fd BC3 -miplevels 4 terrain_concrete_swizzled.png terrain_concrete_n.dds
```

---

## 8. File Manifest

Upon completion, the following 8 files must be present at `assets/terrain/`:

| File | Size on disk (approx.) | Format |
|---|---|---|
| `terrain_grass_d.dds` | ~2.8 MB (DXT1, 4 mip) | DXT1/BC1, sRGB |
| `terrain_asphalt_d.dds` | ~2.8 MB | DXT1/BC1, sRGB |
| `terrain_soil_d.dds` | ~2.8 MB | DXT1/BC1, sRGB |
| `terrain_concrete_d.dds` | ~2.8 MB | DXT1/BC1, sRGB |
| `terrain_grass_n.dds` | ~5.6 MB (DXT5, 4 mip) | DXT5/BC3, linear |
| `terrain_asphalt_n.dds` | ~5.6 MB | DXT5/BC3, linear |
| `terrain_soil_n.dds` | ~5.6 MB | DXT5/BC3, linear |
| `terrain_concrete_n.dds` | ~5.6 MB | DXT5/BC3, linear |

DXT1 VRAM budget per texture (4-mip chain, as computed by `TextureCache`):
`ceil(2048/4) × ceil(2048/4) × 8 × 1.33 = 512 × 512 × 8 × 1.33 ≈ 2.8 MB`

DXT5 VRAM budget per texture:
`ceil(2048/4) × ceil(2048/4) × 16 × 1.33 = 512 × 512 × 16 × 1.33 ≈ 5.6 MB`

Total terrain detail texture VRAM: 4 × 2.8 + 4 × 5.6 = **11.2 + 22.4 = 33.6 MB**

This is within the overall VRAM budget for the terrain system. The splat map
(`terrain_chunk_splat.png`) contributes `16 × 16 × 4 = 1,024 bytes` per chunk (negligible).

---

## 9. Splat Channel Reference (Locked)

Locked 2026-02-25 per sign-off in `architecture/asset-standards/building-atlas-layout.md`:

| Splat channel | Terrain layer | Texture files |
|---|---|---|
| R (red) | Base / grass (grassland biome) | `terrain_grass_d.dds`, `terrain_grass_n.dds` |
| G (green) | Asphalt / road surface | `terrain_asphalt_d.dds`, `terrain_asphalt_n.dds` |
| B (blue) | Soil / bare earth | `terrain_soil_d.dds`, `terrain_soil_n.dds` |
| A (alpha) | Concrete / paved plazas | `terrain_concrete_d.dds`, `terrain_concrete_n.dds` |

Reassignment of any channel requires Product Owner approval and full re-authoring of all
splat map PNGs. This is a project-wide blocking change and is not permitted without sign-off.
