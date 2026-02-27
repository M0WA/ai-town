// IrrlichtRenderer.cpp — IRenderer concrete implementation backed by Irrlicht.
// GLEW must be included BEFORE irrlicht.h (symbol conflict mitigation).
#include <GL/glew.h>

#include <irrlicht.h>

#include "IrrlichtRenderer.h"
#include "src/ui/UIManager.h"  // FULL include here (not in header — per Header Dependency Rule)

#include <algorithm>   // std::min, std::max
#include <cstdio>      // fprintf
#include <cmath>       // M_PI

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

IrrlichtRenderer::IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
    : m_device(device)
    , m_uiManager(uiManager)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_smgr(device ? device->getSceneManager() : nullptr)
{
}

void IrrlichtRenderer::beginFrame() {
    if (!m_driver) return;
    m_driver->beginScene(true, true, SColor(255, 0, 0, 0));
}

void IrrlichtRenderer::drawScene() {
    // Per-frame sequence (must be called INSIDE beginScene/endScene pair):
    //   1. sceneManager->drawAll()  — 3D scene
    //   2. uiManager->draw()        — 2D HUD, explicit Z-order
    // NOTE: m_gui->drawAll() is NOT called — it would bypass the explicit Z-order layering
    // required for the background scrim and modal overlay (per architecture/ui-ux/ui-manager.md).
    if (m_smgr) {
        m_smgr->drawAll();
    }
    if (m_uiManager) {
        m_uiManager->draw();
    }
}

void IrrlichtRenderer::endFrame() {
    if (!m_driver) return;
    m_driver->endScene();
}

TextureHandle IrrlichtRenderer::loadTexture(const std::string& path) {
    if (!m_driver) return kInvalidTexture;

    irr::video::ITexture* tex = m_driver->getTexture(path.c_str());
    if (!tex) return kInvalidTexture;

    TextureHandle handle = m_nextHandle++;
    m_textures[handle] = tex;
    return handle;
}

void IrrlichtRenderer::setCamera(const CameraParams& p) {
    if (!m_smgr) return;

    if (!m_camera) {
        m_camera = m_smgr->addCameraSceneNode();
        if (m_camera) {
            // Remove all default animators (prevents FPS/Maya animator interference).
            // Grab each animator before removal, drop after — per scene-graph-ownership.md.
#ifndef NDEBUG
            if (m_camera->getAnimators().size() > 0) {
                fprintf(stderr,
                    "[IrrlichtRenderer] WARNING: unexpected animators on addCameraSceneNode() "
                    "result — removing %zu animator(s)\n",
                    static_cast<size_t>(m_camera->getAnimators().size()));
            }
#endif
            while (m_camera->getAnimators().size() > 0) {
                ISceneNodeAnimator* anim = *m_camera->getAnimators().begin();
                anim->grab();
                m_camera->removeAnimator(anim);
                anim->drop();
            }
        }
    }

    if (!m_camera) return;

    m_camera->setPosition(core::vector3df(p.position.x, p.position.y, p.position.z));
    m_camera->setTarget(core::vector3df(p.target.x, p.target.y, p.target.z));
    m_camera->setFOV(p.fovDegrees * static_cast<float>(M_PI / 180.0));
    m_camera->setNearValue(p.nearClip);
    m_camera->setFarValue(p.farClip);
}

