# Sky Clouds

## Overview

The sky cloud layer is a lightweight animated cloud effect rendered above the terrain.
It is drawn as a flat scrolling plane positioned at a fixed world-space altitude, on top
of Irrlicht's `addSkyDomeSceneNode`. No volumetric or raymarched clouds are used in V1.

This feature is delivered in **Phase 10b**.

---

## Cloud Plane Geometry

- **Mesh type**: runtime-built `SMesh*` (single `SMeshBuffer`, two CW triangles forming
  one quad). NOT an artist-authored `.b3d` file.
- **Altitude**: `kCloudAltitude = 200.0f` metres (world-space Y). This places the cloud
  plane visually above all V1 building heights (max ~80 m) with a 120 m safety margin,
  ensuring no clipping through tall buildings or elevated terrain peaks.
- **Footprint**: `2000 m × 2000 m` (half-extent `1000.0f` m), centred at world origin
  `(0, kCloudAltitude, 0)`. At maximum map size (1024×1024 tiles, cell size 4 m =
  4096 m × 4096 m), the cloud plane is smaller than the map; at V1 default map size
  (256×256 tiles = 1024 m × 1024 m) it covers the full map with overhead.
  Adjust `cloudHalfExtent` if map extent exceeds 2 km.
- **UV tiling**: `kCloudUVScale = 4.0f` — the 2 km plane tiles the 1024 px cloud texture
  4× in each axis (one texture tile = 500 m).

### Vertex Layout

```text
World-space corners (Irrlicht left-handed, Y-up, Z-forward):

  v0 = (−halfExtent,  kCloudAltitude, −halfExtent)  — near-left   UV (0, 0)
  v1 = (+halfExtent,  kCloudAltitude, −halfExtent)  — near-right  UV (kCloudUVScale, 0)
  v2 = (+halfExtent,  kCloudAltitude, +halfExtent)  — far-right   UV (kCloudUVScale, kCloudUVScale)
  v3 = (−halfExtent,  kCloudAltitude, +halfExtent)  — far-left    UV (0, kCloudUVScale)

Index buffer (CW front face = +Y normal):
  { 0, 2, 1,  0, 3, 2 }
```

**Winding proof** (Irrlicht left-handed CW front):
`(v2 − v0) × (v1 − v0)` = `(2h, 0, 2h) × (2h, 0, 0)` = `(0, +4h², 0)` → +Y normal.
Front face visible from above (camera always looks downward). Correct.

**SMesh lifetime rule** (mandatory):

```cpp
SMesh* smesh = buildCloudMesh();
smesh->getMeshBuffer(0)->recalculateBoundingBox();
smesh->recalculateBoundingBox();          // AFTER all buffer recalculations
IMeshSceneNode* node = smgr->addMeshSceneNode(smesh);   // grab() called internally
smesh->drop();                            // release caller's ref — scene node is now sole owner
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

Applied to the cloud plane `IMeshSceneNode*` after `addMeshSceneNode()`:

| Property | Value | Rationale |
|---|---|---|
| `MaterialType` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | Alpha-channel transparency; cloud density from alpha |
| `Lighting` | `false` | No scene lights in V1 |
| `BackfaceCulling` | `false` | Camera always looks down; plane must be visible from below |
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

At `kCloudScrollX = 0.002f`: one full texture tile (500 m at 4× scale) traverses in
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

The cloud plane is positioned at `Y=150 m`, which is physically above all opaque scene
geometry (terrain max ~80 m, buildings max ~80 m). The depth buffer handles correct
ordering automatically:

- The sky dome is rendered at infinite depth (Irrlicht sky dome nodes set
  `EMF_ZBUFFER=false`); the cloud plane at `Y=150 m` renders in front of the sky dome
  via depth test.
- Zone overlay quads are at terrain height + 0.1 m; the cloud plane at 150 m is always
  farther from the camera than overlay quads (camera pitch −20° to −70°, minimum camera
  height ~30 m over flat terrain).

No explicit render order override (`setAutomaticCulling`, `setRenderOrder`) is required in
the normal case.

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
