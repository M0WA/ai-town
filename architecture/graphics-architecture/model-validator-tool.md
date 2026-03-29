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

### Tile Boundary Overlay (Building Categories Only)

For every building category (`scaleBuilding = true`), a **red 10×10 m square outline** is
rendered on the ground centred on **each loaded model slot**. This means:

- **Single-model mode** (`--model`): one square per LOD slot displayed (e.g. two squares for
  LOD0 and LOD1 of a low/med building, one per model position).
- **Category mode**: one square per model in the category row.

The squares mark the exact in-game tile footprint so the operator can confirm that no geometry
overflows the tile boundary.

| Parameter | Value |
|---|---|
| Shape | 4 `draw2DLine` segments forming a closed square (2D screen-space projected) |
| Colour | `SColor(255, 220, 30, 30)` (bright red) |
| Size | 10 m × 10 m (`kHalf = 5.0f`) |
| Y offset | 0.05 m above ground (`kY = 0.05f`) to avoid z-fighting |
| Centre | Each model's world X position, Z = 0 |
| Projection | `ISceneCollisionManager::getScreenCoordinatesFrom3DPosition()` |

The four 3D corners are projected to screen space with `getScreenCoordinatesFrom3DPosition()`
then connected with `driver->draw2DLine()`. This approach works with all renderers (including
Burnings software driver where `draw3DLine` can be unreliable).

The overlay is drawn after `smgr->drawAll()` and before the HUD text, inside the
`beginScene`/`endScene` block.

---

### Road Tiles (Vehicles Category Only)

**Note**: Road tile geometry is **procedurally generated at runtime** via
`buildTileRoadMesh()`. No road tile `.b3d` asset exists. The validator exercises road
tiles by calling the same `buildTileRoadMesh()` function as `IrrlichtRenderer` — not by
loading a file. This is the identical code path per the validator design goal.

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

Orbiting camera: default radius 65 m, height 15 m, centre (0, 5, 0), advancing 0.3°/frame.
The 65 m radius keeps the maximum 5-model vehicle row (±24 m span) comfortably in view.
All other categories have 4 models (±18 m span at 12 m spacing).

Mouse wheel zooms the camera in/out by adjusting the orbit radius (scroll up = zoom in,
scroll down = zoom out). Range: 5 m (minimum) to 200 m (maximum). Step: 3 m per scroll notch.
The zoom level persists for the duration of the category; it resets to 65 m when advancing
to the next category (new orbit state is initialised per category).

| Orbit parameter | Value |
|---|---|
| Centre | `(0, 5, 0)` |
| Default radius | 65 m |
| Minimum radius | 5 m |
| Maximum radius | 200 m |
| Zoom step | 3 m per scroll notch |
| Height above centre | 15 m |
| Speed | 0.3°/frame |

---

## Asset Categories

Categories are displayed in order; **Spacebar** advances to the next.

Phase 11d expands to **11 categories**: each zone tier (Low / Med / High) is a separate category
with **4 variants** each. Commercial High is listed as its own "Skyscrapers" sub-category because
`com_high_*` uses a distinct tall-tower geometry and must be validated separately from low-rise
commercial buildings. Service buildings are 1 unique model each (no variants).

| # | Name | Assets (LOD0) |
|---|---|---|
| 1 | Residential Low | res\_low\_01, res\_low\_02, res\_low\_03, res\_low\_04 |
| 2 | Residential Med | res\_med\_01, res\_med\_02, res\_med\_03, res\_med\_04 |
| 3 | Residential High | res\_high\_01, res\_high\_02, res\_high\_03, res\_high\_04 |
| 4 | Commercial Low | com\_low\_01, com\_low\_02, com\_low\_03, com\_low\_04 |
| 5 | Commercial Med | com\_med\_01, com\_med\_02, com\_med\_03, com\_med\_04 |
| 6 | Commercial High (Skyscrapers) | com\_high\_01, com\_high\_02, com\_high\_03, com\_high\_04 |
| 7 | Industrial Low | ind\_low\_01, ind\_low\_02, ind\_low\_03, ind\_low\_04 |
| 8 | Industrial Med | ind\_med\_01, ind\_med\_02, ind\_med\_03, ind\_med\_04 |
| 9 | Industrial High | ind\_high\_01, ind\_high\_02, ind\_high\_03, ind\_high\_04 |
| 10 | Service | svc\_fire\_station, svc\_police\_station, svc\_power\_plant, svc\_water\_tower |
| 11 | Vehicles | car\_sedan, car\_hatchback, car\_suv, bus\_standard, truck\_cargo |

The validator displays LOD0 (`.b3d` at `_lod0.b3d` suffix) for all building/service categories.
LOD1 (`_lod1.b3d`) and LOD2 (geometry shells `_lod2.b3d` for High-density zones, billboard
imposters for Low/Med) are not displayed by the validator — validate LOD2 assets visually in the
game at distances > 40 m.

---

## Phase 11d Asset Inventory

V1 total `.b3d` files validated by the tool (LOD0 categories 1–11 above):

