#pragma once

// LODNode.h — Wrapper for IMeshSceneNode that implements the LOD swap sequence.
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
// Phase 9 pre-condition: this class must exist and swapMesh() must be verified by
// the SetMeshGrabDropContract test in tests/rendering/lod_swap_smoke_test.cpp
// before any Phase 9 building LOD mesh production begins.
// (ref: implementation/phase-9.md line 70)
//
// Full LOD distance / hysteresis logic is a Phase 9 deliverable, not implemented here.

#include <irrlicht.h>

// LODNode — non-owning wrapper around IMeshSceneNode providing the LOD swap sequence.
class LODNode {
public:
    // Constructor: node must be non-null and must outlive this LODNode instance.
    // Ownership of the scene node remains with SceneEntityManager.
    explicit LODNode(irr::scene::IMeshSceneNode* node);

    // Non-copyable / non-movable — wraps a raw non-owning pointer with no reference
    // counting semantics; copying would produce two objects with no clear ownership.
    LODNode(const LODNode&)            = delete;
    LODNode& operator=(const LODNode&) = delete;
    LODNode(LODNode&&)                 = delete;
    LODNode& operator=(LODNode&&)      = delete;

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

private:
    // Non-owning pointer to the scene node. Lifetime controlled by SceneEntityManager.
    irr::scene::IMeshSceneNode* m_node;
};
