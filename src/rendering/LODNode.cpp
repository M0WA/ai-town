// LODNode.cpp — Implementation of the LOD swap sequence wrapper.
//
// swapMesh() implements the mandatory sequence from:
//   architecture/graphics-architecture/scene-graph-ownership.md (§ LOD Swap)
//
// Grab/drop contract (Phase 2 spike — VERIFIED):
//   Checkbox A: SMesh::addMeshBuffer() calls buf->grab(); caller must drop() after.
//   Checkbox B: CMeshSceneNode::setMesh() calls grab() on newMesh and drop() on oldMesh.
//               Verified by objdump -d of CMeshSceneNode.cpp.o from libIrrlicht.a.
//   THEREFORE:  caller MUST call newMesh->drop() after setMesh() to transfer ownership.
//               Omitting the drop() leaks the mesh (ref_count stays at 2, never reaches 0).

// GLEW before any Irrlicht/OpenGL includes — prevents symbol conflicts.
// (SceneEntityManager.h and IrrlichtRenderer.cpp use the same convention.)
#include <GL/glew.h>
#include <irrlicht.h>

#include "LODNode.h"

LODNode::LODNode(irr::scene::IMeshSceneNode* node)
    : m_node(node)
{
    // node must be non-null; caller is responsible for providing a valid scene node.
    // Ownership remains with SceneEntityManager — LODNode never calls grab() here.
}

void LODNode::swapMesh(irr::scene::SMesh* newMesh)
{
    // Step 1: recalculate bounding box on each SMeshBuffer.
    //
    // MANDATORY: call on getMeshBuffer() return value, NOT on m_node->getMesh().
    // node->getMesh() returns IMesh* — recalculateBoundingBox() is not on the IMesh
    // interface and calling it via an upcast would not compile. The newMesh SMesh*
    // is still in scope here, so we call directly on the concrete type.
    for (irr::u32 i = 0; i < newMesh->getMeshBufferCount(); ++i) {
        newMesh->getMeshBuffer(i)->recalculateBoundingBox();
    }

    // Step 2: recalculate bounding box on the SMesh itself.
    //
    // MANDATORY: must follow all per-buffer recalculations. A stale bounding box
    // from the previous LOD level causes incorrect frustum culling — the node may
    // be invisible or always visible regardless of camera position.
    //
    // recalculateBoundingBox() is on SMesh (concrete class), NOT on IMesh (interface).
    // The SMesh* type is preserved end-to-end per scene-graph-ownership.md warning
    // (§ WARNING — SMesh* Downcast Safety): never store as IMesh* and cast back.
    newMesh->recalculateBoundingBox();

    // Step 3: setMesh — Irrlicht internally calls grab() on newMesh.
    //
    // VERIFIED (Phase 2 Checkbox B): CMeshSceneNode::setMesh() increments newMesh
    // ref_count via grab() and decrements the old mesh ref_count via drop().
    // After this call: newMesh ref_count == 2 (node + caller).
    // The old mesh ref_count is decremented by setMesh's internal drop().
    //
    // Preserves node transform, rotation, scale, and material assignments.
    // No scene graph node destruction or recreation (per scene-graph-ownership.md:
    // only destroy/recreate the node on entity death or chunk unload).
    m_node->setMesh(newMesh);

    // Step 4: drop caller's reference.
    //
    // VERIFIED (Phase 2 Checkbox B): setMesh() called grab() → ref_count is now 2.
    // After drop(): ref_count drops to 1; scene node is the sole owner.
    // Omitting this drop would leak the mesh (ref_count stays at 2 and never reaches 0
    // when the scene node is later destroyed, leaving ref_count at 1 instead of 0).
    //
    // DO NOT access newMesh after this call — caller's reference is released.
    // Same rule as terrain SMesh attachment (procedural-terrain.md attachment sequence).
    newMesh->drop();
}

irr::scene::IMeshSceneNode* LODNode::getNode() const
{
    return m_node;
}
