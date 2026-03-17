// model_validator_main.cpp — AI Town model validator tool.
//
// Interactive visual verification tool for all V1 building and vehicle B3D assets.
// Displays each asset category in sequence with an orbiting camera so the operator
// can confirm that meshes, textures, and scale look correct before each release.
//
// Build: linked as aitown_model_validator executable (see CMakeLists.txt).
// Run:   ./build/aitown_model_validator [--width W] [--height H]
//
// Include order: GLEW before Irrlicht (project convention — GLEW symbol duplication mitigation).

#include <GL/glew.h>
#include <irrlicht.h>

#include "rendering/BuildingAssetLoader.h"
#include "rendering/LODNode.h"
#include "rendering/TextureCache.h"
#include "rendering/RoadShaderCallback.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// CLI options
// ---------------------------------------------------------------------------
struct ValidatorOptions {
    int width  = 1280;
    int height = 720;
};

static void printUsage(const char* prog)
{
    std::printf(
        "Usage: %s [options]\n"
        "  --width W    window width (default: 1280)\n"
        "  --height H   window height (default: 720)\n"
        "  --help       print this usage message\n",
        prog
    );
}

static bool parseArgs(int argc, char** argv, ValidatorOptions& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--help") == 0)
        {
            return false;
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
// Showcase constants
// ---------------------------------------------------------------------------
static constexpr float kShowcaseSpacing = 12.0f;  // metres between model centres

// ---------------------------------------------------------------------------
// ShowcaseReceiver — minimal event receiver for keyboard input.
// Flags are polled by the render loop; reset between categories.
// ---------------------------------------------------------------------------
class ShowcaseReceiver : public irr::IEventReceiver {
public:
    bool spacePressed = false;
    bool escPressed   = false;

    bool OnEvent(const irr::SEvent& ev) override
    {
        if (ev.EventType == irr::EET_KEY_INPUT_EVENT && ev.KeyInput.PressedDown)
        {
            if (ev.KeyInput.Key == irr::KEY_SPACE)   { spacePressed = true; return true; }
            if (ev.KeyInput.Key == irr::KEY_ESCAPE)  { escPressed   = true; return true; }
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    ValidatorOptions opts;
    bool argsOk = parseArgs(argc, argv, opts);
    if (!argsOk)
    {
        printUsage(argv[0]);
        return 0;
    }

    std::printf("=== AI Town Model Validator ===\n");
    std::printf("Controls: SPACE = next category   ESC = exit\n\n");

    // -----------------------------------------------------------------------
    // 1. Create Irrlicht device (EDT_OPENGL)
    // -----------------------------------------------------------------------
    irr::SIrrlichtCreationParameters params;
    params.DriverType    = irr::video::EDT_OPENGL;
    params.WindowSize    = irr::core::dimension2d<irr::u32>(
                               static_cast<irr::u32>(opts.width),
                               static_cast<irr::u32>(opts.height));
    params.Bits          = 32;
    params.ZBufferBits   = 24;
    params.Fullscreen    = false;
    params.Stencilbuffer = false;
    params.AntiAlias     = 4;
    params.Vsync         = true;
    params.EventReceiver = nullptr;

    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device)
    {
        std::fprintf(stderr, "ERROR: Failed to create Irrlicht device (EDT_OPENGL). "
                             "Ensure a display is available.\n");
        return 1;
    }
    device->setWindowCaption(L"AI Town Model Validator");

    irr::video::IVideoDriver*  driver = device->getVideoDriver();
    irr::scene::ISceneManager* smgr   = device->getSceneManager();

    // -----------------------------------------------------------------------
    // 2. Init GLEW (required before raw GL/GLEW calls; aitown_render internals
    //    may depend on GLEW-resolved function pointers via BuildingAssetLoader).
    // -----------------------------------------------------------------------
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK)
    {
        std::fprintf(stderr, "WARNING: glewInit() failed: %s\n",
                     reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
        // Non-fatal: model loading does not require GLEW extensions directly.
    }

    // -----------------------------------------------------------------------
    // 3. Create shared sky gradient texture (512×256, deep blue to pale horizon).
    //    Same gradient as the benchmark tool for visual consistency.
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
            skyTex = driver->addTexture("__mv_sky", img);
            img->drop();
        }
    }

    // -----------------------------------------------------------------------
    // 4. Asset category definitions (all V1 buildings and vehicles)
    // -----------------------------------------------------------------------
    struct Category {
        const char*              label;
        const char*              pathPrefix;   // relative to AITOWN_ASSETS_DIR
        std::vector<std::string> names;
        bool                     scaleBuilding;  // true = apply 10×10×10 scale
        bool                     hasStreet;      // true = add asphalt road strip under models
    };

    const std::vector<Category> categories = {
        // Zone categories: each tier is its own category (4 variants each) so the
        // orbiting camera keeps all models comfortably in view at 65 m radius.
        { "Residential Low",              "3d/buildings",
          {"res_low_01",  "res_low_02",  "res_low_03",  "res_low_04"},  true, false },
        { "Residential Med",              "3d/buildings",
          {"res_med_01",  "res_med_02",  "res_med_03",  "res_med_04"},  true, false },
        { "Residential High",             "3d/buildings",
          {"res_high_01", "res_high_02", "res_high_03", "res_high_04"}, true, false },
        { "Commercial Low",               "3d/buildings",
          {"com_low_01",  "com_low_02",  "com_low_03",  "com_low_04"},  true, false },
        { "Commercial Med",               "3d/buildings",
          {"com_med_01",  "com_med_02",  "com_med_03",  "com_med_04"},  true, false },
        { "Commercial High (Skyscrapers)","3d/buildings",
          {"com_high_01", "com_high_02", "com_high_03", "com_high_04"}, true, false },
        { "Industrial Low",               "3d/buildings",
          {"ind_low_01",  "ind_low_02",  "ind_low_03",  "ind_low_04"},  true, false },
        { "Industrial Med",               "3d/buildings",
          {"ind_med_01",  "ind_med_02",  "ind_med_03",  "ind_med_04"},  true, false },
        { "Industrial High",              "3d/buildings",
          {"ind_high_01", "ind_high_02", "ind_high_03", "ind_high_04"}, true, false },
        {
            "Service",
            "3d/buildings",
            {"svc_fire_station", "svc_police_station", "svc_power_plant", "svc_water_tower"},
            true, false
        },
        {
            "Vehicles",
            "3d/vehicles",
            {"car_sedan", "car_hatchback", "car_suv", "bus_standard", "truck_cargo"},
            false, true
        }
    };

    const int kTotalCategories = static_cast<int>(categories.size());

    // -----------------------------------------------------------------------
    // 5. Attach keyboard event receiver
    // -----------------------------------------------------------------------
    ShowcaseReceiver receiver;
    device->setEventReceiver(&receiver);

    // Orbit camera constants: slow Y-axis orbit so the operator can inspect
    // all sides of the models without manual camera control.
    // Radius 65 m keeps the full 6-model row (±30 m span) comfortably in view.
    const irr::core::vector3df kOrbitCentre(0.0f, 5.0f, 0.0f);
    const float kOrbitRadius      = 65.0f;
    const float kOrbitHeight      = 15.0f;
    const float kOrbitDegPerFrame = 0.3f;

    bool exitEarly = false;

    // -----------------------------------------------------------------------
    // 6. Category render loop
    // -----------------------------------------------------------------------
    for (int ci = 0; ci < kTotalCategories && !exitEarly; ++ci)
    {
        const Category& cat = categories[static_cast<size_t>(ci)];

        // Fresh scene manager for each category so assets are cleanly unloaded
        // between categories.
        irr::scene::ISceneManager* smgr3 = smgr->createNewSceneManager(false);
        if (!smgr3)
        {
            std::fprintf(stderr, "WARNING: failed to create scene manager for category '%s'. Skipping.\n",
                cat.label);
            continue;
        }

        // --- Ground plane (grey, 200 m × 200 m) ---
        {
            irr::scene::IMesh* gndMesh = smgr3->getGeometryCreator()->createPlaneMesh(
                irr::core::dimension2df(200.0f, 200.0f),
                irr::core::dimension2du(1u, 1u),
                nullptr,
                irr::core::dimension2df(1.0f, 1.0f));
            if (gndMesh)
            {
                irr::scene::IMeshSceneNode* gndNode = smgr3->addMeshSceneNode(gndMesh);
                gndMesh->drop();
                if (gndNode)
                {
                    gndNode->getMaterial(0).DiffuseColor = irr::video::SColor(255, 100, 100, 100);
                    gndNode->setMaterialFlag(irr::video::EMF_LIGHTING,          true);
                    gndNode->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
                    gndNode->setMaterialFlag(irr::video::EMF_COLOR_MATERIAL,    false);
                }
            }
        }

        // --- Road tiles (vehicles category only: real game road shader + geometry) ---
        if (cat.hasStreet)
        {
            // Load road_asphalt_tileable.dds via raw-GL sRGB path (same as IrrlichtRenderer).
            const bool srgbOk = (glewIsExtensionSupported("GL_EXT_texture_sRGB") == GL_TRUE);
            const GLenum fmt  = srgbOk
                ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
                : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            const std::string texPath = std::string(AITOWN_ASSETS_DIR)
                                        + "/textures/roads/road_asphalt_tileable.dds";
            TextureCache roadCache(driver->getDriverType(), driver, device->getFileSystem());
            GLuint roadTex = static_cast<GLuint>(roadCache.loadSRGB(texPath, fmt));

            // Compile road shader (road.vert / road.frag) + bind callback.
            // Uses the same addHighLevelShaderMaterialFromFiles path as IrrlichtRenderer.
            irr::s32 roadMatType = -1;
            {
                irr::video::IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();
                if (gpu)
                {
                    const std::string vsPath = std::string(AITOWN_ASSETS_DIR) + "/shaders/road.vert";
                    const std::string fsPath = std::string(AITOWN_ASSETS_DIR) + "/shaders/road.frag";
                    RoadShaderCallback* cb = new RoadShaderCallback(srgbOk, roadTex);
                    roadMatType = gpu->addHighLevelShaderMaterialFromFiles(
                        vsPath.c_str(), "main", irr::video::EVST_VS_1_1,
                        fsPath.c_str(), "main", irr::video::EPST_PS_1_1,
                        cb);
                    cb->drop();
                }
            }

            // Build road tile LOD0 mesh: 10×10 m flat quad + bevelled kerbs.
            // Geometry mirrors IrrlichtRenderer::ensureRoadMeshes() LOD0 exactly.
            const irr::video::E_MATERIAL_TYPE roadMat = (roadMatType >= 0)
                ? static_cast<irr::video::E_MATERIAL_TYPE>(roadMatType)
                : irr::video::EMT_SOLID;

            irr::scene::SMesh*       roadMesh = new irr::scene::SMesh();
            irr::scene::SMeshBuffer* buf      = new irr::scene::SMeshBuffer();
            buf->Material.MaterialType    = roadMat;
            buf->Material.Lighting        = false;
            buf->Material.BackfaceCulling = false;

            static constexpr float H  = 5.0f;
            static constexpr float KB = 0.05f;
            static constexpr float KW = 0.15f;
            static constexpr float KH = 0.10f;

            auto addV = [&](float x, float y, float z, float u, float v) {
                buf->Vertices.push_back(irr::video::S3DVertex(
                    irr::core::vector3df(x, y, z),
                    irr::core::vector3df(0.f, 1.f, 0.f),
                    irr::video::SColor(255, 255, 255, 255),
                    irr::core::vector2df(u, v)));
            };

            // Central flat quad (indices 0–3, 2 tris)
            addV(-H, 0.f, -H,  0.f, 0.f);
            addV( H, 0.f, -H,  1.f, 0.f);
            addV( H, 0.f,  H,  1.f, 1.f);
            addV(-H, 0.f,  H,  0.f, 1.f);
            buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
            buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

            // Kerb helper: 8 verts, 6 tris per strip
            auto addKerb = [&](
                float lx0, float ly0, float lz0, float rx0, float ry0, float rz0,
                float lx1, float ly1, float lz1, float rx1, float ry1, float rz1,
                float lx2, float ly2, float lz2, float rx2, float ry2, float rz2,
                float lx3, float ly3, float lz3, float rx3, float ry3, float rz3)
            {
                irr::u16 base = static_cast<irr::u16>(buf->Vertices.size());
                addV(lx0,ly0,lz0,0.f,0.f); addV(rx0,ry0,rz0,0.f,0.f);
                addV(lx1,ly1,lz1,0.f,0.f); addV(rx1,ry1,rz1,0.f,0.f);
                addV(lx2,ly2,lz2,0.f,0.f); addV(rx2,ry2,rz2,0.f,0.f);
                addV(lx3,ly3,lz3,0.f,0.f); addV(rx3,ry3,rz3,0.f,0.f);
                buf->Indices.push_back(base+0); buf->Indices.push_back(base+2); buf->Indices.push_back(base+1);
                buf->Indices.push_back(base+2); buf->Indices.push_back(base+3); buf->Indices.push_back(base+1);
                buf->Indices.push_back(base+2); buf->Indices.push_back(base+4); buf->Indices.push_back(base+3);
                buf->Indices.push_back(base+4); buf->Indices.push_back(base+5); buf->Indices.push_back(base+3);
                buf->Indices.push_back(base+4); buf->Indices.push_back(base+6); buf->Indices.push_back(base+5);
                buf->Indices.push_back(base+6); buf->Indices.push_back(base+7); buf->Indices.push_back(base+5);
            };

            addKerb(-H,0.f,-H,       H,0.f,-H,
                    -H,KH,-H-KB,      H,KH,-H-KB,
                    -H,KH,-H-KW,      H,KH,-H-KW,
                    -H,0.f,-H-KW,     H,0.f,-H-KW);
            addKerb(-H,0.f, H,        H,0.f, H,
                    -H,KH, H+KB,      H,KH, H+KB,
                    -H,KH, H+KW,      H,KH, H+KW,
                    -H,0.f, H+KW,     H,0.f, H+KW);
            addKerb(-H,   0.f,-H,    -H,   0.f, H,
                    -H-KB,KH, -H,    -H-KB,KH,  H,
                    -H-KW,KH, -H,    -H-KW,KH,  H,
                    -H-KW,0.f,-H,    -H-KW,0.f, H);
            addKerb( H,   0.f,-H,     H,   0.f, H,
                     H+KB,KH, -H,     H+KB,KH,  H,
                     H+KW,KH, -H,     H+KW,KH,  H,
                     H+KW,0.f,-H,     H+KW,0.f, H);

            buf->recalculateBoundingBox();
            roadMesh->addMeshBuffer(buf);
            buf->drop();
            roadMesh->recalculateBoundingBox();

            // Place 7 tiles along X to cover the vehicle row (-35 m to +35 m).
            for (int ti = -3; ti <= 3; ++ti)
            {
                irr::scene::IMeshSceneNode* tileNode = smgr3->addMeshSceneNode(roadMesh);
                if (tileNode)
                {
                    tileNode->setPosition(irr::core::vector3df(
                        static_cast<float>(ti) * 10.0f, 0.01f, 0.0f));
                }
            }
            roadMesh->drop();
        }

        // --- Sky dome ---
        if (skyTex)
        {
            smgr3->addSkyDomeSceneNode(skyTex, 16, 8, 0.95f, 2.0f);
        }

        // --- Directional sun light (warm sun — same parameters as benchmark) ---
        {
            irr::scene::ILightSceneNode* sun = smgr3->addLightSceneNode(nullptr,
                irr::core::vector3df(300.0f, 600.0f, -200.0f));
            if (sun)
            {
                irr::video::SLight sd;
                sd.Type          = irr::video::ELT_DIRECTIONAL;
                sd.Direction     = irr::core::vector3df(-1.0f, -2.0f, 0.5f).normalize();
                sd.DiffuseColor  = irr::video::SColorf(1.0f, 0.92f, 0.80f, 1.0f);
                sd.AmbientColor  = irr::video::SColorf(0.25f, 0.28f, 0.35f, 1.0f);
                sd.SpecularColor = irr::video::SColorf(0.4f, 0.4f, 0.3f, 1.0f);
                sun->setLightData(sd);
            }
        }

        // --- Load all models for this category via BuildingAssetLoader ---
        std::vector<LODNode*> lodNodes;
        std::vector<std::string> loadedNames;

        const int N = static_cast<int>(cat.names.size());
        BuildingAssetLoader loader(smgr3, driver);

        for (int mi = 0; mi < N; ++mi)
        {
            const std::string& name = cat.names[static_cast<size_t>(mi)];
            std::string basePath = std::string(AITOWN_ASSETS_DIR) + "/"
                                 + cat.pathPrefix + "/" + name;

            LODNode* lodNode = loader.load(basePath);
            if (!lodNode)
            {
                std::fprintf(stderr, "  WARNING: failed to load '%s'. Skipping.\n", name.c_str());
                continue;
            }

            irr::scene::ISceneNode* sn = lodNode->getNode();
            if (sn)
            {
                // Centred horizontal row: X = (i − (N−1)/2) × spacing
                float xPos = (static_cast<float>(mi) - (static_cast<float>(N - 1) * 0.5f))
                             * kShowcaseSpacing;
                sn->setPosition(irr::core::vector3df(xPos, 0.0f, 0.0f));

                if (cat.scaleBuilding)
                {
                    sn->setScale(irr::core::vector3df(10.0f, 10.0f, 10.0f));
                }

                sn->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
                sn->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
            }

            lodNodes.push_back(lodNode);
            loadedNames.push_back(name);
        }

        // --- Orbiting camera ---
        irr::scene::ICameraSceneNode* cam = smgr3->addCameraSceneNode(
            nullptr,
            irr::core::vector3df(0.0f, 8.0f, -30.0f),
            irr::core::vector3df(0.0f, 4.0f,   0.0f));

        // --- Print category header ---
        const int modelsLoaded = static_cast<int>(loadedNames.size());
        std::printf("\n=== [%d/%d]: %s — %d model%s loaded, press SPACE for next (ESC to exit) ===\n",
            ci + 1, kTotalCategories, cat.label,
            modelsLoaded, modelsLoaded == 1 ? "" : "s");

        std::printf("  Loaded:");
        for (const std::string& n : loadedNames)
            std::printf(" %s", n.c_str());
        std::printf("\n");
        std::fflush(stdout);

        // Reset space flag at the start of each category loop.
        receiver.spacePressed = false;

        // --- Render loop for this category ---
        float orbitAngle = 0.0f;
        int   frameCount = 0;
        auto  t0         = std::chrono::steady_clock::now();

        while (device->run())
        {
            if (receiver.escPressed)
            {
                exitEarly = true;
                break;
            }
            if (receiver.spacePressed)
            {
                break;  // advance to next category
            }

            // Update orbiting camera.
            orbitAngle += kOrbitDegPerFrame;
            float rad = orbitAngle * irr::core::DEGTORAD;
            if (cam)
            {
                cam->setPosition(irr::core::vector3df(
                    kOrbitCentre.X + kOrbitRadius * sinf(rad),
                    kOrbitCentre.Y + kOrbitHeight,
                    kOrbitCentre.Z + kOrbitRadius * cosf(rad)));
                cam->setTarget(kOrbitCentre);
            }

            driver->beginScene(true, true, irr::video::SColor(255, 190, 215, 245));
            smgr3->drawAll();
            driver->endScene();
            ++frameCount;
        }

        // If device->run() returned false the window was closed.
        if (!device->run() && !receiver.escPressed && !receiver.spacePressed)
        {
            exitEarly = true;
        }

        // Print frame stats for this category.
        auto t1 = std::chrono::steady_clock::now();
        float elapsedSec = std::chrono::duration<float>(t1 - t0).count();
        float avgFPS = (elapsedSec > 0.0f && frameCount > 0)
                       ? (static_cast<float>(frameCount) / elapsedSec)
                       : 0.0f;
        std::printf("  Category displayed for %d frames (avg %.1f fps)\n",
            frameCount, avgFPS);
        std::fflush(stdout);

        // Cleanup: drop scene manager first (removes all scene nodes owned by it),
        // then delete the LODNode heap wrappers (plain objects, not Irrlicht ref-counted).
        smgr3->drop();
        for (LODNode* ln : lodNodes) { delete ln; }
        lodNodes.clear();
    }

    // Detach event receiver before cleanup.
    device->setEventReceiver(nullptr);

    if (!exitEarly)
    {
        std::printf("\n=== Model Validator complete — all %d categories displayed ===\n",
            kTotalCategories);
    }
    else
    {
        std::printf("\n=== Model Validator exited early ===\n");
    }
    std::fflush(stdout);

    // -----------------------------------------------------------------------
    // 7. Cleanup
    // -----------------------------------------------------------------------
    device->drop();
    return 0;
}
