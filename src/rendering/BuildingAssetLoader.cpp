// BuildingAssetLoader.cpp — Phase 9 building asset loader implementation.
//
// Loads building asset families (LOD0/1/2 .b3d files) and their .meta sidecar JSON,
// then creates and returns a LODNode wrapping the scene node with LOD distances.
//
// Asset loading sequence:
//   1. Parse .meta sidecar to get height_floors, lod_distances[0..2], and
//      atlas_cell {row, col}.
//   2. Load _lod0.b3d via m_smgr->getMesh() → IAnimatedMesh* (CSkinnedMesh).
//   3. Load _lod1.b3d (same approach).
//   4. For height_floors >= 4: load _lod2.b3d geometry shell.
//      For height_floors <= 3: lod2 = nullptr (billboard at this distance).
//   5. Create scene node via addMeshSceneNode(lod0).
//   6. Bind buildings_atlas_d.dds to texture slot 0 of all material slots on the
//      scene node.  The placeholder B3D files carry no TEXS/BRUS texture chunks and
//      have VRTS tc_sets=0, so Irrlicht assigns the default material (EMT_SOLID,
//      no texture, white vertex color) after loading.  Without explicit texture
//      assignment the mesh renders solid white even after BackfaceCulling=false.
//   7. Construct and return LODNode(node, lod0, lod1, lod2, d0, d1, cull).
//
// B3D mesh format note (tc_sets):
//   B3D files use VRTS tc_sets=1 (UV channel 0 = atlas diffuse).  The Irrlicht
//   B3D loader (CB3DMeshFileLoader::readChunkVRTS) reads exactly three VRTS
//   header fields: flags, tex_coord_sets, tex_coord_set_size.  Using tc_sets=2
//   causes the loader to read tc_flags[1] as tex_coord_set_size and then
//   misinterpret the first vertex's position float as another header field,
//   producing totally garbled (invisible) geometry.  tc_sets=1 aligns the
//   loader's reads correctly and is sufficient for atlas-mapped buildings.
//
// IMPORTANT — no dynamic_cast on Irrlicht types:
//   Irrlicht is compiled with -fno-rtti. dynamic_cast on Irrlicht types has no
//   typeinfo in the vtable and will crash at runtime (SIGSEGV in __dynamic_cast).
//   B3D files are always loaded as CSkinnedMesh (not SMesh), so a cast to SMesh*
//   would return null even if RTTI were available. We work with IAnimatedMesh*
//   throughout and use IAnimatedMeshSceneNode for the scene node.
//
// Bounding box:
//   CSkinnedMesh::finalize() (called by the B3D loader) computes the bounding box
//   from all vertices. No explicit recalculateBoundingBox() is required here.

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "BuildingAssetLoader.h"
#include "LODNode.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace irr;
using namespace irr::scene;
using namespace irr::video;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BuildingAssetLoader::BuildingAssetLoader(ISceneManager* smgr, IVideoDriver* driver)
    : m_smgr(smgr)
    , m_driver(driver)
{
}

// ---------------------------------------------------------------------------
// Helper: load an IAnimatedMesh* from a .b3d path.
//
// Returns the borrowed pointer from the Irrlicht mesh cache (no grab for caller).
// Returns nullptr if the file cannot be loaded.
//
// NOTE: dynamic_cast on Irrlicht types is FORBIDDEN — Irrlicht is compiled with
// -fno-rtti and __dynamic_cast will SIGSEGV. B3D files always load as CSkinnedMesh
// (not SMesh), so IAnimatedMesh* is the correct type to work with.
// ---------------------------------------------------------------------------

