// BuildingAssetLoader.cpp — Phase 9 building asset loader implementation.
//
// Loads building asset families (LOD0/1/2 .b3d files) and their .meta sidecar JSON,
// then creates and returns a LODNode wrapping the scene node with LOD distances.
//
// Asset loading sequence:
//   1. Parse .meta sidecar to get height_floors and lod_distances[0..2].
//   2. Load _lod0.b3d via m_smgr->getMesh() → IAnimatedMesh* (CSkinnedMesh).
//   3. Load _lod1.b3d (same approach).
//   4. For height_floors >= 4: load _lod2.b3d geometry shell.
//      For height_floors <= 3: lod2 = nullptr (billboard at this distance).
//   5. Create scene node via addAnimatedMeshSceneNode(lod0).
//   6. Construct and return LODNode(node, lod0, lod1, lod2, d0, d1, cull).
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
    // Step 6: Construct and return the LODNode.
    // LODNode stores borrowed (non-owning) pointers to lod0/1/2 — they remain
    // alive in the Irrlicht mesh cache for the lifetime of the scene.
    // The caller (IrrlichtRenderer) owns the returned LODNode*.
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
