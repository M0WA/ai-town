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
  base ring. The arc artifact does **not** come from the dome edge being inside the view
  frustum; it comes from the opaque zone of the dome extending down to eye level. The
  fix is a correct two-boundary fade (see Vertex Layout below), not an ever-larger radius.
  A 6 000 m radius keeps the dome wall well beyond the typical 2–3 km terrain view
  distance while avoiding the flattening distortion introduced by a 14 000 m radius.
- **Dome height**: `kCloudDomeHeight = 2000.0f` metres — vertical extent from the dome
  base up to the apex. Combined with the 6 000 m radius and the −1 000 m base altitude
  this places the apex at Y = 1 000 m.
- **Base altitude**: `kCloudAltitude = -1000.0f` metres (world-space Y of the dome base
  ring). The apex is at `Y = kCloudAltitude + kCloudDomeHeight = 1000.0f` m. The
  deeply negative base altitude pushes the zero-alpha base ring far below terrain level,
  ensuring the hard bottom edge of the dome is never visible above the landscape even at
  the most oblique camera angles.
- **Tessellation**: `kDomeRings = 32` latitude bands × `kDomeSectors = 32` longitude
  segments → `33 × 33 = 1089` vertices, `32 × 32 × 2 = 2048` triangles. The ring count
  keeps the smoothstep fade smooth across the 500 m fade band (t=0.25 → t=0.50).
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

**UV mapping** (polar, from apex outward):

```text
u = (sin(phi) × 0.5 × t + 0.5) × kCloudUVScale
v = (cos(phi) × 0.5 × t + 0.5) × kCloudUVScale
```

This maps the apex to UV `(0.5, 0.5) × kCloudUVScale` and the base ring to UV
`(0..1, 0..1) × kCloudUVScale`, tiling the texture naturally across the dome.

**Vertex colour alpha** (two-boundary horizon fade):

```text
kFadeStart = 0.25  — t=0.25 → Y = -1000 + 2000×(1-0.25) = 500 m  (fade begins)
kFadeEnd   = 0.46  — t=0.46 → Y = -1000 + 2000×(1-0.46) =  80 m  (fully transparent)

For t ≤ kFadeStart:               alpha = 255   (fully opaque overhead dome)
For kFadeStart < t ≤ kFadeEnd:    s = (t − kFadeStart) / (kFadeEnd − kFadeStart)   ∈ [0, 1]
                                  w = smoothstep(s) = s² × (3 − 2s)
                                  alpha = round(255 × (1 − w))
For t > kFadeEnd:                 alpha = 0     (fully transparent horizon zone)
```

**Why two boundaries eliminate the arc**: with a single `kFadeStart = 0.5` boundary the
dome is only *starting* to fade at Y ≈ 100 m (camera eye level). Looking toward the
horizon, the still-opaque dome wall overlaps terrain and produces a visible circular arc.
The fix is to reach **alpha = 0 at or above the maximum terrain height**. Setting
`kFadeEnd = 0.46` maps to Y = 80 m, which is well above the maximum terrain height
(terrain max ≈ 26 m, safety margin ≈ 54 m). This value must stay above
`maxTerrainHeight + safetyMargin`; do not raise it below Y = 80 m without verifying the
new terrain height ceiling. Everything at and below 80 m is fully transparent, so no
dome surface with non-zero alpha is ever visible against the terrain horizon.
The fade band (500 m → 80 m altitude, t ∈ [0.25, 0.46]) provides a smooth S-curve
transition entirely above ground level.

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
| `MaterialType` | `EMT_TRANSPARENT_VERTEX_ALPHA` | Vertex colour alpha controls transparency — required so the per-vertex horizon fade (alpha=0 at base ring, alpha=255 at apex) is honoured. `EMT_TRANSPARENT_ALPHA_CHANNEL` ignores vertex alpha entirely, which would leave the base ring opaque and produce a hard circular rim at the horizon. |
| `Lighting` | `false` | No scene lights in V1 |
| `BackfaceCulling` | `false` | Camera is inside the dome; disabling culling guarantees visibility from any camera tilt |
| `Texture[0]` | `clouds.png` via `getTexture()` | Cloud diffuse + alpha |

Do NOT use additive blending — clouds are semi-transparent overlays, not emissive.

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

### IrrlichtRenderer Members Required

| Member | Type | Initialised by |
|---|---|---|
| `m_cloudNode` | `IMeshSceneNode*` | `initCloudPlane()` |
| `m_cloudUVOffset` | `irr::core::vector2df` | `initCloudPlane()` → `{0.f, 0.f}` |

---

## Depth Ordering

The cloud dome base is at `Y=−1000 m` (far below terrain) and apex at `Y=1000 m`. Visible
cloud geometry starts at `Y=80 m` (kFadeEnd boundary, below which all vertices are
alpha=0) and reaches full opacity at `Y=500 m` (kFadeStart boundary). This range is
well above all opaque scene geometry (terrain max ≈ 26 m, buildings max ~80 m).
The depth buffer handles correct ordering automatically:

- The sky dome is rendered at infinite depth (Irrlicht sky dome nodes set
  `EMF_ZBUFFER=false`); the cloud dome renders in front of the sky dome via depth test.
- Zone overlay quads are at terrain height + 0.1 m; the cloud dome is always farther
  from the camera than overlay quads (camera pitch −20° to −70°, minimum camera height
  ~30 m over flat terrain).
- The bottom ring of the cloud dome has alpha=0 (transparent), so even if the depth
  test ordering is imperfect near the horizon the fade makes it invisible.

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
