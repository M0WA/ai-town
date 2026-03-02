// benchmark_main.cpp — AI Town standalone benchmark tool.
//
// Measures FPS and VRAM usage across anisotropic filter levels and recommends
// optimal settings for the game based on hardware capability.
//
// Build: linked as aitown_benchmark executable (see CMakeLists.txt).
// Run:   ./build/aitown_benchmark [--frames N] [--target-fps N] [--width W] [--height H] [--json]
//
// Include order: GLEW before Irrlicht (project convention — GLEW symbol duplication mitigation).

#include <GL/glew.h>
#include <irrlicht.h>

#include "rendering/VRAMProfiler.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GL constants that may be absent from older GLEW installs
// ---------------------------------------------------------------------------
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// ---------------------------------------------------------------------------
// VRAM budget thresholds — per architecture/asset-standards/2d-texture-standards.md
// ---------------------------------------------------------------------------
static constexpr float kSceneVRAMBudgetMB = 170.0f;  // scene VRAM limit (MB)

// Anisotropy spec minimums — per 2d-texture-standards.md §Anisotropic filtering
static constexpr int kSpecMinTerrainAniso    = 8;  // terrain base textures + buildings
static constexpr int kSpecMinRoadPropAniso   = 4;  // roads, props, normals, specular

// ---------------------------------------------------------------------------
// CLI options
// ---------------------------------------------------------------------------
struct BenchmarkOptions {
    int   frames    = 200;
    int   targetFPS = 30;
    int   width     = 1280;
    int   height    = 720;
    bool  jsonOut   = false;
};

static void printUsage(const char* prog)
{
    std::printf(
        "Usage: %s [options]\n"
        "  --frames N       frames to render per anisotropy level (default: 200)\n"
        "  --target-fps N   minimum acceptable FPS for recommendations (default: 30)\n"
        "  --width W        window width (default: 1280)\n"
        "  --height H       window height (default: 720)\n"
        "  --json           also write results to benchmark_results.json\n"
        "  --help           print this usage message\n",
        prog
    );
}

