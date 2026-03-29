// IrrlichtRenderer.cpp — IRenderer concrete implementation backed by Irrlicht.
// GLEW must be included BEFORE irrlicht.h (symbol conflict mitigation).
#include <GL/glew.h>

#include <irrlicht.h>

#include "IrrlichtRenderer.h"
#include "src/ui/UIManager.h"            // FULL include here (not in header — per Header Dependency Rule)
#include "src/interfaces/ITerrainQuery.h" // ITerrainQuery full include (forward-decl in header only)
#include "BuildingAssetLoader.h"          // Phase 10: load .b3d asset families via BuildingAssetLoader::load()
#include "LODNode.h"                      // Phase 10: LOD swap wrapper returned by BuildingAssetLoader::load()
#include "TextureCache.h"                 // Phase 10: sRGB texture loading for road diffuse
#include "RoadShaderCallback.h"           // Phase 10: road.vert/road.frag shader callback
#include "render_constants.h"             // Phase 10: RenderConstants::road_lod2_color
#include "TerrainShaderCallback.h"        // Phase 10c: terrain splat-map shader callback
#include "RenderSystem.h"                 // Phase 10c: isSRGBTextureSupported() query

#include <algorithm>   // std::min, std::max
#include <cstdio>      // fprintf
#include <cmath>       // M_PI
#include <string>      // std::string for asset path construction
#include <queue>       // std::queue — BFS for PowerPlant coverage overlay
#include <vector>      // std::vector — BFS visited table

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

// -------------------------------------------------------------------------
// CloudDomeShaderCallback — IShaderConstantSetCallBack for the cloud dome
// GLSL shader.  Sets u_tex (sampler2D, unit 0) on every draw call.
//
// u_cameraY has been REMOVED: the dome node tracks the camera's full XYZ
// position each frame (setPosition(camPos) in update()), so the vertex shader
// receives gl_Vertex already in camera-relative local space.  No world-Y
// offset is needed in the shader.
//
// Lifetime rule (per shader-loading.md and CLAUDE.md):
//   This callback is kept alive by the caller (IrrlichtRenderer) so that it
//   can be dropped in the destructor (stored as void* m_cloudShaderCbRaw in
//   the header).  Irrlicht also calls grab() internally, so the final drop
//   happens when the material renderer is destroyed.
//   Never use std::unique_ptr — causes double-free.
// -------------------------------------------------------------------------
class CloudDomeShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 /*userData*/) override
    {
        // Bind u_tex to texture unit 0.  The cloud texture is always on unit 0
        // (single-texture material); this call is required even though 0 is the
        // default — some GLSL drivers report a warning if the sampler uniform is
        // never explicitly set.
        irr::s32 tex = 0;
        services->setPixelShaderConstant("u_tex", &tex, 1);
    }
};

IrrlichtRenderer::IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
    : m_device(device)
    , m_uiManager(uiManager)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_smgr(device ? device->getSceneManager() : nullptr)
    , m_logger(device ? device->getLogger() : nullptr)
{
    // Capture the driver type immediately — used by initCloudPlane() to guard
    // against EDT_NULL headless contexts (per sky-clouds.md Headless/EDT_NULL Guard).
    m_driverType = m_device ? m_device->getVideoDriver()->getDriverType()
                             : irr::video::EDT_NULL;
    // Allocate the hover tile mesh ONCE at construction time.
    // SMesh and SMeshBuffer are reference-counted Irrlicht objects that do NOT
    // require a driver — they can be safely allocated when m_driver is null
    // (e.g. unit tests that construct IrrlichtRenderer with a null device).
    // Pre-populate 4 vertices + 6 indices so setTileHoverHighlight() only updates
    // vertex positions in-place rather than rebuilding the mesh each frame.
    m_hoveredTileMesh = new SMesh();
    SMeshBuffer* buf = new SMeshBuffer();

    // Four placeholder vertices — positions are overwritten per-frame by setTileHoverHighlight().
    for (int v = 0; v < 4; ++v) {
        buf->Vertices.push_back(S3DVertex(
            core::vector3df(0, 0, 0),
            core::vector3df(0, 1, 0),
            SColor(255, 255, 255, 255),
            core::vector2df(0, 0)));
    }
    // Two triangles: v0→v2→v1, v0→v3→v2 (CW from above = +Y normal)
    buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
    buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

    buf->Material.MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL;
    buf->Material.Lighting     = false;
    buf->Material.ZWriteEnable = false;

    m_hoveredTileMesh->addMeshBuffer(buf);
    buf->drop();  // mesh is sole owner

    m_hoverBuffer = static_cast<SMeshBuffer*>(
        m_hoveredTileMesh->getMeshBuffer(0));  // non-owning observer

    // Phase 10b: build the scrolling cloud plane (no-op under EDT_NULL).
    initCloudPlane();
}

IrrlichtRenderer::~IrrlichtRenderer() {
    // Drop our reference to the cloud dome shader callback.  Irrlicht holds its own
    // grab() reference internally; this drop releases only the caller's reference
    // (retained in initCloudPlane() for the callback's lifetime).
    // Null check covers headless runs where the shader compile failed.
    if (m_cloudShaderCbRaw) {
        static_cast<CloudDomeShaderCallback*>(m_cloudShaderCbRaw)->drop();
        m_cloudShaderCbRaw = nullptr;
    }

    // Drop the hover tile mesh (ref_count 1→0 frees the mesh and its contained buffer).
    // Allocated unconditionally in the constructor — m_hoveredTileMesh is always non-null.
    if (m_hoveredTileMesh) {
        m_hoveredTileMesh->drop();
        m_hoveredTileMesh = nullptr;
        m_hoverBuffer     = nullptr;  // non-owning observer — already freed by the mesh drop
    }

    // Drop the placement preview mesh (null until first non-empty setTilePlacementPreview call).
    if (m_previewMesh) {
        m_previewMesh->drop();
        m_previewMesh = nullptr;
    }
    // m_overlayNode is owned by the Irrlicht scene graph and is removed automatically
    // when the scene manager is destroyed.  We do not call remove() here because the
    // device/smgr may already be in a partially-torn-down state by the time this
    // destructor runs.

    // Phase 10: clean up all LODNode wrappers in building and road registries.
    // LODNode objects are heap-allocated by BuildingAssetLoader::load() and owned by
    // IrrlichtRenderer. Deleting a LODNode does NOT remove its wrapped scene node
    // (scene node lifetime is managed by the Irrlicht scene graph, which is torn down
    // by device->drop() in main.cpp AFTER IrrlichtRenderer is destroyed).
    // We only delete the wrapper objects here; the underlying scene nodes are cleaned
    // up when the scene manager is destroyed.
    for (auto& kv : m_buildingNodes) {
        delete kv.second;
    }
    m_buildingNodes.clear();

    for (auto& kv : m_roadNodes) {
        delete kv.second;
    }
    m_roadNodes.clear();

    // Drop shared procedural road meshes (each ref_count 1 → 0 → freed).
    if (m_sharedRoadMeshLOD0) { m_sharedRoadMeshLOD0->drop(); m_sharedRoadMeshLOD0 = nullptr; }
    if (m_sharedRoadMeshLOD1) { m_sharedRoadMeshLOD1->drop(); m_sharedRoadMeshLOD1 = nullptr; }
    if (m_sharedRoadMeshLOD2) { m_sharedRoadMeshLOD2->drop(); m_sharedRoadMeshLOD2 = nullptr; }

    // Phase 10: clean up all LODNode wrappers in the vehicle registry.
    // Same rationale as building/road cleanup above: only delete the C++ wrapper;
    // the underlying Irrlicht scene nodes are destroyed by device->drop() in main.cpp.
    for (auto& kv : m_vehicleNodes) {
        delete kv.second;
    }
    m_vehicleNodes.clear();

    // Phase 11d: agent nodes — clear map (scene nodes destroyed by scene manager).
    m_agentNodes.clear();

    // Phase 11d: signal nodes — clear map (scene nodes destroyed by scene manager).
    m_signalNodes.clear();

    // Phase 11d: coverage overlay node — clear pointer (destroyed by scene manager).
    m_coverageOverlayNode = nullptr;

    // m_roadTextureCache, m_buildingAssetLoader, m_vehicleAssetLoader (unique_ptrs)
    // destroyed automatically.
}

// -------------------------------------------------------------------------
// clearCity — Phase 11m new-game reset.
//
// Removes all building, road, and agent scene nodes from the scene graph,
// running the full eviction sequence on each. Does NOT remove terrain chunk
// nodes. Shared procedural road meshes (LOD1/LOD2) are emptied so they are
// ready for the next game session. Does not drop the SMesh* itself — it is
// reused across sessions.
// -------------------------------------------------------------------------
void IrrlichtRenderer::clearCity() {
    // --- Building nodes (zone buildings + service buildings) ---
    // LODNode* wrappers are heap-allocated — delete them after eviction.
    // Do NOT call destroyTileNode() during iteration (invalidates the iterator).
    for (auto& kv : m_buildingNodes) {
        LODNode* lodNode = kv.second;
        if (!lodNode) continue;
        scene::ISceneNode* node = lodNode->getNode();
        if (node) {
            // Clear material texture slots.
            for (u32 m = 0; m < node->getMaterialCount(); ++m) {
                for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                    node->getMaterial(m).setTexture(t, nullptr);
                }
            }
            if (m_driver) m_driver->setMaterial(SMaterial{});
            node->remove();
        }
        delete lodNode;
    }
    m_buildingNodes.clear();

    // --- Road nodes ---
    // LODNode* wrappers are heap-allocated — delete them after eviction.
    for (auto& kv : m_roadNodes) {
        LODNode* lodNode = kv.second;
        if (!lodNode) continue;
        scene::ISceneNode* node = lodNode->getNode();
        if (node) {
            for (u32 m = 0; m < node->getMaterialCount(); ++m) {
                for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                    node->getMaterial(m).setTexture(t, nullptr);
                }
            }
            if (m_driver) m_driver->setMaterial(SMaterial{});
            node->remove();
        }
        delete lodNode;
    }
    m_roadNodes.clear();

    // --- Agent nodes (plain IMeshSceneNode*, no LODNode wrapper) ---
    for (auto& kv : m_agentNodes) {
        scene::IMeshSceneNode* node = kv.second;
        if (!node) continue;
        for (u32 i = 0; i < node->getMaterialCount(); ++i) {
            for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                node->getMaterial(i).setTexture(t, nullptr);
            }
        }
        if (m_driver) m_driver->setMaterial(SMaterial{});
        node->remove();
    }
    m_agentNodes.clear();

    // --- Shared procedural road meshes (LOD1 and LOD2 are shared / reused) ---
    // LOD0 is nullptr (per-tile meshes owned by scene nodes — already removed above).
    // LOD1 and LOD2: clear mesh buffers so addRoadTile() rebuilds them fresh.
    // Do NOT ->drop() the SMesh* itself — it is reused in the next game session.
    // (m_sharedRoadMeshLOD0 is always nullptr — no work needed.)
    if (m_sharedRoadMeshLOD1) {
        for (u32 i = 0; i < m_sharedRoadMeshLOD1->getMeshBufferCount(); ++i) {
            m_sharedRoadMeshLOD1->getMeshBuffer(i)->drop();
        }
        m_sharedRoadMeshLOD1->MeshBuffers.clear();
        m_sharedRoadMeshLOD1->recalculateBoundingBox();
    }
    if (m_sharedRoadMeshLOD2) {
        for (u32 i = 0; i < m_sharedRoadMeshLOD2->getMeshBufferCount(); ++i) {
            m_sharedRoadMeshLOD2->getMeshBuffer(i)->drop();
        }
        m_sharedRoadMeshLOD2->MeshBuffers.clear();
        m_sharedRoadMeshLOD2->recalculateBoundingBox();
    }

    // --- Zone overlay node ---
    // Must be cleared so the old game's preview quads don't appear at wrong
    // heights on the new terrain (they would float in mid-air after resize).
    if (m_overlayNode) {
        u32 matCount = m_overlayNode->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            SMaterial& mat = m_overlayNode->getMaterial(m);
            for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        m_overlayNode->remove();
        m_overlayNode = nullptr;
    }
}

void IrrlichtRenderer::beginFrame() {
    if (!m_driver) return;
    // Sky-blue clear color — provides visual feedback that the 3D viewport is active.
    // Pure black (0,0,0) is indistinguishable from "nothing rendered".
    m_driver->beginScene(true, true, SColor(255, 100, 149, 237));
}

void IrrlichtRenderer::drawScene() {
    // Per-frame sequence (must be called INSIDE beginScene/endScene pair):
    //   1. sceneManager->drawAll()     — 3D scene
    //   2. uiManager->draw()           — update panel element states (visibility, text, alpha)
    //   3. guiEnvironment->drawAll()   — render all visible GUI elements
    //
    // Step 2 sets visibility/text/alpha on every panel's elements in explicit Z-order
    // (slots 1-10 per ui-manager.md). Non-active panels hide their elements, so
    // step 3's IGUIEnvironment::drawAll() only renders what should be visible.
    // The Z-order concern is addressed by visibility management — panels that should
    // be behind (e.g. main menu during gameplay) have their elements hidden.
    if (m_smgr) {
        m_smgr->drawAll();
    }

    // Phase 9b: draw hover tile highlight immediately after 3D scene, before 2D GUI.
    // The hover mesh is NOT in the scene graph — we issue a raw drawMeshBuffer() call.
    // Guard: mesh must exist, be non-null, and m_hoverVisible must be true.
    if (m_hoveredTileMesh && m_hoverVisible && m_driver) {
        IMeshBuffer* hoverBuf = m_hoveredTileMesh->getMeshBuffer(0);
        if (hoverBuf) {
            m_driver->setMaterial(hoverBuf->getMaterial());
            m_driver->setTransform(ETS_WORLD, core::IdentityMatrix);
            m_driver->drawMeshBuffer(hoverBuf);
        }
    }

    // Phase 10: draw multi-tile placement preview (Zone rect / Road line).
    // Rendered after the single-tile hover highlight so it appears on top.
    // Each SMeshBuffer in m_previewMesh is one quad; iterate all buffers.
    if (m_previewMesh && m_previewVisible && m_driver) {
        m_driver->setTransform(ETS_WORLD, core::IdentityMatrix);
        for (u32 i = 0; i < m_previewMesh->getMeshBufferCount(); ++i) {
            IMeshBuffer* buf = m_previewMesh->getMeshBuffer(i);
            if (buf) {
                m_driver->setMaterial(buf->getMaterial());
                m_driver->drawMeshBuffer(buf);
            }
        }
    }

    // Scene background: drawn after all 3D scene content but before the GUI layer.
    // Active during main menu and loading transitions (set via setSceneBackground).
    // Covers the otherwise-empty 3D viewport; GUI buttons appear on top because
    // guiEnv->drawAll() runs after this call.
    if (!m_bgTexturePath.empty()) {
        drawFullscreenTexture(m_bgTexturePath);
    }

    if (m_uiManager) {
        m_uiManager->draw();
    }
    // Render all visible GUI elements. UIManager::draw() has already set the
    // correct visibility state on every element; drawAll() paints them.
    if (m_device) {
        irr::gui::IGUIEnvironment* guiEnv = m_device->getGUIEnvironment();
        if (guiEnv) {
            guiEnv->drawAll();
        }
    }
}

void IrrlichtRenderer::endFrame() {
    if (!m_driver) return;
    m_driver->endScene();
}

void IrrlichtRenderer::drawFullscreenTexture(const std::string& path)
{
    if (!m_driver) return;
    irr::video::ITexture* tex = m_driver->getTexture(path.c_str());
    if (!tex) return;
    const irr::core::dimension2d<irr::u32> screenDim = m_driver->getScreenSize();
    const irr::core::dimension2d<irr::u32> texDim    = tex->getOriginalSize();
    m_driver->draw2DImage(
        tex,
        irr::core::rect<irr::s32>(0, 0,
            static_cast<irr::s32>(screenDim.Width),
            static_cast<irr::s32>(screenDim.Height)),
        irr::core::rect<irr::s32>(0, 0,
            static_cast<irr::s32>(texDim.Width),
            static_cast<irr::s32>(texDim.Height)));
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
                if (m_device && m_device->getLogger()) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                        "[IrrlichtRenderer] WARNING: unexpected animators on addCameraSceneNode() "
                        "result - removing %zu animator(s)",
                        static_cast<size_t>(m_camera->getAnimators().size()));
                    m_device->getLogger()->log(buf, irr::ELL_WARNING);
                }
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
    m_lastCameraPosition = p.position;  // cached for getListenerPosition()
}

