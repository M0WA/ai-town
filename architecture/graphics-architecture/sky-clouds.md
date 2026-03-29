# Sky Clouds

## Overview

The sky cloud layer is a lightweight animated cloud effect rendered above the terrain.
It is drawn as a tessellated hemisphere dome scrolling the cloud texture each frame,
on top of Irrlicht's `addSkyDomeSceneNode`. No volumetric or raymarched clouds are
used in V1.

This feature is delivered in **Phase 10b**.

---

## Cloud Dome Geometry

- **Mesh type**: runtime-built `SMesh*` (single `SMeshBuffer`, tessellated hemisphere
  dome). NOT an artist-authored `.b3d` file.
- **Dome radius**: `kCloudDomeRadius = 6000.0f` metres — horizontal radius at the dome
  base ring. The dome radius **must** be less than `farClip` (currently `15000.0f` m set
  in `CameraParams`). With `farClip = 3000 m` OpenGL hard-clips all dome vertices beyond
  3000 m from the camera, producing a hard circular arc ring at the frustum boundary.
  Setting `farClip = 15000 m` ensures dome vertices (at most ~6082 m from camera) are
  always inside the far plane. A 6 000 m radius keeps the dome wall well beyond the
  typical 2–3 km terrain view distance.
- **Dome height**: `kCloudDomeHeight = 2000.0f` metres — vertical extent from the dome
  base up to the apex. Combined with the 6 000 m radius and the −1 000 m base altitude
  this places the apex at Y = 1 000 m.
- **Base altitude**: `kCloudAltitude = -1000.0f` metres (world-space Y of the dome base
  ring). The apex is at `Y = kCloudAltitude + kCloudDomeHeight = 1000.0f` m. The
  deeply negative base altitude pushes the zero-alpha base ring far below terrain level,
  ensuring the hard bottom edge of the dome is never visible above the landscape even at
  the most oblique camera angles.
- **Tessellation**: `kDomeRings = 32` latitude bands × `kDomeSectors = 32` longitude
  segments → `33 × 33 = 1089` vertices, `32 × 32 × 2 = 2048` triangles.
- **Camera tracking**: the dome node is repositioned to camera XZ each frame in
  `update()` so the horizon ring is always centred on the player. The dome vertex Y
  coordinates are absolute world-space values; only the node's XZ translation changes.

### Why a Dome Instead of a Flat Plane

The previous implementation used a flat quad `2000 m × 2000 m` at `Y=200 m`.
At camera heights of 30–200 m the plane edge subtended only a small angular range
above the camera, leaving the upper sky bare and visible above the cloud layer when
looking toward the horizon. The dome ensures cloud coverage from the zenith (90°
elevation) down to near the horizon (≈11° elevation) regardless of camera position
or tilt angle.

### Vertex Layout

Each vertex is parameterised by `(ring, sector)` where `ring ∈ [0, kDomeRings]`
and `sector ∈ [0, kDomeSectors]`:

```text
t       = ring / kDomeRings                  — latitude parameter [0=apex, 1=base]
y       = kCloudAltitude + kCloudDomeHeight × (1 − t)
r       = kCloudDomeRadius × t               — horizontal radius at this ring
phi     = sector / kDomeSectors × 2π
px      = r × sin(phi)
pz      = r × cos(phi)
```

**UV mapping** (cylindrical):

```text
phi_norm = sector / kDomeSectors          — normalised azimuth [0, 1)
u        = phi_norm × kCloudUVScale       — wraps around dome circumference
v        = t × kCloudUVScale              — from apex (0) to base ring (kCloudUVScale)
```

Cylindrical mapping ensures that at every elevation ring, all sectors sample a
uniformly-distributed span of the texture in the u-axis. The previous polar (top-down
projection) mapping sampled a UV circle at each elevation ring; because the tiling
cloud texture has non-uniform alpha content along that circle, some azimuth directions
saw dense cloud near the horizon while others saw clear sky — producing a directional
arc. With cylindrical UV the per-ring sample spans the full texture width regardless
of azimuth, eliminating the directional variation.

`kCloudUVScale = 4.0`: texture tiling factor — tiles the cloud texture 4× around the
dome circumference and 4× from apex to base ring.

**Vertex colour alpha**: all vertices use `SColor(255, 255, 255, 255)` (alpha=255).
Horizon fade is handled entirely in the fragment shader using the elevation angle from
the camera — see §Cloud Dome Shader below. The previous per-vertex alpha fade
(`kFadeStart`, `kFadeEnd`) has been removed.

**Winding**: Inside-CW (camera is inside the dome looking upward and outward). For
quad `(ring r, sector s)`:

