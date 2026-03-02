# Benchmark Tool

## Purpose

The AI Town benchmark tool (`aitown_benchmark`) is a standalone executable that:

- Measures frame-rate (FPS) across anisotropic filter levels on the host GPU.
- Samples GPU VRAM usage after each anisotropy level using the best available query method.
- Compares results against the VRAM budget defined in
  [`architecture/asset-standards/2d-texture-standards.md`](../asset-standards/2d-texture-standards.md)
  (scene VRAM limit: 170 MB, total limit: 1.0 GB).
- Emits a recommended terrain anisotropy level and road/prop/normal/specular anisotropy level for
  the host GPU, validated against the spec minimums (terrain >= 8x, roads/props >= 4x).

The tool is **not** part of the game runtime and is **not** executed in CI (hardware-dependent).
It must be run manually on each target GPU tier before shipping.

---

## VRAMProfiler

### Class Summary

`VRAMProfiler` lives in `src/rendering/VRAMProfiler.h` / `src/rendering/VRAMProfiler.cpp`.
It is a reusable utility class consumed by both the benchmark executable and the runtime
`RenderSystem` (for diagnostic logging, not for game logic).

```cpp
Method: NVX | ATI | MANUAL | UNAVAILABLE
void    init()
float   usedMB()   const   // -1.0f if unavailable
float   totalMB()  const   // -1.0f if unavailable
Method  method()   const
const char* methodName() const
void    addTexture(width, height, bppCompressed, mipLevels)  // MANUAL only
void    resetManual()
```

### Detection Strategy

`init()` must be called after a valid OpenGL context exists and `glewInit()` has been called.
Detection is attempted in the following order:

| Priority | Extension | Provides | Notes |
|---|---|---|---|
| 1 | `GL_NVX_gpu_memory_info` | Total VRAM (cached at init), free VRAM per frame | NVIDIA only. `usedMB()` = total - free. Values in KB. |
| 2 | `GL_ATI_meminfo` | Free texture VRAM only | AMD/ATI only. `usedMB()` returns -1 (no used query). |
| 3 | Manual | Accumulated by caller via `addTexture()` | Requires explicit feeding. |
| 4 | UNAVAILABLE | None | Both `usedMB()` and `totalMB()` return -1. |

**Extension checking**: `glewIsSupported("GL_NVX_gpu_memory_info")` /
`glewIsSupported("GL_ATI_meminfo")`.

**GL constants** (may be absent from older GLEW installs — defined with guards in the header):

```cpp
#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX  0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif
```

### Thread Safety

`VRAMProfiler` is NOT thread-safe. All calls must originate from the render/main thread that
owns the active OpenGL context.

---

## Benchmark Flow

### Scene Setup

1. Create Irrlicht device: `EDT_OPENGL`, requested resolution, `Vsync=false`.
2. Call `glewInit()` immediately after device creation (same timing as `RenderSystem`).
3. Call `VRAMProfiler::init()`.
4. Print GPU info: `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, VRAM method and total.
5. Create a shared procedural sky gradient texture (512x256, ECF_A8R8G8B8) used by both scenes.
6. Build Scene 1 and Scene 2 (see below). Each scene uses its own `ISceneManager*`.

### Scene 1: Anisotropy Ground

Scene 1 is the primary anisotropy stress test. It is always rendered and drives the FPS
measurements that determine the recommended anisotropy level.

Contents:

- **Sky dome** — procedural gradient texture (deep blue zenith to pale horizon).
- **Ground plane** — 16x16 grid of 20 m tiles (320 m x 320 m total) with a checkerboard
  texture and 4x UV repetition per tile. The grazing camera angle makes anisotropy filtering
  differences visually obvious.
- **Building proxy boxes** — 20 colored boxes placed using a fixed LCG seed (`0xDEADBEEF`),
  lit by a warm directional sun light (ELT_DIRECTIONAL). `EMF_LIGHTING=true` so DiffuseColor
  takes effect.
- **Directional sun light** — warm tone (diffuse 1.0/0.92/0.80), ambient 0.25/0.28/0.35,
  direction (-1, -2, 0.5) normalized.
- **Camera** — position (0, 20, -180) looking at (0, 0, 80): low altitude, grazing angle.

### Scene 2: Light / Shadow / Water

Scene 2 is built via a second `ISceneManager*` created with
`smgr->createNewSceneManager(false)`. It exercises point lighting, stencil shadow volumes,
and an animated water surface — representative of a live city district view.

Contents:

- **Sky dome** — same shared gradient texture as Scene 1.
- **Ground plane** — 16x16 grid, green diffuse (`SColor(255, 60, 130, 60)`),
  `EMF_LIGHTING=true`.
- **Water surface** — `addWaterSurfaceSceneNode()` at y=-1.5, offset to (80, -1.5, 40).
  Wave height 1.5, speed 300, length 20. Translucent blue material, Shininess=64.
- **Point sun light** — ELT_POINT at (150, 400, -100), radius 1200,
  `CastShadows=true`. `smgr2->setShadowColor(SColor(120, 0, 0, 0))`.
- **Building proxy boxes** — 20 boxes, same LCG seed as Scene 1 (matching positions).
  Each node calls `addShadowVolumeSceneNode()` for stencil shadow casting.
- **Camera** — same position as Scene 1: (0, 20, -180) looking at (0, 0, 80).

At cleanup, `smgr2->drop()` is called before `device->drop()`.

### Two-Pass Benchmark Structure

For each anisotropy level the benchmark runs **two separate passes** — one for Scene 1 and
one for Scene 2 — each with their own warmup and measurement loop:

1. Apply anisotropy to all scene nodes (via `applyAnisotropyToNode` on the scene's root node).
2. Set the global driver material anisotropy filter for the active scene.
3. Warm-up: render 20 frames (not timed).
4. Measurement: render `--frames` frames. Record wall time per frame via
   `std::chrono::steady_clock`.
5. Metrics: avg FPS, min FPS (slowest frame inverse), max FPS (fastest frame inverse).
6. VRAM sample: call `VRAMProfiler::usedMB()` after the measurement pass.
7. Print formatted row.

Results are stored in separate vectors: `results1` (Scene 1) and `results2` (Scene 2).

The recommendation engine takes the **minimum avgFPS** across both scenes at each anisotropy
level and the **maximum VRAM** reading as the conservative worst-case for the threshold check.

### Output Format

```text
=== Scene 1: Anisotropy Ground (ground plane + building proxies) ===
  [ANISOx 1]  avg=240.5 fps  min=210.2 fps  max=285.1 fps  VRAM=  45.2 MB
  [ANISOx 2]  avg=238.1 fps  min=208.0 fps  max=282.3 fps  VRAM=  45.3 MB
  [ANISOx 4]  avg=232.4 fps  min=203.7 fps  max=276.0 fps  VRAM=  45.5 MB
  [ANISOx 8]  avg=195.3 fps  min=168.2 fps  max=230.1 fps  VRAM=  46.1 MB
  [ANISOx16]  avg=142.6 fps  min=120.0 fps  max=170.4 fps  VRAM=  47.8 MB

