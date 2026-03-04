// LODNode.cpp — Implementation of the LOD swap sequence wrapper and hysteresis logic.
//
// swapMesh() calls IAnimatedMeshSceneNode::setMesh(IAnimatedMesh*) which internally
// drops the old mesh and grabs the new mesh. The LOD mesh pointers are borrowed
// references from the Irrlicht mesh cache — no additional grab/drop by this class.
//
// Grab/drop model for borrowed mesh cache pointers:
//   - Mesh cache holds ref_count == 1 (grabbed during addMesh, dropped by createMesh).
//   - IAnimatedMeshSceneNode::setMesh() grabs new → ref_count == 2 (cache + node).
//   - setMesh() drops old  → old mesh ref_count drops by 1 (back to cache-only == 1).
//   - node->remove()       → node drops current → ref_count 2 → 1 (cache still holds).
//   DO NOT call grab()/drop() on the IAnimatedMesh* pointers — caller does not own them.
//
// NOTE: Irrlicht is compiled with -fno-rtti. dynamic_cast on Irrlicht types crashes.
// This file uses IAnimatedMesh* throughout to avoid any unsafe downcasts.

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
                 irr::scene::IAnimatedMesh*  lod0,
                 irr::scene::IAnimatedMesh*  lod1,
                 irr::scene::IAnimatedMesh*  lod2,
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
    // LODNode does not call grab() on any mesh — they are borrowed from the cache.
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
    irr::scene::IAnimatedMesh* newMesh = nullptr;
    switch (targetLOD) {
        case 0: newMesh = m_lod0; break;
        case 1: newMesh = m_lod1; break;
        case 2: newMesh = m_lod2; break;
        default: break;
    }

    if (!newMesh) return;  // safety: nullptr mesh for requested level — stay put

    // swapMesh() calls IAnimatedMeshSceneNode::setMesh() which drops the old mesh
    // and grabs the new one. No grab/drop by the caller — m_lod0/1/2 are borrowed
    // references from the Irrlicht mesh cache.
    swapMesh(newMesh);

    m_currentLOD = targetLOD;
}

// ---------------------------------------------------------------------------
// swapMesh() — LOD mesh swap via IAnimatedMeshSceneNode::setMesh()
// ---------------------------------------------------------------------------

void LODNode::swapMesh(irr::scene::IAnimatedMesh* newMesh)
{
    // CMeshSceneNode::setMesh(IMesh*) drops the old mesh and grabs the new mesh.
    // Cast IAnimatedMesh* to IMesh* (safe — IAnimatedMesh inherits from IMesh).
    // The LOD mesh pointers are borrowed from the Irrlicht mesh cache, so:
    //   - Before call: newMesh ref_count == 1 (cache) + N (scene node if active).
    //   - After call:  old mesh ref_count drops by 1; newMesh ref_count increases by 1.
    // DO NOT call grab()/drop() on newMesh — this caller does not own the reference.
    m_node->setMesh(static_cast<irr::scene::IMesh*>(newMesh));
}

// ---------------------------------------------------------------------------
// Accessor
// ---------------------------------------------------------------------------

irr::scene::ISceneNode* LODNode::getNode() const
{
    return m_node;  // implicit upcast from IMeshSceneNode* to ISceneNode*
}
