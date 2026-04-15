#pragma once

// BuildingAssetLoader.h — Phase 9 building asset loader.
//
// Loads building asset families (LOD0 .ply/.b3d, LOD1 .ply/.b3d, LOD2 .ply/.b3d or billboard)
// and their .meta sidecar JSON, then creates and returns a LODNode wrapping the scene
// node with the full LOD distance chain configured.
//
// Asset naming convention (per architecture/asset-standards/3d-model-standards.md):
//   <basePath>_lod0.ply / _lod0.b3d    — highest-detail mesh (LOD0)
//   <basePath>_lod1.ply / _lod1.b3d    — medium-detail mesh (LOD1)
//   <basePath>_lod2.ply / _lod2.b3d    — low-detail geometry shell (height_floors >= 4 only)
//   <basePath>.meta                     — JSON sidecar: height_floors, category, lod_distances[]
//
// For buildings with height_floors <= 3, LOD2 is a billboard (DDS path stored but
// no geometry mesh — LODNode is created with lod2 = nullptr).
//
// LOD distances are read from the .meta sidecar "lod_distances" array:
//   [0] = LOD0→LOD1 swap-out distance (metres)
//   [1] = LOD1→LOD2 swap-out distance (metres)
//   [2] = cull distance (metres)
//
// Mesh loading:
//   m_smgr->getMesh() loads the mesh file (.ply preferred, .b3d fallback via
//   resolveModelPath()) and returns IAnimatedMesh*.
//   The first frame of the animated mesh is used as a static SMesh via
//   dynamic_cast<scene::SMesh*>(animMesh->getMesh(0)).
//
// Scene node:
//   m_smgr->addMeshSceneNode() creates the scene node for LOD0.
//   The LODNode wraps this node (non-owning — SceneEntityManager owns it).
//
// JSON parsing:
//   Minimal hand-written parser using sscanf — no external library dependency.
//   Extracts "height_floors":N and "lod_distances":[a,b,c] from the .meta file.
//
// Ownership:
//   The caller owns the returned LODNode* and is responsible for calling
//   SceneEntityManager::destroy() (which calls node->remove()) before deleting.
//   BuildingAssetLoader does not retain any reference to loaded meshes or nodes.

#include <memory>
#include <string>

// Forward declarations.
namespace irr {
    namespace scene {
        class ISceneManager;
        class ISceneNode;
        class SMesh;
    }
    namespace video {
        class IVideoDriver;
    }
}
class LODNode;

// BuildingAssetLoader — loads a building asset family and returns a configured LODNode.
class BuildingAssetLoader {
public:
    // Constructor.
    // smgr   — Irrlicht scene manager (for getMesh() and addMeshSceneNode()).
    //          Must outlive this loader.
    // driver — Irrlicht video driver (for future texture operations).
    //          Must outlive this loader.
    BuildingAssetLoader(irr::scene::ISceneManager* smgr,
                        irr::video::IVideoDriver*  driver);

    // Non-copyable / non-movable — wraps raw non-owning pointers.
    BuildingAssetLoader(const BuildingAssetLoader&)            = delete;
    BuildingAssetLoader& operator=(const BuildingAssetLoader&) = delete;
    BuildingAssetLoader(BuildingAssetLoader&&)                 = delete;
    BuildingAssetLoader& operator=(BuildingAssetLoader&&)      = delete;

    // load() — load a building asset family and return a configured LODNode.
    //
    // basePath — filesystem path prefix (without suffix), e.g.:
    //   "assets/3d/buildings/res_low_01"
    // The loader resolves LOD mesh paths via resolveModelPath() (PLY preferred,
    // B3D fallback), then appends ".meta".
    //
    // Returns a new LODNode on success (unique_ptr). The caller owns the returned pointer.
    // Returns nullptr if:
    //   - The .meta sidecar is not found or cannot be parsed.
    //   - LOD0 or LOD1 mesh cannot be loaded.
    //   - The LOD0 mesh is not an SMesh (cannot call recalculateBoundingBox).
    std::unique_ptr<LODNode> load(const std::string& basePath);

private:
    irr::scene::ISceneManager* m_smgr;
    irr::video::IVideoDriver*  m_driver;

    // parseMeta() — parse a .meta JSON sidecar file.
    //
    // Extracts "height_floors":N, "lod_distances":[a,b,c], and
    // "atlas_cell":{"row":R,"col":C} using sscanf.
    // This is a minimal hand-written parser — no external JSON library is required.
    //
    // metaPath     — full path to the .meta file.
    // heightFloors — output: value of "height_floors" key.
    // lod0dist     — output: lod_distances[0] (LOD0→LOD1 swap-out metres).
    // lod1dist     — output: lod_distances[1] (LOD1→LOD2 swap-out metres).
    // cullDist     — output: lod_distances[2] (cull distance metres).
    // atlasRow     — output: atlas_cell.row (0-based row in 4×4 cell grid).
    // atlasCol     — output: atlas_cell.col (0-based column in 4×4 cell grid).
    //
    // Returns true on success; false if the file cannot be opened or the required
    // keys are not found.  atlasRow/atlasCol default to 0 if "atlas_cell" is
    // absent (safe fallback — renders using cell (0,0) of the atlas).
    bool parseMeta(const std::string& metaPath,
                   int&               heightFloors,
                   float&             lod0dist,
                   float&             lod1dist,
                   float&             cullDist,
                   int&               atlasRow,
                   int&               atlasCol);

    // applyAtlasTexture: binds the buildings atlas texture to all material slots
    // on the given scene node. Looks up the atlas path relative to basePath.
    void applyAtlasTexture(irr::scene::ISceneNode* node, const std::string& basePath) const;

    // --- Phase 11q3 refactoring helpers (Section 5a–5b) ---

    // 5a: load() helper.
    // resolveAtlasPath: determines the atlas texture path from the asset base path.
    std::string resolveAtlasPath(const std::string& basePath) const;

    // 5b: parseMeta() helpers (static — no member state needed).
    // parseLodDistances: extracts lod_distances [a, b, c] from .meta content string.
    static bool parseLodDistances(const std::string& content, float& lod0, float& lod1, float& cull);

    // parseAtlasCell: extracts atlas_cell row/col from .meta content string.
    static bool parseAtlasCell(const std::string& content, int& row, int& col);
};