=== Scene 2: Light / Shadow / Water ===
  [ANISOx 1]  avg=120.3 fps  min= 98.5 fps  max=145.2 fps  VRAM=  68.4 MB
  [ANISOx 2]  avg=118.7 fps  min= 97.1 fps  max=143.0 fps  VRAM=  68.5 MB
  [ANISOx 4]  avg=115.2 fps  min= 94.3 fps  max=139.8 fps  VRAM=  68.7 MB
  [ANISOx 8]  avg= 92.4 fps  min= 75.0 fps  max=112.1 fps  VRAM=  69.4 MB
  [ANISOx16]  avg= 68.1 fps  min= 55.3 fps  max= 83.2 fps  VRAM=  71.0 MB
```

---

## Recommendation Engine

### Algorithm

1. Scan tested levels from **highest to lowest** using `results1` as the level list.
2. For each level, find the matching `results2` entry and compute:
   - `worstAvgFPS = min(r1.avgFPS, r2.avgFPS)`
   - `worstVram   = max(r1.vramUsedMB, r2.vramUsedMB)` (conservative)
3. Select the first (highest) level where:
   - `worstAvgFPS >= target` (default: 30 FPS, configurable via `--target-fps`)
   - `worstVram <= 170.0f` **or** VRAM is unavailable (method = ATI/UNAVAILABLE)
4. If no level satisfies the criteria: recommend lowest tested level (1x) with a **WARNING**.

### Spec Minimum Enforcement

| Texture category | Spec minimum | Source |
|---|---|---|
| Terrain base textures, building facades | 8x | `2d-texture-standards.md` section: Anisotropic filtering |
| Road tileable, props, normal maps, specular/roughness | 4x | `2d-texture-standards.md` section: Anisotropic filtering |
| UI sprite sheet, splat maps | disabled (0) | `2d-texture-standards.md` section: Anisotropic filtering |

If the recommended level is below the spec minimum for terrain (< 8x) or roads/props (< 4x),
the tool prints a WARNING that the GPU may not meet AI Town visual quality requirements.

### Recommendation Output

```text
=== Recommended Settings for this GPU ===
Terrain anisotropy:        16x  (spec minimum: 8x)
Road/prop/normal/specular:  4x  (spec minimum: 4x)
Scene VRAM at recommended: 71.0 MB / 170.0 MB budget
Avg FPS at recommended:     68.1  (target: >=30, worst of both scenes)
Min FPS at recommended:     55.3
Budget: PASS  (scene VRAM within 170 MB limit)
```

---

## JSON Output

Enabled by `--json`. Writes `benchmark_results.json` in the working directory.

```json
{
  "gpu": "NVIDIA GeForce RTX 3080",
  "total_vram_mb": 10240.0,
  "vram_method": "NVX",
  "scene1_results": [
    {"anisotropy": 1,  "avg_fps": 240.5, "min_fps": 210.2, "max_fps": 285.1, "vram_used_mb": 45.2},
    {"anisotropy": 2,  "avg_fps": 238.1, "min_fps": 208.0, "max_fps": 282.3, "vram_used_mb": 45.3},
    {"anisotropy": 4,  "avg_fps": 232.4, "min_fps": 203.7, "max_fps": 276.0, "vram_used_mb": 45.5},
    {"anisotropy": 8,  "avg_fps": 195.3, "min_fps": 168.2, "max_fps": 230.1, "vram_used_mb": 46.1},
    {"anisotropy": 16, "avg_fps": 142.6, "min_fps": 120.0, "max_fps": 170.4, "vram_used_mb": 47.8}
  ],
  "scene2_results": [
    {"anisotropy": 1,  "avg_fps": 120.3, "min_fps":  98.5, "max_fps": 145.2, "vram_used_mb": 68.4},
    {"anisotropy": 2,  "avg_fps": 118.7, "min_fps":  97.1, "max_fps": 143.0, "vram_used_mb": 68.5},
    {"anisotropy": 4,  "avg_fps": 115.2, "min_fps":  94.3, "max_fps": 139.8, "vram_used_mb": 68.7},
    {"anisotropy": 8,  "avg_fps":  92.4, "min_fps":  75.0, "max_fps": 112.1, "vram_used_mb": 69.4},
    {"anisotropy": 16, "avg_fps":  68.1, "min_fps":  55.3, "max_fps":  83.2, "vram_used_mb": 71.0}
  ],
  "recommendation": {
    "terrain_anisotropy": 16,
    "road_prop_anisotropy": 4,
    "scene_vram_mb": 71.0,
    "avg_fps": 68.1,
    "budget_pass": true
  }
}
```

Fields that are unavailable (VRAM method = ATI or UNAVAILABLE) are written as `null`.

---

## CLI Reference

```text
Usage: aitown_benchmark [options]
  --frames N       frames to render per anisotropy level (default: 200)
  --target-fps N   minimum acceptable FPS for recommendations (default: 30)
  --width W        window width (default: 1280)
  --height H       window height (default: 720)
  --json           also write results to benchmark_results.json
  --help           print this usage message