void IrrlichtRenderer::removeTerrainChunk(uint64_t chunkId) {
    auto nodeIt = m_chunkNodes.find(chunkId);
    if (nodeIt == m_chunkNodes.end()) return;

    IMeshSceneNode* node = nodeIt->second;
    if (node) {
        // Eviction sequence per scene-graph-ownership.md:
        // Step 1a: clear material texture slots.
        u32 matCount = node->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                node->getMaterial(m).setTexture(t, nullptr);
            }
        }
        // Step 1b: flush driver last-bound material state.
        if (m_driver) m_driver->setMaterial(SMaterial{});
        // Step 1c: null the map entry BEFORE remove() — dangling-pointer prevention.
        nodeIt->second = nullptr;
        m_chunkNodes.erase(nodeIt);
        node->remove();
    } else {
        m_chunkNodes.erase(nodeIt);
    }
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

            // Height-based vertex colour: interpolate from forest green (lowlands)
            // to brown (highlands) so terrain is clearly visible against the sky.
            // Phase 9 replaces this with textured materials.
            float normH = std::clamp(h / 80.0f, 0.0f, 1.0f);  // normalize to [0,1] over ~80m range
            u8 r = static_cast<u8>(34  + normH * (139 - 34));   // 34→139
            u8 g = static_cast<u8>(139 - normH * (139 - 90));   // 139→90
            u8 b = static_cast<u8>(34  - normH * (34  - 20));   // 34→20
            buf->Vertices.push_back(S3DVertex(pos, normal, SColor(255, r, g, b), uv));
        }
    }

    // Build index array — CW winding from above (left-handed Y-up), two triangles per quad.
    //
    // Irrlicht uses a LEFT-HANDED coordinate system. Front faces are CW from the viewer.
    // Terrain is viewed from above (+Y looking toward -Y), so front-face normals must
    // point UP (+Y). The winding v0→v2→v1 / v0→v3→v2 produces upward normals:
    //   (v2-v0)×(v1-v0) = (cs,0,cs)×(cs,0,0) = (0, +cs², 0) → +Y normal.
    // The previous v0→v1→v2 winding produced DOWNWARD normals, causing all terrain
    // faces to be backface-culled when the camera is above the terrain.
    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            u32 v0 = static_cast<u32>(row       * verts + col);
            u32 v1 = static_cast<u32>(row       * verts + col + 1);
            u32 v2 = static_cast<u32>((row + 1) * verts + col + 1);
            u32 v3 = static_cast<u32>((row + 1) * verts + col);

            // Triangle 1: v0→v2→v1 (normal = +Y)
            buf->Indices.push_back(v0);
            buf->Indices.push_back(v2);
            buf->Indices.push_back(v1);

            // Triangle 2: v0→v3→v2 (normal = +Y)
            buf->Indices.push_back(v0);
            buf->Indices.push_back(v3);
            buf->Indices.push_back(v2);
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
        newNode->setMaterialFlag(EMF_LIGHTING, false);  // material type set in Phase 10c — see initTerrainShader()
        newNode->setMaterialFlag(EMF_BACK_FACE_CULLING, false);  // both sides visible — Phase 5 has no winding-dependent lighting

        // Assign terrain splat shader material type if available (Phase 10c).
        irr::video::SMaterial& mat = newNode->getMaterial(0);
        if (m_terrainMaterialType != -1) {
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(m_terrainMaterialType);
        }
        // EMF_LIGHTING stays false — per-pixel lighting is Phase 11+
        mat.setFlag(irr::video::EMF_LIGHTING, false);
        mat.setFlag(irr::video::EMF_BACK_FACE_CULLING, false);

        // -------------------------------------------------------------------------
        // Step 5: Register the new node in the chunk node map.
        // -------------------------------------------------------------------------
        m_chunkNodes[params.chunkId] = newNode;
    }
}

// -------------------------------------------------------------------------
// pickTerrainTile — O(1) DDA grid traversal (Amanatides & Woo 1987).
//
// Normative algorithm per
// architecture/graphics-architecture/procedural-terrain.md —
// "pickTerrainTile DDA Algorithm".
//
// Performance: at most (mapTilesX + mapTilesZ) iterations, each O(1).
// Worst case on a 1024×1024 map: 2048 × ~15 ns = ~30 µs per call.
// At 10 MouseMove events/frame at 60 FPS: ~300 µs — within the 1 ms budget.
// -------------------------------------------------------------------------
bool IrrlichtRenderer::pickTerrainTile(int screenX, int screenY,
                                        int& tileX, int& tileZ) const
{
    if (!m_terrain)               return false;
    if (!m_smgr)                  return false;
    if (m_mapTilesX <= 0 || m_mapTilesZ <= 0) return false;
    if (m_cellSize <= 0.0f)       return false;

    // Use the scene manager's active camera — IrrlichtRenderer::m_camera is only
    // populated if setCamera() is called, but in the current architecture the
    // camera scene node is owned directly by CameraController and is the active
    // camera in the scene graph.  getActiveCamera() returns that node reliably.
    irr::scene::ICameraSceneNode* cam = m_smgr->getActiveCamera();
    if (!cam) return false;

    // Obtain world-space ray through the screen pixel.
    irr::core::line3df ray =
        m_smgr->getSceneCollisionManager()
            ->getRayFromScreenCoordinates(
                irr::core::position2di(screenX, screenY), cam);

    irr::core::vector3df ro = ray.start;
    irr::core::vector3df rd = (ray.end - ray.start);
    rd.normalize();

    // A near-horizontal ray cannot usefully intersect the terrain grid — bail early.
    if (std::fabs(rd.Y) < 1e-5f) return false;

    // ---- DDA Step 1: Starting tile (clamp to map) ----
    int cx = static_cast<int>(ro.X / m_cellSize);
    int cz = static_cast<int>(ro.Z / m_cellSize);
    cx = std::max(0, std::min(cx, m_mapTilesX - 1));
    cz = std::max(0, std::min(cz, m_mapTilesZ - 1));

    // ---- DDA Step 2: Step direction per axis ----
    int stepX = (rd.X >= 0.0f) ? 1 : -1;
    int stepZ = (rd.Z >= 0.0f) ? 1 : -1;

    // ---- DDA Step 3: Distance to first boundary crossing ----
    float tMaxX, tMaxZ;
    if (std::fabs(rd.X) < 1e-6f) {
        tMaxX = 1e30f;  // axis-aligned in Z — no X crossings
    } else {
        float nextBoundX = (stepX > 0)
            ? (static_cast<float>(cx + 1) * m_cellSize)
            : (static_cast<float>(cx)     * m_cellSize);
        tMaxX = (nextBoundX - ro.X) / rd.X;
    }
    if (std::fabs(rd.Z) < 1e-6f) {
        tMaxZ = 1e30f;
    } else {
        float nextBoundZ = (stepZ > 0)
            ? (static_cast<float>(cz + 1) * m_cellSize)
            : (static_cast<float>(cz)     * m_cellSize);
        tMaxZ = (nextBoundZ - ro.Z) / rd.Z;
    }

    // ---- DDA Step 4: Per-axis delta (consecutive-boundary distance) ----
    float tDeltaX = (std::fabs(rd.X) < 1e-6f) ? 1e30f : std::fabs(m_cellSize / rd.X);
    float tDeltaZ = (std::fabs(rd.Z) < 1e-6f) ? 1e30f : std::fabs(m_cellSize / rd.Z);

    // ---- DDA Step 5: Traverse at most (mapTilesX + mapTilesZ) cells ----
    int maxSteps = m_mapTilesX + m_mapTilesZ;
    for (int i = 0; i < maxSteps; ++i) {
        // Ray parameter t at the centre of the current cell.
        float tc = std::min(tMaxX, tMaxZ) - 0.5f * std::min(tDeltaX, tDeltaZ);
        tc = std::max(tc, 0.0f);

        // Ray Y at this cell's approximate centre.
        float rayY = ro.Y + tc * rd.Y;

        // Sample terrain height at the current cell.
        float h = m_terrain->getHeightAt(cx, cz);

        if (rayY <= h) {
            // Hit — ray has entered or gone below terrain at this cell.
            tileX = std::max(0, std::min(cx, m_mapTilesX - 1));
            tileZ = std::max(0, std::min(cz, m_mapTilesZ - 1));
            return true;
        }

        // Advance to the next cell boundary.
        if (tMaxX < tMaxZ) {
            cx    += stepX;
            tMaxX += tDeltaX;
        } else {
            cz    += stepZ;
            tMaxZ += tDeltaZ;
        }

        // Exit map bounds — ray has left the terrain grid.
        if (cx < 0 || cx >= m_mapTilesX || cz < 0 || cz >= m_mapTilesZ)
            return false;
    }

    return false;  // Traversed all cells without intersecting terrain.
}

// -------------------------------------------------------------------------
// setTileHoverHighlight — update the hover-quad mesh buffer in-place.
//
// footprintSize: 1=1×1, 2=2×2, 3=3×3. Quad covers the full NxN footprint.
// Color is selected from m_activeTool:
//   Zone    → semi-transparent green (0x6600FF00)
//   Demolish→ semi-transparent red   (0x66FF0000)
//   Other   → semi-transparent white (0x66FFFFFF)
//
// Pass tileX = -1 to clear (sets m_hoverVisible = false without touching
// the buffer, so the next valid call can immediately overwrite).
//
// The actual drawMeshBuffer() call is deferred to drawScene(), after
// sceneManager->drawAll(), per the Phase 9b per-frame sequence in
// architecture/graphics-architecture/irrlicht-device-lifecycle.md.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setTileHoverHighlight(int tileX, int tileZ, int footprintSize)
{
    // Clear request.
    if (tileX < 0) {
        m_hoverVisible = false;
        return;
    }
    if (!m_terrain || !m_driver) {
        m_hoverVisible = false;
        return;
    }

    // Clamp footprint to valid range [1, 3].
    const int N = (footprintSize < 1) ? 1 : (footprintSize > 3) ? 3 : footprintSize;

    // Select color from active tool.
    SColor colour(0x66, 0xFF, 0xFF, 0xFF);  // default: semi-transparent white
    switch (m_activeTool) {
        case ToolMode::Zone: {
            // Use the per-zone-type colour stored by setZoneHoverColour().
            colour = SColor(m_zoneHoverArgb);
            break;
        }
        case ToolMode::Demolish: colour = SColor(0x66, 0xFF, 0x00, 0x00); break;  // red
        default: break;
    }

    // Build the four footprint-corner positions slightly above terrain surface.
    float yOffset = 0.1f;  // 10 cm above terrain to avoid Z-fighting
    // v0: bottom-left corner of footprint (tileX, tileZ)
    // v2: top-right corner of footprint (tileX+N-1, tileZ+N-1) — but vertex coords
    //     use the far corner of those tiles (i.e. tileX+N, tileZ+N vertices).
    float x0 = static_cast<float>(tileX)     * m_cellSize;
    float x1 = static_cast<float>(tileX + N) * m_cellSize;
    float z0 = static_cast<float>(tileZ)     * m_cellSize;
    float z1 = static_cast<float>(tileZ + N) * m_cellSize;

    float h00 = m_terrain->getHeightAt(tileX,     tileZ)     + yOffset;
    float h10 = m_terrain->getHeightAt(tileX + N, tileZ)     + yOffset;
    float h11 = m_terrain->getHeightAt(tileX + N, tileZ + N) + yOffset;
    float h01 = m_terrain->getHeightAt(tileX,     tileZ + N) + yOffset;

    // Update vertex positions and colours in-place.
    m_hoverBuffer->Vertices[0].Pos   = core::vector3df(x0, h00, z0);
    m_hoverBuffer->Vertices[0].Color = colour;
    m_hoverBuffer->Vertices[1].Pos   = core::vector3df(x1, h10, z0);
    m_hoverBuffer->Vertices[1].Color = colour;
    m_hoverBuffer->Vertices[2].Pos   = core::vector3df(x1, h11, z1);
    m_hoverBuffer->Vertices[2].Color = colour;
    m_hoverBuffer->Vertices[3].Pos   = core::vector3df(x0, h01, z1);
    m_hoverBuffer->Vertices[3].Color = colour;

    // Mandatory bounding box update after vertex position changes.
    m_hoverBuffer->recalculateBoundingBox();
    m_hoveredTileMesh->recalculateBoundingBox();

    m_hoverVisible = true;
}

// -------------------------------------------------------------------------
// setActiveTool — store the active tool for hover-highlight color selection.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setActiveTool(ToolMode mode)
{
    m_activeTool = mode;
}

// -------------------------------------------------------------------------
// setZoneHoverColour — store the ARGB colour for zone-tool hover highlights.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setZoneHoverColour(unsigned int argb)
{
    m_zoneHoverArgb = argb;
}

// -------------------------------------------------------------------------
// clearDemolishHighlight — clear any pending demolition highlight.
// -------------------------------------------------------------------------
void IrrlichtRenderer::clearDemolishHighlight()
{
    setTileHoverHighlight(-1, -1, 1);
}

// -------------------------------------------------------------------------
// setTilePlacementPreview — rebuild the multi-tile placement preview mesh.
//
// Called every MouseMove while LMB is held for Zone (rect) and Road (line)
// tools.  Each call drops the old preview mesh (if any) and builds a fresh
// SMesh* containing one SMeshBuffer quad per tile.  This is a cold-ish path
// (at most one rebuild per mouse-move event) so full rebuild is acceptable;
// the tile count is bounded by the map size.
//
// Pass an empty vector to clear the preview (m_previewVisible = false).
//
// The SMeshBuffer batching limit of 10,922 quads per buffer (u16 index cap)
// applies here too — new buffers are opened automatically when the limit is
// reached, following the same pattern as setZoneOverlay.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setTilePlacementPreview(
    const std::vector<std::pair<int,int>>& freeTiles,
    uint32_t freeArgb,
    const std::vector<std::pair<int,int>>& blockedTiles)  // Phase 11d Deliverable 5d: render blocked tiles
{
    // Clear request: if both lists are empty, clear preview.
    if (freeTiles.empty() && blockedTiles.empty()) {
        m_previewVisible = false;
        return;
    }
    if (!m_terrain || !m_driver) {
        m_previewVisible = false;
        return;
    }

    // Drop the old preview mesh before rebuilding.
    if (m_previewMesh) {
        m_previewMesh->drop();
        m_previewMesh = nullptr;
    }

    // Decode ARGB (0xAARRGGBB) for Irrlicht SColor(A, R, G, B).
    u8 a = static_cast<u8>((freeArgb >> 24) & 0xFF);
    u8 r = static_cast<u8>((freeArgb >> 16) & 0xFF);
    u8 g = static_cast<u8>((freeArgb >>  8) & 0xFF);
    u8 b = static_cast<u8>( freeArgb        & 0xFF);
    SColor colour(a, r, g, b);

    const float yOffset = 0.05f;  // same as single-tile hover highlight

    // Maximum quads per SMeshBuffer (u16 index cap: 65535 / 6 indices per quad).
    static constexpr u32 kMaxQuadsPerBuffer = 10922u;

    m_previewMesh = new SMesh();

    SMeshBuffer* cur = nullptr;
    u32 quadsInCur   = 0;

    auto openBuffer = [&]() {
        cur = new SMeshBuffer();
        cur->Material.MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL;
        cur->Material.Lighting     = false;
        cur->Material.ZWriteEnable = false;
        quadsInCur = 0;
    };

    auto closeBuffer = [&]() {
        if (!cur) return;
        cur->recalculateBoundingBox();
        m_previewMesh->addMeshBuffer(cur);
        cur->drop();  // mesh is now sole owner
        cur = nullptr;
    };

    openBuffer();

    for (const auto& tile : freeTiles) {
        int tx = tile.first;
        int tz = tile.second;

        // Clamp to valid map bounds.
        if (tx < 0 || tx >= m_mapTilesX || tz < 0 || tz >= m_mapTilesZ)
            continue;

        if (quadsInCur >= kMaxQuadsPerBuffer) {
            closeBuffer();
            openBuffer();
        }

        float x0 = static_cast<float>(tx)     * m_cellSize;
        float x1 = static_cast<float>(tx + 1) * m_cellSize;
        float z0 = static_cast<float>(tz)     * m_cellSize;
        float z1 = static_cast<float>(tz + 1) * m_cellSize;

        float h00 = m_terrain->getHeightAt(tx,     tz)     + yOffset;
        float h10 = m_terrain->getHeightAt(tx + 1, tz)     + yOffset;
        float h11 = m_terrain->getHeightAt(tx + 1, tz + 1) + yOffset;
        float h01 = m_terrain->getHeightAt(tx,     tz + 1) + yOffset;

        u32 base = quadsInCur * 4;

        // Four vertices — CW winding from above (+Y normal).
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h00, z0), core::vector3df(0,1,0), colour,
            core::vector2df(0, 0)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h10, z0), core::vector3df(0,1,0), colour,
            core::vector2df(0, 0)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h11, z1), core::vector3df(0,1,0), colour,
            core::vector2df(0, 0)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h01, z1), core::vector3df(0,1,0), colour,
            core::vector2df(0, 0)));

        // Indices: v0→v2→v1, v0→v3→v2 (CW from +Y view per scene-graph-ownership.md).
        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 2));
        cur->Indices.push_back(static_cast<u16>(base + 1));
        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 3));
        cur->Indices.push_back(static_cast<u16>(base + 2));

        ++quadsInCur;
    }

    closeBuffer();

    // Phase 11d Deliverable 5d: add blocked tiles in semi-opaque red (kHoverArgbBlocked).
    // kHoverArgbBlocked = 0xBBFF2222 (semi-opaque red, alpha=0xBB≈73%).
    if (!blockedTiles.empty() && m_terrain) {
        static constexpr uint32_t kBlockedArgb = 0xBBFF2222u;
        u8 ba = static_cast<u8>((kBlockedArgb >> 24) & 0xFF);
        u8 br = static_cast<u8>((kBlockedArgb >> 16) & 0xFF);
        u8 bg = static_cast<u8>((kBlockedArgb >>  8) & 0xFF);
        u8 bb = static_cast<u8>( kBlockedArgb        & 0xFF);
        SColor blockedColour(ba, br, bg, bb);

        openBuffer();
        for (const auto& tile : blockedTiles) {
            int tx = tile.first;
            int tz = tile.second;
            if (tx < 0 || tx >= m_mapTilesX || tz < 0 || tz >= m_mapTilesZ) continue;
            if (quadsInCur >= kMaxQuadsPerBuffer) {
                closeBuffer();
                openBuffer();
            }
            float x0 = static_cast<float>(tx)     * m_cellSize;
            float x1 = static_cast<float>(tx + 1) * m_cellSize;
            float z0 = static_cast<float>(tz)     * m_cellSize;
            float z1 = static_cast<float>(tz + 1) * m_cellSize;
            float h00 = m_terrain->getHeightAt(tx,     tz)     + yOffset;
            float h10 = m_terrain->getHeightAt(tx + 1, tz)     + yOffset;
            float h11 = m_terrain->getHeightAt(tx + 1, tz + 1) + yOffset;
            float h01 = m_terrain->getHeightAt(tx,     tz + 1) + yOffset;
            u32 base = quadsInCur * 4;
            cur->Vertices.push_back(S3DVertex(core::vector3df(x0, h00, z0), core::vector3df(0,1,0), blockedColour, core::vector2df(0,0)));
            cur->Vertices.push_back(S3DVertex(core::vector3df(x1, h10, z0), core::vector3df(0,1,0), blockedColour, core::vector2df(0,0)));
            cur->Vertices.push_back(S3DVertex(core::vector3df(x1, h11, z1), core::vector3df(0,1,0), blockedColour, core::vector2df(0,0)));
            cur->Vertices.push_back(S3DVertex(core::vector3df(x0, h01, z1), core::vector3df(0,1,0), blockedColour, core::vector2df(0,0)));
            cur->Indices.push_back(static_cast<u16>(base + 0));
            cur->Indices.push_back(static_cast<u16>(base + 2));
            cur->Indices.push_back(static_cast<u16>(base + 1));
            cur->Indices.push_back(static_cast<u16>(base + 0));
            cur->Indices.push_back(static_cast<u16>(base + 3));
            cur->Indices.push_back(static_cast<u16>(base + 2));
            ++quadsInCur;
        }
        closeBuffer();
    }

    // If no valid tiles were added (all out-of-bounds), show nothing.
    if (m_previewMesh->getMeshBufferCount() == 0) {
        m_previewMesh->drop();
        m_previewMesh   = nullptr;
        m_previewVisible = false;
        return;
    }

    m_previewMesh->recalculateBoundingBox();
    m_previewVisible = true;
}

