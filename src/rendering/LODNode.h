#pragma once

// LODNode.h — Wrapper for IAnimatedMeshSceneNode that implements the LOD swap
// sequence and hysteresis-based LOD level selection.
//
// LODNode is a non-owning wrapper around an Irrlicht IAnimatedMeshSceneNode.
// Lifetime of the wrapped scene node is controlled exclusively by
// SceneEntityManager (which is the sole caller of addXxxSceneNode() and
// node->remove()). LODNode never calls grab() or remove() on the node it wraps.
//
// swapMesh() implements the LOD swap sequence:
//   IAnimatedMeshSceneNode::setMesh(newMesh) — Irrlicht drops old mesh and grabs
//   new mesh internally. Caller must NOT call grab()/drop() around this call
//   because the LOD mesh pointers are borrowed references from the Irrlicht mesh
//   cache (ref_count owned by cache). setMesh() takes ownership via grab() on the
//   new mesh; the cache's reference is still valid (ref_count == 2 while node is
//   alive, drops back to 1 when the node is destroyed).
//
// NOTE: Irrlicht is compiled with -fno-rtti (no RTTI). dynamic_cast on Irrlicht
// types is undefined behaviour and will crash at runtime. This class uses
// IAnimatedMesh* throughout to avoid any downcast to SMesh*.
//
// update() implements LOD hysteresis per architecture/asset-standards/3d-model-standards.md
// LOD Distance Thresholds table. Hysteresis prevents rapid LOD flicker at threshold
// boundaries:
//   LOD0 → LOD1 swap-out when dist > lod0to1;  swap-in when dist < lod0to1 - 5 m
//   LOD1 → LOD2 swap-out when dist > lod1to2;  swap-in when dist < lod1to2 - 10 m

#include <irrlicht.h>

// LODNode — non-owning wrapper around IMeshSceneNode providing the LOD swap
// sequence and hysteresis-based LOD level selection.
class LODNode {
public:
    // Legacy constructor: node must be non-null and must outlive this LODNode.
    // Ownership of the scene node remains with SceneEntityManager.
    // Creates a LODNode with no LOD mesh pointers — only position/removal is used.
    explicit LODNode(irr::scene::IMeshSceneNode* node);

    // Full LOD constructor (Phase 9 deliverable).
    //
    // node       — the static mesh scene node to wrap (non-owning; must outlive
    //              this LODNode). Created via addMeshSceneNode().
    //              CMeshSceneNode is used (not CAnimatedMeshSceneNode) so that
    //              each mesh buffer is rendered with the node's world transform
    //              only — bone/joint transforms from the B3D skeleton are NOT applied.
    //              This is required for static buildings where bone transforms would
    //              displace geometry from the intended scene-node position.
    // lod0       — highest-detail mesh (IAnimatedMesh* from B3D loader, i.e. CSkinnedMesh).
    //              Borrowed from the Irrlicht mesh cache; LODNode does NOT grab() this.
    // lod1       — medium-detail mesh (LOD1). Same ownership rules as lod0.
    // lod2       — low-detail mesh (LOD2) or nullptr for billboard-only buildings.
    // lod0to1    — swap-out threshold in metres.
    // lod1to2    — swap-out threshold: LOD1 → LOD2.
    // cullDist   — frustum/distance cull threshold (stored; not yet enforced).
    LODNode(irr::scene::IMeshSceneNode* node,
            irr::scene::IAnimatedMesh*  lod0,
            irr::scene::IAnimatedMesh*  lod1,
            irr::scene::IAnimatedMesh*  lod2,
            float                       lod0to1,
            float                       lod1to2,
            float                       cullDist);

    // Non-copyable / non-movable — wraps a raw non-owning pointer with no reference
    // counting semantics; copying would produce two objects with no clear ownership.
    LODNode(const LODNode&)            = delete;
    LODNode& operator=(const LODNode&) = delete;
    LODNode(LODNode&&)                 = delete;
    LODNode& operator=(LODNode&&)      = delete;

