# Benchmark Tool

## Purpose

The AI Town benchmark tool (`aitown_benchmark`) is a standalone executable that:

- Measures frame-rate (FPS) across anisotropic filter levels on the host GPU.
- Samples GPU VRAM usage after each anisotropy level using the best available query method.
- Compares results against the VRAM budget defined in
  [`architecture/asset-standards/2d-texture-standards.md`](../asset-standards/2d-texture-standards.md)
  (scene VRAM limit: 170 MB, total limit: 1.0 GB).
- Emits a recommended terrain anisotropy level and road/prop/normal/specular anisotropy level for
  the host GPU, validated against the spec minimums (terrain ≥ 8×, roads/props ≥ 4×).

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
| 1 | `GL_NVX_gpu_memory_info` | Total VRAM (cached at init), free VRAM per frame | NVIDIA only. `usedMB()` = total − free. Values in KB. |
| 2 | `GL_ATI_meminfo` | Free texture VRAM only | AMD/ATI only. `usedMB()` returns −1 (no used query). |
| 3 | Manual | Accumulated by caller via `addTexture()` | Requires explicit feeding. |
| 4 | UNAVAILABLE | None | Both `usedMB()` and `totalMB()` return −1. |

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
5. Load representative building asset:
   `assets/3d/buildings/res_low_01_lod0.b3d` via `smgr->getMesh()`.
   If the file is missing, print a warning and continue with an empty scene.
6. Add a directional light and a camera at position (0, 50, −100) looking at the origin.

### Anisotropy Test Levels

Tested levels: **{1, 2, 4, 8, 16}**.

Hardware maximum is queried via `glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &hwMax)`.
Any level exceeding `hwMax` is skipped with a printed notice.

For each level:

1. Apply `SMaterial::AnisotropicFilter = level` to all scene node materials.
2. **Warm-up**: render 20 frames (not timed) to allow driver JIT and pipeline caching.
3. **Measurement**: render `--frames` frames. Record wall time per frame via
   `std::chrono::steady_clock`.
4. **Metrics**: avg FPS, min FPS (slowest frame inverse), max FPS (fastest frame inverse).
5. **VRAM sample**: call `VRAMProfiler::usedMB()` after the measurement pass.
6. Print formatted row.

### Output Table Format

```text
Anisotropy    Avg FPS    Min FPS    Max FPS    VRAM (MB)
----------    -------    -------    -------    ---------
  [ANISOx 1]  avg= 145.2 fps  min= 112.0 fps  max= 180.1 fps  VRAM= 128.3 MB
  [ANISOx 2]  avg= 142.1 fps  min= 110.5 fps  max= 178.2 fps  VRAM= 128.5 MB
  [ANISOx 4]  avg= 138.6 fps  min= 106.3 fps  max= 175.0 fps  VRAM= 128.8 MB
  [ANISOx 8]  avg=  98.4 fps  min=  78.2 fps  max= 120.3 fps  VRAM= 132.1 MB
  [ANISOx16]  avg=  58.4 fps  min=  44.1 fps  max=  72.5 fps  VRAM= 142.3 MB
```

---

## Recommendation Engine

### Algorithm

1. Scan tested levels from **highest to lowest**.
2. Select the first (highest) level where:
   - `avgFPS >= target` (default: 30 FPS, configurable via `--target-fps`)
   - `vramUsedMB <= 170.0f` **or** VRAM is unavailable (method = ATI/UNAVAILABLE)
3. If no level satisfies the criteria: recommend lowest tested level (1×) with a **WARNING**.

### Spec Minimum Enforcement

| Texture category | Spec minimum | Source |
|---|---|---|
| Terrain base textures, building facades | 8× | `2d-texture-standards.md` §Anisotropic filtering |
| Road tileable, props, normal maps, specular/roughness | 4× | `2d-texture-standards.md` §Anisotropic filtering |
| UI sprite sheet, splat maps | disabled (0) | `2d-texture-standards.md` §Anisotropic filtering |

If the recommended level is below the spec minimum for terrain (< 8×) or roads/props (< 4×),
the tool prints a WARNING that the GPU may not meet AI Town visual quality requirements.

### Recommendation Output

```text
=== Recommended Settings for this GPU ===
Terrain anisotropy:        16×  (spec minimum: 8×)
Road/prop/normal/specular:  4×  (spec minimum: 4×)
Scene VRAM at recommended: 142.3 MB / 170.0 MB budget
Avg FPS at recommended:     58.4  (target: >=30)
Min FPS at recommended:     44.1
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
  "results": [
    {"anisotropy": 1,  "avg_fps": 145.2, "min_fps": 112.0, "max_fps": 180.1, "vram_used_mb": 128.3},
    {"anisotropy": 2,  "avg_fps": 142.1, "min_fps": 110.5, "max_fps": 178.2, "vram_used_mb": 128.5},
    {"anisotropy": 4,  "avg_fps": 138.6, "min_fps": 106.3, "max_fps": 175.0, "vram_used_mb": 128.8},
    {"anisotropy": 8,  "avg_fps":  98.4, "min_fps":  78.2, "max_fps": 120.3, "vram_used_mb": 132.1},
    {"anisotropy": 16, "avg_fps":  58.4, "min_fps":  44.1, "max_fps":  72.5, "vram_used_mb": 142.3}
  ],
  "recommendation": {
    "terrain_anisotropy": 16,
    "road_prop_anisotropy": 4,
    "scene_vram_mb": 142.3,
    "avg_fps": 58.4,
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
§Scene VRAM Budget:

| Budget | Value |
|---|---|
| Scene VRAM (all simultaneously-resident textures) | ≤ 170 MB |
| Total GPU VRAM allocation (all pools) | ≤ 1.0 GB |

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
