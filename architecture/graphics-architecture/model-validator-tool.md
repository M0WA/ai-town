# Model Validator Tool

## Purpose

The AI Town model validator (`aitown_model_validator`) is a standalone interactive tool that
visually verifies all V1 building and vehicle B3D assets load and display correctly. It is the
canonical tool for per-release asset sign-off.

The tool:

- Displays all V1 building and vehicle B3D assets grouped by category in a centred horizontal
  row with an orbiting camera.
- Loads assets via `BuildingAssetLoader` (same code path as the game) so texture binding,
  scale, and LOD selection are exercised exactly as in production.
- Reports which models loaded successfully and which failed.
- Allows the operator to step through categories with **Spacebar** and exit with **ESC**.

The tool is **not** part of the game runtime and is **not** executed in CI (requires a real
OpenGL display). It must be run manually after any change to B3D assets, atlas textures, or
`BuildingAssetLoader`.

---

## Scene Layout

### Ground Plane

A 200 m × 200 m grey plane (`DiffuseColor = SColor(255, 100, 100, 100)`,
`EMF_LIGHTING=true`, back-face culling disabled). Provides a neutral surface under all models.

### Sky Dome

A procedural 512×256 vertical gradient texture (deep blue zenith to pale horizon).
Same gradient as the benchmark tool for visual consistency.

### Sun Light

A single directional light with warm sun parameters (identical to Scene 1 of the benchmark):

| Parameter | Value |
|---|---|
| Type | `ELT_DIRECTIONAL` |
| Direction | `(-1, -2, 0.5)` normalised |
| Diffuse | `(1.0, 0.92, 0.80)` |
| Ambient | `(0.25, 0.28, 0.35)` |
| Specular | `(0.4, 0.4, 0.3)` |

### Model Placement

All models in the active category are displayed simultaneously in a horizontal row centred on
X=0. Spacing between model centres: 12 m (`kShowcaseSpacing = 12.0f`).

X position for model at index `i` of `N` total:

```text
X = (i − (N−1) / 2.0) × 12
```

Y=0, Z=0. Building nodes are scaled 10×10×10 m (same as `IrrlichtRenderer`). Vehicle nodes
are not scaled (authored at world scale).

### Road Tiles (Vehicles Category Only)

The Vehicles category places 7 real game road tiles along the X axis beneath all vehicle
models using the **identical rendering code path as `IrrlichtRenderer`**: the road shader,
`TextureCache::loadSRGB()`, and the procedural LOD0 mesh geometry (10 × 10 m flat quad +
bevelled kerbs).

**Tile placement**: tiles at `X = −30, −20, −10, 0, 10, 20, 30` (Y = 0.01 m, Z = 0).
Seven tiles cover the 5-vehicle row (−24 m to +24 m) with 5 m overhang each end.

**Rendering pipeline** (per tile):

| Step | Detail |
|---|---|
| Texture | `road_asphalt_tileable.dds` via `TextureCache::loadSRGB()` (sRGB DXT5 raw-GL path) |
| Shader | `road.vert` / `road.frag` compiled via `addHighLevelShaderMaterialFromFiles` |
| Callback | `RoadShaderCallback(srgbOk, roadTex)` — sets `u_diffuseMap` and `u_srgbLinear` |
| Geometry | `SMesh` + `SMeshBuffer`: 10 × 10 m quad (H=5) + 4 bevelled kerb strips |
| Tiling | Shader multiplies UV × 2.0 (matches `road.frag` spec: 2× tiling per tile) |
| Lighting | `Material.Lighting = false` (no light nodes in road shader path) |

The fallback when `getGPUProgrammingServices()` returns null (e.g. software driver) is
`EMT_SOLID` with no texture.

### Camera

Fixed orbit: radius 65 m, height 15 m, centre (0, 5, 0), advancing 0.3°/frame.
The 65 m radius keeps the full 6-model row (±30 m span) comfortably in view.

| Orbit parameter | Value |
|---|---|
| Centre | `(0, 5, 0)` |
| Radius | 65 m |
| Height above centre | 15 m |
| Speed | 0.3°/frame |

---

## Asset Categories