```text
tl = r       × (kDomeSectors+1) + s
tr = r       × (kDomeSectors+1) + s+1
bl = (r+1)   × (kDomeSectors+1) + s
br = (r+1)   × (kDomeSectors+1) + s+1

Triangle 1: tl → bl → tr
Triangle 2: bl → br → tr
```

**SMesh lifetime rule** (mandatory):

```cpp
SMesh* smesh = buildCloudDomeMesh();   // ref_count = 1; bboxes already recalculated inside
IMeshSceneNode* node = smgr->addMeshSceneNode(smesh);  // grab() called internally
smesh->drop();                         // release caller's ref — scene node is now sole owner
```

(ref: `architecture/graphics-architecture/procedural-terrain.md` — SMesh lifetime)

---

## Cloud Asset

| Property | Value |
|---|---|
| Filename | `assets/textures/sky/clouds.png` |
| Format | PNG RGBA 1024×1024 |
| Tileability | Seamless (no hard edges at boundaries) |
| Content | Grey-white cloud pattern; alpha channel encodes cloud density (0=clear, 255=full cloud) |
| Load path | `IVideoDriver::getTexture()` — linear pool (NOT `TextureCache::loadSRGB()`) |

**Why PNG not DDS**: Irrlicht 1.8.5's DDS loader is disabled by default
(`_IRR_COMPILE_WITH_DDS_LOADER_` commented out in `IrrCompileConfig.h`). PNG is loaded
correctly via `IVideoDriver::getTexture()`. See
`architecture/graphics-architecture/texture-cache.md` — "IVideoDriver::getTexture()
cannot load DDS files".

**Why no `_d` suffix and why linear pool**: The `_d` suffix is reserved for sRGB-encoded
photographic diffuse textures (per `architecture/asset-standards/2d-texture-standards.md`).
The cloud texture encodes synthetic colour/alpha data in linear space — sRGB decoding is
not appropriate. The filename `clouds.png` (no suffix) avoids routing confusion in
`TextureCache::loadSRGB()` and explicitly signals this is a linear-pool asset.

---

## Material Settings

Applied to the cloud dome `IMeshSceneNode*` after `addMeshSceneNode()`:

| Property | Value | Rationale |
|---|---|---|
| `MaterialType` | Custom GLSL shader (see below); fallback `EMT_TRANSPARENT_VERTEX_ALPHA` | Shader multiplies `tex.a * v_fadeAlpha` so non-cloud texels are fully transparent. See §Cloud Dome Shader. |
| `Lighting` | `false` | No scene lights in V1 |
| `BackfaceCulling` | `false` | Camera is inside the dome; disabling culling guarantees visibility from any camera tilt |
| `ZWriteEnable` | `false` | Transparent domes must never write to the depth buffer. If depth writes are on, the dome's partially-transparent lower band deposits depth values that can occlude terrain geometry in the same render pass, manifesting as a hard arc in the one azimuth where the dome intersects the terrain frustum. Setting `false` (Irrlicht 1.8.5 `bool`; 1.9+ uses `EZW_OFF`) disables all depth writes while depth reads remain active so the dome still sits correctly behind foreground objects. |
| `Texture[0]` | `clouds.png` via `getTexture()` | Cloud diffuse + alpha |

Do NOT use additive blending — clouds are semi-transparent overlays, not emissive.

---

## Cloud Dome Shader

### Why Per-Vertex Alpha Caused a Directional Arc Artefact

The previous implementation stored a per-vertex horizon fade in `SColor.alpha`
and passed it to the fragment shader as `v_fadeAlpha`. Although the custom shader
correctly gated output on `tex.a * v_fadeAlpha` (fixing the original
`EMT_TRANSPARENT_VERTEX_ALPHA` artefact where texture alpha was ignored entirely),
a second artefact remained:

The cloud texture's alpha content varies by azimuth because polar UV mapping means
different compass directions sample different parts of the tiling texture. In the
per-vertex fade band (the ring of vertices at a given latitude) some azimuths had
dense clouds visible near the horizon while others were sparse. The result was a
**directional arc** — a visible semi-transparent band that appeared at one compass
direction, moved with camera rotation, and was most pronounced when looking toward
the azimuth where the texture happened to be densest near the fade boundary.

### Elevation-Angle Fade (Rev 2 Fix)

The fix moves all horizon fade logic into the GLSL shaders, keyed on the
**elevation angle** from the camera to each fragment. Because elevation angle is a
function of vertical angle only (independent of azimuth), the resulting fade band
is a perfectly horizontal ring — symmetric in all compass directions. No directional
arc is possible.

**Fragment shader fade constants:**

| Constant | Value | Meaning |
|---|---|---|
| `kElevAlphaEnd` | `+0.0873` rad (+5°) | Fully transparent at or below this elevation |
| `kElevAlphaHigh` | `0.3491` rad (20°) | Fully opaque at or above this elevation |

