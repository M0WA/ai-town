// LODNode.cpp — Implementation of the LOD swap sequence wrapper and hysteresis logic.
//
// swapMesh() implements the mandatory sequence from:
//   architecture/graphics-architecture/scene-graph-ownership.md (§ LOD Swap)
//
// update() implements hysteresis-based LOD switching per:
//   architecture/asset-standards/3d-model-standards.md (§ LOD Distance Thresholds)
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

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

LODNode::LODNode(irr::scene::IMeshSceneNode* node)
    : m_node(node)
    , m_lod0(nullptr)
    , m_lod1(nullptr)
    , m_lod2(nullptr)
    , m_lod0to1(50.0f)
    , m_lod1to2(200.0f)
    , m_cullDist(500.0f)
    , m_currentLOD(0)
{
    // node must be non-null; caller is responsible for providing a valid scene node.
    // Ownership remains with SceneEntityManager — LODNode never calls grab() here.
}

LODNode::LODNode(irr::scene::IMeshSceneNode* node,
                 irr::scene::SMesh*          lod0,
                 irr::scene::SMesh*          lod1,
                 irr::scene::SMesh*          lod2,
                 float                       lod0to1,
                 float                       lod1to2,
                 float                       cullDist)
    : m_node(node)
    , m_lod0(lod0)
    , m_lod1(lod1)
    , m_lod2(lod2)
    , m_lod0to1(lod0to1)
    , m_lod1to2(lod1to2)
    , m_cullDist(cullDist)
    , m_currentLOD(0)
{
    // node must be non-null; lod0 should be non-null for a functional LOD chain.
    // lod2 may be nullptr for small buildings (≤3 floors) that use billboard LOD2.
    // LODNode does not call grab() on any mesh — lifetimes owned by the asset loader.
}

// ---------------------------------------------------------------------------
// update() — hysteresis-based LOD selection
// ---------------------------------------------------------------------------

void LODNode::update(const irr::core::vector3df& cameraPos)
{
    if (!m_node) return;

    // Compute squared distance from camera to this node's world position.
    // Using getAbsolutePosition() to account for parent transforms.
    const irr::core::vector3df nodePos = m_node->getAbsolutePosition();
    const float dist = cameraPos.getDistanceFrom(nodePos);

    // ------------------------------------------------------------------
    // Hysteresis LOD selection.
    //
    // Swap-out thresholds (moving away from camera — lower quality):
    //   LOD0 → LOD1 when dist > m_lod0to1
    //   LOD1 → LOD2 when dist > m_lod1to2 (only if lod2 is available)
    //
    // Swap-in thresholds (moving toward camera — higher quality):
    //   LOD2 → LOD1 when dist < m_lod1to2 - kHysteresis12
    //   LOD1 → LOD0 when dist < m_lod0to1 - kHysteresis01
    //
    // Only one level change per update() call to avoid cascaded transitions.
    // ------------------------------------------------------------------

    int targetLOD = m_currentLOD;

    if (m_currentLOD == 0) {
        // At LOD0: check if we need to step down to LOD1.
        if (dist > m_lod0to1) {
            targetLOD = 1;
        }
    } else if (m_currentLOD == 1) {
        // At LOD1: check step-up to LOD0 or step-down to LOD2.
        if (dist < m_lod0to1 - kHysteresis01) {
            targetLOD = 0;
        } else if (m_lod2 != nullptr && dist > m_lod1to2) {
            targetLOD = 2;
        }
    } else {
        // At LOD2: check step-up to LOD1.
        if (dist < m_lod1to2 - kHysteresis12) {
            targetLOD = 1;
        }
    }

    if (targetLOD == m_currentLOD) return;  // no change needed

    // Perform the mesh swap for the new LOD level.
    irr::scene::SMesh* newMesh = nullptr;
    switch (targetLOD) {
        case 0: newMesh = m_lod0; break;
        case 1: newMesh = m_lod1; break;
        case 2: newMesh = m_lod2; break;
        default: break;
    }

    if (!newMesh) return;  // safety: nullptr mesh for requested level — stay put

    // ------------------------------------------------------------------
    // Perform the LOD swap.
    //
    // IMPORTANT: swapMesh() calls newMesh->drop() internally (Step 4 of the
    // mandatory grab/drop sequence). The LOD mesh pointers m_lod0/1/2 are
    // non-owning references managed by the asset loader — dropping here would
    // decrement the ref count by one and eventually free the mesh prematurely
    // on repeated LOD transitions.
    //
    // SOLUTION: grab() the mesh before passing to swapMesh(), so that after
    // swapMesh()'s internal drop(), the ref count returns to its prior value
    // (held by the asset loader).  This is the correct pattern when a LODNode
    // does not transfer ownership of its mesh pointers to swapMesh().
    // ------------------------------------------------------------------
    newMesh->grab();  // balance swapMesh()'s internal drop() — we are NOT transferring ownership
    swapMesh(newMesh);
    // After swapMesh() returns: newMesh has been drop()'d by swapMesh().
    // The asset loader's reference (m_lod0/1/2) is the sole remaining owner.

    m_currentLOD = targetLOD;
}

// ---------------------------------------------------------------------------
// swapMesh() — mandatory LOD swap sequence
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Accessor
// ---------------------------------------------------------------------------

irr::scene::IMeshSceneNode* LODNode::getNode() const
{
    return m_node;
}