Categories are displayed in order; **Spacebar** advances to the next:

| # | Name | Assets |
|---|---|---|
| 1 | Residential | res\_low\_01, res\_low\_02, res\_med\_01, res\_med\_02, res\_high\_01, res\_high\_02 |
| 2 | Commercial | com\_low\_01, com\_low\_02, com\_med\_01, com\_med\_02, com\_high\_01, com\_high\_02 |
| 3 | Industrial | ind\_low\_01, ind\_low\_02, ind\_med\_01, ind\_med\_02, ind\_high\_01, ind\_high\_02 |
| 4 | Service | svc\_fire\_station, svc\_police\_station, svc\_power\_plant, svc\_water\_tower |
| 5 | Vehicles | car\_sedan, car\_hatchback, car\_suv, bus\_standard, truck\_cargo |

---

## Per-Category Scene Manager Lifecycle

1. `smgr->createNewSceneManager(false)` — fresh scene manager per category so textures and
   mesh buffers from the previous category are released before the next loads.
2. Add: grey ground plane, sky dome, directional sun light.
3. Load models via `BuildingAssetLoader::load()`. Models that fail to load are skipped with a
   `WARNING` to stderr; they do not abort the session.
4. On Spacebar (or last category): print frame count and average FPS for the category, then
   `smgr3->drop()`, then `delete` each `LODNode*` in the per-category vector.

**LODNode memory contract**: `smgr3->drop()` removes all scene nodes owned by that scene
manager. After `drop()`, the raw `LODNode*` wrappers (plain heap objects, not Irrlicht
ref-counted) must be `delete`d explicitly. Store them in a `std::vector<LODNode*>` per
category.

---

## Interaction

| Input | Action |
|---|---|
| **Spacebar** | Advance to next category. When the last category completes, exit. |
| **ESC** | Skip remaining categories and exit immediately. |
| **Window close** | `device->run()` returns false — exit immediately. |

---

## stdout Output

```text
=== AI Town Model Validator ===
Controls: SPACE = next category   ESC = exit

=== [1/5]: Residential — 6 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: res_low_01 res_low_02 res_med_01 res_med_02 res_high_01 res_high_02
  Category displayed for 312 frames (avg 104.0 fps)

=== [2/5]: Commercial — 6 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: com_low_01 com_low_02 com_med_01 com_med_02 com_high_01 com_high_02
  Category displayed for 412 frames (avg 87.3 fps)

...

=== Model Validator complete — all 5 categories displayed ===
```

Failed loads appear on stderr:

```text
  WARNING: failed to load 'res_med_01'. Skipping.
```

---

## CLI Reference

```text
Usage: aitown_model_validator [options]
  --width W    window width (default: 1280)
  --height H   window height (default: 720)
  --help       print this usage message
```

Exit codes:

- `0` — success (all categories displayed or operator exited early)
- `1` — device creation failure (no OpenGL context available)

---

## CMake Target

```cmake
add_executable(aitown_model_validator
    src/benchmark/model_validator_main.cpp
)
target_link_libraries(aitown_model_validator PRIVATE aitown_render GLEW::GLEW Irrlicht)
target_include_directories(aitown_model_validator PRIVATE src/ ${CMAKE_SOURCE_DIR})
target_compile_definitions(aitown_model_validator PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
```

Linux transitive Irrlicht system libraries (JPEG, PNG, ZLIB, BZip2, Xxf86vm) must be linked
explicitly — same rationale as `aitown_benchmark`.

---

## CI Integration

The model validator is **not** run in CI. Reasons:

- Requires a real GPU and OpenGL display context.
- Pass/fail is a human visual judgement, not a machine-checkable metric.

**Required manual runs**:

- Before each major release: run on a representative development machine and step through
  all 5 categories confirming correct mesh, texture, and scale for every model.
- After any change to B3D assets, atlas textures (currently PNG — see V1 exception in
  `architecture/asset-standards/building-atlas-layout.md`), or `BuildingAssetLoader`.
- After any change to `road_asphalt_tileable.dds`, `road.vert`/`road.frag`, or
  `RoadShaderCallback`.
- After any change to `LODNode` or the `IrrlichtRenderer` building-placement scale.