| Asset type | Count | Notes |
|---|---|---|
| Zone building LOD0 | 36 | 4 variants × 9 zone-tiers |
| Zone building LOD1 | 36 | 4 variants × 9 zone-tiers |
| Zone building LOD2 geometry shells | 12 | High-density only (res/com/ind high × 4 variants); Low/Med use billboard imposters |
| Service building LOD0 | 4 | 1 unique model each (fire, police, power, water) |
| Service building LOD1 | 4 | 1 unique model each |
| Vehicle LOD0 | 5 | car\_sedan, car\_hatchback, car\_suv, bus\_standard, truck\_cargo |
| Vehicle LOD1 | 5 | car\_sedan, car\_hatchback, car\_suv, bus\_standard, truck\_cargo |
| **Total LOD0 `.b3d`** | **45** | 36 zone buildings + 4 service buildings + 5 vehicles (LOD0 only) |
| **Total `.b3d`** | **102** | All LOD levels across all asset categories |
| Billboard DDS (`*_billboard.dds`) | 28 | 24 zone (Low/Med × 3 zone-types × 4 variants) + 4 service; High excluded (uses `_lod2.b3d`) |

The validator tool exercises the 45 LOD0 `.b3d` files across categories 1–11. LOD1 and LOD2
assets are validated by `validate_assets.py` (CI) and manual game playback at appropriate distances.

**Phase 11d polygon budget reference** (see `architecture/asset-standards/3d-model-standards.md`
§Vehicle Polygon Budget and §LOD Requirements table for full specs):

| Asset | LOD0 target | LOD1 target |
|---|---|---|
| Small buildings (Low/Med all zones) | 1,500–3,000 tris | 200–400 tris |
| Large buildings (High all zones) | 4,000–8,000 tris | 1,000–1,500 tris |
| Commercial High (skyscrapers) | 7,000–10,000 tris | 1,200–2,000 tris |
| Cars (`car_sedan`, `car_hatchback`, `car_suv`) | 1,800–2,000 tris | ≤ 400 tris |
| Bus (`bus_standard`) | 2,500–3,000 tris | ≤ 500 tris |
| Truck (`truck_cargo`) | 2,500–3,000 tris | ≤ 500 tris |

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

## On-Screen HUD

Two overlaid 2D text elements are rendered on top of the 3D scene each frame:

### Category Banner

A single line of white text rendered in the top-left corner using the Irrlicht GUI font
(`IGUIFont* font = device->getGUIEnvironment()->getBuiltInFont()`):

```text
[N/11] Category Name
```

Example: `[3/11] Residential High`

Rendered at screen position `(12, 12)` with colour `SColor(255, 255, 255, 255)` (opaque white).

### Floor Labels (per model)

For each loaded model at screen-space projected position of a point on the ground in front
of it, a short name label is drawn. The label text is the asset name (e.g. `res_low_01`,
`car_sedan`).

**World position of the label anchor**: `(modelWorldX, 0.05f, modelWorldZ + 6.0f)` — 6 m
in front of the model centre (in the +Z direction), 5 cm above the ground plane so it is
not z-fighting with the ground.

**Projection**: use `smgr->getSceneCollisionManager()->getScreenCoordinatesFrom3DPosition()`
to convert the world anchor to 2D screen coordinates. If the projected point is off-screen
(x or y outside `[0, width]` / `[0, height]`), skip drawing that label.

**Rendering**: `font->draw(irr::core::stringw(name.c_str()), irr::core::rect<irr::s32>(sx, sy, sx+200, sy+20), irr::video::SColor(255, 255, 220, 60))` — amber/yellow text
(`RGB 255, 220, 60`), width budget 200 px.

**Draw order**: HUD elements are drawn after `smgr->drawAll()` and before
`driver->endScene()`, inside the `driver->beginScene()`/`endScene()` block.

### Annotation Tool Bar

A single line of text at the bottom of the screen (drawn in the current mark colour)
shows the active tool state and available hotkeys:

```text
Draw: 1=Red 2=Green 3=Blue 4=Yellow 5=Magenta  C=shape[DOT]  LClick=place  Z=undo  X=clear  S=screenshot
```

---

## Annotation Drawing Mode

The validator includes a 2D overlay annotation system for visual review.
The operator clicks to place marks; screenshots of annotated views are saved for analysis.

### Controls

| Input | Action |
|---|---|
| **Left hold/drag** | Freehand draw annotation marks |
| **Right drag** | Orbit camera (all modes) |
| **Mouse wheel** | Zoom in/out (adjusts orbit radius, 3 m per notch, range 5–200 m) |
| **C** | Cycle annotation colour (Red → Green → Blue → Yellow → Cyan → Magenta → White → Black) |
| **V** | Cycle mark shape: `DOT` → `CIRCLE` → `CROSS` → `DOT` |
| **S** | Save an annotated screenshot (one file per press, counter resumes across sessions) |
| **Z** | Undo last stroke |
| **X** | Clear all marks for the current category |

Marks are cleared automatically when advancing to the next category (Spacebar).

