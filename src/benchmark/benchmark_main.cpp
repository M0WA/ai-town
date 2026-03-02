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
// Render one timed pass for a given scene manager; returns true if device is still running.
// Writes frame-time statistics into the provided accumulators.
// ---------------------------------------------------------------------------
static bool renderTimedFrame(
    irr::IrrlichtDevice*       device,
    irr::video::IVideoDriver*  driver,
    irr::scene::ISceneManager* smgr,
    float&                     minFrameTime,
    float&                     maxFrameTime,
    float&                     totalTime,
    int&                       frameCount)
{
    if (!device->run()) { return false; }

    auto t0 = std::chrono::steady_clock::now();
    driver->beginScene(true, true, irr::video::SColor(255, 190, 215, 245));
    smgr->drawAll();
    driver->endScene();
    auto t1 = std::chrono::steady_clock::now();

    float dt = std::chrono::duration<float>(t1 - t0).count();
    if (dt < minFrameTime) { minFrameTime = dt; }
    if (dt > maxFrameTime) { maxFrameTime = dt; }
    totalTime += dt;
    ++frameCount;
    return true;
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
    // 4. Create shared sky gradient texture
    //
    // A 512×256 vertical gradient from deep blue zenith (row 0) to pale
    // horizon (row 255). Used by both Scene 1 and Scene 2 sky domes.
    // The horizon color also drives the beginScene clear colour so the sky
    // seam is invisible.
    // -----------------------------------------------------------------------
    irr::video::ITexture* skyTex = nullptr;
    {
        irr::video::IImage* img = driver->createImage(
            irr::video::ECF_A8R8G8B8,
            irr::core::dimension2du(512u, 256u));
        if (img)
        {
            for (irr::u32 py = 0; py < 256u; ++py)
            {
                float t = static_cast<float>(py) / 255.0f;
                irr::u32 r = static_cast<irr::u32>(15.0f  + (190.0f - 15.0f)  * t);
                irr::u32 g = static_cast<irr::u32>(60.0f  + (215.0f - 60.0f)  * t);
                irr::u32 b = static_cast<irr::u32>(150.0f + (245.0f - 150.0f) * t);
                irr::video::SColor col(255u, r, g, b);
                for (irr::u32 px = 0; px < 512u; ++px)
                    img->setPixel(px, py, col);
            }
            skyTex = driver->addTexture("__bench_sky", img);
            img->drop();
        }
    }

    // -----------------------------------------------------------------------
    // 5. Build Scene 1: Anisotropy Ground
    //
    // Scene layout:
    //   - Sky dome with procedural gradient texture.
    //   - 16×16 tiled ground plane (320 m × 320 m) with a checkerboard
    //     texture and high UV repetition — ideal for showing anisotropy
    //     differences at a grazing camera angle.
    //   - 20 colored box proxies lit by a directional sun light.
    //   - Camera at low altitude looking along the ground surface (grazing
    //     angle makes anisotropy filtering differences visually obvious).
    // -----------------------------------------------------------------------

    // --- Sky dome Scene 1 ---
    if (skyTex)
    {
        smgr->addSkyDomeSceneNode(skyTex, 16, 8, 0.95f, 2.0f);
    }

    // --- Directional sun light for Scene 1 ---
    {
        irr::scene::ILightSceneNode* sun1 = smgr->addLightSceneNode(nullptr,
            irr::core::vector3df(300.0f, 600.0f, -200.0f));
        if (sun1)
        {
            irr::video::SLight sd;
            sd.Type          = irr::video::ELT_DIRECTIONAL;
            sd.Direction     = irr::core::vector3df(-1.0f, -2.0f, 0.5f).normalize();
            sd.DiffuseColor  = irr::video::SColorf(1.0f, 0.92f, 0.80f, 1.0f);
            sd.AmbientColor  = irr::video::SColorf(0.25f, 0.28f, 0.35f, 1.0f);
            sd.SpecularColor = irr::video::SColorf(0.4f, 0.4f, 0.3f, 1.0f);
            sun1->setLightData(sd);
        }
    }

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

    // --- Ground plane via IGeometryCreator::createPlaneMesh() ---
    // createPlaneMesh() guarantees correct vertex winding (top face visible
    // from +Y) and proper UV mapping — avoids the hand-rolled winding errors
    // that caused the previous gray-screen regression.
    // 16×16 tiles of 20 m each = 320 m × 320 m total.
    // UV repeats 4× per tile so the checkerboard tiles densely; the grazing
    // camera angle (y=20, z=-180) makes anisotropy differences obvious.
    {
        const irr::scene::IGeometryCreator* geo = smgr->getGeometryCreator();
        irr::scene::IMesh* planeMesh = geo->createPlaneMesh(
            irr::core::dimension2df(20.0f, 20.0f),   // tile size in world units
            irr::core::dimension2du(16u,   16u),      // 16×16 tile grid
            nullptr,                                   // no material baked in
            irr::core::dimension2df(4.0f,  4.0f));    // UV repeats 4× per tile
        if (planeMesh)
        {
            irr::scene::IMeshSceneNode* gn = smgr->addMeshSceneNode(planeMesh);
            planeMesh->drop();
            if (gn)
            {
                if (checkerTex) { gn->setMaterialTexture(0, checkerTex); }
                gn->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
                gn->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER,   true);
                gn->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER,  true);
                gn->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
            }
        }
    }

    // --- Building proxy boxes (geometry creator — no file I/O) ---
    // EMF_LIGHTING = true so DiffuseColor is applied by the directional sun.
    {
        const irr::scene::IGeometryCreator* geo = smgr->getGeometryCreator();
        const int   NUM_BOXES = 20;
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
            float px = (lcg() * 2.0f - 1.0f) * 120.0f;
            float pz = (lcg() * 2.0f - 1.0f) * 120.0f;
            bn->setPosition(irr::core::vector3df(px, bh * 0.5f, pz));
            bn->getMaterial(0).DiffuseColor = irr::video::SColor(
                255,
                static_cast<irr::u32>(100 + lcg() * 100),
                static_cast<irr::u32>(100 + lcg() * 100),
                static_cast<irr::u32>(100 + lcg() * 100));
            bn->setMaterialFlag(irr::video::EMF_LIGHTING,        true);
            // Disable vertex-color override so DiffuseColor is used for shading
            // (createCubeMesh produces white vertices that swamp getMaterial DiffuseColor).
            bn->setMaterialFlag(irr::video::EMF_COLOR_MATERIAL,  false);
        }
    }

    // --- Camera Scene 1: low altitude looking along the ground (grazing angle) ---
    // Grazing angle makes anisotropy differences clearly visible on the
    // ground plane — the primary purpose of the benchmark scene layout.
    {
        irr::scene::ICameraSceneNode* cam1 = smgr->addCameraSceneNode(
            nullptr,
            irr::core::vector3df(  0.0f, 20.0f, -180.0f),  // low above ground
            irr::core::vector3df(  0.0f,  0.0f,   80.0f)   // looking forward
        );
        (void)cam1;
    }

    std::printf("INFO: Scene 1 ready — sky dome + directional light + 16x16 ground grid + 20 building proxies.\n\n");

    // -----------------------------------------------------------------------
    // 6. Build Scene 2: Light / Shadow / Water
    //
    // A separate ISceneManager rendered in the second pass of each anisotropy
    // level. Demonstrates point lighting, stencil shadow volumes, and an
    // animated water surface — representative of a live city district view.
    //
    // Contents:
    //   - Sky dome (same gradient texture as Scene 1).
    //   - Green grass ground plane.
    //   - Animated water surface (WaterSurfaceSceneNode).
    //   - Point sun light with shadow casting enabled.
    //   - 20 building proxy boxes with stencil shadow volumes.
    //   - Camera at same vantage as Scene 1.
    // -----------------------------------------------------------------------
    irr::scene::ISceneManager* smgr2 = smgr->createNewSceneManager(false);

    if (smgr2)
    {
        // --- Sky dome Scene 2 ---
        if (skyTex)
        {
            smgr2->addSkyDomeSceneNode(skyTex, 16, 8, 0.95f, 2.0f);
        }

        // --- Green grassy ground plane ---
        {
            irr::scene::IMesh* gnd2 = smgr2->getGeometryCreator()->createPlaneMesh(
                irr::core::dimension2df(20.0f, 20.0f),
                irr::core::dimension2du(16u, 16u), nullptr,
                irr::core::dimension2df(1.0f, 1.0f));
            irr::scene::IMeshSceneNode* gndNode2 = smgr2->addMeshSceneNode(gnd2);
            gnd2->drop();
            if (gndNode2)
            {
                gndNode2->getMaterial(0).DiffuseColor = irr::video::SColor(255, 60, 130, 60);
                gndNode2->setMaterialFlag(irr::video::EMF_LIGHTING,          true);
                gndNode2->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
                // Disable vertex-color override so the green DiffuseColor is used.
                gndNode2->setMaterialFlag(irr::video::EMF_COLOR_MATERIAL,    false);
            }
        }

        // --- Animated water surface ---
        // 16×16 tiles of 10 m = 160 m × 160 m, centered in the scene at y=0.3
        // (slightly above the ground plane to avoid z-fighting).
        // Positioned at scene centre so it is clearly visible from the camera.
        {
            irr::scene::IMesh* waterMesh = smgr2->getGeometryCreator()->createPlaneMesh(
                irr::core::dimension2df(10.0f, 10.0f),
                irr::core::dimension2du(16u, 16u), nullptr,
                irr::core::dimension2df(2.0f, 2.0f));
            irr::scene::ISceneNode* waterNode = smgr2->addWaterSurfaceSceneNode(
                waterMesh, 1.5f, 300.0f, 20.0f, nullptr, -1,
                irr::core::vector3df(0.0f, 0.3f, 60.0f));
            waterMesh->drop();
            if (waterNode)
            {
                // Fully opaque blue — DiffuseColor only works with EMF_COLOR_MATERIAL off.
                waterNode->getMaterial(0).DiffuseColor  = irr::video::SColor(255,  20,  80, 180);
                waterNode->getMaterial(0).AmbientColor  = irr::video::SColor(255,  10,  40, 100);
                waterNode->getMaterial(0).SpecularColor = irr::video::SColor(255, 200, 220, 255);
                waterNode->getMaterial(0).Shininess     = 64.0f;
                waterNode->setMaterialFlag(irr::video::EMF_LIGHTING,       true);
                // Disable vertex-color override so the blue DiffuseColor shows.
                waterNode->setMaterialFlag(irr::video::EMF_COLOR_MATERIAL, false);
            }
        }

        // --- Point sun light (required for shadow volumes) ---
        {
            irr::scene::ILightSceneNode* sun2 = smgr2->addLightSceneNode(nullptr,
                irr::core::vector3df(150.0f, 400.0f, -100.0f));
            if (sun2)
            {
                irr::video::SLight sd2;
                sd2.Type          = irr::video::ELT_POINT;
                sd2.DiffuseColor  = irr::video::SColorf(1.5f, 1.4f, 1.1f, 1.0f);
                sd2.AmbientColor  = irr::video::SColorf(0.15f, 0.18f, 0.22f, 1.0f);
                sd2.SpecularColor = irr::video::SColorf(1.0f, 1.0f, 0.8f, 1.0f);
                sd2.Radius        = 1200.0f;
                sd2.CastShadows   = true;
                sun2->setLightData(sd2);
            }
            smgr2->setShadowColor(irr::video::SColor(120, 0, 0, 0));
        }

        // --- Building proxy boxes with stencil shadow volumes ---
        // Same LCG seed as Scene 1 so box positions match across scenes.
        {
            irr::u32 rng2 = 0xDEADBEEFu;
            auto lcg2 = [&]() -> float {
                rng2 = rng2 * 1664525u + 1013904223u;
                return static_cast<float>(rng2 & 0xFFFFu) / 65535.0f;
            };
            for (int bi = 0; bi < 20; ++bi)
            {
                float bw = 8.0f  + lcg2() * 10.0f;
                float bh = 15.0f + lcg2() * 40.0f;
                float bd = 8.0f  + lcg2() * 10.0f;
                irr::scene::IMesh* box = smgr2->getGeometryCreator()->createCubeMesh(
                    irr::core::vector3df(bw, bh, bd));
                if (!box) { continue; }
                irr::scene::IMeshSceneNode* bn = smgr2->addMeshSceneNode(box);
                box->drop();
                if (!bn) { continue; }
                float px = (lcg2() * 2.0f - 1.0f) * 120.0f;
                float pz = (lcg2() * 2.0f - 1.0f) * 120.0f;
                bn->setPosition(irr::core::vector3df(px, bh * 0.5f, pz));
                bn->getMaterial(0).DiffuseColor = irr::video::SColor(255,
                    static_cast<irr::u32>(80 + lcg2() * 120),
                    static_cast<irr::u32>(80 + lcg2() * 120),
                    static_cast<irr::u32>(80 + lcg2() * 120));
                bn->setMaterialFlag(irr::video::EMF_LIGHTING,       true);
                // Disable vertex-color override so DiffuseColor is used for shading.
                bn->setMaterialFlag(irr::video::EMF_COLOR_MATERIAL, false);
                bn->addShadowVolumeSceneNode();  // stencil shadow caster
            }
        }

        // --- Camera Scene 2: same vantage as Scene 1 ---
        smgr2->addCameraSceneNode(nullptr,
            irr::core::vector3df(0.0f, 20.0f, -180.0f),
            irr::core::vector3df(0.0f,  0.0f,   80.0f));

        std::printf("INFO: Scene 2 ready — sky dome + point light + shadows + water + 20 building proxies.\n\n");
    }
    else
    {
        std::fprintf(stderr, "WARNING: Failed to create Scene 2 scene manager — Scene 2 skipped.\n\n");
    }

    // -----------------------------------------------------------------------
    // 7. Determine anisotropy test levels (skip levels beyond hardware max)
    // -----------------------------------------------------------------------
    const int kTestLevels[]  = {1, 2, 4, 8, 16};
    const int kNumTestLevels = static_cast<int>(sizeof(kTestLevels) / sizeof(kTestLevels[0]));

    std::vector<LevelResult> results1;
    std::vector<LevelResult> results2;
    results1.reserve(kNumTestLevels);
    results2.reserve(kNumTestLevels);

    // -----------------------------------------------------------------------
    // 8. Benchmark loop — two passes per anisotropy level (Scene 1 then Scene 2)
    // -----------------------------------------------------------------------

    // --- Scene 1 header ---
    std::printf("=== Scene 1: Anisotropy Ground (ground plane + building proxies) ===\n");

    for (int li = 0; li < kNumTestLevels; ++li)
    {
        int level = kTestLevels[li];
        if (level > hwMaxAnisoInt)
        {
            std::printf("  [ANISOx%2d]  (skipped — exceeds hardware max %d×)\n",
                level, hwMaxAnisoInt);
            continue;
        }

        irr::u32 uLevel = static_cast<irr::u32>(level);

        // Apply anisotropy to Scene 1 nodes.
        applyAnisotropyToNode(smgr->getRootSceneNode(), uLevel);

        // Apply globally via driver material state.
        irr::video::SMaterial globalMat;
        for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t)
            globalMat.TextureLayer[t].AnisotropicFilter = static_cast<irr::u8>(level);
        driver->setMaterial(globalMat);

        // Warm-up: 20 frames not timed.
        for (int f = 0; f < 20; ++f)
        {
            if (!device->run()) { break; }
            driver->beginScene(true, true, irr::video::SColor(255, 190, 215, 245));
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
            if (!renderTimedFrame(device, driver, smgr,
                                  minFrameTime, maxFrameTime, totalTime, frameCount))
            {
                break;
            }
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
        results1.push_back(res);

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

    // --- Scene 2 header ---
    std::printf("\n=== Scene 2: Light / Shadow / Water ===\n");

    if (smgr2)
    {
        for (int li = 0; li < kNumTestLevels; ++li)
        {
            int level = kTestLevels[li];
            if (level > hwMaxAnisoInt)
            {
                std::printf("  [ANISOx%2d]  (skipped — exceeds hardware max %d×)\n",
                    level, hwMaxAnisoInt);
                continue;
            }

            irr::u32 uLevel = static_cast<irr::u32>(level);

            // Apply anisotropy to Scene 2 nodes.
            applyAnisotropyToNode(smgr2->getRootSceneNode(), uLevel);

            // Apply globally via driver material state.
            irr::video::SMaterial globalMat2;
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t)
                globalMat2.TextureLayer[t].AnisotropicFilter = static_cast<irr::u8>(level);
            driver->setMaterial(globalMat2);

            // Warm-up: 20 frames not timed.
            for (int f = 0; f < 20; ++f)
            {
                if (!device->run()) { break; }
                driver->beginScene(true, true, irr::video::SColor(255, 190, 215, 245));
                smgr2->drawAll();
                driver->endScene();
            }

            // Measurement pass.
            float minFrameTime =  1e30f;
            float maxFrameTime = -1e30f;
            float totalTime    =  0.0f;
            int   frameCount   =  0;

            for (int f = 0; f < opts.frames; ++f)
            {
                if (!renderTimedFrame(device, driver, smgr2,
                                      minFrameTime, maxFrameTime, totalTime, frameCount))
                {
                    break;
                }
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
            results2.push_back(res);

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
    }
    else
    {
        std::printf("  (Scene 2 unavailable — skipped)\n");
    }

    // -----------------------------------------------------------------------
    // 9. Recommendation engine
    //
    // Compares both scenes at each anisotropy level; uses the worst-case
    // (minimum) avgFPS across the two scenes to drive the recommendation.
    // -----------------------------------------------------------------------
    std::printf("\n=== Recommended Settings for this GPU ===\n");

    // Build a combined view: for each level recorded in results1, find the
    // matching level in results2 (if present) and take the minimum avgFPS.
    // Use results1 as the authoritative level list since Scene 1 always runs.
    const LevelResult* recommended = nullptr;

    for (int i = static_cast<int>(results1.size()) - 1; i >= 0; --i)
    {
        const LevelResult& r1 = results1[static_cast<size_t>(i)];

        // Find matching Scene 2 result (same anisotropy level).
        float worstAvgFPS = r1.avgFPS;
        float worstVram   = r1.vramUsedMB;
        for (const LevelResult& r2 : results2)
        {
            if (r2.anisotropy == r1.anisotropy)
            {
                worstAvgFPS = std::min(r1.avgFPS, r2.avgFPS);
                // Use higher VRAM reading as the conservative estimate.
                if (r1.vramUsedMB >= 0.0f && r2.vramUsedMB >= 0.0f)
                    worstVram = std::max(r1.vramUsedMB, r2.vramUsedMB);
                else if (r2.vramUsedMB >= 0.0f)
                    worstVram = r2.vramUsedMB;
                break;
            }
        }

        bool fpsOk  = (worstAvgFPS >= static_cast<float>(opts.targetFPS));
        bool vramOk = (worstVram < 0.0f) || (worstVram <= kSceneVRAMBudgetMB);
        if (fpsOk && vramOk)
        {
            recommended = &r1;
            break;
        }
    }

    bool lowFPSWarning = false;
    if (!recommended && !results1.empty())
    {
        // No level met FPS threshold — recommend lowest (1×) with warning.
        recommended   = &results1.front();
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

        // Determine VRAM for reporting (worst of both scenes at recommended level).
        float reportVram = recommended->vramUsedMB;
        for (const LevelResult& r2 : results2)
        {
            if (r2.anisotropy == recommended->anisotropy)
            {
                if (recommended->vramUsedMB >= 0.0f && r2.vramUsedMB >= 0.0f)
                    reportVram = std::max(recommended->vramUsedMB, r2.vramUsedMB);
                else if (r2.vramUsedMB >= 0.0f)
                    reportVram = r2.vramUsedMB;
                break;
            }
        }

        std::printf("Terrain anisotropy:        %2d×  (spec minimum: %d×)\n",
            terrainAniso, kSpecMinTerrainAniso);
        std::printf("Road/prop/normal/specular: %2d×  (spec minimum: %d×)\n",
            roadAniso, kSpecMinRoadPropAniso);

        if (reportVram >= 0.0f)
        {
            std::printf("Scene VRAM at recommended: %.1f MB / %.1f MB budget\n",
                reportVram, kSceneVRAMBudgetMB);
        }
        else
        {
            std::printf("Scene VRAM at recommended: unavailable / %.1f MB budget\n",
                kSceneVRAMBudgetMB);
        }

        // Report worst-case avgFPS across both scenes at recommended level.
        float reportAvgFPS = recommended->avgFPS;
        float reportMinFPS = recommended->minFPS;
        for (const LevelResult& r2 : results2)
        {
            if (r2.anisotropy == recommended->anisotropy)
            {
                reportAvgFPS = std::min(reportAvgFPS, r2.avgFPS);
                reportMinFPS = std::min(reportMinFPS, r2.minFPS);
                break;
            }
        }

        std::printf("Avg FPS at recommended:    %.1f  (target: >=%d, worst of both scenes)\n",
            reportAvgFPS, opts.targetFPS);
        std::printf("Min FPS at recommended:    %.1f\n",
            reportMinFPS);

        bool budgetPass = (reportVram < 0.0f)
                       || (reportVram <= kSceneVRAMBudgetMB);
        std::printf("Budget: %s  (scene VRAM %s)\n",
            budgetPass ? "PASS" : "FAIL",
            budgetPass ? "within 170 MB limit" : "EXCEEDS 170 MB limit");

        if (lowFPSWarning)
        {
            std::printf("\nWARNING: No anisotropy level achieved target FPS (%d). "
                        "Recommending lowest level (1x) — GPU may be underpowered.\n",
                        opts.targetFPS);
        }

        if (terrainAniso < kSpecMinTerrainAniso)
        {
            std::printf("\nWARNING: Recommended terrain anisotropy (%dx) is below spec minimum (%dx). "
                        "GPU may not meet AI Town visual quality requirements at target FPS.\n",
                        terrainAniso, kSpecMinTerrainAniso);
        }

        if (roadAniso < kSpecMinRoadPropAniso)
        {
            std::printf("\nWARNING: Recommended road/prop anisotropy (%dx) is below spec minimum (%dx). "
                        "GPU may not meet AI Town visual quality requirements at target FPS.\n",
                        roadAniso, kSpecMinRoadPropAniso);
        }

        // -------------------------------------------------------------------
        // 10. JSON output
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

                // Scene 1 results.
                js << "  \"scene1_results\": [\n";
                for (size_t ri = 0; ri < results1.size(); ++ri)
                {
                    const LevelResult& r = results1[ri];
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
                    if (ri + 1 < results1.size()) { js << ","; }
                    js << "\n";
                }
                js << "  ],\n";

                // Scene 2 results.
                js << "  \"scene2_results\": [\n";
                for (size_t ri = 0; ri < results2.size(); ++ri)
                {
                    const LevelResult& r = results2[ri];
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
                    if (ri + 1 < results2.size()) { js << ","; }
                    js << "\n";
                }
                js << "  ],\n";

                js << "  \"recommendation\": {\n";
                js << "    \"terrain_anisotropy\": " << terrainAniso << ",\n";
                js << "    \"road_prop_anisotropy\": " << roadAniso << ",\n";
                if (reportVram >= 0.0f)
                    js << "    \"scene_vram_mb\": " << reportVram << ",\n";
                else
                    js << "    \"scene_vram_mb\": null,\n";
                js << "    \"avg_fps\": " << reportAvgFPS << ",\n";
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
    // 11. Cleanup
    // -----------------------------------------------------------------------
    if (smgr2) { smgr2->drop(); }
    device->drop();

    return 0;
}