static bool parseArgs(int argc, char** argv, BenchmarkOptions& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--help") == 0)
        {
            return false;  // caller will print usage and exit 0
        }
        else if (std::strcmp(argv[i], "--json") == 0)
        {
            opts.jsonOut = true;
        }
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            opts.frames = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--target-fps") == 0 && i + 1 < argc)
        {
            opts.targetFPS = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            opts.width = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            opts.height = std::atoi(argv[++i]);
        }
        else
        {
            std::printf("Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per-level result
// ---------------------------------------------------------------------------
struct LevelResult {
    int   anisotropy;
    float avgFPS;
    float minFPS;
    float maxFPS;
    float vramUsedMB;  // -1.0f if unavailable
};

// ---------------------------------------------------------------------------
// Apply anisotropy to all materials on a scene node (recursive)
// ---------------------------------------------------------------------------
static void applyAnisotropyToNode(irr::scene::ISceneNode* node, irr::u32 level)
{
    if (!node) { return; }

    irr::u32 matCount = node->getMaterialCount();
    for (irr::u32 m = 0; m < matCount; ++m)
    {
        irr::video::SMaterial& mat = node->getMaterial(m);
        for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t)
            mat.TextureLayer[t].AnisotropicFilter = static_cast<irr::u8>(level);
    }
    for (auto* child : node->getChildren())
    {
        applyAnisotropyToNode(child, level);
    }
}

// ---------------------------------------------------------------------------
// Escape a string for JSON output (minimal — printable ASCII only)
// ---------------------------------------------------------------------------
static std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s)
    {
        if      (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else                { out += c; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    BenchmarkOptions opts;
    bool argsOk = parseArgs(argc, argv, opts);
    if (!argsOk)
    {
        printUsage(argv[0]);
        return 0;
    }

    std::printf("=== AI Town Benchmark ===\n");
    std::printf("Config: %d frames/level, target FPS >= %d, window %dx%d\n\n",
        opts.frames, opts.targetFPS, opts.width, opts.height);

    // -----------------------------------------------------------------------
    // 1. Create Irrlicht device (EDT_OPENGL, no vsync)
    // -----------------------------------------------------------------------
    irr::SIrrlichtCreationParameters params;
    params.DriverType       = irr::video::EDT_OPENGL;
    params.WindowSize       = irr::core::dimension2d<irr::u32>(
                                  static_cast<irr::u32>(opts.width),
                                  static_cast<irr::u32>(opts.height));
    params.Bits             = 32;
    params.ZBufferBits      = 24;
    params.Fullscreen       = false;
    params.Stencilbuffer    = true;
    params.AntiAlias        = 4;
    params.Vsync            = false;
    params.EventReceiver    = nullptr;

    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device)
    {
        std::fprintf(stderr, "ERROR: Failed to create Irrlicht device (EDT_OPENGL). "
                             "Ensure a display is available.\n");
        return 1;
    }

    irr::video::IVideoDriver*  driver = device->getVideoDriver();
    irr::scene::ISceneManager* smgr   = device->getSceneManager();

    // -----------------------------------------------------------------------
    // 2. Init GLEW and VRAMProfiler
    // -----------------------------------------------------------------------
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
    {
        std::fprintf(stderr, "WARNING: glewInit() failed: %s — VRAM queries unavailable.\n",
                     reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
    }

    VRAMProfiler vram;
    vram.init();

    // -----------------------------------------------------------------------
    // 3. Query hardware limits
    // -----------------------------------------------------------------------
    const char* glVendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glVersion  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!glVendor)   { glVendor   = "(unknown)"; }
    if (!glRenderer) { glRenderer = "(unknown)"; }
    if (!glVersion)  { glVersion  = "(unknown)"; }

    // Query hardware max anisotropy.
    GLfloat hwMaxAniso = 1.0f;
    if (glewIsSupported("GL_EXT_texture_filter_anisotropic"))
    {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &hwMaxAniso);
    }
    int hwMaxAnisoInt = static_cast<int>(hwMaxAniso);

    std::printf("GPU Vendor:    %s\n", glVendor);
    std::printf("GPU Renderer:  %s\n", glRenderer);
    std::printf("GL Version:    %s\n", glVersion);
    std::printf("VRAM Method:   %s\n", vram.methodName());
    if (vram.totalMB() >= 0.0f)
    {
        std::printf("Total VRAM:    %.0f MB\n", vram.totalMB());
    }
    else
    {
        std::printf("Total VRAM:    unavailable\n");
    }
    std::printf("Max Aniso:     %d×\n\n", hwMaxAnisoInt);

    // -----------------------------------------------------------------------
    // 4. Build procedural benchmark scene
    //
    // Stub B3D asset files carry no geometry (12-byte minimal headers).
    // We build the scene procedurally so the benchmark always shows visible
    // content regardless of asset build state.
    //
    // Scene layout:
    //   - 16×16 tiled ground plane (320 m × 320 m) with a checkerboard
    //     texture and high UV repetition — ideal for showing anisotropy
    //     differences at a grazing camera angle.
    //   - 20 colored box proxies scattered across the ground to give the
    //     GPU draw-call and rasterisation work proportional to a city scene.
    //   - Camera at low altitude looking along the ground surface (grazing
    //     angle makes anisotropy filtering differences visually obvious).
    // -----------------------------------------------------------------------

    // --- Procedural checkerboard texture (no file I/O) ---
    const irr::u32 TEX_SZ   = 512u;
    const irr::u32 CHECKER   = 32u;  // texels per checker square
    irr::video::ITexture* checkerTex = nullptr;
    {
        irr::video::IImage* img = driver->createImage(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2du(TEX_SZ, TEX_SZ));
        if (img)
        {
            for (irr::u32 py = 0; py < TEX_SZ; ++py)
                for (irr::u32 px = 0; px < TEX_SZ; ++px)
                {
                    bool light2 = ((px / CHECKER) + (py / CHECKER)) % 2 == 0;
                    img->setPixel(px, py,
                        light2 ? irr::video::SColor(255, 220, 220, 220)
                               : irr::video::SColor(255,  30,  30,  30));
                }
            checkerTex = driver->addTexture("__bench_checker", img);
            img->drop();
        }
    }

    // --- Ground plane: 16×16 grid of 20 m × 20 m quads ---
    // 256 quads total → 1024 vertices (well within u16 index limit).
    // UV repeats 4× per quad so the texture tiles densely at low angle.
    {
        const int   TILES   = 16;
        const float TILE_SZ = 20.0f;
        const float UV_REP  = 4.0f;
        const float HALF    = (TILES * TILE_SZ) * 0.5f;

        irr::scene::SMesh*       gmesh = new irr::scene::SMesh();
        irr::scene::SMeshBuffer* gbuf  = new irr::scene::SMeshBuffer();

        const irr::video::SColor white(255, 255, 255, 255);
        for (int gz = 0; gz < TILES; ++gz)
        {
            for (int gx = 0; gx < TILES; ++gx)
            {
                float x0 = gx * TILE_SZ - HALF;
                float z0 = gz * TILE_SZ - HALF;
                float x1 = x0 + TILE_SZ;
                float z1 = z0 + TILE_SZ;
                irr::u16 base = static_cast<irr::u16>(gbuf->Vertices.size());
                gbuf->Vertices.push_back(irr::video::S3DVertex(
                    x0, 0.f, z0,  0.f, 1.f, 0.f,  white,  0.f,    0.f));
                gbuf->Vertices.push_back(irr::video::S3DVertex(
                    x1, 0.f, z0,  0.f, 1.f, 0.f,  white,  UV_REP, 0.f));
                gbuf->Vertices.push_back(irr::video::S3DVertex(
                    x1, 0.f, z1,  0.f, 1.f, 0.f,  white,  UV_REP, UV_REP));
                gbuf->Vertices.push_back(irr::video::S3DVertex(
                    x0, 0.f, z1,  0.f, 1.f, 0.f,  white,  0.f,    UV_REP));
                gbuf->Indices.push_back(base);
                gbuf->Indices.push_back(static_cast<irr::u16>(base + 1));
                gbuf->Indices.push_back(static_cast<irr::u16>(base + 2));
                gbuf->Indices.push_back(base);
                gbuf->Indices.push_back(static_cast<irr::u16>(base + 2));
                gbuf->Indices.push_back(static_cast<irr::u16>(base + 3));
            }
        }
        gbuf->recalculateBoundingBox();
        gmesh->addMeshBuffer(gbuf);
        gbuf->drop();
        gmesh->recalculateBoundingBox();

        irr::scene::IMeshSceneNode* gn = smgr->addMeshSceneNode(gmesh);
        gmesh->drop();
        if (gn)
        {
            if (checkerTex) { gn->setMaterialTexture(0, checkerTex); }
            gn->setMaterialFlag(irr::video::EMF_LIGHTING,         false);
            gn->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER,  true);
            gn->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
        }
    }

    // --- Building proxy boxes (geometry creator — no file I/O) ---
    {
        const irr::scene::IGeometryCreator* geo = smgr->getGeometryCreator();
        const int   NUM_BOXES = 20;
        const float SPREAD    = 120.0f;
        irr::u32 rng = 0xDEADBEEFu;
        auto lcg = [&]() -> float {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>(rng & 0xFFFFu) / 65535.0f;
        };
        for (int bi = 0; bi < NUM_BOXES; ++bi)
        {
            float bw = 8.0f  + lcg() * 10.0f;
            float bh = 15.0f + lcg() * 40.0f;
            float bd = 8.0f  + lcg() * 10.0f;
            irr::scene::IMesh* box = geo->createCubeMesh(
                irr::core::vector3df(bw, bh, bd));
            if (!box) { continue; }
            irr::scene::IMeshSceneNode* bn = smgr->addMeshSceneNode(box);
            box->drop();
            if (!bn) { continue; }
            float px = (lcg() * 2.0f - 1.0f) * SPREAD;
            float pz = (lcg() * 2.0f - 1.0f) * SPREAD;
            bn->setPosition(irr::core::vector3df(px, bh * 0.5f, pz));
            bn->getMaterial(0).DiffuseColor = irr::video::SColor(
                255,
                static_cast<irr::u32>(100 + lcg() * 100),
                static_cast<irr::u32>(100 + lcg() * 100),
                static_cast<irr::u32>(100 + lcg() * 100));
            bn->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        }
    }

    std::printf("INFO: Scene ready — 16×16 ground grid + 20 building proxies.\n\n");

    // --- Camera: low altitude looking along the ground (grazing angle) ---
    // Grazing angle makes anisotropy differences clearly visible on the
    // ground plane — the primary purpose of the benchmark scene layout.
    irr::scene::ICameraSceneNode* camera = smgr->addCameraSceneNode(
        nullptr,
        irr::core::vector3df(  0.0f, 20.0f, -180.0f),  // low above ground
        irr::core::vector3df(  0.0f,  0.0f,   80.0f)   // looking forward
    );
    (void)camera;

    // -----------------------------------------------------------------------
    // 5. Determine anisotropy test levels (skip levels beyond hardware max)
    // -----------------------------------------------------------------------
    const int kTestLevels[] = {1, 2, 4, 8, 16};
    const int kNumTestLevels = static_cast<int>(sizeof(kTestLevels) / sizeof(kTestLevels[0]));

    std::printf("%-12s  %10s  %10s  %10s  %10s\n",
        "Anisotropy", "Avg FPS", "Min FPS", "Max FPS", "VRAM (MB)");
    std::printf("%-12s  %10s  %10s  %10s  %10s\n",
        "----------", "-------", "-------", "-------", "---------");

    std::vector<LevelResult> results;
    results.reserve(kNumTestLevels);

    for (int li = 0; li < kNumTestLevels; ++li)
    {
        int level = kTestLevels[li];
        if (level > hwMaxAnisoInt)
        {
            std::printf("  [ANISOx%2d]  (skipped — exceeds hardware max %d×)\n",
                level, hwMaxAnisoInt);
            continue;
        }

        // Apply anisotropy level to all scene nodes (ground + boxes).
        irr::u32 uLevel = static_cast<irr::u32>(level);
        applyAnisotropyToNode(smgr->getRootSceneNode(), uLevel);

        // Also apply globally via driver material — affects state for subsequent draws.
        irr::video::SMaterial globalMat;
        for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t)
            globalMat.TextureLayer[t].AnisotropicFilter = static_cast<irr::u8>(level);
        driver->setMaterial(globalMat);

        // Warm-up: 20 frames not timed.
        for (int f = 0; f < 20; ++f)
        {
            if (!device->run()) { break; }
            driver->beginScene(true, true, irr::video::SColor(255, 30, 30, 40));
            smgr->drawAll();
            driver->endScene();
        }

        // Measurement pass.
        float minFrameTime =  1e30f;
        float maxFrameTime = -1e30f;
        float totalTime    =  0.0f;
        int   frameCount   =  0;

        for (int f = 0; f < opts.frames; ++f)
        {
            if (!device->run()) { break; }

            auto t0 = std::chrono::steady_clock::now();
            driver->beginScene(true, true, irr::video::SColor(255, 30, 30, 40));
            smgr->drawAll();
            driver->endScene();
            auto t1 = std::chrono::steady_clock::now();

            float dt = std::chrono::duration<float>(t1 - t0).count();
            if (dt < minFrameTime) { minFrameTime = dt; }
            if (dt > maxFrameTime) { maxFrameTime = dt; }
            totalTime += dt;
            ++frameCount;
        }

        if (frameCount == 0) { break; }

        float avgFrameTime = totalTime / static_cast<float>(frameCount);
        float avgFPS = (avgFrameTime > 0.0f) ? (1.0f / avgFrameTime) : 0.0f;
        float minFPS = (maxFrameTime > 0.0f) ? (1.0f / maxFrameTime) : 0.0f;
        float maxFPS = (minFrameTime > 0.0f) ? (1.0f / minFrameTime) : 0.0f;

        float usedMB = vram.usedMB();

        LevelResult res;
        res.anisotropy  = level;
        res.avgFPS      = avgFPS;
        res.minFPS      = minFPS;
        res.maxFPS      = maxFPS;
        res.vramUsedMB  = usedMB;
        results.push_back(res);

        // Print row.
        if (usedMB >= 0.0f)
        {
            std::printf("  [ANISOx%2d]  avg=%6.1f fps  min=%6.1f fps  max=%6.1f fps  VRAM=%6.1f MB\n",
                level, avgFPS, minFPS, maxFPS, usedMB);
        }
        else
        {
            std::printf("  [ANISOx%2d]  avg=%6.1f fps  min=%6.1f fps  max=%6.1f fps  VRAM=unavail\n",
                level, avgFPS, minFPS, maxFPS);
        }
    }

    // -----------------------------------------------------------------------
    // 6. Recommendation engine
    // -----------------------------------------------------------------------
    std::printf("\n=== Recommended Settings for this GPU ===\n");

    // Find the highest anisotropy level where:
    //   avgFPS >= targetFPS  AND  vramUsedMB <= kSceneVRAMBudgetMB (or VRAM unavailable).
    const LevelResult* recommended = nullptr;
    for (int i = static_cast<int>(results.size()) - 1; i >= 0; --i)
    {
        const LevelResult& r = results[static_cast<size_t>(i)];
        bool fpsOk  = (r.avgFPS >= static_cast<float>(opts.targetFPS));
        bool vramOk = (r.vramUsedMB < 0.0f) || (r.vramUsedMB <= kSceneVRAMBudgetMB);
        if (fpsOk && vramOk)
        {
            recommended = &r;
            break;
        }
    }

    bool lowFPSWarning = false;
    if (!recommended && !results.empty())
    {
        // No level met FPS threshold — recommend lowest (1×) with warning.
        recommended  = &results.front();
        lowFPSWarning = true;
    }

    if (!recommended)
    {
        std::printf("No results recorded — no recommendation available.\n");
    }
    else
    {
        // Terrain anisotropy: use recommended level, but note spec minimums.
        int terrainAniso = recommended->anisotropy;
        int roadAniso    = (terrainAniso >= kSpecMinRoadPropAniso)
                               ? kSpecMinRoadPropAniso
                               : terrainAniso;

        std::printf("Terrain anisotropy:        %2d×  (spec minimum: %d×)\n",
            terrainAniso, kSpecMinTerrainAniso);
        std::printf("Road/prop/normal/specular: %2d×  (spec minimum: %d×)\n",
            roadAniso, kSpecMinRoadPropAniso);

        if (recommended->vramUsedMB >= 0.0f)
        {
            std::printf("Scene VRAM at recommended: %.1f MB / %.1f MB budget\n",
                recommended->vramUsedMB, kSceneVRAMBudgetMB);
        }
        else
        {
            std::printf("Scene VRAM at recommended: unavailable / %.1f MB budget\n",
                kSceneVRAMBudgetMB);
        }

        std::printf("Avg FPS at recommended:    %.1f  (target: >=%d)\n",
            recommended->avgFPS, opts.targetFPS);
        std::printf("Min FPS at recommended:    %.1f\n",
            recommended->minFPS);

        bool budgetPass = (recommended->vramUsedMB < 0.0f)
                       || (recommended->vramUsedMB <= kSceneVRAMBudgetMB);
        std::printf("Budget: %s  (scene VRAM %s)\n",
            budgetPass ? "PASS" : "FAIL",
            budgetPass ? "within 170 MB limit" : "EXCEEDS 170 MB limit");

        if (lowFPSWarning)
        {
            std::printf("\nWARNING: No anisotropy level achieved target FPS (%d). "
                        "Recommending lowest level (1×) — GPU may be underpowered.\n",
                        opts.targetFPS);
        }

        if (terrainAniso < kSpecMinTerrainAniso)
        {
            std::printf("\nWARNING: Recommended terrain anisotropy (%d×) is below spec minimum (%d×). "
                        "GPU may not meet AI Town visual quality requirements at target FPS.\n",
                        terrainAniso, kSpecMinTerrainAniso);
        }

        if (roadAniso < kSpecMinRoadPropAniso)
        {
            std::printf("\nWARNING: Recommended road/prop anisotropy (%d×) is below spec minimum (%d×). "
                        "GPU may not meet AI Town visual quality requirements at target FPS.\n",
                        roadAniso, kSpecMinRoadPropAniso);
        }

        // -------------------------------------------------------------------
        // 7. JSON output
        // -------------------------------------------------------------------
        if (opts.jsonOut)
        {
            std::ofstream js("benchmark_results.json");
            if (js.is_open())
            {
                js << "{\n";
                js << "  \"gpu\": \"" << jsonEscape(glRenderer) << "\",\n";
                if (vram.totalMB() >= 0.0f)
                    js << "  \"total_vram_mb\": " << vram.totalMB() << ",\n";
                else
                    js << "  \"total_vram_mb\": null,\n";

                // Compact method name for JSON (strip long description).
                std::string methodTag;
                switch (vram.method())
                {
                case VRAMProfiler::Method::NVX:         methodTag = "NVX";         break;
                case VRAMProfiler::Method::ATI:         methodTag = "ATI";         break;
                case VRAMProfiler::Method::MANUAL:      methodTag = "MANUAL";      break;
                case VRAMProfiler::Method::UNAVAILABLE: methodTag = "UNAVAILABLE"; break;
                default:                                methodTag = "UNKNOWN";     break;
                }
                js << "  \"vram_method\": \"" << methodTag << "\",\n";

                js << "  \"results\": [\n";
                for (size_t ri = 0; ri < results.size(); ++ri)
                {
                    const LevelResult& r = results[ri];
                    js << "    {";
                    js << "\"anisotropy\": " << r.anisotropy << ", ";
                    js << "\"avg_fps\": "    << r.avgFPS     << ", ";
                    js << "\"min_fps\": "    << r.minFPS     << ", ";
                    js << "\"max_fps\": "    << r.maxFPS     << ", ";
                    if (r.vramUsedMB >= 0.0f)
                        js << "\"vram_used_mb\": " << r.vramUsedMB;
                    else
                        js << "\"vram_used_mb\": null";
                    js << "}";
                    if (ri + 1 < results.size()) { js << ","; }
                    js << "\n";
                }
                js << "  ],\n";

                js << "  \"recommendation\": {\n";
                js << "    \"terrain_anisotropy\": " << terrainAniso << ",\n";
                js << "    \"road_prop_anisotropy\": " << roadAniso << ",\n";
                if (recommended->vramUsedMB >= 0.0f)
                    js << "    \"scene_vram_mb\": " << recommended->vramUsedMB << ",\n";
                else
                    js << "    \"scene_vram_mb\": null,\n";
                js << "    \"avg_fps\": " << recommended->avgFPS << ",\n";
                js << "    \"budget_pass\": " << (budgetPass ? "true" : "false") << "\n";
                js << "  }\n";
                js << "}\n";

                std::printf("\nJSON results written to benchmark_results.json\n");
            }
            else
            {
                std::fprintf(stderr, "WARNING: Could not open benchmark_results.json for writing.\n");
            }
        }
    }

    // -----------------------------------------------------------------------
    // 8. Cleanup
    // -----------------------------------------------------------------------
    device->drop();

    return 0;
}