static IAnimatedMesh* loadAnimMesh(ISceneManager* smgr, const std::string& path)
{
    return smgr->getMesh(path.c_str());  // nullptr on failure; borrowed (no grab)
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

std::unique_ptr<LODNode> BuildingAssetLoader::load(const std::string& basePath)
{
    // ------------------------------------------------------------------
    // Step 1: Parse .meta sidecar.
    // ------------------------------------------------------------------
    int   heightFloors = 0;
    float lod0dist     = 50.0f;
    float lod1dist     = 200.0f;
    float cullDist     = 500.0f;
    int   atlasRow     = 0;
    int   atlasCol     = 0;

    const std::string metaPath = basePath + ".meta";
    if (!parseMeta(metaPath, heightFloors, lod0dist, lod1dist, cullDist,
                   atlasRow, atlasCol)) {
        return nullptr;  // .meta missing or malformed
    }

    // ------------------------------------------------------------------
    // Step 2: Load LOD0 mesh.
    // Returns a borrowed IAnimatedMesh* from the mesh cache (no grab for caller).
    // ------------------------------------------------------------------
    const std::string lod0Path = basePath + "_lod0.b3d";
    IAnimatedMesh* lod0 = loadAnimMesh(m_smgr, lod0Path);
    if (!lod0) {
        return nullptr;  // mandatory LOD0 not found
    }

    // ------------------------------------------------------------------
    // Step 3: Load LOD1 mesh.
    // ------------------------------------------------------------------
    const std::string lod1Path = basePath + "_lod1.b3d";
    IAnimatedMesh* lod1 = loadAnimMesh(m_smgr, lod1Path);
    if (!lod1) {
        return nullptr;  // mandatory LOD1 not found
    }

    // ------------------------------------------------------------------
    // Step 4: Load LOD2 mesh (only for tall buildings, height_floors >= 4).
    // For buildings with height_floors <= 3, lod2 = nullptr (billboard).
    // ------------------------------------------------------------------
    IAnimatedMesh* lod2 = nullptr;
    if (heightFloors >= 4) {
        const std::string lod2Path = basePath + "_lod2.b3d";
        lod2 = loadAnimMesh(m_smgr, lod2Path);
        // lod2 failure is non-fatal for tall buildings — we continue with nullptr
        // but this will suppress the LOD2 transition.
    }

    // ------------------------------------------------------------------
    // Step 5: Create a STATIC mesh scene node (CMeshSceneNode) wrapping LOD0.
    //
    // addMeshSceneNode() is used (NOT addAnimatedMeshSceneNode()) to create a
    // CMeshSceneNode. This is critical for correct building placement:
    //   - CMeshSceneNode::render() applies only the node's world transform.
    //   - CAnimatedMeshSceneNode::render() additionally applies per-buffer bone
    //     transforms (SSkinMeshBuffer::Transformation), which would offset static
    //     buildings from their intended tile positions.
    //
    // IAnimatedMesh* is cast to IMesh* (safe — IAnimatedMesh inherits from IMesh).
    // addMeshSceneNode() grabs lod0 internally (ref_count: 1→2).
    // ------------------------------------------------------------------
    IMeshSceneNode* node = m_smgr->addMeshSceneNode(static_cast<IMesh*>(lod0));
    if (!node) {
        return nullptr;  // scene graph full or null scene manager
    }

    // ------------------------------------------------------------------
    // Step 6: Bind the buildings atlas texture to all material slots.
    //
    // B3D files embed a TEXS/BRUS chunk referencing "buildings_atlas_d.dds" by
    // bare filename.  The Irrlicht B3D loader attempts to resolve it relative to
    // the mesh file's directory — that look-up always fails (the file is not in
    // assets/3d/buildings/) and produces an expected "Could not open file of
    // texture" log line.  We override that failed binding by calling
    // IVideoDriver::getTexture() with the full absolute path to the atlas and
    // then calling setTexture(0, atlas) on every material slot.
    //
    // Atlas format — PNG (not DDS):
    //   Irrlicht 1.8.5 ships with its DDS image loader DISABLED by default.
    //   The macro _IRR_COMPILE_WITH_DDS_LOADER_ is commented out in
    //   IrrCompileConfig.h, so IVideoDriver::getTexture() cannot load any DDS
    //   file.  Additionally, the ddsBuffer struct contains a void* pointer (8
    //   bytes on 64-bit) that shifts pixelFormat.fourCC to file offset 88
    //   instead of the spec-mandated 84, causing even DXT1 files to be
    //   misidentified as ARGB8888 on x86_64.
    //
    //   The V1 workaround: atlas files are authored as PNG and loaded via
    //   IVideoDriver::getTexture() (Irrlicht's built-in PNG loader is always
    //   enabled).  The production raw-GL sRGB DXT1 upload path (TextureCache::
    //   loadSRGB) is planned for Phase 11+ and does not use getTexture().
    //
    // Bind the texture to slot 0 of every material on the node.
    //
    // Note: atlasRow and atlasCol are parsed from the .meta but UV sub-rect
    // selection is handled per-vertex in the B3D mesh (authored UV channel 0
    // maps into the correct atlas cell).  For placeholder meshes that lack UVs
    // the whole texture is sampled at UV (0,0) — a single atlas cell is visible,
    // which is correct placeholder behavior.
    // ------------------------------------------------------------------
    if (m_driver) {
        // Build the atlas path relative to the asset base directory.
        // basePath is e.g. "assets/3d/buildings/res_low_01"; strip back to the
        // assets root and append the texture sub-path.
        // The atlas always lives at assets/textures/buildings/buildings_atlas_d.dds
        // relative to AITOWN_ASSETS_DIR (defined in CMakeLists.txt as the absolute
        // path to the assets/ directory).
        //
        // We derive the assets root from basePath by finding the last occurrence
        // of "/3d/buildings/" and trimming everything from that point onward.
        // This avoids a hard dependency on AITOWN_ASSETS_DIR being available here
        // (it is only defined in IrrlichtRenderer.cpp as a compile-time macro via
        // CMakeLists.txt).  BuildingAssetLoader receives basePath from
        // IrrlichtRenderer which already has AITOWN_ASSETS_DIR prepended — so we
        // can reliably strip the known suffix.
        std::string atlasPath;
        const auto buildingPos = basePath.rfind("/3d/buildings/");
        const auto vehiclePos  = basePath.rfind("/3d/vehicles/");
        if (buildingPos != std::string::npos) {
            atlasPath = basePath.substr(0, buildingPos)
                        + "/textures/buildings/buildings_atlas_d.png";
        } else if (vehiclePos != std::string::npos) {
            atlasPath = basePath.substr(0, vehiclePos)
                        + "/textures/vehicles/vehicles_diffuse_atlas_d.png";
        }

        if (!atlasPath.empty()) {
            ITexture* atlas = m_driver->getTexture(atlasPath.c_str());
            if (atlas) {
                u32 matCount = node->getMaterialCount();
                for (u32 m = 0; m < matCount; ++m) {
                    node->getMaterial(m).setTexture(0, atlas);
                }
            } else {
                fprintf(stderr,
                    "[BuildingAssetLoader] WARNING: buildings atlas not found at "
                    "'%s' — building will render white\n", atlasPath.c_str());
            }
        }
    }

    // ------------------------------------------------------------------
    // Step 7: Construct and return the LODNode.
    // LODNode stores borrowed (non-owning) pointers to lod0/1/2 — they remain
    // alive in the Irrlicht mesh cache for the lifetime of the scene.
    // The caller (IrrlichtRenderer) owns the returned LODNode*.
    // ------------------------------------------------------------------
    return std::make_unique<LODNode>(node, lod0, lod1, lod2, lod0dist, lod1dist, cullDist);
}

// ---------------------------------------------------------------------------
// parseMeta() — minimal hand-written JSON parser
// ---------------------------------------------------------------------------
//
// Expected .meta format (subset parsed here):
//   {
//     "height_floors": 3,
//     "category": "residential",
//     "atlas_cell": { "row": 0, "col": 0 },
//     "lod_distances": [30.0, 100.0, 300.0]
//   }
//
// Parser strategy:
//   - Read the entire file into a string.
//   - Use strstr() to locate "height_floors" key.
//   - Use sscanf() to extract the integer value.
//   - Use strstr() to locate "lod_distances" key.
//   - Scan forward for '[', then parse three floats separated by commas.
//   - Use strstr() to locate "atlas_cell" key, then locate "row" and "col"
//     within the braces that follow.  Both default to 0 if not found.
// ---------------------------------------------------------------------------

bool BuildingAssetLoader::parseMeta(const std::string& metaPath,
                                     int&               heightFloors,
                                     float&             lod0dist,
                                     float&             lod1dist,
                                     float&             cullDist,
                                     int&               atlasRow,
                                     int&               atlasCol)
{
    std::ifstream file(metaPath);
    if (!file.is_open()) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string content = ss.str();
    if (content.empty()) return false;

    const char* data = content.c_str();

    // ------------------------------------------------------------------
    // Parse "height_floors": N
    // ------------------------------------------------------------------
    const char* hfKey = std::strstr(data, "height_floors");
    if (!hfKey) return false;

    const char* hfColon = std::strchr(hfKey, ':');
    if (!hfColon) return false;

    if (std::sscanf(hfColon + 1, " %d", &heightFloors) != 1) return false;

    // ------------------------------------------------------------------
    // Parse "lod_distances": [a, b, c]
    // ------------------------------------------------------------------
    const char* ldKey = std::strstr(data, "lod_distances");
    if (!ldKey) return false;

    const char* lBracket = std::strchr(ldKey, '[');
    if (!lBracket) return false;

    if (std::sscanf(lBracket + 1, " %f , %f , %f",
                    &lod0dist, &lod1dist, &cullDist) != 3) {
        return false;
    }

    // ------------------------------------------------------------------
    // Parse "atlas_cell": { "row": R, "col": C }
    //
    // Optional — default to (0,0) if the key is absent (safe fallback).
    // Search within the "atlas_cell" object only to avoid accidentally
    // matching a "row" or "col" key from a different object.
    // ------------------------------------------------------------------
    atlasRow = 0;
    atlasCol = 0;

    const char* acKey = std::strstr(data, "atlas_cell");
    if (acKey) {
        // Advance to the opening brace of the atlas_cell object.
        const char* acBrace = std::strchr(acKey, '{');
        if (acBrace) {
            const char* acEnd = std::strchr(acBrace, '}');
            if (acEnd) {
                // Parse "row": R within [acBrace, acEnd].
                const char* rowKey = std::strstr(acBrace, "\"row\"");
                if (rowKey && rowKey < acEnd) {
                    const char* rowColon = std::strchr(rowKey, ':');
                    if (rowColon && rowColon < acEnd) {
                        std::sscanf(rowColon + 1, " %d", &atlasRow);
                    }
                }
                // Parse "col": C within [acBrace, acEnd].
                const char* colKey = std::strstr(acBrace, "\"col\"");
                if (colKey && colKey < acEnd) {
                    const char* colColon = std::strchr(colKey, ':');
                    if (colColon && colColon < acEnd) {
                        std::sscanf(colColon + 1, " %d", &atlasCol);
                    }
                }
            }
        }
    }

    return true;
}