    // update() — evaluate LOD distance and switch level if the hysteresis band is crossed.
    //
    // Called once per frame by the entity manager (or BuildingAssetLoader consumer).
    // Implements hysteresis to prevent rapid LOD flicker at threshold boundaries:
    //
    //   LOD0 → LOD1: swap-out when dist > m_lod0to1
    //               swap-in  when dist < m_lod0to1 - kHysteresis01   (5 m band)
    //   LOD1 → LOD2: swap-out when dist > m_lod1to2
    //               swap-in  when dist < m_lod1to2 - kHysteresis12   (10 m band)
    //
    // Never transitions up AND down in the same frame (only one level change per call).
    // When lod2 is nullptr, LOD1 is the lowest available level and the LOD1→LOD2
    // transition is suppressed.
    //
    // cameraPos — world-space camera position (obtained from
    //             ISceneManager::getActiveCamera()->getAbsolutePosition()).
    void update(const irr::core::vector3df& cameraPos);

    // swapMesh() — perform an in-place LOD mesh swap on the wrapped scene node.
    //
    // Calls IMeshSceneNode::setMesh((IMesh*)newMesh) which internally drops the old
    // mesh and grabs the new mesh. The LOD mesh pointers are borrowed from the
    // Irrlicht mesh cache — caller must NOT call grab()/drop() on newMesh.
    //
    // Preserves the node's transform and material assignments.
    void swapMesh(irr::scene::IAnimatedMesh* newMesh);

    // swapMeshRaw() — same as swapMesh() but accepts IMesh* directly.
    // Used by road tile LOD transitions where procedural SMesh* (which is IMesh*
    // but NOT IAnimatedMesh*) must be swapped in without an IAnimatedMesh cast.
    // Caller must NOT call grab()/drop() — ownership stays with IrrlichtRenderer
    // (m_sharedRoadMeshLOD0/1/2 are the authoritative references).
    void swapMeshRaw(irr::scene::IMesh* mesh);

    // getNode() — accessor for the wrapped scene node (exposed as ISceneNode* for
    // positioning/removal/material access). Caller must not call remove() or drop().
    irr::scene::ISceneNode* getNode() const;

    // getCurrentLOD() — returns the active LOD index (0, 1, or 2).
    int getCurrentLOD() const { return m_currentLOD; }

private:
    // Hysteresis band widths (metres).
    // LOD0↔LOD1 uses a 5 m band; LOD1↔LOD2 uses a 10 m band.
    // Per architecture/asset-standards/3d-model-standards.md LOD Distance Thresholds table.
    static constexpr float kHysteresis01 = 5.0f;
    static constexpr float kHysteresis12 = 10.0f;

    // Non-owning pointer to the scene node (CMeshSceneNode). Lifetime controlled by
    // SceneEntityManager. Stored as IMeshSceneNode* to call setMesh(IMesh*) for LOD
    // swaps. getNode() exposes this as ISceneNode* for positioning/removal/materials.
    irr::scene::IMeshSceneNode* m_node;

    // LOD mesh pointers — borrowed from the Irrlicht mesh cache (non-owning).
    // nullptr means that LOD level is not available (e.g., billboard-only small buildings
    // have no lod2 geometry mesh).
    irr::scene::IAnimatedMesh* m_lod0{nullptr};
    irr::scene::IAnimatedMesh* m_lod1{nullptr};
    irr::scene::IAnimatedMesh* m_lod2{nullptr};  // nullptr for billboard small buildings

    // LOD distance thresholds (metres).  Set at construction from .meta lod_distances array.
    float m_lod0to1{50.0f};
    float m_lod1to2{200.0f};
    float m_cullDist{500.0f};

    // Active LOD level (0 = closest/highest detail, 2 = farthest/lowest detail).
    int m_currentLOD{0};
};
