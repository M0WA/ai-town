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
#ifndef GL_BGRA_EXT
#  define GL_BGRA_EXT 0x80E1
#endif

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
    std::vector<std::string> filterModels;  // --model <n1> [n2 ...]: show only these
    std::string screenshotPath;             // --screenshot <file>: save PNG and exit
    int screenshotFrame = 3;                // --screenshot-frame N: frame to capture (3 = stable first pose)
    bool softwareDriver = false;            // --driver burnings: software renderer (headless-safe)
};

static void printUsage(const char* prog)
{
    std::printf(
        "Usage: %s [options]\n"
        "  --width W              window width (default: 1280)\n"
        "  --height H             window height (default: 720)\n"
        "  --model <n1> [n2...]   show only the named model(s) then exit\n"
        "  --screenshot <file>    render --screenshot-frame frames, save PNG, exit\n"
        "  --screenshot-frame N   frame number to capture (default: 3)\n"
        "  --driver burnings      use software renderer (headless/xvfb safe)\n"
        "  --help                 print this usage message\n",
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
        else if (std::strcmp(argv[i], "--model") == 0)
        {
            // Consume all following non-flag args as model names
            while (i + 1 < argc && argv[i + 1][0] != '-')
                opts.filterModels.push_back(argv[++i]);
            if (opts.filterModels.empty())
            {
                std::printf("--model requires at least one model name\n");
                return false;
            }
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            opts.screenshotPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc)
        {
            opts.screenshotFrame = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--driver") == 0 && i + 1 < argc)
        {
            ++i;
            if (std::strcmp(argv[i], "burnings") == 0 ||
                std::strcmp(argv[i], "software") == 0)
                opts.softwareDriver = true;
            else
            {
                std::printf("Unknown driver: %s (supported: burnings, software)\n", argv[i]);
                return false;
            }
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
static constexpr float kShowcaseSpacing       = 22.0f;  // metres between model centres (normal mode)
static constexpr float kSingleModelSpacing    = 14.0f;  // tighter spacing when showing LODs side by side

// ---------------------------------------------------------------------------
// ShowcaseReceiver — keyboard + mouse input for navigation and annotation.
//
// Left hold/drag : freehand draw (continuous marks along mouse path)
// Right drag     : orbit camera
// C              : cycle annotation colour
// V              : cycle mark shape (DOT → CIRCLE → CROSS)
// S              : save annotated screenshot
// Z              : undo last stroke
// X              : clear all marks
// ---------------------------------------------------------------------------
class ShowcaseReceiver : public irr::IEventReceiver {
public:
    // Navigation
    bool spacePressed = false;
    bool escPressed   = false;

    // Camera drag (right mouse)
    int  orbitDeltaX = 0;   // accumulated drag pixels; reset by render loop
    bool rightHeld   = false;

    // Annotation
    enum MarkType { DOT, CIRCLE, CROSS };
    struct Mark { int x, y; irr::video::SColor color; MarkType type; };
    std::vector<Mark>  marks;
    std::vector<int>   strokeStarts;  // marks[] index where each stroke begins
    bool leftHeld  = false;
    int  lastMarkX = -9999, lastMarkY = -9999;  // last placed mark position
    MarkType currentType { DOT };

    // Colour palette — C cycles through these
    static irr::video::SColor colorAt(int idx)
    {
        static const irr::video::SColor kPalette[] = {
            {255, 220,  30,  30},   // Red
            {255,  30, 180,  30},   // Green
            {255,  30, 100, 220},   // Blue
            {255, 220, 200,   0},   // Yellow
            {255,   0, 200, 220},   // Cyan
            {255, 220,   0, 220},   // Magenta
            {255, 255, 255, 255},   // White
            {255,  20,  20,  20},   // Black
        };
        static const int kN = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
        return kPalette[((idx % kN) + kN) % kN];
    }
    static const char* colorNameAt(int idx)
    {
        static const char* kNames[] = {
            "Red","Green","Blue","Yellow","Cyan","Magenta","White","Black"
        };
        static const int kN = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
        return kNames[((idx % kN) + kN) % kN];
    }
    static int kNumColors() { return 8; }

    int colorIdx = 0;
    irr::video::SColor currentColor() const { return colorAt(colorIdx); }

    int   mouseX = 0, mouseY = 0;
    float wheelDelta = 0.0f;          // accumulated scroll; consumed by render loop
    bool  screenshotRequested = false;
    bool  clearRequested      = false;
    bool  sKeyDown            = false;  // physical key state — debounce key-repeat

    void placeMark(int x, int y)
    {
        marks.push_back({x, y, currentColor(), currentType});
        lastMarkX = x;
        lastMarkY = y;
    }

    void undoLastStroke()
    {
        if (strokeStarts.empty()) return;
        int start = strokeStarts.back();
        strokeStarts.pop_back();
        marks.resize(static_cast<size_t>(start));
    }

    bool OnEvent(const irr::SEvent& ev) override
    {
        if (ev.EventType == irr::EET_KEY_INPUT_EVENT && !ev.KeyInput.PressedDown)
        {
            if (ev.KeyInput.Key == irr::KEY_KEY_S) sKeyDown = false;
        }
        if (ev.EventType == irr::EET_KEY_INPUT_EVENT && ev.KeyInput.PressedDown)
        {
            switch (ev.KeyInput.Key)
            {
            case irr::KEY_SPACE:  spacePressed = true; return true;
            case irr::KEY_ESCAPE: escPressed   = true; return true;
            case irr::KEY_KEY_C:
                colorIdx = (colorIdx + 1) % kNumColors();
                return true;
            case irr::KEY_KEY_V:
                currentType = static_cast<MarkType>(
                    (static_cast<int>(currentType) + 1) % 3);
                return true;
            case irr::KEY_KEY_S:
                if (!sKeyDown) screenshotRequested = true;  // fire once per physical press
                sKeyDown = true;
                return true;
            case irr::KEY_KEY_Z: undoLastStroke();            return true;
            case irr::KEY_KEY_X: marks.clear(); strokeStarts.clear(); return true;
            default: break;
            }
        }
        if (ev.EventType == irr::EET_MOUSE_INPUT_EVENT)
        {
            const int curX = ev.MouseInput.X;
            const int curY = ev.MouseInput.Y;
            switch (ev.MouseInput.Event)
            {
            case irr::EMIE_LMOUSE_PRESSED_DOWN:
                leftHeld = true;
                strokeStarts.push_back(static_cast<int>(marks.size()));
                placeMark(curX, curY);
                break;
            case irr::EMIE_LMOUSE_LEFT_UP:
                leftHeld = false;
                lastMarkX = -9999; lastMarkY = -9999;
                break;
            case irr::EMIE_RMOUSE_PRESSED_DOWN:
                rightHeld = true;
                break;
            case irr::EMIE_RMOUSE_LEFT_UP:
                rightHeld = false;
                break;
            case irr::EMIE_MOUSE_MOVED:
                if (leftHeld)
                {
                    int dx = curX - lastMarkX, dy = curY - lastMarkY;
                    if (dx * dx + dy * dy >= 25)   // 5 px threshold
                        placeMark(curX, curY);
                }
                if (rightHeld)
                    orbitDeltaX += curX - mouseX;
                break;
            case irr::EMIE_MOUSE_WHEEL:
                wheelDelta += ev.MouseInput.Wheel;
                return true;
            default: break;
            }
            mouseX = curX;
            mouseY = curY;
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
    params.DriverType    = opts.softwareDriver
                           ? irr::video::EDT_BURNINGSVIDEO
                           : irr::video::EDT_OPENGL;
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
    // 2. Init GLEW (OpenGL mode only; skip for software renderer).
    // -----------------------------------------------------------------------
    if (!opts.softwareDriver)
    {
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK)
        {
            std::fprintf(stderr, "WARNING: glewInit() failed: %s\n",
                         reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
        }
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

    // --model filter: build a synthetic single category from the requested names.
    // Auto-detect path prefix: names starting with "car_", "bus_", or "truck_"
    // are vehicles; everything else is buildings.
    std::vector<Category> filteredCategories;
    if (!opts.filterModels.empty())
    {
        bool isVehicle = false;
        for (const std::string& mn : opts.filterModels)
        {
            if (mn.rfind("car_", 0) == 0 || mn.rfind("bus_", 0) == 0 ||
                mn.rfind("truck_", 0) == 0)
            {
                isVehicle = true;
                break;
            }
        }
        Category custom;
        custom.label         = "Custom Selection";
        custom.pathPrefix    = isVehicle ? "3d/vehicles" : "3d/buildings";
        custom.scaleBuilding = !isVehicle;
        custom.hasStreet     = isVehicle;
        custom.names         = opts.filterModels;
        filteredCategories.push_back(custom);
    }

    const std::vector<Category>& activeCategories =
        opts.filterModels.empty() ? categories : filteredCategories;
    const int kTotalCategories = static_cast<int>(activeCategories.size());

    // -----------------------------------------------------------------------
    // 5. Attach keyboard event receiver
    // -----------------------------------------------------------------------
    ShowcaseReceiver receiver;
    device->setEventReceiver(&receiver);

    // Orbit camera constants: slow Y-axis orbit so the operator can inspect
    // all sides of the models without manual camera control.
    // Radius 65 m keeps the full 6-model row (±30 m span) comfortably in view.
    const irr::core::vector3df kOrbitCentre(0.0f, 5.0f, 0.0f);
    const float kOrbitRadiusDefault = 65.0f;
    const float kOrbitRadiusMin     =  5.0f;
    const float kOrbitRadiusMax     = 200.0f;
    float orbitRadius               = kOrbitRadiusDefault;
    const float kOrbitHeight      = 15.0f;
    const float kOrbitDegPerFrame = 0.3f;

    bool exitEarly = false;

    // -----------------------------------------------------------------------
    // 6. Category render loop
    // -----------------------------------------------------------------------
    for (int ci = 0; ci < kTotalCategories && !exitEarly; ++ci)
    {
        const Category& cat = activeCategories[static_cast<size_t>(ci)];

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
        // In --model (single-model) mode: load LOD0, LOD1, LOD2 separately side by side.
        // In normal category mode: use BuildingAssetLoader (distance-based LOD switching).
        std::vector<LODNode*> lodNodes;
        std::vector<std::string> loadedNames;
        std::vector<float> loadedXPositions;

        const bool singleModelMode = !opts.filterModels.empty();

        if (singleModelMode)
        {
            // Collect all existing LOD files across all requested model names.
            struct LodSlot { std::string label; std::string meshPath; };
            std::vector<LodSlot> slots;
            for (const std::string& name : cat.names)
            {
                std::string base = std::string(AITOWN_ASSETS_DIR) + "/"
                                 + cat.pathPrefix + "/" + name;
                const char* suffixes[] = {"_lod0.b3d", "_lod1.b3d", "_lod2.b3d"};
                const char* labels[]   = {" [LOD0]",   " [LOD1]",   " [LOD2]"  };
                for (int li = 0; li < 3; ++li)
                {
                    std::string path = base + suffixes[li];
                    irr::scene::IAnimatedMesh* m = smgr3->getMesh(path.c_str());
                    if (m) slots.push_back({name + labels[li], path});
                }
            }

            // Atlas texture used by all building meshes.
            std::string texPath = std::string(AITOWN_ASSETS_DIR)
                                + "/textures/buildings/buildings_atlas_d.png";
            irr::video::ITexture* atlasTex = driver->getTexture(texPath.c_str());

            const int totalSlots = static_cast<int>(slots.size());
            for (int si = 0; si < totalSlots; ++si)
            {
                float xPos = (static_cast<float>(si)
                              - static_cast<float>(totalSlots - 1) * 0.5f)
                             * kSingleModelSpacing;
                irr::scene::IAnimatedMesh* mesh = smgr3->getMesh(slots[si].meshPath.c_str());
                if (!mesh)
                {
                    std::fprintf(stderr, "  WARNING: failed to load '%s'. Skipping.\n",
                        slots[si].label.c_str());
                    continue;
                }
                irr::scene::IAnimatedMeshSceneNode* sn =
                    smgr3->addAnimatedMeshSceneNode(mesh);
                if (!sn) continue;
                if (atlasTex)
                    sn->setMaterialTexture(0, atlasTex);
                sn->setPosition(irr::core::vector3df(xPos, 0.f, 0.f));
                if (cat.scaleBuilding)
                    sn->setScale(irr::core::vector3df(10.f, 10.f, 10.f));
                sn->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
                sn->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
                loadedNames.push_back(slots[si].label);
                loadedXPositions.push_back(xPos);
            }
        }
        else
        {
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
                    std::fprintf(stderr, "  WARNING: failed to load '%s'. Skipping.\n",
                        name.c_str());
                    continue;
                }
                irr::scene::ISceneNode* sn = lodNode->getNode();
                if (sn)
                {
                    float xPos = (static_cast<float>(mi) - (static_cast<float>(N - 1) * 0.5f))
                                 * kShowcaseSpacing;
                    sn->setPosition(irr::core::vector3df(xPos, 0.0f, 0.0f));
                    if (cat.scaleBuilding)
                        sn->setScale(irr::core::vector3df(10.0f, 10.0f, 10.0f));
                    sn->setMaterialFlag(irr::video::EMF_LIGHTING,          false);
                    sn->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
                }
                lodNodes.push_back(lodNode);
                loadedNames.push_back(name);
                float xPos = (static_cast<float>(mi) - (static_cast<float>(N - 1) * 0.5f))
                             * kShowcaseSpacing;
                loadedXPositions.push_back(xPos);
            }
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

        // Reset navigation flags and clear annotations for the new category.
        receiver.spacePressed = false;
        receiver.marks.clear();
        receiver.strokeStarts.clear();

        // --- Render loop for this category ---
        // In screenshot mode, start at a 35° orbit angle so the front face AND
        // one side are visible without relying on the animation to reach a good pose.
        float orbitAngle = opts.screenshotPath.empty() ? 0.0f : 35.0f;
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
            // Single-model mode: no auto-rotation; right-drag steers orbit, scroll zooms.
            // Normal mode: auto-rotate at fixed speed; scroll zooms.
            if (receiver.wheelDelta != 0.0f)
            {
                orbitRadius -= receiver.wheelDelta * 3.0f;  // scroll up = zoom in
                if (orbitRadius < kOrbitRadiusMin)  orbitRadius = kOrbitRadiusMin;
                if (orbitRadius > kOrbitRadiusMax)  orbitRadius = kOrbitRadiusMax;
                receiver.wheelDelta = 0.0f;
            }
            if (receiver.orbitDeltaX != 0)
            {
                orbitAngle -= static_cast<float>(receiver.orbitDeltaX) * 0.5f;
                receiver.orbitDeltaX = 0;
            }
            else if (!singleModelMode)
            {
                orbitAngle += kOrbitDegPerFrame;
            }
            float rad = orbitAngle * irr::core::DEGTORAD;
            if (cam)
            {
                cam->setPosition(irr::core::vector3df(
                    kOrbitCentre.X + orbitRadius * sinf(rad),
                    kOrbitCentre.Y + kOrbitHeight,
                    kOrbitCentre.Z + orbitRadius * cosf(rad)));
                cam->setTarget(kOrbitCentre);
            }

            driver->beginScene(true, true, irr::video::SColor(255, 190, 215, 245));
            smgr3->drawAll();

            // --- Red 10×10 m tile boundary square (2D-projected, works with all renderers) ---
            // One square drawn per loaded model (single-model mode: one per LOD slot;
            // category mode: one per model in the row).
            if (cat.scaleBuilding)
            {
                const irr::video::SColor kTileRed(255, 220, 30, 30);
                constexpr float kHalf = 5.0f;  // half of a 10 m game tile
                constexpr float kY    = 0.05f; // just above ground to avoid z-fight

                irr::scene::ISceneCollisionManager* cm =
                    smgr3->getSceneCollisionManager();
                irr::scene::ICameraSceneNode* activeCam = smgr3->getActiveCamera();

                // Centres to draw squares around — one per loaded model in all modes
                const std::vector<float>& tileCentres = loadedXPositions;

                for (float xp : tileCentres)
                {
                    irr::core::vector3df w[4] = {
                        {xp - kHalf, kY, -kHalf},
                        {xp + kHalf, kY, -kHalf},
                        {xp + kHalf, kY,  kHalf},
                        {xp - kHalf, kY,  kHalf},
                    };
                    irr::core::position2d<irr::s32> s[4];
                    for (int i = 0; i < 4; ++i)
                        s[i] = cm->getScreenCoordinatesFrom3DPosition(w[i], activeCam);
                    for (int i = 0; i < 4; ++i)
                        driver->draw2DLine(s[i], s[(i + 1) % 4], kTileRed);
                }
            }

            // --- On-screen HUD ---
            irr::gui::IGUIFont* font = device->getGUIEnvironment()->getBuiltInFont();
            const irr::core::dimension2du screenSz = driver->getScreenSize();
            const irr::s32 W = static_cast<irr::s32>(screenSz.Width);
            const irr::s32 H2 = static_cast<irr::s32>(screenSz.Height);

            // 1. Category banner (top-left): "[N/11] Category Name"
            std::string banner = "[" + std::to_string(ci + 1) + "/" +
                                 std::to_string(kTotalCategories) +
                                 "] " + cat.label;
            font->draw(
                irr::core::stringw(banner.c_str()),
                irr::core::rect<irr::s32>(12, 12, 600, 36),
                irr::video::SColor(255, 255, 255, 255)
            );

            // 2. Floor labels: one per successfully loaded model.
            for (int li = 0; li < static_cast<int>(loadedNames.size()); ++li)
            {
                const std::string& modelName = loadedNames[static_cast<size_t>(li)];
                float modelX = loadedXPositions[static_cast<size_t>(li)];
                irr::core::vector3df anchor(modelX, 0.05f, 6.0f);
                irr::core::position2d<irr::s32> sp =
                    smgr3->getSceneCollisionManager()
                        ->getScreenCoordinatesFrom3DPosition(anchor, smgr3->getActiveCamera());

                if (sp.X < 0 || sp.X > W || sp.Y < 0 || sp.Y > H2)
                    continue;

                font->draw(
                    irr::core::stringw(modelName.c_str()),
                    irr::core::rect<irr::s32>(sp.X - 60, sp.Y - 10, sp.X + 140, sp.Y + 14),
                    irr::video::SColor(255, 255, 220, 60)
                );
            }

            // 3. Annotation tool HUD (bottom bar) — colour name + shape + hotkeys
            {
                const char* shapeNames[] = {"DOT", "CIRCLE", "CROSS"};
                std::string toolHud =
                    std::string("Colour: ") +
                    ShowcaseReceiver::colorNameAt(receiver.colorIdx) +
                    " (C=next)  Shape: " +
                    shapeNames[receiver.currentType] +
                    " (V=cycle)  LHold=draw  RDrag=rotate  Z=undo  X=clear  S=screenshot";
                font->draw(
                    irr::core::stringw(toolHud.c_str()),
                    irr::core::rect<irr::s32>(8, H2 - 18, W - 8, H2),
                    receiver.currentColor()
                );
            }

            // 4. Handle annotation actions (clear — undo/clear handled in OnEvent)
            if (receiver.clearRequested)
            {
                receiver.clearRequested = false;
                receiver.marks.clear();
                receiver.strokeStarts.clear();
            }

            // 5. Draw annotation marks
            for (const auto& mk : receiver.marks)
            {
                const irr::video::SColor& c = mk.color;
                if (mk.type == ShowcaseReceiver::DOT)
                {
                    driver->draw2DRectangle(c,
                        irr::core::rect<irr::s32>(mk.x - 5, mk.y - 5, mk.x + 5, mk.y + 5));
                }
                else if (mk.type == ShowcaseReceiver::CIRCLE)
                {
                    constexpr int R  = 18;
                    constexpr int SEG = 24;
                    for (int s = 0; s < SEG; ++s)
                    {
                        float a0 = static_cast<float>(s)     / SEG * 2.0f * irr::core::PI;
                        float a1 = static_cast<float>(s + 1) / SEG * 2.0f * irr::core::PI;
                        driver->draw2DLine(
                            {mk.x + static_cast<int>(R * cosf(a0)),
                             mk.y + static_cast<int>(R * sinf(a0))},
                            {mk.x + static_cast<int>(R * cosf(a1)),
                             mk.y + static_cast<int>(R * sinf(a1))},
                            c);
                    }
                }
                else  // CROSS
                {
                    constexpr int A = 12;
                    driver->draw2DLine({mk.x - A, mk.y - A}, {mk.x + A, mk.y + A}, c);
                    driver->draw2DLine({mk.x + A, mk.y - A}, {mk.x - A, mk.y + A}, c);
                }
            }

            // 6. Mouse cursor preview (small crosshair in current color)
            {
                const irr::video::SColor cc = receiver.currentColor();
                constexpr int CR = 6;
                driver->draw2DLine({receiver.mouseX - CR, receiver.mouseY},
                                   {receiver.mouseX + CR, receiver.mouseY}, cc);
                driver->draw2DLine({receiver.mouseX, receiver.mouseY - CR},
                                   {receiver.mouseX, receiver.mouseY + CR}, cc);
            }

            // Screenshot: read GL_BACK before endScene (avoids compositor redirection
            // of GL_FRONT which causes black images on composited desktops).
            // Falls back to createScreenShot() for the software renderer.
            if (receiver.screenshotRequested)
            {
                receiver.screenshotRequested = false;
                // Resume counter from existing files so sessions don't overwrite each other.
                static int shotCounter = -1;
                if (shotCounter < 0) {
                    shotCounter = 0;
                    while (true) {
                        std::string t = "annotation_" + std::to_string(shotCounter + 1) + ".png";
                        FILE* f = std::fopen(t.c_str(), "r");
                        if (!f) break;
                        std::fclose(f);
                        ++shotCounter;
                    }
                }
                std::string shotPath = "annotation_" + std::to_string(++shotCounter) + ".png";

                bool saved = false;
                if (!opts.softwareDriver)
                {
                    const irr::core::dimension2du sz = driver->getScreenSize();
                    const int sw = static_cast<int>(sz.Width);
                    const int sh = static_cast<int>(sz.Height);
                    std::vector<irr::u8> raw(static_cast<size_t>(sw * sh * 4));
                    glReadBuffer(GL_BACK);
                    glReadPixels(0, 0, sw, sh, GL_BGRA_EXT, GL_UNSIGNED_BYTE, raw.data());
                    // OpenGL origin is bottom-left; flip to top-left for the image.
                    std::vector<irr::u8> flipped(raw.size());
                    for (int row = 0; row < sh; ++row)
                        std::memcpy(&flipped[static_cast<size_t>(row * sw * 4)],
                                    &raw[static_cast<size_t>((sh - 1 - row) * sw * 4)],
                                    static_cast<size_t>(sw * 4));
                    irr::video::IImage* img = driver->createImageFromData(
                        irr::video::ECF_A8R8G8B8, sz, flipped.data(), false);
                    if (img)
                    {
                        driver->writeImageToFile(img, shotPath.c_str());
                        img->drop();
                        saved = true;
                    }
                }
                if (!saved)
                {
                    irr::video::IImage* shot = driver->createScreenShot();
                    if (shot)
                    {
                        driver->writeImageToFile(shot, shotPath.c_str());
                        shot->drop();
                        saved = true;
                    }
                }
                if (saved)
                {
                    std::printf("  Annotation screenshot saved: %s\n", shotPath.c_str());
                    std::fflush(stdout);
                }
            }

            driver->endScene();
            ++frameCount;

            // Auto-screenshot: capture frame N then advance to next category.
            if (!opts.screenshotPath.empty() && frameCount == opts.screenshotFrame)
            {
                // Build per-category filename: insert category index before extension.
                std::string shotPath = opts.screenshotPath;
                if (kTotalCategories > 1)
                {
                    // Insert "_N" before last '.' (or append if no extension).
                    size_t dot = shotPath.rfind('.');
                    std::string idx = "_" + std::to_string(ci + 1);
                    if (dot == std::string::npos)
                        shotPath += idx;
                    else
                        shotPath = shotPath.substr(0, dot) + idx + shotPath.substr(dot);
                }
                irr::video::IImage* shot = driver->createScreenShot();
                if (shot)
                {
                    driver->writeImageToFile(shot, shotPath.c_str());
                    shot->drop();
                    std::printf("  Screenshot saved: %s\n", shotPath.c_str());
                    std::fflush(stdout);
                }
                break;  // advance to next category
            }
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