void IrrlichtRenderer::rebuildTerrainChunk(const TerrainChunkRebuildParams& params) {
    if (!m_smgr || !m_driver) return;

    // -------------------------------------------------------------------------
    // Step 1: Remove the old scene node for this chunk (if any).
    //
    // Per scene-graph-ownership.md eviction sequence:
    //   a. Iterate material slots — clear all texture pointers.
    //      (Phase 5 terrain chunks have no assigned textures; this loop is a
    //      defensive no-op but is required for correctness when Phase 6+
    //      terrain texturing is added.)
    //   b. driver->setMaterial(SMaterial{}) — flushes driver last-bound state.
    //   c. Null the node pointer BEFORE calling node->remove() — prevents
    //      any dangling-pointer access if downstream code holds a copy.
    //      After remove() the node belongs to Irrlicht's reference counting;
    //      do NOT access it after the pointer is passed to remove().
    // -------------------------------------------------------------------------
    auto nodeIt = m_chunkNodes.find(params.chunkId);
    if (nodeIt != m_chunkNodes.end()) {
        IMeshSceneNode* oldNode = nodeIt->second;

        // Step 1a: clear material texture slots.
        u32 matCount = oldNode->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            // MANDATORY (C-4 rule from scene-graph-ownership.md): getMaterial(m) called
            // exactly once per outer loop iteration and cached as SMaterial&.
            SMaterial& mat = oldNode->getMaterial(m);
            for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }

        // Step 1b: flush driver last-bound material state.
        m_driver->setMaterial(SMaterial{});

        // Step 1c: null the map entry BEFORE remove() — dangling-pointer prevention.
        nodeIt->second = nullptr;
        m_chunkNodes.erase(nodeIt);
        oldNode->remove();  // do NOT access oldNode after this line
    }

    // -------------------------------------------------------------------------
    // Step 2: Build a new SMesh* at the target LOD grid resolution.
    //
    // Uses the same vertex/index layout as TerrainChunk::buildMesh():
    //   - One SMeshBuffer per chunk (entire chunk in one draw call).
    //   - S3DVertex: position, normal, SColor(255,255,255,255), UV [0,1].
    //   - Row-major vertex order: vertex[z*(gridSize+1)+x].
    //   - CCW winding: Triangle 1 = v0,v1,v2; Triangle 2 = v0,v2,v3.
    //   - Normals computed from finite differences (central for interior vertices,
    //     clamped for boundary vertices).
    //
    // The SMesh is created with ref_count = 1 (caller owns).
    // SMeshBuffer is grab()'d by addMeshBuffer() → caller drops it immediately.
    // -------------------------------------------------------------------------
    SMesh*       smesh = new SMesh();     // ref_count = 1
    SMeshBuffer* buf   = new SMeshBuffer(); // ref_count = 1

    const int gridSize  = params.gridSize;
    const int verts     = gridSize + 1;           // vertex count per side
    const int vertCount = verts * verts;
    const int quadCount = gridSize * gridSize;
    const int idxCount  = quadCount * 6;          // 2 triangles × 3 indices per quad

    buf->Vertices.reallocate(static_cast<u32>(vertCount));
    buf->Indices.reallocate(static_cast<u32>(idxCount));

    const std::vector<float>& hmap = params.heightmap;

    // Build vertex array — identical layout to TerrainChunk::buildMesh().
    for (int z = 0; z < verts; ++z) {
        for (int x = 0; x < verts; ++x) {
            float h = hmap[static_cast<size_t>(z * verts + x)];

            core::vector3df pos(
                static_cast<f32>(x) * params.cellSize,
                h,
                static_cast<f32>(z) * params.cellSize
            );

            // Normal from central finite differences; boundary vertices clamp to edge.
            int xR = std::min(x + 1, gridSize);
            int xL = std::max(x - 1, 0);
            int zD = std::min(z + 1, gridSize);
            int zU = std::max(z - 1, 0);

            float hR = hmap[static_cast<size_t>(z  * verts + xR)];
            float hL = hmap[static_cast<size_t>(z  * verts + xL)];
            float hDn= hmap[static_cast<size_t>(zD * verts + x )];
            float hUp= hmap[static_cast<size_t>(zU * verts + x )];

            float dX = (hR - hL) * 0.5f / params.cellSize;
            float dZ = (hDn - hUp) * 0.5f / params.cellSize;
            core::vector3df normal(-dX, 1.0f, -dZ);
            normal.normalize();

            core::vector2df uv(
                static_cast<f32>(x) / static_cast<f32>(gridSize),
                static_cast<f32>(z) / static_cast<f32>(gridSize)
            );

            buf->Vertices.push_back(S3DVertex(pos, normal, SColor(255, 255, 255, 255), uv));
        }
    }

    // Build index array — CCW winding, two triangles per quad.
    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            u32 v0 = static_cast<u32>(row       * verts + col);
            u32 v1 = static_cast<u32>(row       * verts + col + 1);
            u32 v2 = static_cast<u32>((row + 1) * verts + col + 1);
            u32 v3 = static_cast<u32>((row + 1) * verts + col);

            buf->Indices.push_back(v0);
            buf->Indices.push_back(v1);
            buf->Indices.push_back(v2);

            buf->Indices.push_back(v0);
            buf->Indices.push_back(v2);
            buf->Indices.push_back(v3);
        }
    }

    // -------------------------------------------------------------------------
    // Step 3: recalculateBoundingBox() — MANDATORY before addMeshSceneNode().
    //
    // Per procedural-terrain.md and CLAUDE.md:
    //   Call on every SMeshBuffer first, THEN on the SMesh.
    //   Omitting this leaves a degenerate bounding box at origin — frustum culling
    //   incorrectly discards the chunk when the camera is not near (0,0,0).
    // -------------------------------------------------------------------------
    buf->recalculateBoundingBox();

    // Transfer buffer ownership to the mesh.
    // SMesh::addMeshBuffer() calls grab() on buf → ref_count becomes 2.
    // We then drop() our reference → ref_count returns to 1 (mesh is sole owner).
    smesh->addMeshBuffer(buf);
    buf->drop();  // ref_count 2→1; smesh is now the sole buffer owner

    // Recalculate SMesh bounding box from all its buffers (must come AFTER buffer recalc).
    smesh->recalculateBoundingBox();

    // -------------------------------------------------------------------------
    // Step 4: addMeshSceneNode(smesh) + smesh->drop().
    //
    // addMeshSceneNode() calls grab() on smesh → ref_count becomes 2.
    // smesh->drop() releases the caller's reference → ref_count returns to 1.
    // The scene node is now the sole owner of the mesh.
    // NEVER call drop() before addMeshSceneNode() — that would free the mesh
    // before the scene node acquires it.
    //
    // Set world translation so the rebuilt chunk occupies its correct footprint.
    // -------------------------------------------------------------------------
    IMeshSceneNode* newNode = m_smgr->addMeshSceneNode(smesh);
    smesh->drop();  // ref_count 2→1; scene node is now sole mesh owner

    if (newNode) {
        newNode->setPosition(core::vector3df(
            params.worldOriginX, 0.0f, params.worldOriginZ));
        newNode->setMaterialFlag(EMF_LIGHTING, false);  // unlit until Phase 6 lighting pass

        // -------------------------------------------------------------------------
        // Step 5: Register the new node in the chunk node map.
        // -------------------------------------------------------------------------
        m_chunkNodes[params.chunkId] = newNode;
    }
}