```

Exit codes:

- `0` — success (results printed; JSON written if `--json` was requested)
- `1` — device creation failure (no OpenGL context available)

---

## CMake Target

The benchmark is defined in `CMakeLists.txt` as a standalone executable target:

```cmake
add_executable(aitown_benchmark
    src/benchmark/benchmark_main.cpp
    src/rendering/VRAMProfiler.cpp
)
target_link_libraries(aitown_benchmark PRIVATE aitown_render GLEW::GLEW Irrlicht)
target_include_directories(aitown_benchmark PRIVATE src/ ${CMAKE_SOURCE_DIR})
target_compile_definitions(aitown_benchmark PRIVATE
    AITOWN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
```

`VRAMProfiler.cpp` is listed directly (not via `aitown_render`) because `aitown_render` compiles
`VRAMProfiler.cpp` as part of its own static library. The benchmark target is a separate
translation unit context; linking `aitown_render` provides the compiled object transitively via
`PRIVATE` linkage. On Linux, the standard Irrlicht transitive dependencies
(JPEG, PNG, ZLIB, BZip2, Xxf86vm) must also be linked explicitly — the vcpkg Irrlicht port
does not export `INTERFACE_LINK_LIBRARIES` for system libraries.

---

## Relationship to VRAM Budget Spec

The benchmark enforces the scene VRAM budget defined in
[`architecture/asset-standards/2d-texture-standards.md`](../asset-standards/2d-texture-standards.md)
section: Scene VRAM Budget:

| Budget | Value |
|---|---|
| Scene VRAM (all simultaneously-resident textures) | <= 170 MB |
| Total GPU VRAM allocation (all pools) | <= 1.0 GB |

The benchmark uses 170 MB as its hard threshold in the recommendation engine. The total 1.0 GB
ceiling is informational only (displayed from `VRAMProfiler::totalMB()` if available).

---

## CI Integration

The benchmark is **not** run in CI. Reasons:

- Requires a real GPU with an OpenGL context (no `EDT_NULL` fallback — the tool measures actual
  render performance).
- FPS results are hardware-dependent and produce non-deterministic output.
- VRAM query extensions (`GL_NVX_gpu_memory_info`, `GL_ATI_meminfo`) are absent on virtual
  display servers used in CI (`xvfb`).

**Required manual runs**:

- Before each major release: run on the minimum-spec GPU (integrated Intel UHD 630 or
  equivalent mid-range discrete GPU with 4 GB VRAM) and verify `budget_pass: true`.
- When new texture atlases or LOD meshes are added to the asset manifest.
- After any change to `TextureCache` upload logic that could affect VRAM residency.

Results should be committed to `docs/benchmark/` (not tracked in CI) for release notes.