The smoothstep between +5° and 20° produces a gradual, symmetric fade in all
compass directions. The fade band starts slightly above the terrain horizon (+5°),
ensuring the dome is fully transparent at and below +5° so no cloud arch can appear
at the horizon; it reaches full opacity at 20° above the horizon. Below +5° elevation
the dome is always fully transparent regardless of texture content.

### Atmospheric Haze Colour Blend

In addition to the alpha fade, the fragment shader blends cloud RGB toward the
sky background colour near the horizon. This eliminates any residual directional
arc caused by azimuthal variation in cloud texture density near the fade boundary:
even where the cloud texture is dense close to the horizon, those fragments appear
as sky-coloured haze rather than dark clouds against the sky.

| Property | Value |
|---|---|
| Sky colour constant | `vec3(0.392, 0.584, 0.929)` = `SColor(255, 100, 149, 237) / 255` |
| `kHazeEnd` | +5° (full sky colour at this elevation and below) |
| `kHazeHigh` | 20° (no haze at this elevation and above) |

The haze blend uses the same elevation-angle smoothstep as the alpha fade:
a `hazeBlend` factor of `1.0` at +5° (full sky colour) tapering to `0.0` at 20°
(original cloud colour). The blended cloud colour is then combined with the
alpha fade to produce the final output `vec4(cloudColor, tex.a * horizFade)`.

### Shader Files

| File | Purpose |
|---|---|
| `assets/shaders/cloud_dome.vert` | Computes `v_elevAngle` (elevation angle in radians from camera to vertex) and passes `v_texCoord` to the fragment stage. Receives `u_cameraY` uniform. |
| `assets/shaders/cloud_dome.frag` | Samples `u_tex`; applies elevation-angle smoothstep alpha fade (+5° → 20°); blends cloud RGB toward sky colour `vec3(0.392, 0.584, 0.929)` near the horizon (atmospheric haze); outputs `vec4(cloudColor, tex.a * horizFade)` |

The vertex shader computes:

```glsl
float horizDist = length(gl_Vertex.xz);
float deltaY    = gl_Vertex.y - u_cameraY;
v_elevAngle     = atan(deltaY, max(horizDist, 0.1));
```

The fragment shader computes:

```glsl
float t          = clamp((v_elevAngle - kElevAlphaEnd) / (kElevAlphaHigh - kElevAlphaEnd), 0.0, 1.0);
float horizFade  = t * t * (3.0 - 2.0 * t);   // smoothstep
float hazeBlend  = 1.0 - horizFade;            // 1 at +5°, 0 at 20°
vec3  skyColor   = vec3(0.392, 0.584, 0.929);
vec3  cloudColor = mix(tex.rgb, skyColor, hazeBlend);
gl_FragColor     = vec4(cloudColor, tex.a * horizFade);
```

- `tex.a = 0` (non-cloud area) → `alpha = 0` fully transparent.
- Elevation ≤ +5° → `horizFade = 0` → dome fully transparent at and below +5°.
- Elevation ≥ 20° → `horizFade = 1`, `hazeBlend = 0` → fully opaque overhead clouds in original colour.
- At the horizon band (+5° to 20°) cloud colour blends toward sky colour, eliminating dark-cloud artefacts.
- Identical result in every compass direction — no directional arc.

### Base Material and Blending

The shader is registered with `EMT_TRANSPARENT_ALPHA_CHANNEL` as the base
material. This sets `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` blending — correct
for semi-transparent cloud overlays.

### Shader Callback

`CloudDomeShaderCallback` (defined inline in `IrrlichtRenderer.cpp`) sets two
uniforms in `OnSetConstants`:

- `u_tex` — sampler2D bound to texture unit 0.
- `u_cameraY` — `float` world-space Y of the camera, updated each frame via
  `setCameraY()` before the scene is drawn.

`u_cameraY` is consumed by the vertex shader. It is set via both
`setVertexShaderConstant` and `setPixelShaderConstant` for cross-platform
compatibility (Irrlicht GLSL backend behaviour differs across drivers; both calls
target the same linked program uniform).

**Lifetime**: the callback is allocated on the raw heap and passed to
`addHighLevelShaderMaterialFromFiles`. Unlike the standard drop-after-pass
pattern, the caller (`IrrlichtRenderer`) **keeps its own reference** (stored as
`void* m_cloudShaderCbRaw` in the header, cast to `CloudDomeShaderCallback*`
inside the .cpp) so that `setCameraY()` can be called each frame in `update()`.
The caller's reference is released via `->drop()` in
`IrrlichtRenderer::~IrrlichtRenderer()`. Irrlicht also holds its own `grab()`
reference; the final drop happens when the material renderer is destroyed.
Never `std::unique_ptr` — causes double-free.

