#pragma once

// LODNode.h — Wrapper for IMeshSceneNode that implements the LOD swap sequence
// and hysteresis-based LOD level selection.
//
// LODNode is a non-owning wrapper around an Irrlicht IMeshSceneNode. Lifetime of
// the wrapped scene node is controlled exclusively by SceneEntityManager (which is
// the sole caller of addXxxSceneNode() and node->remove()). LODNode never calls
// grab() or remove() on the node it wraps.
//
// swapMesh() implements the mandatory LOD swap sequence from:
//   architecture/graphics-architecture/scene-graph-ownership.md (§ LOD Swap)
//
//   1. recalculateBoundingBox() on every SMeshBuffer in newMesh.
//   2. recalculateBoundingBox() on the SMesh itself (must follow all buffer calls).
//   3. node->setMesh(newMesh)  — Irrlicht internally calls grab() → ref_count +1.
//   4. newMesh->drop()         — release caller's reference → ref_count back to 1;
//                                scene node is the sole owner.
//
// The SMesh* parameter type in swapMesh() is MANDATORY. recalculateBoundingBox()
// is a concrete method on SMesh, not on the IMesh interface. Passing IMesh* would
// fail to compile at the recalculateBoundingBox() call sites.
//
// update() implements LOD hysteresis per architecture/asset-standards/3d-model-standards.md
// LOD Distance Thresholds table. Hysteresis prevents rapid LOD flicker at threshold
// boundaries:
//   LOD0 → LOD1 swap-out when dist > lod0to1;  swap-in when dist < lod0to1 - 5 m
//   LOD1 → LOD2 swap-out when dist > lod1to2;  swap-in when dist < lod1to2 - 10 m
//
// Phase 9 pre-condition: this class must exist and swapMesh() must be verified by
// the SetMeshGrabDropContract test in tests/rendering/lod_swap_smoke_test.cpp
// before any Phase 9 building LOD mesh production begins.
// (ref: implementation/phase-9.md line 70)

#include <irrlicht.h>

// LODNode — non-owning wrapper around IMeshSceneNode providing the LOD swap
// sequence and hysteresis-based LOD level selection.
class LODNode {
public:
    // Legacy constructor: node must be non-null and must outlive this LODNode.
    // Ownership of the scene node remains with SceneEntityManager.
    // Creates a LODNode with no LOD mesh pointers — only swapMesh() is usable.
    explicit LODNode(irr::scene::IMeshSceneNode* node);

    // Full LOD constructor (Phase 9 deliverable).
    //
    // node       — the scene node to wrap (non-owning; must outlive this LODNode).
    // lod0       — highest-detail mesh (LOD0). LODNode does NOT grab() this pointer;
    //              the caller (BuildingAssetLoader) must manage its lifetime until the
    //              entity is destroyed.
    // lod1       — medium-detail mesh (LOD1). Same ownership rules as lod0.
    // lod2       — low-detail mesh (LOD2) or billboard proxy. Same ownership rules.
    //              For small buildings (≤3 floors) this may be nullptr; in that case
    //              the LOD2 transition falls back to holding at LOD1.
    // lod0to1    — swap-out threshold in metres: when camera distance exceeds this
    //              value, the node transitions from LOD0 to LOD1. Per spec table:
    //              large buildings 50 m, small buildings/props 30 m.
    // lod1to2    — swap-out threshold: LOD1 → LOD2. Per spec: large 200 m, small 100 m.
    // cullDist   — frustum/distance cull threshold (beyond this distance the node may
    //              be hidden). Stored for future use; not yet enforced by update().
    LODNode(irr::scene::IMeshSceneNode* node,
            irr::scene::SMesh*          lod0,
            irr::scene::SMesh*          lod1,
            irr::scene::SMesh*          lod2,
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
    // Implements the mandatory sequence (scene-graph-ownership.md § LOD Swap):
    //   1. recalculateBoundingBox() on every SMeshBuffer.
    //   2. recalculateBoundingBox() on the SMesh.
    //   3. node->setMesh(newMesh)  — grab() called internally by Irrlicht.
    //   4. newMesh->drop()         — caller's reference released; node is sole owner.
    //
    // Preserves the node's transform and material assignments (no scene graph
    // node destruction or recreation). Caller must NOT access newMesh after this
    // call — caller's reference has been dropped.
    //
    // newMesh MUST be typed as SMesh* (not IMesh*) at the call site. The caller
    // retains no reference to newMesh after this call returns.
    void swapMesh(irr::scene::SMesh* newMesh);

    // getNode() — accessor for the wrapped scene node.
    // Returns the non-owning pointer. Caller must not call remove() or drop() on it.
    irr::scene::IMeshSceneNode* getNode() const;

    // getCurrentLOD() — returns the active LOD index (0, 1, or 2).
    int getCurrentLOD() const { return m_currentLOD; }

private:
    // Hysteresis band widths (metres).
    // LOD0↔LOD1 uses a 5 m band; LOD1↔LOD2 uses a 10 m band.
    // Per architecture/asset-standards/3d-model-standards.md LOD Distance Thresholds table.
    static constexpr float kHysteresis01 = 5.0f;
    static constexpr float kHysteresis12 = 10.0f;

    // Non-owning pointer to the scene node. Lifetime controlled by SceneEntityManager.
    irr::scene::IMeshSceneNode* m_node;

    // LOD mesh pointers — non-owning. Lifetimes managed by the asset loader / entity owner.
    // nullptr means that LOD level is not available (e.g., billboard-only small buildings
    // have no lod2 geometry mesh).
    irr::scene::SMesh* m_lod0{nullptr};
    irr::scene::SMesh* m_lod1{nullptr};
    irr::scene::SMesh* m_lod2{nullptr};  // geometry shell; nullptr for billboard small buildings

    // LOD distance thresholds (metres).  Set at construction from .meta lod_distances array.
    float m_lod0to1{50.0f};
    float m_lod1to2{200.0f};
    float m_cullDist{500.0f};

    // Active LOD level (0 = closest/highest detail, 2 = farthest/lowest detail).
    int m_currentLOD{0};
};