// -------------------------------------------------------------------------
// setZoneOverlay — rebuild the persistent zone-colour overlay mesh.
//
// Each entry in sparseOverlay is one tile quad rendered semi-transparently
// above the terrain.  The overlay node is a persistent IMeshSceneNode* that
// is rebuilt (not just updated) whenever the zone layout changes.
//
// Capped at 100K overlay quads for V1 (per IRenderer.h contract).
// Multiple SMeshBuffers are used because u16 indices cap at 16383 quads each
// (vertex index 4*16383+3 = 65535 = u16 max; 16384 would overflow to 0).
// Called once per budget tick — NOT every frame.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setZoneOverlay(
    int mapTilesX, int mapTilesZ,
    const std::unordered_map<uint64_t, uint32_t>& sparseOverlay)
{
    if (!m_smgr || !m_driver) return;

    // Remove the previous overlay node if present.
    if (m_overlayNode) {
        // Eviction sequence: clear textures, flush driver state, then remove.
        u32 matCount = m_overlayNode->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            SMaterial& mat = m_overlayNode->getMaterial(m);
            for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        
        m_overlayNode->remove();
        m_overlayNode = nullptr;
    }

    // Nothing to render — overlay cleared.
    if (sparseOverlay.empty()) return;

    // Cap at 100K quads for V1.
    static constexpr size_t kMaxOverlayQuads = 100000u;
    // u16 index cap: max vertex index per buffer = 65535; 4 vertices per quad
    // → max 16383 quads per buffer (16384th quad's base index = 65536 > u16 max).
    static constexpr u32 kMaxQuadsPerBuffer = 16383u;

    float yOffset = 0.25f;  // 25 cm above terrain — enough to prevent Z-fighting at far zoom

    SMesh*       omesh     = new SMesh();
    SMeshBuffer* cur       = nullptr;
    u32          quadsInCur = 0;

    auto openBuffer = [&]() {
        cur = new SMeshBuffer();
        cur->Material.MaterialType           = EMT_TRANSPARENT_ALPHA_CHANNEL;
        cur->Material.Lighting               = false;
        cur->Material.ZWriteEnable           = false;
        // Polygon offset: push overlay toward camera to win depth test at far zoom.
        cur->Material.PolygonOffsetFactor    = 3;
        cur->Material.PolygonOffsetDirection = irr::video::EPO_FRONT;
        quadsInCur = 0;
    };

    auto closeBuffer = [&]() {
        if (!cur) return;
        cur->recalculateBoundingBox();
        omesh->addMeshBuffer(cur);
        cur->drop();  // mesh is sole owner
        cur = nullptr;
    };

    openBuffer();

    size_t written = 0;
    for (const auto& kv : sparseOverlay) {
        if (written >= kMaxOverlayQuads) break;

        // Decode tile index from key.
        int tx = static_cast<int>(kv.first % static_cast<uint64_t>(mapTilesX));
        int tz = static_cast<int>(kv.first / static_cast<uint64_t>(mapTilesX));

        // Skip out-of-bounds tiles.
        if (tx < 0 || tx >= mapTilesX || tz < 0 || tz >= mapTilesZ) continue;

        if (quadsInCur >= kMaxQuadsPerBuffer) {
            closeBuffer();
            openBuffer();
        }

        // Decode ARGB colour.
        uint32_t ac = kv.second;
        u8 ca = static_cast<u8>((ac >> 24) & 0xFF);
        u8 cr = static_cast<u8>((ac >> 16) & 0xFF);
        u8 cg = static_cast<u8>((ac >>  8) & 0xFF);
        u8 cb = static_cast<u8>( ac        & 0xFF);
        SColor colour(ca, cr, cg, cb);

        float x0 = static_cast<float>(tx)     * m_cellSize;
        float x1 = static_cast<float>(tx + 1) * m_cellSize;
        float z0 = static_cast<float>(tz)     * m_cellSize;
        float z1 = static_cast<float>(tz + 1) * m_cellSize;

        float h00 = (m_terrain ? m_terrain->getHeightAt(tx,     tz)     : 0.0f) + yOffset;
        float h10 = (m_terrain ? m_terrain->getHeightAt(tx + 1, tz)     : 0.0f) + yOffset;
        float h11 = (m_terrain ? m_terrain->getHeightAt(tx + 1, tz + 1) : 0.0f) + yOffset;
        float h01 = (m_terrain ? m_terrain->getHeightAt(tx,     tz + 1) : 0.0f) + yOffset;

        u32 base = quadsInCur * 4;

        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h00, z0), core::vector3df(0, 1, 0), colour,
            core::vector2df(0, 0)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h10, z0), core::vector3df(0, 1, 0), colour,
            core::vector2df(1, 0)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h11, z1), core::vector3df(0, 1, 0), colour,
            core::vector2df(1, 1)));
        cur->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h01, z1), core::vector3df(0, 1, 0), colour,
            core::vector2df(0, 1)));

        // Two triangles per quad: v0→v2→v1, v0→v3→v2 (CW from above = +Y normal).
        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 2));
        cur->Indices.push_back(static_cast<u16>(base + 1));
        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 3));
        cur->Indices.push_back(static_cast<u16>(base + 2));

        ++quadsInCur;
        ++written;
    }

    closeBuffer();

    if (omesh->getMeshBufferCount() == 0) {
        // All entries were out-of-bounds — drop and bail.
        omesh->drop();
        return;
    }

    omesh->recalculateBoundingBox();

    m_overlayNode = m_smgr->addMeshSceneNode(omesh);
    omesh->drop();  // scene node is now sole owner

    if (m_overlayNode) {
        m_overlayNode->setPosition(core::vector3df(0.0f, 0.0f, 0.0f));
        m_overlayNode->setMaterialFlag(EMF_LIGHTING, false);
        m_overlayNode->setMaterialFlag(EMF_BACK_FACE_CULLING, false);
    }
}

// -------------------------------------------------------------------------
// getTileScreenBounds — project tile corners to screen space via camera.
//
// Returns ScreenRect in physical pixels, or ScreenRect{} if the tile is
// off-screen, m_terrain is null, or the tile coordinates are out of bounds.
// Used by UIManager for the 3-step InspectorPanel position cascade.
// -------------------------------------------------------------------------
ScreenRect IrrlichtRenderer::getTileScreenBounds(int tileX, int tileZ) const
{
    if (!m_driver || !m_smgr) return ScreenRect{};
    if (m_mapTilesX <= 0 || m_mapTilesZ <= 0) return ScreenRect{};
    if (tileX < 0 || tileX >= m_mapTilesX) return ScreenRect{};
    if (tileZ < 0 || tileZ >= m_mapTilesZ) return ScreenRect{};
    irr::scene::ICameraSceneNode* cam = m_smgr->getActiveCamera();
    if (!cam) return ScreenRect{};

    float h = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f;
    float yWorld = h + 0.1f;  // slightly above terrain surface

    float x0 = static_cast<float>(tileX)     * m_cellSize;
    float x1 = static_cast<float>(tileX + 1) * m_cellSize;
    float z0 = static_cast<float>(tileZ)     * m_cellSize;
    float z1 = static_cast<float>(tileZ + 1) * m_cellSize;

    irr::core::vector3df corners[4] = {
        { x0, yWorld, z0 },
        { x1, yWorld, z0 },
        { x1, yWorld, z1 },
        { x0, yWorld, z1 },
    };

    irr::core::dimension2d<u32> screenSize = m_driver->getScreenSize();

    // Project each corner through the camera view+projection matrices.
    // Irrlicht does not expose a direct worldToScreen API at the scene manager
    // level, so we use the camera's combined view-projection matrix directly.
    const irr::core::matrix4& view = cam->getViewMatrix();
    const irr::core::matrix4& proj = cam->getProjectionMatrix();
    irr::core::matrix4 vp = proj * view;

    int minX = INT_MAX, minY = INT_MAX;
    int maxX = INT_MIN, maxY = INT_MIN;
    bool anyVisible = false;

    for (int i = 0; i < 4; ++i) {
        irr::core::vector3df p = corners[i];

        // Transform to clip space: clip = VP * world.
        float cx = vp[0]*p.X + vp[4]*p.Y + vp[8]*p.Z  + vp[12];
        float cy = vp[1]*p.X + vp[5]*p.Y + vp[9]*p.Z  + vp[13];
        // cz not used for 2D projection
        float cw = vp[3]*p.X + vp[7]*p.Y + vp[11]*p.Z + vp[15];

        if (cw <= 0.0f) continue;  // behind camera — skip this corner

        // Perspective divide → NDC [-1, +1].
        float ndcX = cx / cw;
        float ndcY = cy / cw;

        // Clamp to slightly beyond screen to detect near-miss cases.
        if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) continue;

        // Convert NDC to pixel coordinates.
        // NDC (+1,-1) = top-left in most conventions; Irrlicht uses Y-down screen.
        int sx = static_cast<int>((ndcX  + 1.0f) * 0.5f * static_cast<float>(screenSize.Width));
        int sy = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(screenSize.Height));

        minX = std::min(minX, sx);
        minY = std::min(minY, sy);
        maxX = std::max(maxX, sx);
        maxY = std::max(maxY, sy);
        anyVisible = true;
    }

    if (!anyVisible) return ScreenRect{};

    // Clamp to screen bounds.
    minX = std::max(minX, 0);
    minY = std::max(minY, 0);
    maxX = std::min(maxX, static_cast<int>(screenSize.Width)  - 1);
    maxY = std::min(maxY, static_cast<int>(screenSize.Height) - 1);

    if (maxX <= minX || maxY <= minY) return ScreenRect{};

    return ScreenRect{ minX, minY, maxX - minX, maxY - minY };
}

// getListenerPosition — return the camera eye position most recently set via setCamera().
// Used by CitySimulation::tick() for the sfx_intersection_tick 80 m pre-acquisition cull.
// Returns vec3{} before the first setCamera() call (m_lastCameraPosition is zero-initialised).
vec3 IrrlichtRenderer::getListenerPosition() const
{
    return m_lastCameraPosition;
}

// =============================================================================
// Phase 10 — Building mesh spawning and road mesh rendering
//
// Implements the six IRenderer pure-virtual methods for creating and removing
// 3D scene nodes when the simulation places or demolishes tiles.
//
// Building and service-building nodes are stored in m_buildingNodes.
// Road tile nodes are stored in m_roadNodes.
// Both maps are keyed by tileKey(tileX, tileZ) (uint64_t).
//
// Placement always calls ensureAssetLoader() first — returns early if m_smgr
// is null (headless / unit-test context).
//
// Removal runs the eviction sequence per scene-graph-ownership.md:
//   clear material texture slots → driver->setMaterial(SMaterial{}) → node->remove()
// then deletes the LODNode wrapper.
//
// main-thread-only — do NOT call from audio thread or simulation thread.
// =============================================================================

// -------------------------------------------------------------------------
// ensureAssetLoader — lazily construct m_buildingAssetLoader.
// Returns true if the loader is ready; false if m_smgr is null.
// -------------------------------------------------------------------------
bool IrrlichtRenderer::ensureAssetLoader()
{
    if (m_buildingAssetLoader) return true;
    if (!m_smgr || !m_driver) {
        if (m_logger) {
            m_logger->log("[IrrlichtRenderer] ensureAssetLoader() called with null "
                "m_smgr/m_driver — scene node cannot be created", irr::ELL_WARNING);
        } else {
            fprintf(stderr,
                "[IrrlichtRenderer WARNING] ensureAssetLoader() called with null "
                "m_smgr/m_driver — scene node cannot be created\n");
        }
        return false;
    }
    m_buildingAssetLoader = std::make_unique<BuildingAssetLoader>(m_smgr, m_driver);
    return true;
}

// -------------------------------------------------------------------------
// destroyTileNode — eviction sequence + LODNode deletion for one registry entry.
//
// Per scene-graph-ownership.md eviction sequence (simplified — no TextureCache
// here because IrrlichtRenderer does not yet maintain a TextureCache reference;
// buildings and roads created in Phase 10 use the driver's default material pool
// and have no sRGB/splat textures requiring explicit reference-count management):
//   Step 1: Clear all material texture slots.
//   Step 2: driver->setMaterial(SMaterial{}) — flush driver last-bound state.
//   Step 3: Null the LODNode's internal node pointer BEFORE remove().
//           (LODNode::getNode() returns the raw pointer; we call remove() on it
//           after nulling the LODNode wrapper.)
//   Step 4: Delete the LODNode wrapper (does not call remove() — wrapper is non-owning).
//
// No-op if the key is not in the registry.
// -------------------------------------------------------------------------
void IrrlichtRenderer::destroyTileNode(
    std::unordered_map<uint64_t, LODNode*>& registry,
    int tileX, int tileZ)
{
    uint64_t key = tileKey(tileX, tileZ);
    auto it = registry.find(key);
    if (it == registry.end() || !it->second) return;

    LODNode* lodNode = it->second;
    scene::ISceneNode* node = lodNode->getNode();

    if (node && m_driver) {
        // Step 1: clear material texture slots.
        u32 matCount = node->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            SMaterial& mat = node->getMaterial(m);
            for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        // Step 2: flush driver last-bound state.
        
        // Step 3 + 4: remove the scene node.
        node->remove();  // do NOT access node* after this
    }

    // Delete the LODNode wrapper (non-owning — scene node already removed above).
    delete lodNode;
    registry.erase(it);
}

// -------------------------------------------------------------------------
// getOrCreateZoneTexture — solid-colour placeholder texture per zone prefix.
//
// All placeholder B3D meshes have no UV coordinates (VRTS tc_sets=0); Irrlicht
// defaults unset UV channels to (0,0), so every vertex samples the same texel.
// A 4×4 uniform-colour image therefore renders as a flat solid colour regardless
// of the absence of real UV data — giving each zone type a visually distinct look.
//
// The texture is created once and reused for all buildings of the same zone type.
// Irrlicht's driver cache owns the ITexture* (no explicit drop needed here).
// -------------------------------------------------------------------------
irr::video::ITexture* IrrlichtRenderer::getOrCreateZoneTexture(const std::string& prefix)
{
    if (!m_driver) return nullptr;

    auto it = m_zoneTextures.find(prefix);
    if (it != m_zoneTextures.end()) return it->second;

    // Choose a visually distinct colour per zone type.
    irr::video::SColor color(255, 140, 140, 140);  // default: mid-gray
    if      (prefix == "res_") color = irr::video::SColor(255, 200, 165, 110);  // warm tan
    else if (prefix == "com_") color = irr::video::SColor(255,  90, 140, 210);  // steel blue
    else if (prefix == "ind_") color = irr::video::SColor(255, 145, 145, 110);  // khaki gray
    else if (prefix == "svc_") color = irr::video::SColor(255, 210, 160,  50);  // amber

    // Build a 4×4 solid-colour image.
    irr::video::IImage* img = m_driver->createImage(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(4, 4));
    if (!img) return nullptr;

    for (irr::u32 y = 0; y < 4; ++y)
        for (irr::u32 x = 0; x < 4; ++x)
            img->setPixel(x, y, color);

    // Add to the Irrlicht driver texture cache under a unique name.
    const std::string texName = "aitown_placeholder_bldg_" + prefix;
    irr::video::ITexture* tex = m_driver->addTexture(texName.c_str(), img);
    img->drop();  // driver has its own reference; release ours

    m_zoneTextures[prefix] = tex;
    return tex;
}