### IrrlichtRenderer Members Required

| Member | Type | Initialised by |
|---|---|---|
| `m_cloudNode` | `IMeshSceneNode*` | `initCloudPlane()` |
| `m_cloudUVOffset` | `irr::core::vector2df` | member initialiser → `{0.f, 0.f}` |
| `m_cloudShaderCbRaw` | `void*` | `initCloudPlane()` on shader success; `nullptr` on failure or EDT_NULL |

### Fallback

If `gpu->addHighLevelShaderMaterialFromFiles` returns `-1` (no GLSL support,
or `EDT_NULL`), `initCloudPlane()` falls back to `EMT_TRANSPARENT_VERTEX_ALPHA`
with a `fprintf(stderr, ...)` warning and leaves `m_cloudShaderCbRaw = nullptr`.
The directional arc artefact will be visible in that case but the engine will not
crash.

---

## UV Scrolling

UV scrolling animates the cloud texture each frame to create a sense of wind movement.

### Scroll Speeds

| Axis | Speed | Direction |
|---|---|---|
| `kCloudScrollX` | `0.002f` UV units/second | Primary wind direction |
| `kCloudScrollZ` | `0.0008f` UV units/second | Secondary drift |

At `kCloudScrollX = 0.002f`: one full texture tile traverses in
`1 / 0.002 = 500 seconds` (~8 minutes). This is imperceptibly slow frame-to-frame and
produces a natural drifting appearance.

### Implementation

```cpp
// In IrrlichtRenderer::update(float dt):
m_cloudUVOffset.X = std::fmod(m_cloudUVOffset.X + kCloudScrollX * dt, 1.0f);
m_cloudUVOffset.Y = std::fmod(m_cloudUVOffset.Y + kCloudScrollZ * dt, 1.0f);

m_cloudNode->getMaterial(0)
    .getTextureMatrix(0)
    .setTextureTranslate(m_cloudUVOffset.X, m_cloudUVOffset.Y);

// Reposition dome to camera XZ each frame:
if (m_camera) {
    const core::vector3df camPos = m_camera->getPosition();
    m_cloudNode->setPosition(core::vector3df(camPos.X, 0.0f, camPos.Z));
}
```

`std::fmod` wraps both components into `[0.0f, 1.0f)` to prevent float precision
accumulation over play sessions longer than ~500 seconds.

---

## Depth Ordering

The cloud dome base is at `Y=−1000 m` (far below terrain) and apex at `Y=1000 m`. Visible
cloud geometry fades in between +5° and 20° elevation angle (`kElevAlphaEnd` and
`kElevAlphaHigh` shader constants), reaching full opacity above 20°. This range is well
above all opaque scene geometry (terrain max ≈ 26 m, buildings max ~80 m).
The depth buffer handles correct ordering automatically:

- The sky dome is rendered at infinite depth (Irrlicht sky dome nodes set
  `EMF_ZBUFFER=false`); the cloud dome renders in front of the sky dome via depth test.
- Zone overlay quads are at terrain height + 0.1 m; the cloud dome is always farther
  from the camera than overlay quads (camera pitch −20° to −70°, minimum camera height
  ~30 m over flat terrain).
- Near-horizon fragments have `horizFade = 0` (elevation ≤ +5°), so even if the depth
  test ordering is imperfect near the horizon the elevation-angle fade makes them invisible.

The cloud dome material has `ZWriteEnable = false` so it never writes depth values that
could occlude terrain rendered in the same pass. Depth reads remain active.

No explicit render order override (`setAutomaticCulling`, `setRenderOrder`) is required
in the normal case.

---

## Headless / EDT_NULL Guard

`initCloudPlane()` MUST check `if (m_driverType == EDT_NULL) return;` before any mesh
construction or `getTexture()` call. Under `EDT_NULL` (used in integration tests):

- `IVideoDriver::getTexture()` returns null.
- `addMeshSceneNode()` behaviour is undefined without a real driver.
- `m_cloudNode` remains `nullptr`; the `update()` cloud scroll block must guard with
  `if (m_cloudNode)`.

---

## CI Asset Gate

**Check #24 — Cloud texture format gate** (added to `tools/validate_assets.py` in Phase 10b):

- Verify `assets/textures/sky/clouds.png` is exactly 1024×1024 pixels and RGBA
  (4 channels).
- Uses Pillow (already a CI dependency from Phase 10).
- No-op when the file does not exist.

Cloud texture presence is also enforced as a hard-fail gate in `build-linux`,
`build-windows`, and `coverage-linux` CI jobs.