**Click vs drag detection**: a left-button release is treated as a click (mark placed) only if
the mouse moved ≤ 4 px from the press position. Larger movement is treated as a camera drag.

### Colour Palette

Right-click cycles through 8 visually distinct colours:

| # | Name | RGB |
|---|---|---|
| 0 | Red | `(220, 30, 30)` |
| 1 | Green | `(30, 180, 30)` |
| 2 | Blue | `(30, 100, 220)` |
| 3 | Yellow | `(220, 200, 0)` |
| 4 | Cyan | `(0, 200, 220)` |
| 5 | Magenta | `(220, 0, 220)` |
| 6 | White | `(255, 255, 255)` |
| 7 | Black | `(20, 20, 20)` |

### Mark Shapes

| Shape | Rendering | Reference name |
|---|---|---|
| `DOT` | Filled 10×10 px square centred on click | "red dot", "green dot" |
| `CIRCLE` | 24-segment outline circle, radius 18 px | "blue circle", "yellow circle" |
| `CROSS` | Two diagonal lines, ±12 px arms | "magenta cross", "red cross" |

### Mouse Cursor

A small crosshair (±6 px) is rendered at the current mouse position in the active
colour so the operator can see exactly where a click will land.

### Screenshot Output

`S` saves `annotation_N.png` (N increments globally across the session) in the current
working directory and prints the path to stdout:

```text
  Annotation screenshot saved: annotation_1.png
```

The operator can then ask Claude Code to read the screenshot path and analyse the
annotated marks.

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

=== [1/11]: Residential Low — 4 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: res_low_01 res_low_02 res_low_03 res_low_04
  Category displayed for 312 frames (avg 104.0 fps)

=== [2/11]: Residential Med — 4 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: res_med_01 res_med_02 res_med_03 res_med_04
  Category displayed for 298 frames (avg 99.3 fps)

...

=== [6/11]: Commercial High (Skyscrapers) — 4 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: com_high_01 com_high_02 com_high_03 com_high_04
  Category displayed for 350 frames (avg 87.5 fps)

...

=== [10/11]: Service — 4 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: svc_fire_station svc_police_station svc_power_plant svc_water_tower
  Category displayed for 280 frames (avg 93.3 fps)

=== [11/11]: Vehicles — 5 models loaded, press SPACE for next (ESC to exit) ===
  Loaded: car_sedan car_hatchback car_suv bus_standard truck_cargo
  Category displayed for 412 frames (avg 87.3 fps)

=== Model Validator complete — all 11 categories displayed ===
```

Failed loads appear on stderr:

```text
  WARNING: failed to load 'res_med_01'. Skipping.
```

---

## CLI Reference

### Usage

```text
Usage: aitown_model_validator [options]
  --width W              window width (default: 1280)
  --height H             window height (default: 720)
  --model <n1> [n2...]   show only the named model(s) in a single "Custom Selection"
                         category. Auto-detects vehicles vs buildings from the model
                         name prefix (car_, bus_, truck_ = vehicles; everything else
                         = buildings at 10× scale). All available LODs (LOD0, LOD1,
                         LOD2) are displayed simultaneously side by side, labelled
                         "[LOD0]", "[LOD1]", "[LOD2]" — no distance-based switching.
                         Camera does NOT auto-rotate; right-drag to orbit manually.
                         One 10×10 m tile square is drawn around each LOD slot.
  --screenshot <file>    after rendering --screenshot-frame frames, save a PNG
                         screenshot to the given path and advance to the next
                         category. Multiple categories produce numbered files
                         (e.g. shot_1.png, shot_2.png). In screenshot mode the
                         orbit starts at 35° so both the front face and one side
                         are visible immediately
  --screenshot-frame N   frame number at which to capture the screenshot
                         (default: 3)
  --help                 print this usage message
```

### Exit Codes

- `0` — success (all categories displayed or operator exited early)
- `1` — device creation failure (no OpenGL context available)

### Examples

```bash
# Display only a single building and exit
./build/aitown_model_validator --model res_low_01

# Display multiple models in a custom selection
./build/aitown_model_validator --model res_low_01 res_med_02 com_high_03

# Display vehicles with road shader
./build/aitown_model_validator --model car_sedan car_hatchback bus_standard

# Capture screenshots of all categories, 1920×1080 resolution
./build/aitown_model_validator --width 1920 --height 1080 --screenshot screenshots/shot.png

# Capture a screenshot at frame 60 (stable pose after longer orbit)
./build/aitown_model_validator --model res_med_01 --screenshot out.png --screenshot-frame 60
```

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
  all 11 categories confirming correct mesh, texture, and scale for every model.
- After any change to B3D assets, atlas textures (currently PNG — see V1 exception in
  `architecture/asset-standards/building-atlas-layout.md`), `BuildingAssetLoader`, or
  any `*_billboard.dds` asset rework.
- After any change to `road_asphalt_tileable.dds`, `road.vert`/`road.frag`, or
  `RoadShaderCallback`.
- After any change to `LODNode` or the `IrrlichtRenderer` building-placement scale.