// -------------------------------------------------------------------------
// placeBuildingMesh — zone building placement.
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeBuildingMesh(int tileX, int tileZ,
                                          const std::string& assetBaseName)
{
    if (assetBaseName.empty()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeBuildingMesh(%d,%d) called with "
            "empty assetBaseName — skipping node creation", tileX, tileZ);
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }
    if (!ensureAssetLoader()) return;

    // Remove any existing building on this tile first (e.g. density upgrade swap).
    destroyTileNode(m_buildingNodes, tileX, tileZ);

    // Construct the asset base path: assets/3d/buildings/<assetBaseName>
    // BuildingAssetLoader::load() appends _lod0.b3d, _lod1.b3d, _lod2.b3d, .meta.
    std::string basePath = std::string(AITOWN_ASSETS_DIR) +
                           "/3d/buildings/" + assetBaseName;

    // Parse density tier from assetBaseName (format: "zone_dens_NN", e.g. "res_low_01").
    // Done BEFORE terrain flattening and before asset load so that footprintN is available
    // for the flatten loop even when the asset file is absent (e.g. EDT_NULL test context).
    int footprintN = 1;  // default: Low = 1x1
    {
        size_t first = assetBaseName.find('_');
        if (first != std::string::npos) {
            size_t second = assetBaseName.find('_', first + 1);
            std::string tierStr = (second != std::string::npos)
                ? assetBaseName.substr(first + 1, second - first - 1)
                : assetBaseName.substr(first + 1);
            if (tierStr == "med")       footprintN = 2;
            else if (tierStr == "high") footprintN = 3;
            // "low" and any unknown -> 1
        }
    }

    // Full-footprint terrain flattening (Phase 11l Deliverable 3):
    // Average all (footprintN+1)x(footprintN+1) corner vertex heights, then flatten
    // every corner to that average so the ground plate sits flush with terrain across
    // the entire NxN tile footprint.  For LOW (N=1) this produces 4 calls (same as
    // original); for MED (N=2) it covers 9 vertices; for HIGH (N=3) it covers 16.
    // Runs unconditionally when m_terrain is set — independent of whether the scene
    // node is created successfully, so that terrain is always consistent on placement.
    float heightSum = 0.0f;
    int   heightCount = 0;
    for (int cx = 0; cx <= footprintN; ++cx) {
        for (int cz = 0; cz <= footprintN; ++cz) {
            heightSum += m_terrain ? m_terrain->getHeightAt(tileX + cx, tileZ + cz) : 0.0f;
            ++heightCount;
        }
    }
    const float targetH = (heightCount > 0) ? (heightSum / heightCount) : 0.0f;
    if (m_terrain) {
        for (int cx = 0; cx <= footprintN; ++cx)
            for (int cz = 0; cz <= footprintN; ++cz)
                m_terrain->setTileHeight(tileX + cx, tileZ + cz, targetH);
        m_terrain->flushTerrainRebuilds();
    }

    // Rebuild road tiles within +(footprintN+2) of the building origin.
    // setTileHeight() applies weighted neighbour blending to the 8 surrounding vertices;
    // for larger footprints the affected radius grows accordingly.
    {
        const int rebuildRadius = footprintN + 2;
        for (int dz = -rebuildRadius; dz <= footprintN + rebuildRadius; ++dz) {
            for (int dx = -rebuildRadius; dx <= footprintN + rebuildRadius; ++dx) {
                if (dx >= 0 && dx < footprintN && dz >= 0 && dz < footprintN) continue;
                const int nx = tileX + dx;
                const int nz = tileZ + dz;
                if (m_roadNodes.count(tileKey(nx, nz)) > 0)
                    placeRoadMesh(nx, nz, /*flattenTerrain=*/false,
                                          /*rebuildNeighbors=*/false);
            }
        }
    }

    LODNode* lodNode = m_buildingAssetLoader->load(basePath);
    if (!lodNode) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeBuildingMesh(%d,%d): failed to load "
            "asset '%s' — node not created", tileX, tileZ, basePath.c_str());
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    // Position the scene node at the tile's world-space centre.
    // B3D building meshes are 1x1xheight unit boxes centred on X and Z.
    // Scale by kTileSize so they fill the full tile footprint (10x10 m).
    // Offset by kTileSize/2 in X and Z to centre the mesh on the tile.
    // Disable lighting — no light nodes in the scene yet (Phase 6+).
    //
    // Texture fallback: BuildingAssetLoader::load() binds the buildings_atlas_d.dds
    // to every material slot.  If the atlas fails to load (missing file, driver
    // unsupported format) the slot remains null and the zone-coloured placeholder
    // texture is bound instead so the building is still visually distinguishable.
    if (scene::ISceneNode* node = lodNode->getNode()) {

        // Use targetH directly — NOT getHeightAt() after setTileHeight().
        // setTileHeight() applies neighbour blending to surrounding tiles;
        // subsequent corner calls bleed back, so getHeightAt() would return a
        // blended-down value and position the node below the rendered terrain surface.
        const float postY = m_terrain ? targetH : 0.0f;
        // World center of the NxN footprint: (tileX + N*0.5) * kTileSize, same for Z.
        // Low (N=1): tile centre = (tileX + 0.5) * 10.  Med (N=2): (tileX + 1.0) * 10.
        const float worldCentreX = (static_cast<f32>(tileX) + footprintN * 0.5f) * kTileSize;
        const float worldCentreZ = (static_cast<f32>(tileZ) + footprintN * 0.5f) * kTileSize;
        node->setPosition(core::vector3df(
            worldCentreX,
            postY + 0.05f,
            worldCentreZ));
        // Phase 11h: buildings are authored at world scale — no tile-size scaling.
        node->setScale(core::vector3df(1.0f, 1.0f, 1.0f));

        // Zone-colour fallback: only applied per-slot when atlas was NOT bound.
        const std::string prefix = (assetBaseName.size() >= 4)
            ? assetBaseName.substr(0, 4) : "dflt";
        video::ITexture* zoneTex = getOrCreateZoneTexture(prefix);

        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            video::SMaterial& mat = node->getMaterial(m);
            mat.Lighting = false;
            // Disable backface culling for B3D building assets.
            // The procedural B3D generator uses CCW winding in Irrlicht left-handed
            // space, but we keep BackfaceCulling=false as a robust default while
            // production Blender assets are being authored.
            mat.BackfaceCulling = false;
            // Polygon offset: push the building base surface toward the camera so it
            // wins the depth test against the co-planar terrain quad at all distances.
            mat.PolygonOffsetDirection = irr::video::EPO_FRONT;
            mat.PolygonOffsetFactor    = 1;
            // Apply zone-coloured fallback only if atlas was not bound by loader.
            if (!mat.getTexture(0) && zoneTex) mat.setTexture(0, zoneTex);
        }
    }

    m_buildingNodes[tileKey(tileX, tileZ)] = lodNode;
}

// -------------------------------------------------------------------------
// removeBuildingMesh — zone and service building removal.
// -------------------------------------------------------------------------
void IrrlichtRenderer::removeBuildingMesh(int tileX, int tileZ)
{
    destroyTileNode(m_buildingNodes, tileX, tileZ);
}

// -------------------------------------------------------------------------
// initRoadShader — load road.vert/road.frag + road_asphalt_tileable.dds.
// Idempotent: returns immediately if already initialised or if no GL context.
// -------------------------------------------------------------------------
bool IrrlichtRenderer::initRoadShader()
{
    // Already initialised: return true if shader loaded successfully (>= 0),
    // false if shader load previously failed (-2 sentinel).
    // -1 = not yet attempted (fall through to load).
    if (m_roadMaterialType == -2) return false;  // failed sentinel — no retry
    if (m_roadMaterialType >= 0) return true;    // already loaded successfully
    if (!m_driver || !m_smgr) return false;


    // Lazily create TextureCache for road diffuse texture.
    if (!m_roadTextureCache) {
        m_roadTextureCache = std::make_unique<TextureCache>(
            m_driver->getDriverType(),
            m_driver,
            m_device ? m_device->getFileSystem() : nullptr,
            /*maxTextureSize=*/2048,
            m_device ? m_device->getLogger() : nullptr);
    }

    // Determine sRGB support — drives texture upload format and shader correction.
    //
    // This project does NOT use an sRGB framebuffer (GL_FRAMEBUFFER_SRGB is not enabled).
    // The authored texel values (~RGB 82,80,82 asphalt gray) must reach the display
    // as-authored regardless of the upload path chosen.
    //
    // Case A — sRGB extension present (srgbOk=true):
    //   Upload: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT.
    //   GPU decodes stored sRGB values to linear on sample (~0.085 for asphalt ~82/255).
    //   road.frag receives u_srgbLinear=0 → applies pow(x, 1/2.2) to re-encode
    //   linear → sRGB before writing to the non-sRGB framebuffer, recovering the
    //   authored ~RGB(82,80,82) gray.
    //
    // Case B — sRGB extension absent (srgbOk=false):
    //   Upload: GL_COMPRESSED_RGBA_S3TC_DXT5_EXT (linear, no GPU sRGB decode).
    //   Sampled values are the raw authored bytes (~0.32 for asphalt gray).
    //   road.frag receives u_srgbLinear=1 → outputs the sample directly.
    //   Authored ~RGB(82,80,82) appears correctly on the non-sRGB framebuffer.
    const bool srgbOk = (glewIsExtensionSupported("GL_EXT_texture_sRGB") == GL_TRUE);

    const std::string texPath = std::string(AITOWN_ASSETS_DIR)
                                + "/textures/roads/road_asphalt_tileable.dds";

    GLuint texHandle = m_roadTextureCache->loadSRGB(texPath,
        srgbOk ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
               : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT);
    m_roadDiffuseTexGLuint = static_cast<unsigned int>(texHandle);
    // 0 = load failed or EDT_NULL — road tiles will render without texture.

    // Load road shader via GPUProgrammingServices.
    IGPUProgrammingServices* gpu = m_driver->getGPUProgrammingServices();
    if (!gpu) {
        // No GPU programs (EDT_NULL or software driver) — EMT_SOLID fallback.
        // Mark as "done" with -2 so we don't retry every frame.
        m_roadMaterialType = -2;
        return false;
    }

    const std::string vsPath = std::string(AITOWN_ASSETS_DIR) + "/shaders/road.vert";
    const std::string fsPath = std::string(AITOWN_ASSETS_DIR) + "/shaders/road.frag";

    // srgbOk drives u_srgbLinear in the callback (0 when sRGB, 1 when linear upload).
    // road.frag uses this to decide whether to apply the linear→sRGB inverse correction.
    RoadShaderCallback* cb = new RoadShaderCallback(srgbOk, texHandle);
    s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
        vsPath.c_str(), "main", video::EVST_VS_1_1,
        fsPath.c_str(), "main", video::EPST_PS_1_1,
        cb, video::EMT_SOLID);
    cb->drop();  // unconditional per shader-loading.md

    if (matType == -1) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] road shader compile failed "
            "(vs=%s, fs=%s) — road tiles will use EMT_SOLID fallback",
            vsPath.c_str(), fsPath.c_str());
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        m_roadMaterialType = -2;  // mark done-with-failure
        return false;
    }

    m_roadMaterialType = matType;
    return true;
}

// -------------------------------------------------------------------------
// setRenderSystem — inject RenderSystem* and trigger terrain shader init.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setRenderSystem(RenderSystem* rs)
{
    m_renderSystem = rs;
    initTerrainShader();
}

