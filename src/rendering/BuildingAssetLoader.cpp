// BuildingAssetLoader.cpp — Phase 9 building asset loader implementation.
//
// Loads building asset families (LOD0/1/2 .b3d files) and their .meta sidecar JSON,
// then creates and returns a LODNode wrapping the scene node with LOD distances.
//
// Asset loading sequence:
//   1. Parse .meta sidecar to get height_floors and lod_distances[0..2].
//   2. Load _lod0.b3d via m_smgr->getMesh() → dynamic_cast<SMesh*>.
//   3. Load _lod1.b3d (same path).
//   4. For height_floors >= 4: load _lod2.b3d geometry shell.
//      For height_floors <= 3: lod2 = nullptr (billboard at this distance).
//   5. Create scene node via addMeshSceneNode(lod0).
//   6. Construct and return LODNode(node, lod0, lod1, lod2, d0, d1, cull).
//
// SMesh* type safety (per architecture/graphics-architecture/scene-graph-ownership.md
// §WARNING — SMesh* Downcast Safety):
//   Use dynamic_cast<SMesh*> in debug builds; static_cast in release (after verification).
//   Assert on null in debug. Store as SMesh* throughout — never as IMesh*.
//
// bounding box requirement (per scene-graph-ownership.md §LOD Swap):
//   recalculateBoundingBox() on each SMeshBuffer then the SMesh before addMeshSceneNode().

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

#include "BuildingAssetLoader.h"
#include "LODNode.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
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
// Helper: load an SMesh* from a .b3d (or .obj) path.
// Returns nullptr if the mesh cannot be loaded or is not an SMesh.
// ---------------------------------------------------------------------------

static SMesh* loadSMesh(ISceneManager* smgr, const std::string& path)
{
    IAnimatedMesh* animMesh = smgr->getMesh(path.c_str());
    if (!animMesh) return nullptr;

    // Get frame 0 of the animated mesh as a static IMesh*.
    IMesh* iMesh = animMesh->getMesh(0);
    if (!iMesh) return nullptr;

    // Use dynamic_cast in debug builds to verify the pointer is truly an SMesh.
    // Per scene-graph-ownership.md §WARNING — SMesh* Downcast Safety:
    // static_cast on a non-SMesh IMesh* is UB; dynamic_cast is required in debug.
#ifndef NDEBUG
    SMesh* sMesh = dynamic_cast<SMesh*>(iMesh);
    assert(sMesh != nullptr && "IMesh* from .b3d is not an SMesh — static_cast would be UB");
#else
    SMesh* sMesh = static_cast<SMesh*>(iMesh); // safe after debug verification
#endif

    return sMesh;
}

// ---------------------------------------------------------------------------
// Helper: recalculate bounding box on an SMesh and all its buffers.
// Must be called before addMeshSceneNode() or setMesh() to avoid stale culling.
// ---------------------------------------------------------------------------

static void recalcBounds(SMesh* mesh)
{
    if (!mesh) return;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        mesh->getMeshBuffer(i)->recalculateBoundingBox();
    }
    mesh->recalculateBoundingBox();
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

LODNode* BuildingAssetLoader::load(const std::string& basePath)
{
    // ------------------------------------------------------------------
    // Step 1: Parse .meta sidecar.
    // ------------------------------------------------------------------
    int   heightFloors = 0;
    float lod0dist     = 50.0f;
    float lod1dist     = 200.0f;
    float cullDist     = 500.0f;

    const std::string metaPath = basePath + ".meta";
    if (!parseMeta(metaPath, heightFloors, lod0dist, lod1dist, cullDist)) {
        return nullptr;  // .meta missing or malformed
    }

    // ------------------------------------------------------------------
    // Step 2: Load LOD0 mesh.
    // ------------------------------------------------------------------
    const std::string lod0Path = basePath + "_lod0.b3d";
    SMesh* lod0 = loadSMesh(m_smgr, lod0Path);
    if (!lod0) {
        return nullptr;  // mandatory LOD0 not found
    }

    // ------------------------------------------------------------------
    // Step 3: Load LOD1 mesh.
    // ------------------------------------------------------------------
    const std::string lod1Path = basePath + "_lod1.b3d";
    SMesh* lod1 = loadSMesh(m_smgr, lod1Path);
    if (!lod1) {
        return nullptr;  // mandatory LOD1 not found
    }

    // ------------------------------------------------------------------
    // Step 4: Load LOD2 mesh (only for tall buildings, height_floors >= 4).
    // For buildings with height_floors <= 3, lod2 = nullptr (billboard).
    // ------------------------------------------------------------------
    SMesh* lod2 = nullptr;
    if (heightFloors >= 4) {
        const std::string lod2Path = basePath + "_lod2.b3d";
        lod2 = loadSMesh(m_smgr, lod2Path);
        // lod2 failure is non-fatal for tall buildings — we continue with nullptr
        // but this will suppress the LOD2 transition. A real pipeline would warn here.
    }

    // ------------------------------------------------------------------
    // Step 5: Recalculate bounding boxes before creating the scene node.
    // MANDATORY per scene-graph-ownership.md §LOD Swap — Bounding Box Requirement:
    // stale bounding box causes incorrect frustum culling.
    // ------------------------------------------------------------------
    recalcBounds(lod0);
    recalcBounds(lod1);
    recalcBounds(lod2);  // no-op when lod2 == nullptr

    // ------------------------------------------------------------------
    // Step 6: Create the scene node wrapping LOD0.
    // addMeshSceneNode() calls grab() on lod0 internally. The returned node
    // is parented to the root scene node and managed by SceneEntityManager.
    // ------------------------------------------------------------------
    IMeshSceneNode* node = m_smgr->addMeshSceneNode(lod0);
    if (!node) {
        return nullptr;  // scene graph full or null scene manager
    }

    // ------------------------------------------------------------------
    // Step 7: Construct and return the LODNode.
    // LODNode stores non-owning references to lod0/1/2 — their lifetimes
    // are managed by the Irrlicht scene manager (via addMeshSceneNode grab).
    // The caller (SceneEntityManager) owns the returned LODNode*.
    // ------------------------------------------------------------------
    return new LODNode(node, lod0, lod1, lod2, lod0dist, lod1dist, cullDist);
}

// ---------------------------------------------------------------------------
// parseMeta() — minimal hand-written JSON parser
// ---------------------------------------------------------------------------
//
// Expected .meta format (subset parsed here):
//   {
//     "height_floors": 3,
//     "category": "residential",
//     "lod_distances": [30.0, 100.0, 300.0]
//   }
//
// Parser strategy:
//   - Read the entire file into a string.
//   - Use strstr() to locate "height_floors" key.
//   - Use sscanf() to extract the integer value.
//   - Use strstr() to locate "lod_distances" key.
//   - Scan forward for '[', then parse three floats separated by commas.
// ---------------------------------------------------------------------------

bool BuildingAssetLoader::parseMeta(const std::string& metaPath,
                                     int&               heightFloors,
                                     float&             lod0dist,
                                     float&             lod1dist,
                                     float&             cullDist)
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

    // Advance past "height_floors" to find the colon and value.
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

    // Parse three floats from "[a, b, c]".
    if (std::sscanf(lBracket + 1, " %f , %f , %f",
                    &lod0dist, &lod1dist, &cullDist) != 3) {
        return false;
    }

    return true;
}