// -------------------------------------------------------------------------
// initTerrainShader — load terrain.vert/terrain.frag + 4 diffuse layers
// + splat map texture.  EDT_NULL guard: no-op in headless contexts.
// -------------------------------------------------------------------------
void IrrlichtRenderer::initTerrainShader()
{
    // EDT_NULL early-return guard — no GL context available.
    if (m_driverType == video::EDT_NULL) return;
    if (!m_driver || !m_smgr) return;

    // Lazily create TextureCache for terrain textures.
    if (!m_terrainTextureCache) {
        m_terrainTextureCache = std::make_unique<TextureCache>(
            m_driver->getDriverType(),
            m_driver,
            m_device ? m_device->getFileSystem() : nullptr,
            /*maxTextureSize=*/2048,
            m_device ? m_device->getLogger() : nullptr);
    }

    // Build paths for the 4 terrain diffuse layers (splat channel order: R/G/B/A).
    const std::string assetsDir = std::string(AITOWN_ASSETS_DIR);
    const std::string grassPath    = assetsDir + "/textures/terrain/terrain_grass_d.dds";
    const std::string asphaltPath  = assetsDir + "/textures/terrain/terrain_asphalt_d.dds";
    const std::string soilPath     = assetsDir + "/textures/terrain/terrain_soil_d.dds";
    const std::string concretePath = assetsDir + "/textures/terrain/terrain_concrete_d.dds";
    const std::string splatPath    = assetsDir + "/textures/terrain/terrain_chunk_splat.png";

    // Load 4 diffuse textures as sRGB DXT1.
    m_terrainTextureCache->loadSRGB(grassPath,    GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    m_terrainTextureCache->loadSRGB(asphaltPath,  GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    m_terrainTextureCache->loadSRGB(soilPath,     GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    m_terrainTextureCache->loadSRGB(concretePath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

    // Load splat map as linear RGBA8.
    m_terrainTextureCache->loadSplatMap(splatPath);

    // detail paths in splat channel order: R=grass, G=asphalt, B=soil, A=concrete.
    const std::array<std::string, 4> detailPaths = {
        grassPath, asphaltPath, soilPath, concretePath
    };

    // Construct callback — raw heap per shader-loading.md (Irrlicht calls grab() internally).
    TerrainShaderCallback* cb = new TerrainShaderCallback(
        m_renderSystem, m_terrainTextureCache.get(), splatPath, detailPaths);

    // Build absolute shader paths.
    const std::string vsPath = assetsDir + "/shaders/terrain.vert";
    const std::string fsPath = assetsDir + "/shaders/terrain.frag";

    // Get GPU programming services.
    IGPUProgrammingServices* gpu = m_driver->getGPUProgrammingServices();
    if (!gpu) {
        cb->drop();
        return;
    }

    s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
        vsPath.c_str(), "main", video::EVST_VS_1_1,
        fsPath.c_str(), "main", video::EPST_PS_1_1,
        cb, video::EMT_SOLID);

    cb->drop();  // unconditional per shader-loading.md

    if (matType == -1) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] terrain shader compile failed "
            "(vs=%s, fs=%s) — terrain will use default material",
            vsPath.c_str(), fsPath.c_str());
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    m_terrainMaterialType = matType;
}

// -------------------------------------------------------------------------
// ensureRoadMeshes — build shared LOD1/LOD2 meshes (idempotent).
//
// LOD0 is no longer shared — it is built per-tile by buildTileRoadMesh() with
// actual terrain heights at the 4 tile corners.  Only LOD1 and LOD2 (distance
// fallbacks) are built here.
//
// Geometry (world-space, node placed at tile world position with Y=0):
//   LOD1: flat quad (2 tris, road shader) — 50–150 m range.
//   LOD2: flat colored quad (2 tris, EMT_SOLID) — 150–300 m range.
//
// BackfaceCulling = true on LOD1/LOD2 (simple flat quads, one face only).
// Lighting = false on all LODs (no light nodes in scene).
// -------------------------------------------------------------------------
void IrrlichtRenderer::ensureRoadMeshes()
{
    if (m_sharedRoadMeshLOD1) return;  // already built (LOD1 guards both)

    // Road tile half-extent: tile is kTileSize (10 m) → half is 5 m.
    static constexpr float H  = 5.0f;

    // Effective material type for LOD0/LOD1 (road shader or EMT_SOLID fallback).
    const E_MATERIAL_TYPE roadMat = (m_roadMaterialType >= 0)
        ? static_cast<E_MATERIAL_TYPE>(m_roadMaterialType)
        : EMT_SOLID;

    // ------------------------------------------------------------------
    // LOD1: flat quad only (4 verts, 2 tris). Same road shader, no kerbs.
    // ------------------------------------------------------------------
    {
        SMesh* mesh = new SMesh();
        SMeshBuffer* buf = new SMeshBuffer();

        buf->Material.MaterialType              = roadMat;
        buf->Material.Lighting                  = false;
        buf->Material.BackfaceCulling           = true;
        buf->Material.PolygonOffsetDirection    = irr::video::EPO_FRONT;
        buf->Material.PolygonOffsetFactor       = 1;

        buf->Vertices.push_back(S3DVertex(core::vector3df(-H, 0.f, -H), core::vector3df(0,1,0), SColor(255,255,255,255), core::vector2df(0.f, 0.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df( H, 0.f, -H), core::vector3df(0,1,0), SColor(255,255,255,255), core::vector2df(1.f, 0.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df( H, 0.f,  H), core::vector3df(0,1,0), SColor(255,255,255,255), core::vector2df(1.f, 1.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df(-H, 0.f,  H), core::vector3df(0,1,0), SColor(255,255,255,255), core::vector2df(0.f, 1.f)));
        buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
        buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

        buf->recalculateBoundingBox();
        mesh->addMeshBuffer(buf);
        buf->drop();
        mesh->recalculateBoundingBox();
        m_sharedRoadMeshLOD1 = mesh;
    }

    // ------------------------------------------------------------------
    // LOD2: flat quad with road_lod2_color vertex color. No texture, EMT_SOLID.
    // ------------------------------------------------------------------
    {
        SMesh* mesh = new SMesh();
        SMeshBuffer* buf = new SMeshBuffer();

        buf->Material.MaterialType              = EMT_SOLID;
        buf->Material.Lighting                  = false;
        buf->Material.BackfaceCulling           = true;
        buf->Material.PolygonOffsetDirection    = irr::video::EPO_FRONT;
        buf->Material.PolygonOffsetFactor       = 1;

        const SColor lod2Col = RenderConstants::road_lod2_color;
        buf->Vertices.push_back(S3DVertex(core::vector3df(-H, 0.f, -H), core::vector3df(0,1,0), lod2Col, core::vector2df(0.f, 0.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df( H, 0.f, -H), core::vector3df(0,1,0), lod2Col, core::vector2df(1.f, 0.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df( H, 0.f,  H), core::vector3df(0,1,0), lod2Col, core::vector2df(1.f, 1.f)));
        buf->Vertices.push_back(S3DVertex(core::vector3df(-H, 0.f,  H), core::vector3df(0,1,0), lod2Col, core::vector2df(0.f, 1.f)));
        buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
        buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

        buf->recalculateBoundingBox();
        mesh->addMeshBuffer(buf);
        buf->drop();
        mesh->recalculateBoundingBox();
        m_sharedRoadMeshLOD2 = mesh;
    }
}

// -------------------------------------------------------------------------
// buildTileRoadMesh — build a terrain-conforming LOD0 road mesh for one tile.
//
// Each tile gets a unique SMesh* whose 4 quad corners sit at the actual world-space
// terrain heights (h00, h10, h01, h11).  The node is placed at world X/Z centre
// with Y=0; all height is encoded directly in vertex Y coordinates.
//
// Vertex layout (corners, H = kTileSize/2 = 5 m, world-space relative to tile centre
// in X/Z but absolute in Y):
//   v0 = (-H, h00, -H)  back-left   (tileX,   tileZ)
//   v1 = (+H, h10, -H)  back-right  (tileX+1, tileZ)
//   v2 = (+H, h11, +H)  front-right (tileX+1, tileZ+1)
//   v3 = (-H, h01, +H)  front-left  (tileX,   tileZ+1)
//
// A small bias (kRoadBias = 0.10 m) is added to each vertex Y so the road sits
// slightly above the terrain and the polygon offset material flag handles Z-fighting
// at any camera distance.
//
// Kerb geometry follows the same corner heights so kerbs hug the terrain edge.
// -------------------------------------------------------------------------
irr::scene::SMesh* IrrlichtRenderer::buildTileRoadMesh(
    float h00, float h10, float h01, float h11, bool isEW) const
{
    if (!m_driver) return nullptr;

    using namespace RenderConstants;

    // Road tile half-extent in X and Z (5 m).
    static constexpr float H  = kTileSize * 0.5f;
    // Y bias: road surface sits 25 cm above terrain.
    static constexpr float B  = 0.25f;

    // Effective material type (road shader or EMT_SOLID fallback).
    const E_MATERIAL_TYPE roadMat = (m_roadMaterialType >= 0)
        ? static_cast<E_MATERIAL_TYPE>(m_roadMaterialType)
        : EMT_SOLID;

    // Pre-compute biased corner heights.
    const float y00 = h00 + B;   // back-left
    const float y10 = h10 + B;   // back-right
    const float y01 = h01 + B;   // front-left
    const float y11 = h11 + B;   // front-right

    SMesh* mesh = new SMesh();

    // Helper: create and configure a road-material buffer.
    auto makeRoadBuf = [&]() -> SMeshBuffer* {
        SMeshBuffer* b = new SMeshBuffer();
        b->Material.MaterialType           = roadMat;
        b->Material.Lighting               = false;
        b->Material.BackfaceCulling        = false;
        b->Material.PolygonOffsetDirection = irr::video::EPO_FRONT;
        b->Material.PolygonOffsetFactor    = 4;
        return b;
    };

    // Helper: add a vertex to a buffer.
    auto addV = [](SMeshBuffer* b, float x, float y, float z,
                   float u, float v, SColor col) {
        b->Vertices.push_back(S3DVertex(
            core::vector3df(x, y, z),
            core::vector3df(0.f, 1.f, 0.f),
            col,
            core::vector2df(u, v)));
    };

    // Helper: add two triangles for a quad (indices relative to base).
    auto addQuadIdx = [](SMeshBuffer* b, u16 base) {
        b->Indices.push_back(base + 0); b->Indices.push_back(base + 2); b->Indices.push_back(base + 1);
        b->Indices.push_back(base + 0); b->Indices.push_back(base + 3); b->Indices.push_back(base + 2);
    };

    // Road geometry is orientation-dependent:
    //   isEW=false (N/S): carriageway spans X=±cH, full Z=±H; center-line at X=0 along Z.
    //   isEW=true  (E/W): carriageway spans Z=±cH, full X=±H; center-line at Z=0 along X.
    // In both cases h00/h10/h01/h11 are the four tile-corner heights (SW/SE/NW/NE).
    // For the E/W inner-edge vertices (at Z=±cH instead of ±H) we use the nearest
    // outer-corner heights — the error over 1.25 m is ≤0.06 m at the 5% max grade.

    const float cH = kCarriagewayHalfWidth;

    // Helper: expand bounding box Y if nearly flat (EAC_BOX false-rejects near-horizontal tiles).
    auto expandBBY = [](SMeshBuffer* b) {
        b->recalculateBoundingBox();
        core::aabbox3df box = b->getBoundingBox();
        const float yMid = (box.MaxEdge.Y + box.MinEdge.Y) * 0.5f;
        if (box.MaxEdge.Y - box.MinEdge.Y < 0.5f) {
            box.MinEdge.Y = yMid - 0.25f;
            box.MaxEdge.Y = yMid + 0.25f;
            b->setBoundingBox(box);
        }
    };

    if (!isEW) {
        // --- N/S orientation: carriageway along Z, kerbs on X sides, center-line at X=0 ---

        // Buffer 0: Asphalt carriageway (X=±cH, full Z=±H)
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor white(255, 255, 255, 255);
            const float uLeft  = 0.5f - cH / H * 0.5f;
            const float uRight = 0.5f + cH / H * 0.5f;
            addV(buf, -cH, y00, -H,  uLeft,  0.f, white);
            addV(buf,  cH, y10, -H,  uRight, 0.f, white);
            addV(buf,  cH, y11,  H,  uRight, 1.f, white);
            addV(buf, -cH, y01,  H,  uLeft,  1.f, white);
            addQuadIdx(buf, 0);
            expandBBY(buf);
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 1: Left kerb (X: -H to -cH)
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor kerbGray(255, 100, 100, 100);
            addV(buf, -H,  y00, -H,  0.f, 0.f, kerbGray);
            addV(buf, -cH, y10, -H,  1.f, 0.f, kerbGray);
            addV(buf, -cH, y11,  H,  1.f, 1.f, kerbGray);
            addV(buf, -H,  y01,  H,  0.f, 1.f, kerbGray);
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 2: Right kerb (X: +cH to +H)
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor kerbGray(255, 100, 100, 100);
            addV(buf,  cH, y10, -H,  0.f, 0.f, kerbGray);
            addV(buf,  H,  y00, -H,  1.f, 0.f, kerbGray);
            addV(buf,  H,  y01,  H,  1.f, 1.f, kerbGray);
            addV(buf,  cH, y11,  H,  0.f, 1.f, kerbGray);
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 3: Center-line strip (X=0, along Z, 0.3 m wide)
        {
            SMeshBuffer* buf = new SMeshBuffer();
            buf->Material.MaterialType           = EMT_SOLID;
            buf->Material.Lighting               = false;
            buf->Material.BackfaceCulling        = false;
            buf->Material.PolygonOffsetDirection = irr::video::EPO_FRONT;
            buf->Material.PolygonOffsetFactor    = 5;
            const SColor lineWhite(255, 255, 255, 255);
            const float lHW = 0.15f;
            addV(buf, -lHW, y00 + 0.005f, -H,  0.f, 0.f, lineWhite);
            addV(buf,  lHW, y10 + 0.005f, -H,  1.f, 0.f, lineWhite);
            addV(buf,  lHW, y11 + 0.005f,  H,  1.f, 1.f, lineWhite);
            addV(buf, -lHW, y01 + 0.005f,  H,  0.f, 1.f, lineWhite);
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
    } else {
        // --- E/W orientation: carriageway along X, kerbs on Z sides, center-line at Z=0 ---

        // Buffer 0: Asphalt carriageway (full X=±H, Z=±cH)
        // Heights: y00/y10 approximate the south inner edge (Z=-cH), y01/y11 the north (Z=+cH).
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor white(255, 255, 255, 255);
            const float uLeft  = 0.5f - cH / H * 0.5f;
            const float uRight = 0.5f + cH / H * 0.5f;
            addV(buf, -H,  y00, -cH,  0.f,    uLeft,  white);  // west, south inner
            addV(buf,  H,  y10, -cH,  1.f,    uLeft,  white);  // east, south inner
            addV(buf,  H,  y11,  cH,  1.f,    uRight, white);  // east, north inner
            addV(buf, -H,  y01,  cH,  0.f,    uRight, white);  // west, north inner
            addQuadIdx(buf, 0);
            expandBBY(buf);
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 1: South kerb (Z: -H to -cH)
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor kerbGray(255, 100, 100, 100);
            addV(buf, -H,  y00, -H,   0.f, 0.f, kerbGray);  // west, outer
            addV(buf,  H,  y10, -H,   1.f, 0.f, kerbGray);  // east, outer
            addV(buf,  H,  y10, -cH,  1.f, 1.f, kerbGray);  // east, inner
            addV(buf, -H,  y00, -cH,  0.f, 1.f, kerbGray);  // west, inner
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 2: North kerb (Z: +cH to +H)
        {
            SMeshBuffer* buf = makeRoadBuf();
            const SColor kerbGray(255, 100, 100, 100);
            addV(buf, -H,  y01,  cH,  0.f, 0.f, kerbGray);  // west, inner
            addV(buf,  H,  y11,  cH,  1.f, 0.f, kerbGray);  // east, inner
            addV(buf,  H,  y11,  H,   1.f, 1.f, kerbGray);  // east, outer
            addV(buf, -H,  y01,  H,   0.f, 1.f, kerbGray);  // west, outer
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
        // Buffer 3: Center-line strip (Z=0, along X, 0.3 m wide)
        // Heights at Z=0 interpolated from west/east corner pairs.
        {
            SMeshBuffer* buf = new SMeshBuffer();
            buf->Material.MaterialType           = EMT_SOLID;
            buf->Material.Lighting               = false;
            buf->Material.BackfaceCulling        = false;
            buf->Material.PolygonOffsetDirection = irr::video::EPO_FRONT;
            buf->Material.PolygonOffsetFactor    = 5;
            const SColor lineWhite(255, 255, 255, 255);
            const float lHW = 0.15f;
            const float yCL_W = (y00 + y01) * 0.5f + 0.005f;  // west edge at Z=0
            const float yCL_E = (y10 + y11) * 0.5f + 0.005f;  // east edge at Z=0
            addV(buf, -H, yCL_W, -lHW,  0.f, 0.f, lineWhite);  // west, south
            addV(buf,  H, yCL_E, -lHW,  1.f, 0.f, lineWhite);  // east, south
            addV(buf,  H, yCL_E,  lHW,  1.f, 1.f, lineWhite);  // east, north
            addV(buf, -H, yCL_W,  lHW,  0.f, 1.f, lineWhite);  // west, north
            addQuadIdx(buf, 0);
            buf->recalculateBoundingBox();
            mesh->addMeshBuffer(buf);
            buf->drop();
        }
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

// -------------------------------------------------------------------------
// placeRoadMesh — public IRenderer override (delegates to internal version).
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeRoadMesh(int tileX, int tileZ)
{
    // Two-phase flattening (5% max grade):
    //   Phase 1: flatten main tile + all cardinal road neighbors → single flush
    //   Phase 2: re-read heights → build mesh → create node
    //   Phase 3: rebuild neighbor meshes (no re-flatten, no recurse)
    placeRoadMesh(tileX, tileZ, /*flattenTerrain=*/true, /*rebuildNeighbors=*/true);
}

// -------------------------------------------------------------------------
// placeRoadMesh (internal) — terrain-conforming sloped road placement.
//
// Two-phase flattening (flattenTerrain=true only):
//   Phase 1 — flatten: apply 5% max-grade constraint to the main tile AND
//     every cardinal neighbor that already has a road node, then flush once.
//     A single flush ensures all shared corners are committed to the terrain
//     heightmap before any mesh is read back, preventing the cascade-warp
//     that occurs when each tile writes a different average to a shared corner.
//   Phase 2 — build: re-read the 4 corner heights from the now-consistent
//     terrain and build the terrain-conforming LOD0 mesh + scene node.
//   Phase 3 — neighbor rebuild: rebuild each road neighbor's mesh from the
//     current terrain heights (flattenTerrain=false, rebuildNeighbors=false).
//
// Per-tile LOD0 mesh:
//   buildTileRoadMesh(h00, h10, h01, h11) returns a terrain-conforming quad
//   with world-space heights baked into vertex Y.  Node is placed at tile
//   world X/Z centre with Y=0 (heights already encoded in vertices).
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeRoadMesh(int tileX, int tileZ,
                                      bool flattenTerrain, bool rebuildNeighbors)
{
    if (!m_smgr || !m_driver) return;

    // Remove any existing road on this tile first (before mesh build to avoid
    // a stale node blocking the registry slot).
    destroyTileNode(m_roadNodes, tileX, tileZ);

    // Ensure shader and shared LOD1/LOD2 meshes are ready (idempotent).
    initRoadShader();
    ensureRoadMeshes();

    // Affected tile radius for Phase 3.
    // setTileHeight() applies weighted neighbour blending: each call modifies not
    // only the target vertex but also its 8 surrounding vertices (cardinal ×0.5,
    // diagonal ×0.25).  Flattening the 4 corners of this tile therefore touches
    // vertices in the range [tileX-1..tileX+2] × [tileZ-1..tileZ+2].  A road tile
    // at offset (dx, dz) is affected if any of its 4 corner vertices falls in that
    // range.  The maximum Manhattan offset for such a tile is |dx| ≤ 2, |dz| ≤ 2,
    // requiring a 5×5 rebuild area (dx, dz ∈ [-2..+2], centre excluded).
    // Rebuilding only ±1 (8 neighbours) leaves tiles 2 steps away with road meshes
    // built from pre-blend heights while the terrain uses post-blend heights → seam.

    // --- Phase 1: flatten main tile + road neighbors, then flush once ---
    //
    // Maximum road grade: 5% (0.05 rise/run).  If the steepest gradient across
    // the tile exceeds this, all four corner heights are scaled toward their
    // average until the max gradient equals exactly 5%.  Tiles below the
    // threshold are left as-is (roads can tilt naturally up to ~5%).
    //
    // Only setTileHeight() is called here — no flush, no mesh rebuild — so that
    // all height writes for this tile and its neighbors are committed in one
    // flushTerrainRebuilds() call.  This prevents the shared-corner cascade
    // where a neighbor's flush overwrites a corner that this tile already read.
    static constexpr float kMaxRoadGrade = 0.05f;  // 5% grade (0.05 rise/run)

    auto flattenTile = [&](int tx, int tz) {
        if (!m_terrain) return;
        float f00 = m_terrain->getHeightAt(tx,     tz);
        float f10 = m_terrain->getHeightAt(tx + 1, tz);
        float f01 = m_terrain->getHeightAt(tx,     tz + 1);
        float f11 = m_terrain->getHeightAt(tx + 1, tz + 1);

        // Gradient magnitude at each corner (X and Z components separately,
        // combined as sqrt(dX² + dZ²)).
        const float dX0 = (f10 - f00) / kTileSize;
        const float dX1 = (f11 - f01) / kTileSize;
        const float dZ0 = (f01 - f00) / kTileSize;
        const float dZ1 = (f11 - f10) / kTileSize;

        auto mag = [](float a, float b) { return std::sqrt(a*a + b*b); };
        const float gradeMax = std::max({
            mag(dX0, dZ0), mag(dX0, dZ1),
            mag(dX1, dZ0), mag(dX1, dZ1)
        });

        if (gradeMax > kMaxRoadGrade) {
            const float scale = kMaxRoadGrade / gradeMax;
            const float avg = (f00 + f10 + f01 + f11) * 0.25f;
            f00 = avg + (f00 - avg) * scale;
            f10 = avg + (f10 - avg) * scale;
            f01 = avg + (f01 - avg) * scale;
            f11 = avg + (f11 - avg) * scale;
            m_terrain->setTileHeight(tx,     tz,     f00);
            m_terrain->setTileHeight(tx + 1, tz,     f10);
            m_terrain->setTileHeight(tx,     tz + 1, f01);
            m_terrain->setTileHeight(tx + 1, tz + 1, f11);
        }
        // Grade ≤ 5%: leave heights as-is; road will naturally tilt.
    };

    if (flattenTerrain && m_terrain) {
        // Flatten ONLY the main tile.  Flattening neighbors here would cause each
        // neighbor's flattenTile() to read the already-modified shared corners
        // (written by the main tile), compute a different average, and overwrite
        // those same corners with a different value.  Phase 2 would then re-read
        // the neighbor-corrupted values and produce an inconsistent main tile mesh.
        // Neighbors need their meshes rebuilt from the correct terrain (Phase 3),
        // not their terrain re-flattened.
        flattenTile(tileX, tileZ);
        // Single flush: commits the height writes to the terrain heightmap and
        // triggers chunk rebuilds.  No further setTileHeight calls follow.
        m_terrain->flushTerrainRebuilds();
    }

    // --- Phase 2: re-read heights from terrain (after all modifications) ---
    // Reading back after the flush guarantees the mesh is built from the same
    // height values that are now stored in the terrain, matching every neighbor.
    float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
    float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
    float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
    float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;

    // Detect E/W orientation: tile has at least one E/W neighbour but no N/S neighbours.
    // Intersections and T-junctions (both N/S and E/W neighbours present) use N/S geometry.
    const bool hasNS_dir = m_roadNodes.count(tileKey(tileX, tileZ - 1)) > 0
                        || m_roadNodes.count(tileKey(tileX, tileZ + 1)) > 0;
    const bool hasEW_dir = m_roadNodes.count(tileKey(tileX + 1, tileZ)) > 0
                        || m_roadNodes.count(tileKey(tileX - 1, tileZ)) > 0;
    const bool isEW = hasEW_dir && !hasNS_dir;

    // --- Build per-tile LOD0 terrain-conforming mesh ---
    SMesh* tileMesh = buildTileRoadMesh(h00, h10, h01, h11, isEW);
    if (!tileMesh) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeRoadMesh(%d,%d): buildTileRoadMesh"
            " failed — node not created", tileX, tileZ);
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    // Create scene node.  addMeshSceneNode() calls grab() → tileMesh ref_count 1→2.
    IMeshSceneNode* node = m_smgr->addMeshSceneNode(tileMesh);
    tileMesh->drop();  // release caller's ref; scene node now sole owner
    if (!node) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeRoadMesh(%d,%d): addMeshSceneNode"
            " failed — node not created", tileX, tileZ);
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    // Position node at tile world X/Z centre with Y=0.
    // All height is encoded in vertex Y values (absolute world-space heights);
    // node Y must be 0 so vertex positions are not double-offset.
    node->setPosition(core::vector3df(
        static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
        0.0f,
        static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
    node->setScale(core::vector3df(1.0f, 1.0f, 1.0f));

    // Disable Irrlicht's automatic box frustum culling for road tiles.
    // Road tile meshes are nearly flat so their AABB has little vertical headroom.
    // At oblique camera angles tiles near the frustum boundary can be false-rejected
    // by EAC_BOX even with the 0.5 m Y-extent expansion in buildTileRoadMesh().
    // Road tiles are small (10 m × 10 m); skipping the AABB test is negligible.
    node->setAutomaticCulling(irr::scene::EAC_OFF);

    // Disable lighting, back-face culling, and re-apply polygon offset on the node's
    // own material copies.  Irrlicht's CMeshSceneNode initialises its internal
    // material list from the mesh buffer materials, but the copy behaviour is not
    // guaranteed — re-setting all three properties here ensures the driver always
    // receives the correct values regardless of the Irrlicht version or copy path.
    // BackfaceCulling=false: the road quad is nearly flat but can tilt up to 5%.
    // At oblique camera angles one of the two triangles can face away, causing half
    // the tile to disappear.  Disabling back-face culling makes both triangles always
    // visible regardless of terrain slope or camera elevation.
    //
    // Buffer layout: [0]=carriageway, [1]=south kerb, [2]=north kerb, [3]=center-line.
    // The center-line buffer (index 3) uses PolygonOffsetFactor=5 (set in the mesh
    // buffer) to keep it on top of the carriageway (factor=4).  Preserve that value
    // here — only reset the offset factor for buffers 0–2.
    for (u32 m = 0; m < node->getMaterialCount(); ++m) {
        SMaterial& mat = node->getMaterial(m);
        mat.Lighting               = false;
        mat.BackfaceCulling        = false;
        mat.PolygonOffsetDirection = irr::video::EPO_FRONT;
        // Buffer 3 is the center-line; keep its factor=5 from mesh creation.
        if (m != 3) mat.PolygonOffsetFactor = 4;
    }

    // Wrap in LODNode.
    // LOD transitions (swap to m_sharedRoadMeshLOD1/LOD2) are a future per-frame
    // update path; distance thresholds stored for documentation.
    static constexpr float kRoadLOD0to1 = 50.0f;
    static constexpr float kRoadLOD1to2 = 150.0f;
    static constexpr float kRoadCullDist = 300.0f;
    (void)kRoadLOD0to1; (void)kRoadLOD1to2; (void)kRoadCullDist;

    LODNode* lodNode = new LODNode(node);
    m_roadNodes[tileKey(tileX, tileZ)] = lodNode;

    // --- Phase 3: rebuild road meshes in the 5×5 affected area ---
    // Terrain was already flushed in Phase 1.  Any road tile within ±2 tiles
    // may have had a corner vertex modified by setTileHeight's blending; rebuild
    // all such tiles so their meshes match the updated terrain.
    // flattenTerrain=false: no second height writes (prevents cascade).
    // rebuildNeighbors=false: prevents infinite recursion.
    if (rebuildNeighbors) {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (dx == 0 && dz == 0) continue;  // main tile already built above
                const int nx = tileX + dx;
                const int nz = tileZ + dz;
                if (m_roadNodes.count(tileKey(nx, nz)) > 0) {
                    placeRoadMesh(nx, nz, /*flattenTerrain=*/false,
                                          /*rebuildNeighbors=*/false);
                }
            }
        }
    }
}

// -------------------------------------------------------------------------
// removeRoadMesh — road tile removal.
// -------------------------------------------------------------------------
void IrrlichtRenderer::removeRoadMesh(int tileX, int tileZ)
{
    destroyTileNode(m_roadNodes, tileX, tileZ);
}

// -------------------------------------------------------------------------
// placeServiceBuildingMesh — service building placement.
//
// Asset path derived from ServiceBuildingType:
//   PowerPlant    → assets/3d/buildings/svc_power_plant
//   WaterTower    → assets/3d/buildings/svc_water_tower
//   FireStation   → assets/3d/buildings/svc_fire_station
//   PoliceStation → assets/3d/buildings/svc_police_station
// LOD thresholds read from the corresponding .meta sidecar (small building
// category: 30/25 m close, 100/90 m far, billboard LOD2).
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeServiceBuildingMesh(int tileX, int tileZ,
                                                  ServiceBuildingType type)
{
    if (!ensureAssetLoader()) return;

    // Derive the asset stem from the ServiceBuildingType enum.
    const char* stem = nullptr;
    switch (type) {
        case ServiceBuildingType::PowerPlant:    stem = "svc_power_plant";    break;
        case ServiceBuildingType::WaterTower:    stem = "svc_water_tower";    break;
        case ServiceBuildingType::FireStation:   stem = "svc_fire_station";   break;
        case ServiceBuildingType::PoliceStation: stem = "svc_police_station"; break;
    }
    if (!stem) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeServiceBuildingMesh(%d,%d): "
            "unknown ServiceBuildingType %d — skipping",
            tileX, tileZ, static_cast<int>(type));
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    // Remove any existing building on this tile first.
    destroyTileNode(m_buildingNodes, tileX, tileZ);

    // Full-footprint terrain flattening (Phase 11l Deliverable 3):
    // Service buildings have a fixed 2x2 tile footprint (footprintN = 2).
    // Average all (footprintN+1)x(footprintN+1) = 3x3 = 9 corner vertex heights,
    // then flatten every corner to that average so the ground plate sits flush
    // with terrain across the entire 2x2 footprint.
    // Runs unconditionally when m_terrain is set — independent of whether the scene
    // node is created successfully, so terrain is always consistent on placement.
    static constexpr int kSvcFootprintN = 2;
    float svcHeightSum   = 0.0f;
    int   svcHeightCount = 0;
    for (int cx = 0; cx <= kSvcFootprintN; ++cx) {
        for (int cz = 0; cz <= kSvcFootprintN; ++cz) {
            svcHeightSum += m_terrain ? m_terrain->getHeightAt(tileX + cx, tileZ + cz) : 0.0f;
            ++svcHeightCount;
        }
    }
    const float svcTargetH = (svcHeightCount > 0) ? (svcHeightSum / svcHeightCount) : 0.0f;
    if (m_terrain) {
        for (int cx = 0; cx <= kSvcFootprintN; ++cx)
            for (int cz = 0; cz <= kSvcFootprintN; ++cz)
                m_terrain->setTileHeight(tileX + cx, tileZ + cz, svcTargetH);
        m_terrain->flushTerrainRebuilds();
    }

    // Rebuild road tiles within +(kSvcFootprintN+2) of the service building origin.
    // setTileHeight() applies weighted neighbour blending to surrounding vertices;
    // for the 2x2 footprint the affected radius grows to kSvcFootprintN + 2 = 4.
    {
        const int rebuildRadius = kSvcFootprintN + 2;
        for (int dz = -rebuildRadius; dz <= kSvcFootprintN + rebuildRadius; ++dz) {
            for (int dx = -rebuildRadius; dx <= kSvcFootprintN + rebuildRadius; ++dx) {
                if (dx >= 0 && dx < kSvcFootprintN && dz >= 0 && dz < kSvcFootprintN) continue;
                const int nx = tileX + dx;
                const int nz = tileZ + dz;
                if (m_roadNodes.count(tileKey(nx, nz)) > 0)
                    placeRoadMesh(nx, nz, /*flattenTerrain=*/false,
                                          /*rebuildNeighbors=*/false);
            }
        }
    }

    std::string basePath = std::string(AITOWN_ASSETS_DIR) +
                           "/3d/buildings/" + stem;

    LODNode* lodNode = m_buildingAssetLoader->load(basePath);
    if (!lodNode) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeServiceBuildingMesh(%d,%d): "
            "failed to load asset '%s' — node not created",
            tileX, tileZ, basePath.c_str());
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    if (scene::ISceneNode* node = lodNode->getNode()) {
        // Use svcTargetH directly — NOT getHeightAt() after setTileHeight().
        // setTileHeight() applies neighbour blending to the 8 surrounding tiles;
        // subsequent corner calls bleed back into vertex (tileX, tileZ), leaving
        // its stored height below svcTargetH. getHeightAt() would return that
        // blended-down value and position the node below the rendered terrain surface.
        const float postY = m_terrain ? svcTargetH : 0.0f;
        // Phase 11h: service buildings have a 2×2 footprint.
        // World center of the 2×2 footprint: (tileX + 1.0) * kTileSize, same for Z.
        // The origin tile is (tileX, tileZ); footprint spans [tileX, tileX+2) × [tileZ, tileZ+2),
        // so the geometric centre is tileX*kTileSize + kTileSize = (tileX+1)*kTileSize.
        node->setPosition(core::vector3df(
            (static_cast<f32>(tileX) + 1.0f) * kTileSize,
            postY + 0.10f,
            (static_cast<f32>(tileZ) + 1.0f) * kTileSize));
        // Phase 11h: service buildings are authored at world scale — no tile-size scaling.
        node->setScale(core::vector3df(1.0f, 1.0f, 1.0f));

        // Zone-colour fallback (amber) for service buildings — only used when
        // the atlas was not successfully bound by BuildingAssetLoader::load().
        video::ITexture* zoneTex = getOrCreateZoneTexture("svc_");

        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            video::SMaterial& mat = node->getMaterial(m);
            mat.Lighting = false;
            // Disable backface culling — same rationale as placeBuildingMesh().
            mat.BackfaceCulling = false;
            // Polygon offset: push the service building base toward the camera so it
            // wins the depth test against the co-planar terrain quad at all distances.
            mat.PolygonOffsetDirection = irr::video::EPO_FRONT;
            mat.PolygonOffsetFactor    = 1;
            if (!mat.getTexture(0) && zoneTex) mat.setTexture(0, zoneTex);
        }
    }

    // Service buildings share the m_buildingNodes registry with zone buildings:
    // a tile can hold at most one building of any type (simulation invariant).
    m_buildingNodes[tileKey(tileX, tileZ)] = lodNode;
}

// -------------------------------------------------------------------------
// removeServiceBuildingMesh — service building removal.
// -------------------------------------------------------------------------
void IrrlichtRenderer::removeServiceBuildingMesh(int tileX, int tileZ)
{
    // Service buildings use the same m_buildingNodes registry as zone buildings.
    destroyTileNode(m_buildingNodes, tileX, tileZ);
}

// =============================================================================
// Phase 10 — Vehicle rendering
//
// Vehicles are spawned by the traffic simulation and identified by a stable
// uint32_t vehicleId. They use the same B3D loading path as buildings
// (BuildingAssetLoader), but with a separate loader instance and the vehicle
// diffuse atlas (vehicles_diffuse_atlas_d.dds) rather than the buildings atlas.
//
// Vehicles are authored at world scale — no setScale() is applied.
// Y-axis rotation (yawDegrees) is applied via setRotation(0, yaw, 0).
//
// Vehicle nodes are stored in m_vehicleNodes (keyed by vehicleId).
// The eviction sequence on removal mirrors destroyTileNode().
//
// main-thread-only — do NOT call from audio thread or simulation thread.
// =============================================================================

// -------------------------------------------------------------------------
// ensureVehicleLoader — lazily construct m_vehicleAssetLoader.
// Returns true if the loader is ready; false if m_smgr/m_driver is null.
// -------------------------------------------------------------------------
bool IrrlichtRenderer::ensureVehicleLoader()
{
    if (m_vehicleAssetLoader) return true;
    if (!m_smgr || !m_driver) {
        if (m_logger) {
            m_logger->log("[IrrlichtRenderer] ensureVehicleLoader() called with null "
                "m_smgr/m_driver — vehicle scene node cannot be created", irr::ELL_WARNING);
        } else {
            fprintf(stderr,
                "[IrrlichtRenderer WARNING] ensureVehicleLoader() called with null "
                "m_smgr/m_driver — vehicle scene node cannot be created\n");
        }
        return false;
    }
    m_vehicleAssetLoader = std::make_unique<BuildingAssetLoader>(m_smgr, m_driver);
    return true;
}

// -------------------------------------------------------------------------
// destroyVehicleNode — eviction sequence + LODNode deletion for one vehicle.
//
// Per scene-graph-ownership.md eviction sequence:
//   Step 1: Clear all material texture slots.
//   Step 2: driver->setMaterial(SMaterial{}) — flush driver last-bound state.
//   Step 3: node->remove() — release from scene graph.
//   Step 4: delete the LODNode wrapper (non-owning — scene node already removed).
//
// No-op if vehicleId is not in m_vehicleNodes.
// -------------------------------------------------------------------------
void IrrlichtRenderer::destroyVehicleNode(uint32_t vehicleId)
{
    auto it = m_vehicleNodes.find(vehicleId);
    if (it == m_vehicleNodes.end() || !it->second) return;

    LODNode* lodNode = it->second;
    scene::ISceneNode* node = lodNode->getNode();

    if (node && m_driver) {
        // Step 1: clear material texture slots.
        u32 matCount = node->getMaterialCount();
        for (u32 m = 0; m < matCount; ++m) {
            SMaterial& mat = node->getMaterial(m);
            for (u32 t = 0; t < MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        // Step 2: flush driver last-bound state.
        
        // Step 3: remove scene node from the scene graph.
        node->remove();  // do NOT access node* after this
    }

    // Step 4: delete the LODNode wrapper.
    delete lodNode;
    m_vehicleNodes.erase(it);
}

// -------------------------------------------------------------------------
// placeVehicle — load vehicle B3D assets and create a scene node.
//
// If vehicleId is already registered, the old node is removed first.
// Asset base path: AITOWN_ASSETS_DIR/3d/vehicles/<assetName>
// BuildingAssetLoader::load() appends _lod0.b3d, _lod1.b3d, .meta.
//
// Vehicles are authored at world scale; no setScale() is applied.
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeVehicle(uint32_t vehicleId,
                                     const std::string& assetName,
                                     float worldX, float worldY, float worldZ,
                                     float yawDegrees)
{
    if (assetName.empty()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeVehicle(id=%u) called with empty "
            "assetName — skipping node creation", vehicleId);
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    // Remove any existing node for this vehicleId before placing the new one.
    destroyVehicleNode(vehicleId);

    if (!ensureVehicleLoader()) return;

    const std::string basePath = std::string(AITOWN_ASSETS_DIR)
                                 + "/3d/vehicles/" + assetName;

    LODNode* lodNode = m_vehicleAssetLoader->load(basePath);
    if (!lodNode) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] placeVehicle(id=%u): failed to load "
            "asset '%s' — node not created", vehicleId, basePath.c_str());
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    scene::ISceneNode* node = lodNode->getNode();
    if (node) {
        // Position at the given world-space coordinates.
        node->setPosition(core::vector3df(worldX, worldY, worldZ));
        // Apply Y-axis yaw rotation (0=+Z forward, 90=+X right).
        node->setRotation(core::vector3df(0.0f, yawDegrees, 0.0f));
        // Vehicles are authored at world scale — do NOT apply tile-based setScale.

        // Apply material settings.
        // BackfaceCulling=false: procedural B3D assets may have inverted or
        // mixed winding after the axis-reorientation pass; disabling culling
        // guarantees all faces are visible from any camera angle, matching
        // the approach used for building assets.
        // Lighting=false: no light nodes in scene yet (Phase 6+).
        // Atlas fallback: if BuildingAssetLoader did not bind the atlas (file missing),
        // bind vehicles_diffuse_atlas_d.dds directly as a safety fallback.
        const std::string atlasPath = std::string(AITOWN_ASSETS_DIR)
            + "/textures/vehicles/vehicles_diffuse_atlas_d.dds";

        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            SMaterial& mat = node->getMaterial(m);
            mat.Lighting        = false;
            mat.BackfaceCulling = false;
            // Bind vehicle atlas as fallback only when slot 0 is still unbound.
            if (!mat.getTexture(0) && m_driver) {
                ITexture* atlas = m_driver->getTexture(atlasPath.c_str());
                if (atlas) mat.setTexture(0, atlas);
            }
        }
    }

    m_vehicleNodes[vehicleId] = lodNode;
}

// -------------------------------------------------------------------------
// moveVehicle — update position/yaw of an existing vehicle node.
//
// If vehicleId is unknown, delegates to placeVehicle and returns.
// -------------------------------------------------------------------------
void IrrlichtRenderer::moveVehicle(uint32_t vehicleId,
                                    float worldX, float worldY, float worldZ,
                                    float yawDegrees)
{
    auto it = m_vehicleNodes.find(vehicleId);
    if (it == m_vehicleNodes.end() || !it->second) {
        // Unknown vehicleId — treat as a late-arriving place call.
        // assetName is not available here; caller must use placeVehicle() for
        // first-time placement.  Log and return to avoid a crash.
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[IrrlichtRenderer] moveVehicle(id=%u): vehicleId not "
            "registered — ignoring move (caller should use placeVehicle first)",
            vehicleId);
        if (m_logger) {
            m_logger->log(buf, irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
        }
        return;
    }

    scene::ISceneNode* node = it->second->getNode();
    if (node) {
        node->setPosition(core::vector3df(worldX, worldY, worldZ));
        node->setRotation(core::vector3df(0.0f, yawDegrees, 0.0f));
    }
}

// -------------------------------------------------------------------------
// removeVehicle — destroy the vehicle scene node for vehicleId.
// No-op if vehicleId is not registered.
// -------------------------------------------------------------------------
void IrrlichtRenderer::removeVehicle(uint32_t vehicleId)
{
    destroyVehicleNode(vehicleId);
}

// =============================================================================
// Phase 10b — Sky cloud dome
//
// A tessellated hemisphere dome (kDomeRings × kDomeSectors) centred above the
// camera, scrolling the cloud texture each frame to simulate wind movement.
// Replaces the original flat-quad approach: the dome geometry ensures cloud
// coverage extends from directly overhead all the way to the horizon (360°).
//
// Design rationale:
//   - A flat plane at Y=200 m with half-extent 1000 m covers only a limited
//     angular range; at camera heights of 30–200 m the plane edge is well
//     above the geometric horizon, leaving the upper sky bare.
//   - A large dome (radius 4000 m, height 800 m) subtends angles from 90°
//     (zenith) down to arctan(800/4000) ≈ 11° above the mathematical horizon,
//     so clouds appear to reach the horizon from any camera position.
//   - Vertex colour alpha fades from 255 at the top ring to 0 at the bottom
//     ring, blending clouds into the sky colour at the horizon edge.
//   - The dome node is repositioned to camera XZ each frame so the horizon
//     ring is never reached regardless of player movement.
//
// The cloud texture (clouds.png, RGBA) is loaded via getTexture() — the linear
// pool, not TextureCache::loadSRGB(). UV translation is advanced each frame
// by update() using std::fmod to prevent float accumulation.
//
// (ref: architecture/graphics-architecture/sky-clouds.md)
// =============================================================================

// -------------------------------------------------------------------------
// buildCloudDomeMesh — helper that constructs and returns the cloud dome SMesh.
//
// Dome geometry (Irrlicht left-handed, Y-up):
//   - A single shared apex vertex at the dome top (index 0).
//   - kDomeRings rings of kDomeSectors+1 vertices each, numbered ring 1..kDomeRings
//     (ring 1 = first latitude band below apex, ring kDomeRings = base).
//   - Top cap: kDomeSectors triangles fanning from the shared apex vertex to the
//     first ring — no degenerate triangles (each fan triangle has three distinct
//     world positions).
//   - Body: (kDomeRings-1) × kDomeSectors quads (2 triangles each).
//
// Vertex layout in the buffer:
//   index 0                                  — shared apex vertex
//   indices 1 .. (kDomeSectors+1)            — ring 1 (kDomeSectors+1 vertices)
//   indices (kDomeSectors+1)+1 .. 2*(kDomeSectors+1) — ring 2
//   ...
//   General: ring r (1-based) starts at offset 1 + (r-1)*(kDomeSectors+1).
//
// UV mapping (cylindrical):
//   u = phi_norm * kCloudUVScale  (0 at sec=0, kCloudUVScale at sec=kDomeSectors)
//   v = t * kCloudUVScale         (0 at apex, kCloudUVScale at base ring)
//   Apex vertex: u=0.5*kCloudUVScale, v=0 (centred; not sampled by fan edges).
//   Texture wrap mode ETC_REPEAT is set on the material so tiling works correctly.
//
// Vertex colour: alpha=255 everywhere — horizon fade is applied in the fragment
//   shader (cloud_dome.frag) using the elevation angle from the camera.
//
// Winding: CW from inside the dome (camera looks up and outward).
//   Irrlicht back-face culling is disabled so the inner surface is always rendered.
//
// SMesh lifetime: caller owns the returned pointer (ref_count=1) and MUST call
// smesh->drop() after addMeshSceneNode() to release its own reference.
// -------------------------------------------------------------------------
static SMesh* buildCloudDomeMesh()
{
    constexpr int   kDomeRings         = 32;     // latitude bands — keep fade smooth
    constexpr int   kDomeSectors       = 32;     // longitude segments
    constexpr float kCloudAltitude     = -1000.0f;  // base ring 1000 m below camera — atan(-1000/6000)≈-9.5°,
                                                    // safely below the horizon so base is fully transparent
    constexpr float kCloudDomeRadius   = 6000.0f;   // horizontal radius — must be < far clip (15000 m)
    constexpr float kCloudDomeHeight   = 2000.0f;   // apex 1000 m above camera (-1000+2000), base 1000 m below
    constexpr float kCloudUVScale      = 4.0f;   // texture tiling factor
    // Vertex alpha: always 255 (fully opaque in vertex color).
    // Horizon fade is handled entirely in the fragment shader using elevation angle.
    // In EMT_TRANSPARENT_VERTEX_ALPHA fallback (shader compile failure) the dome
    // renders as opaque vertex-coloured shell — acceptable degraded fallback.
    // UV is cylindrical (not polar) for uniform texture distribution at each ring.

    SMesh*       mesh = new SMesh();
    SMeshBuffer* buf  = new SMeshBuffer();

    const float piF = static_cast<float>(M_PI);

    // -----------------------------------------------------------------------
    // Vertex 0: shared apex (single vertex, no degenerate triangles at the top).
    // The apex sits at local position (0, kCloudAltitude + kCloudDomeHeight, 0)
    // relative to the node, which tracks the camera position each frame.
    // Its UV is centred (u=kCloudUVScale*0.5, v=0) — this value is only used by
    // the fan cap triangles and is never interpolated across a seam.
    // Apex vertex alpha = 255; shader zeroes it via fade = 0 above +40°.
    // -----------------------------------------------------------------------
    const float apexY = kCloudAltitude + kCloudDomeHeight;
    buf->Vertices.push_back(S3DVertex(
        core::vector3df(0.0f, apexY, 0.0f),
        core::vector3df(0.0f, 1.0f, 0.0f),          // straight-up inward normal at apex
        SColor(255, 255, 255, 255),
        core::vector2df(kCloudUVScale * 0.5f, 0.0f)));

    // -----------------------------------------------------------------------
    // Rings 1 .. kDomeRings.
    // Ring index r (1-based): t = r / kDomeRings.
    // Each ring has kDomeSectors+1 vertices so that sector 0 (u=0) and sector
    // kDomeSectors (u=kCloudUVScale) share the same XZ world position but carry
    // distinct U coordinates, giving the texture sampler two separate texels to
    // interpolate between rather than wrapping across a seam fold.  With
    // ETC_REPEAT on the material this is seamless.
    //
    // All vertices: alpha=255; horizon fade is handled entirely in cloud_dome.frag.
    // -----------------------------------------------------------------------
    for (int ring = 1; ring <= kDomeRings; ++ring) {
        const float t  = static_cast<float>(ring) / static_cast<float>(kDomeRings);
        const float y  = kCloudAltitude + kCloudDomeHeight * (1.0f - t);
        const float r  = kCloudDomeRadius * t;  // horizontal radius at this ring

        // All vertices white (255,255,255,255) — horizon fade and haze blend are
        // handled entirely in cloud_dome.frag via elevation-angle smoothstep.
        const irr::u32 vtxAlpha = 255u;
        const irr::u32 vtxR     = 255u;
        const irr::u32 vtxG     = 255u;
        const irr::u32 vtxB     = 255u;

        for (int sec = 0; sec <= kDomeSectors; ++sec) {
            const float phi      = static_cast<float>(sec)
                                   / static_cast<float>(kDomeSectors) * 2.0f * piF;
            const float nx       = std::sin(phi);
            const float nz       = std::cos(phi);
            const float phi_norm = static_cast<float>(sec)
                                   / static_cast<float>(kDomeSectors);
            const float u = phi_norm * kCloudUVScale;
            const float v = t        * kCloudUVScale;

            buf->Vertices.push_back(S3DVertex(
                core::vector3df(r * nx, y, r * nz),
                core::vector3df(-nx, 1.0f, -nz),    // approximate inward normal
                SColor(vtxAlpha, vtxR, vtxG, vtxB),
                core::vector2df(u, v)));
        }
    }

    // -----------------------------------------------------------------------
    // Index buffer.
    //
    // Helper: offset of sector s in ring r (1-based) within the vertex buffer.
    //   ringOffset(r, s) = 1 + (r-1)*(kDomeSectors+1) + s
    // -----------------------------------------------------------------------
    auto ringOffset = [&](int r, int s) -> irr::u16 {
        return static_cast<irr::u16>(1 + (r - 1) * (kDomeSectors + 1) + s);
    };

    // Top cap: kDomeSectors fan triangles from the apex (vertex 0) to ring 1.
    // CW winding from inside (looking up): apex → ring1[s+1] → ring1[s].
    for (int sec = 0; sec < kDomeSectors; ++sec) {
        buf->Indices.push_back(static_cast<irr::u16>(0));    // apex
        buf->Indices.push_back(ringOffset(1, sec + 1));
        buf->Indices.push_back(ringOffset(1, sec));
    }

    // Body: quads for rings 1..(kDomeRings-1) → rings 2..kDomeRings.
    // Each quad (ring r, sector s):
    //   tl = ring r,   sector s        tr = ring r,   sector s+1
    //   bl = ring r+1, sector s        br = ring r+1, sector s+1
    // CW from inside: tl → bl → tr  and  bl → br → tr.
    for (int ring = 1; ring < kDomeRings; ++ring) {
        for (int sec = 0; sec < kDomeSectors; ++sec) {
            const irr::u16 tl = ringOffset(ring,     sec);
            const irr::u16 tr = ringOffset(ring,     sec + 1);
            const irr::u16 bl = ringOffset(ring + 1, sec);
            const irr::u16 br = ringOffset(ring + 1, sec + 1);

            // Triangle 1: tl → bl → tr  (CW from inside)
            buf->Indices.push_back(tl);
            buf->Indices.push_back(bl);
            buf->Indices.push_back(tr);
            // Triangle 2: bl → br → tr  (CW from inside)
            buf->Indices.push_back(bl);
            buf->Indices.push_back(br);
            buf->Indices.push_back(tr);
        }
    }

    // Mandatory bounding box recalculation before attaching to scene graph
    // (per procedural-terrain.md SMesh lifetime rule).
    buf->recalculateBoundingBox();
    mesh->addMeshBuffer(buf);
    buf->drop();                     // SMesh::addMeshBuffer grab()d buf → release caller ref
    mesh->recalculateBoundingBox();  // AFTER all buffer recalculations

    return mesh;  // ref_count = 1; caller must ->drop() after addMeshSceneNode()
}

// -------------------------------------------------------------------------
// initCloudPlane — build the cloud dome mesh and create its scene node.
//
// Guards with if (m_driverType == EDT_NULL) return; as the FIRST line so that
// headless CI runs (which use EDT_NULL) never construct the mesh or call
// getTexture(), both of which are undefined under EDT_NULL.
// -------------------------------------------------------------------------
void IrrlichtRenderer::initCloudPlane()
{
    if (m_driverType == irr::video::EDT_NULL) return;  // headless guard — MUST be first

    if (!m_smgr || !m_driver) return;

    SMesh* mesh = buildCloudDomeMesh();

    m_cloudNode = m_smgr->addMeshSceneNode(mesh);  // grab() called internally
    mesh->drop();  // release caller's ref — scene node is now sole owner

    if (m_cloudNode) {
        // Load cloud texture from linear pool (PNG — Irrlicht DDS loader disabled).
        // Per sky-clouds.md: IVideoDriver::getTexture(), NOT TextureCache::loadSRGB().
        ITexture* tex = m_driver->getTexture("assets/textures/sky/clouds.png");

        // --- Cloud dome shader ---
        // Load cloud_dome.vert / cloud_dome.frag via the GPU programming services.
        // The shader multiplies tex.a * v_fadeAlpha so that non-cloud texels (tex.a=0)
        // are fully transparent regardless of vertex alpha.  This eliminates the blue-dome
        // arc artefact produced by EMT_TRANSPARENT_VERTEX_ALPHA, which ignores texture
        // alpha entirely and lets the texture's non-sky RGB show through the fade band.
        //
        // Base material EMT_TRANSPARENT_ALPHA_CHANNEL sets GL_SRC_ALPHA,
        // GL_ONE_MINUS_SRC_ALPHA blending — correct for semi-transparent cloud overlays.
        //
        // Fallback: if shader loading fails (returns -1, e.g. no GLSL support) we fall
        // back to EMT_TRANSPARENT_VERTEX_ALPHA, which is the original behaviour.
        irr::s32 cloudMatType = EMT_TRANSPARENT_VERTEX_ALPHA;  // fallback

        irr::video::IGPUProgrammingServices* gpu = m_driver->getGPUProgrammingServices();
        if (gpu) {
            const std::string vsPath =
                std::string(AITOWN_ASSETS_DIR) + "/shaders/cloud_dome.vert";
            const std::string fsPath =
                std::string(AITOWN_ASSETS_DIR) + "/shaders/cloud_dome.frag";

            // CloudDomeShaderCallback: raw heap allocation.  We keep our own reference
            // (stored as m_cloudShaderCbRaw) so the destructor can drop it.
            // Irrlicht also calls grab() internally on the passed pointer.  The caller's
            // reference is dropped in IrrlichtRenderer::~IrrlichtRenderer().
            // Never std::unique_ptr — causes double-free (see CLAUDE.md shader callbacks).
            CloudDomeShaderCallback* cb = new CloudDomeShaderCallback();
            irr::s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
                vsPath.c_str(), "main", irr::video::EVST_VS_1_1,
                fsPath.c_str(), "main", irr::video::EPST_PS_1_1,
                cb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
            // Do NOT drop cb unconditionally here — we store our reference for its
            // lifetime.  Drop only on shader failure (we have no further use for it).
            if (matType == -1) {
                char buf[512];
                std::snprintf(buf, sizeof(buf),
                    "[IrrlichtRenderer] cloud dome shader compile failed "
                    "(vs=%s, fs=%s) — falling back to EMT_TRANSPARENT_VERTEX_ALPHA",
                    vsPath.c_str(), fsPath.c_str());
                if (m_logger) {
                    m_logger->log(buf, irr::ELL_WARNING);
                } else {
                    fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", buf);
                }
                cb->drop();   // shader failed — discard our reference
                // cloudMatType stays EMT_TRANSPARENT_VERTEX_ALPHA (set above).
            } else {
                cloudMatType = matType;
                m_cloudShaderCbRaw = cb;  // store caller's reference; dropped in destructor
            }
        }

        auto& mat = m_cloudNode->getMaterial(0);
        mat.MaterialType    = static_cast<irr::video::E_MATERIAL_TYPE>(cloudMatType);
        mat.Lighting        = false;
        // Back-face culling must be off: camera is inside the dome looking outward/upward,
        // so only the inner surface is visible. Irrlicht's default CW-front-face culls the
        // outer surface, which is correct. However, disabling culling guarantees visibility
        // from any camera tilt without winding analysis.
        mat.BackfaceCulling = false;
        // Transparent domes must NOT write to the depth buffer.
        // If ZWriteEnable is left on (the default), the dome surface deposits depth values
        // that can occlude terrain geometry rendered in the same pass — most visibly as a
        // hard arc where the dome's partially-transparent lower band intersects the terrain
        // frustum in one azimuth direction.  EZW_OFF disables all depth writes while still
        // reading depth (the dome correctly sits behind foreground objects).
        mat.ZWriteEnable    = false;  // Irrlicht 1.8.5: bool (EZW_OFF is 1.9+ only)
        // Texture tiling: kCloudUVScale=4 means UV coordinates reach 4.0 at the dome
        // base ring.  Without ETC_REPEAT the driver may clamp at 1.0, producing a hard
        // edge / seam wherever u or v crosses 1.  ETC_REPEAT must be set explicitly
        // because getTexture() does not guarantee a particular default wrap state.
        mat.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
        mat.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
        if (tex) mat.setTexture(0, tex);
    }

    // m_cloudUVOffset initialised to {0.f, 0.f} by member initialiser in header.

    // NOTE: Ground plane REMOVED.  The previous sky-blue ground plane at Y=-5
    // was intended to fill the void beyond the finite terrain mesh.  However,
    // because it used ZWriteEnable=true (opaque depth writes), it deposited
    // depth values into the depth buffer.  At the elevation angle where the
    // ground plane's depth equals the cloud dome's depth (~1.8 deg below
    // horizontal at default zoom), the cloud dome's semi-transparent fade-band
    // fragments FAILED the depth test and were discarded — creating a sharp
    // horizontal arch/band artifact at that angle.
    //
    // The ground plane was the same sky-blue as the clear colour, so removing
    // it changes nothing visually: the clear colour already fills the void
    // behind the terrain.  Without the ground plane's depth writes, the cloud
    // dome's fade band renders correctly at all elevation angles and the arch
    // artifact is eliminated.
}

// -------------------------------------------------------------------------
// update — per-frame cloud UV scrolling and dome repositioning.
//
// Guards with if (m_cloudNode) — null under EDT_NULL (initCloudPlane early-return)
// and before init() runs. Uses std::fmod to atomically increment and wrap each
// UV component to [0,1) preventing float accumulation over long sessions.
//
// Dome repositioning: the dome node is moved to the camera's XZ position each
// frame (Y stays at 0 — dome vertices embed absolute world-space Y coordinates).
// This keeps the horizon ring always centred on the player regardless of movement.
//
// Scroll speeds (per sky-clouds.md):
//   kCloudScrollX = 0.002f UV units/second (primary wind direction)
//   kCloudScrollZ = 0.0008f UV units/second (secondary drift)
// -------------------------------------------------------------------------
void IrrlichtRenderer::update(float dt)
{
    if (!m_cloudNode) return;

    constexpr float kCloudScrollX = 0.002f;
    constexpr float kCloudScrollZ = 0.0008f;

    m_cloudUVOffset.X = std::fmod(m_cloudUVOffset.X + kCloudScrollX * dt, 1.0f);
    m_cloudUVOffset.Y = std::fmod(m_cloudUVOffset.Y + kCloudScrollZ * dt, 1.0f);

    m_cloudNode->getMaterial(0)
        .getTextureMatrix(0)
        .setTextureTranslate(m_cloudUVOffset.X, m_cloudUVOffset.Y);

    // Node tracks full camera position (X, Y, Z) so dome stays centred on camera at all heights.
    // m_lastCameraPosition is updated by setCamera() every frame before update() runs.
    if (m_camera) {
        const core::vector3df camPos = m_camera->getPosition();
        // Node tracks full camera XYZ — dome stays centred on camera at all heights.
        // The vertex shader uses gl_Vertex.y directly (already cam-relative local
        // space) so no per-frame camera-Y uniform is needed.
        m_cloudNode->setPosition(camPos);
    }
}

// -------------------------------------------------------------------------
// Phase 11d Deliverable 3a — Vehicle agent rendering.
//
// Agent nodes are plain IMeshSceneNode* stored in m_agentNodes, keyed by
// AgentHandle. They are NOT LODNode wrappers — agents use LOD0 only and have
// no LOD swap. Vehicle meshes are loaded from the Irrlicht mesh cache via
// smgr->getMesh() — the scene manager retains ownership; do NOT drop the mesh.
//
// Zone → vehicle type mapping:
//   Residential → car (handle%3: 0=sedan, 1=hatchback, 2=suv)
//   Commercial  → bus_standard
//   Industrial  → truck_cargo
//
// Distance cull: only agents within 150 m of getListenerPosition() are spawned.
// -------------------------------------------------------------------------

// Helper: return the vehicle mesh filename (relative to assets root) for a given zone/handle.
static std::string vehicleMeshPath(ZoneType zone, AgentHandle handle)
{
    switch (zone) {
        case ZoneType::Residential: {
            const char* variants[3] = {"car_sedan", "car_hatchback", "car_suv"};
            return std::string(AITOWN_ASSETS_DIR) + "/3d/vehicles/"
                   + variants[handle % 3] + "_lod0.b3d";
        }
        case ZoneType::Commercial:
            return std::string(AITOWN_ASSETS_DIR) + "/3d/vehicles/bus_standard_lod0.b3d";
        case ZoneType::Industrial:
            return std::string(AITOWN_ASSETS_DIR) + "/3d/vehicles/truck_cargo_lod0.b3d";
    }
    return std::string(AITOWN_ASSETS_DIR) + "/3d/vehicles/car_sedan_lod0.b3d";
}

void IrrlichtRenderer::spawnVehicleAgent(AgentHandle handle, int tileX, int tileZ, ZoneType zone)
{
    if (!m_smgr || !m_driver) return;

    // If a node already exists for this handle, despawn first (replace guard).
    if (m_agentNodes.count(handle)) {
        despawnVehicleAgent(handle);
    }

    // Load vehicle mesh from scene manager cache (no drop — smgr retains ownership).
    std::string meshPath = vehicleMeshPath(zone, handle);
    IAnimatedMesh* animMesh = m_smgr->getMesh(meshPath.c_str());
    if (!animMesh) {
        std::string meshMsg = "[IrrlichtRenderer] spawnVehicleAgent: mesh not found: ";
        meshMsg += meshPath;
        if (m_logger) {
            m_logger->log(meshMsg.c_str(), irr::ELL_WARNING);
        } else {
            fprintf(stderr, "[IrrlichtRenderer WARNING] %s\n", meshMsg.c_str());
        }
        return;
    }

    // Create a static mesh scene node (NOT animated — B3D root bone displaces position).
    IMeshSceneNode* node = m_smgr->addMeshSceneNode(static_cast<IMesh*>(animMesh));
    if (!node) return;

    // Position at tile centre (world coords = tile * kTileSize + half-tile offset).
    float wx = static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
    float wz = static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f;
    node->setPosition(core::vector3df(wx, 0.0f, wz));
    node->setRotation(core::vector3df(0.0f, 0.0f, 0.0f));

    // Apply vehicle atlas texture. Use PNG — the DDS atlas uses BC1_UNORM_SRGB
    // (DXGI format 72) which Irrlicht's DDS loader does not recognise.
    ITexture* vehicleTex = m_driver->getTexture(
        (std::string(AITOWN_ASSETS_DIR) + "/textures/vehicles/vehicles_diffuse_atlas_d.png").c_str());

    for (u32 m = 0; m < node->getMaterialCount(); ++m) {
        SMaterial& mat = node->getMaterial(m);
        mat.Lighting = false;
        // BackfaceCulling=false: procedural B3D assets may have inverted or
        // mixed winding after the axis-reorientation pass; disabling culling
        // ensures the vehicle is visible from all camera angles.
        mat.BackfaceCulling = false;
        if (vehicleTex && !mat.getTexture(0)) {
            mat.setTexture(0, vehicleTex);
        }
    }
    // Ensure slot 0 always gets the atlas even if it already had a texture bound.
    if (vehicleTex) {
        node->getMaterial(0).setTexture(0, vehicleTex);
    }

    m_agentNodes[handle] = node;
}

// isIntersectionTile — check if the tile at (tileX, tileZ) has road nodes in 3+
// cardinal directions (indicating an intersection tile where lane offset = 0).
bool IrrlichtRenderer::isIntersectionTile(int tileX, int tileZ) const
{
    int count = 0;
    if (m_roadNodes.count(tileKey(tileX,     tileZ - 1)) > 0) ++count;  // north
    if (m_roadNodes.count(tileKey(tileX,     tileZ + 1)) > 0) ++count;  // south
    if (m_roadNodes.count(tileKey(tileX + 1, tileZ    )) > 0) ++count;  // east
    if (m_roadNodes.count(tileKey(tileX - 1, tileZ    )) > 0) ++count;  // west
    return count >= 3;
}

void IrrlichtRenderer::moveVehicleAgent(AgentHandle handle, float worldX, float worldZ,
                                         float headingDeg)
{
    auto it = m_agentNodes.find(handle);
    if (it == m_agentNodes.end()) return;  // no-op if handle not found

    // Sample terrain height at this world position so vehicles sit on the ground.
    float y = 0.0f;
    int tileX = static_cast<int>(worldX / kTileSize);
    int tileZ = static_cast<int>(worldZ / kTileSize);
    if (m_terrain) {
        y = m_terrain->getHeightAt(tileX, tileZ);
    }

    // Apply lane center offset based on heading, unless at an intersection tile.
    using namespace RenderConstants;
    float laneX = worldX;
    float laneZ = worldZ;
    if (!isIntersectionTile(tileX, tileZ)) {
        // Determine primary direction from headingDeg.
        // headingDeg: 0=+Z (north), 90=+X (east), 180=-Z (south), 270=-X (west).
        // Normalize to [0,360).
        float h = headingDeg;
        while (h < 0.0f)   h += 360.0f;
        while (h >= 360.0f) h -= 360.0f;

        if (h < 45.0f || h >= 315.0f) {
            // Northbound (+Z): offset worldX += kLaneCenterOffset
            laneX += kLaneCenterOffset;
        } else if (h >= 45.0f && h < 135.0f) {
            // Eastbound (+X): offset worldZ += kLaneCenterOffset
            laneZ += kLaneCenterOffset;
        } else if (h >= 135.0f && h < 225.0f) {
            // Southbound (-Z): offset worldX -= kLaneCenterOffset
            laneX -= kLaneCenterOffset;
        } else {
            // Westbound (-X): offset worldZ -= kLaneCenterOffset
            laneZ -= kLaneCenterOffset;
        }
    }

    IMeshSceneNode* node = it->second;
    node->setPosition(core::vector3df(laneX, y, laneZ));
    node->setRotation(core::vector3df(0.0f, headingDeg, 0.0f));
}

void IrrlichtRenderer::despawnVehicleAgent(AgentHandle handle)
{
    auto it = m_agentNodes.find(handle);
    if (it == m_agentNodes.end()) return;

    IMeshSceneNode* node = it->second;
    if (node) {
        // Eviction sequence: clear material texture slots first.
        for (u32 i = 0; i < node->getMaterialCount(); ++i) {
            for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                node->getMaterial(i).setTexture(t, nullptr);
            }
        }
        // Reset material state.
        if (m_driver) m_driver->setMaterial(SMaterial{});
        // Remove from scene graph.
        node->remove();
        // Do NOT drop() the mesh — scene manager retains B3D mesh cache ownership.
    }
    m_agentNodes.erase(it);
}

// -------------------------------------------------------------------------
// Phase 11d Deliverable 3b — Traffic signal visual state.
//
// Each intersection receives a small billboard quad above its position.
// Green phase → emissive green SColor(255, 0, 200, 0).
// Red phase   → emissive red   SColor(255, 200, 0, 0).
// Material: EMT_TRANSPARENT_ADD_COLOR for emissive glow.
// PolygonOffsetFactor = 1, PolygonOffsetMode = EPO_FRONT to prevent z-fighting.
// Stored in m_signalNodes keyed by tileX*10000 + tileZ.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setIntersectionSignalState(int tileX, int tileZ, SignalPhase phase)
{
    if (!m_smgr || !m_driver) return;

    int key = tileX * 10000 + tileZ;

    // Determine signal colour.
    SColor signalColour = (phase == SignalPhase::Green)
        ? SColor(255, 0, 200, 0)
        : SColor(255, 200, 0, 0);

    // Look up existing node.
    auto it = m_signalNodes.find(key);
    if (it != m_signalNodes.end()) {
        // Update existing node material colour.
        IMeshSceneNode* node = it->second;
        if (node) {
            node->getMaterial(0).EmissiveColor = signalColour;
        }
        return;
    }

    // Create a small billboard quad (1 m × 1 m) slightly above the intersection.
    SMesh* mesh = new SMesh();
    SMeshBuffer* buf = new SMeshBuffer();

    buf->Material.MaterialType    = EMT_TRANSPARENT_ADD_COLOR;
    buf->Material.Lighting        = false;
    buf->Material.ZWriteEnable    = false;
    buf->Material.EmissiveColor   = signalColour;
    buf->Material.PolygonOffsetFactor = 1;
    buf->Material.PolygonOffsetDirection = EPO_FRONT;

    // Small quad: 0.5 m × 0.5 m centred at (0, 2.5, 0) above the tile.
    // Vertices in world-local space (node position anchors the tile centre).
    const float s = 0.25f;  // half-size
    const float h = 2.5f;   // height above ground
    SColor white(255, 255, 255, 255);

    buf->Vertices.push_back(S3DVertex(core::vector3df(-s, h, -s), core::vector3df(0,1,0), white, core::vector2df(0,0)));
    buf->Vertices.push_back(S3DVertex(core::vector3df( s, h, -s), core::vector3df(0,1,0), white, core::vector2df(1,0)));
    buf->Vertices.push_back(S3DVertex(core::vector3df( s, h,  s), core::vector3df(0,1,0), white, core::vector2df(1,1)));
    buf->Vertices.push_back(S3DVertex(core::vector3df(-s, h,  s), core::vector3df(0,1,0), white, core::vector2df(0,1)));

    buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
    buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

    buf->recalculateBoundingBox();
    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();

    float wx = static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f;
    float wz = static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f;

    IMeshSceneNode* node = m_smgr->addMeshSceneNode(mesh);
    mesh->drop();

    if (node) {
        node->setPosition(core::vector3df(wx, 0.0f, wz));
        node->getMaterial(0).EmissiveColor            = signalColour;
        node->getMaterial(0).MaterialType             = EMT_TRANSPARENT_ADD_COLOR;
        node->getMaterial(0).Lighting                 = false;
        node->getMaterial(0).ZWriteEnable             = false;
        node->getMaterial(0).PolygonOffsetFactor      = 1;
        node->getMaterial(0).PolygonOffsetDirection   = EPO_FRONT;
        m_signalNodes[key] = node;
    }
}

// -------------------------------------------------------------------------
// Phase 11d Deliverable 4a — Service coverage radius overlay.
//
// Builds a dynamic SMesh* with tile quads covering the service building's
// radius. The mesh is owned by a scene node stored in m_coverageOverlayNode.
// Uses PolygonOffsetFactor = 1, EPO_FRONT for z-fighting prevention.
//
// Radius per type (per architecture/game-design/service-coverage.md):
//   Fire Station:   800 m (400 m when degraded)
//   Police Station: 600 m (300 m when degraded)
//   Water Tower:    700 m (350 m when degraded)
//   Power Plant:    BFS footprint — yellow (#F1C40F) tile quads
// -------------------------------------------------------------------------
void IrrlichtRenderer::showServiceCoverageOverlay(int tileX, int tileZ,
                                                   ServiceBuildingType type,
                                                   bool degraded)
{
    if (!m_smgr || !m_driver || !m_terrain) return;

    // Remove any existing overlay first.
    hideServiceCoverageOverlay();

    // Determine coverage parameters and colour.
    float    radiusM  = 0.0f;
    int      maxDepth = 0;     // BFS depth limit (PowerPlant path only)
    uint32_t argb     = 0x60FFFFFFU;  // default: semi-transparent white

    switch (type) {
        case ServiceBuildingType::FireStation:
            radiusM = degraded ? 400.0f : 800.0f;
            argb    = 0x60C0392BU;  // Fire: semi-transparent red #C0392B
            break;
        case ServiceBuildingType::PoliceStation:
            radiusM = degraded ? 300.0f : 600.0f;
            argb    = 0x602E4482U;  // Police: semi-transparent blue #2E4482
            break;
        case ServiceBuildingType::WaterTower:
            radiusM = degraded ? 350.0f : 700.0f;
            argb    = 0x601ABC9CU;  // Water: semi-transparent cyan #1ABC9C
            break;
        case ServiceBuildingType::PowerPlant:
            // Power Plant: BFS tile highlight — yellow (#F1C40F).
            // maxDepth = 80 tiles full (800 m / 10 m), 40 tiles degraded (400 m / 10 m).
            maxDepth = degraded ? 40 : 80;
            argb     = 0x60F1C40FU;  // Power: semi-transparent yellow #F1C40F
            break;
        case ServiceBuildingType::None:
            return;
    }

    // Decode ARGB for SColor(A, R, G, B).
    u8 ca = static_cast<u8>((argb >> 24) & 0xFF);
    u8 cr = static_cast<u8>((argb >> 16) & 0xFF);
    u8 cg = static_cast<u8>((argb >>  8) & 0xFF);
    u8 cb = static_cast<u8>( argb        & 0xFF);
    SColor colour(ca, cr, cg, cb);

    const float yOffset = 0.08f;  // slightly above placement preview to avoid z-fighting

    SMesh*       mesh      = new SMesh();
    SMeshBuffer* cur       = new SMeshBuffer();
    cur->Material.MaterialType         = EMT_TRANSPARENT_ALPHA_CHANNEL;
    cur->Material.Lighting             = false;
    cur->Material.ZWriteEnable         = false;
    cur->Material.PolygonOffsetFactor  = 1;
    cur->Material.PolygonOffsetDirection = EPO_FRONT;

    static constexpr u32 kMaxQuadsPerBuffer = 10922u;
    u32 quadsInCur = 0;

    auto closeAndOpenBuffer = [&]() {
        cur->recalculateBoundingBox();
        mesh->addMeshBuffer(cur);
        cur->drop();
        cur = new SMeshBuffer();
        cur->Material.MaterialType         = EMT_TRANSPARENT_ALPHA_CHANNEL;
        cur->Material.Lighting             = false;
        cur->Material.ZWriteEnable         = false;
        cur->Material.PolygonOffsetFactor  = 1;
        cur->Material.PolygonOffsetDirection = EPO_FRONT;
        quadsInCur = 0;
    };

    // Helper: add one quad for tile (tx, tz) into current buffer.
    auto addTileQuad = [&](int tx, int tz) {
        if (quadsInCur >= kMaxQuadsPerBuffer) {
            closeAndOpenBuffer();
        }
        float x0  = static_cast<float>(tx)     * kTileSize;
        float x1  = static_cast<float>(tx + 1) * kTileSize;
        float z0f = static_cast<float>(tz)     * kTileSize;
        float z1f = static_cast<float>(tz + 1) * kTileSize;

        float h00 = m_terrain->getHeightAt(tx,     tz)     + yOffset;
        float h10 = m_terrain->getHeightAt(tx + 1, tz)     + yOffset;
        float h11 = m_terrain->getHeightAt(tx + 1, tz + 1) + yOffset;
        float h01 = m_terrain->getHeightAt(tx,     tz + 1) + yOffset;

        u32 base = quadsInCur * 4;
        cur->Vertices.push_back(S3DVertex(core::vector3df(x0, h00, z0f), core::vector3df(0,1,0), colour, core::vector2df(0,0)));
        cur->Vertices.push_back(S3DVertex(core::vector3df(x1, h10, z0f), core::vector3df(0,1,0), colour, core::vector2df(1,0)));
        cur->Vertices.push_back(S3DVertex(core::vector3df(x1, h11, z1f), core::vector3df(0,1,0), colour, core::vector2df(1,1)));
        cur->Vertices.push_back(S3DVertex(core::vector3df(x0, h01, z1f), core::vector3df(0,1,0), colour, core::vector2df(0,1)));

        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 2));
        cur->Indices.push_back(static_cast<u16>(base + 1));
        cur->Indices.push_back(static_cast<u16>(base + 0));
        cur->Indices.push_back(static_cast<u16>(base + 3));
        cur->Indices.push_back(static_cast<u16>(base + 2));
        ++quadsInCur;
    };

    if (type == ServiceBuildingType::PowerPlant) {
        // -----------------------------------------------------------------------
        // Power Plant path: BFS over 4-connected grid tiles up to maxDepth steps.
        // This matches the simulation's graph-traversal coverage model and produces
        // a BFS frontier (diamond) shape instead of a Euclidean circle.
        // Visited table: flat row-major bool vector sized m_mapTilesX * m_mapTilesZ.
        // BFS queue entry: (tileX, tileZ, depth).
        // -----------------------------------------------------------------------
        if (tileX < 0 || tileX >= m_mapTilesX || tileZ < 0 || tileZ >= m_mapTilesZ) {
            // Origin tile is out of bounds — nothing to render.
            cur->drop();
            mesh->drop();
            return;
        }

        const int  mapW    = m_mapTilesX;
        const int  mapH    = m_mapTilesZ;
        std::vector<bool> visited(static_cast<size_t>(mapW) * static_cast<size_t>(mapH), false);

        struct BFSNode { int x; int z; int depth; };
        std::queue<BFSNode> bfsQ;

        auto markVisited = [&](int x, int z) {
            visited[static_cast<size_t>(z) * static_cast<size_t>(mapW) + static_cast<size_t>(x)] = true;
        };
        auto isVisited = [&](int x, int z) -> bool {
            return visited[static_cast<size_t>(z) * static_cast<size_t>(mapW) + static_cast<size_t>(x)];
        };

        markVisited(tileX, tileZ);
        bfsQ.push({tileX, tileZ, 0});

        static constexpr int kDx[4] = { 1, -1, 0,  0};
        static constexpr int kDz[4] = { 0,  0, 1, -1};

        while (!bfsQ.empty()) {
            BFSNode node = bfsQ.front();
            bfsQ.pop();

            addTileQuad(node.x, node.z);

            if (node.depth >= maxDepth) continue;

            for (int d = 0; d < 4; ++d) {
                int nx = node.x + kDx[d];
                int nz = node.z + kDz[d];
                if (nx < 0 || nx >= mapW || nz < 0 || nz >= mapH) continue;
                if (isVisited(nx, nz)) continue;
                markVisited(nx, nz);
                bfsQ.push({nx, nz, node.depth + 1});
            }
        }
    } else {
        // -----------------------------------------------------------------------
        // Radius-based path: Fire Station, Police Station, Water Tower.
        // Enumerate all tiles within radiusM via Euclidean distance check.
        // -----------------------------------------------------------------------
        int radiusTiles = static_cast<int>(radiusM / kTileSize) + 1;

        for (int dz = -radiusTiles; dz <= radiusTiles; ++dz) {
            for (int dx = -radiusTiles; dx <= radiusTiles; ++dx) {
                float dist = std::sqrt(static_cast<float>(dx*dx + dz*dz)) * kTileSize;
                if (dist > radiusM) continue;

                int tx = tileX + dx;
                int tz = tileZ + dz;
                if (tx < 0 || tx >= m_mapTilesX || tz < 0 || tz >= m_mapTilesZ) continue;

                addTileQuad(tx, tz);
            }
        }
    }

    // Flush final buffer.
    if (quadsInCur > 0) {
        cur->recalculateBoundingBox();
        mesh->addMeshBuffer(cur);
        cur->drop();
        cur = nullptr;
    } else {
        cur->drop();
        cur = nullptr;
    }

    if (mesh->getMeshBufferCount() == 0) {
        mesh->drop();
        return;
    }

    mesh->recalculateBoundingBox();
    m_coverageOverlayNode = m_smgr->addMeshSceneNode(mesh);
    mesh->drop();

    if (m_coverageOverlayNode) {
        // Set polygon offset on the scene node's materials.
        for (u32 i = 0; i < m_coverageOverlayNode->getMaterialCount(); ++i) {
            m_coverageOverlayNode->getMaterial(i).PolygonOffsetFactor    = 1;
            m_coverageOverlayNode->getMaterial(i).PolygonOffsetDirection = EPO_FRONT;
        }
        m_coverageOverlayNode->setPosition(core::vector3df(0.0f, 0.0f, 0.0f));
    }
}

void IrrlichtRenderer::hideServiceCoverageOverlay()
{
    if (!m_coverageOverlayNode) return;

    // Eviction sequence: clear textures → reset material → remove.
    for (u32 i = 0; i < m_coverageOverlayNode->getMaterialCount(); ++i) {
        for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
            m_coverageOverlayNode->getMaterial(i).setTexture(t, nullptr);
        }
    }
    if (m_driver) m_driver->setMaterial(SMaterial{});
    m_coverageOverlayNode->remove();
    m_coverageOverlayNode = nullptr;
}
