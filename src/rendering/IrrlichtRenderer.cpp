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

#include <algorithm>   // std::min, std::max
#include <cstdio>      // fprintf
#include <cmath>       // M_PI
#include <string>      // std::string for asset path construction

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

// -------------------------------------------------------------------------
// CloudDomeShaderCallback — IShaderConstantSetCallBack for the cloud dome
// GLSL shader.  Sets u_tex (sampler2D, unit 0) and u_cameraY (float, world-
// space Y of the camera) on every draw call.
//
// Lifetime rule (per shader-loading.md and CLAUDE.md):
//   Unlike the original drop-after-pass pattern, this callback is kept alive
//   by the caller (IrrlichtRenderer) so that setCameraY() can be called each
//   frame.  The caller holds its own reference and must ->drop() it in the
//   destructor (stored as void* m_cloudShaderCbRaw in the header).  Irrlicht
//   also calls grab() internally, so the final drop happens when the material
//   renderer is destroyed.  Never use std::unique_ptr — causes double-free.
// -------------------------------------------------------------------------
class CloudDomeShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
    void setCameraY(float y) { m_cameraY = y; }

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 /*userData*/) override
    {
        // Bind u_tex to texture unit 0.  The cloud texture is always on unit 0
        // (single-texture material); this call is required even though 0 is the
        // default — some GLSL drivers report a warning if the sampler uniform is
        // never explicitly set.
        irr::s32 tex = 0;
        services->setPixelShaderConstant("u_tex", &tex, 1);

        // u_cameraY is used in the vertex shader for elevation-angle fade.
        // Set via both vertex and pixel shader constant setters — Irrlicht's GLSL
        // backend behaviour differs across platforms; setting both ensures the
        // uniform is visible to the linked program regardless of backend quirks.
        services->setVertexShaderConstant("u_cameraY", &m_cameraY, 1);
        services->setPixelShaderConstant("u_cameraY", &m_cameraY, 1);
    }

private:
    float m_cameraY{0.0f};
};

IrrlichtRenderer::IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
    : m_device(device)
    , m_uiManager(uiManager)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_smgr(device ? device->getSceneManager() : nullptr)
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
    // (the one we retained in initCloudPlane() so that setCameraY() could be called
    // each frame).  Null check covers headless runs where the shader compile failed.
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

    // m_roadTextureCache, m_buildingAssetLoader, m_vehicleAssetLoader (unique_ptrs)
    // destroyed automatically.
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
    m_lastCameraPosition = p.position;  // cached for getListenerPosition()
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
        newNode->setMaterialFlag(EMF_LIGHTING, false);  // unlit until Phase 6 lighting pass
        newNode->setMaterialFlag(EMF_BACK_FACE_CULLING, false);  // both sides visible — Phase 5 has no winding-dependent lighting

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
// The mesh + buffer are allocated once in the constructor (never null during
// gameplay).  This method only updates vertex positions and colours — no
// heap allocation or deallocation per event.
//
// Pass tileX = -1 to clear (sets m_hoverVisible = false without touching
// the buffer, so the next valid call can immediately overwrite).
//
// The actual drawMeshBuffer() call is deferred to drawScene(), after
// sceneManager->drawAll(), per the Phase 9b per-frame sequence in
// architecture/graphics-architecture/irrlicht-device-lifecycle.md.
// -------------------------------------------------------------------------
void IrrlichtRenderer::setTileHoverHighlight(int tileX, int tileZ, uint32_t argb)
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

    // Decode ARGB (0xAARRGGBB) for Irrlicht SColor(A, R, G, B).
    u8 a = static_cast<u8>((argb >> 24) & 0xFF);
    u8 r = static_cast<u8>((argb >> 16) & 0xFF);
    u8 g = static_cast<u8>((argb >>  8) & 0xFF);
    u8 b = static_cast<u8>( argb        & 0xFF);
    SColor colour(a, r, g, b);

    // Build the four tile-corner positions slightly above terrain surface.
    float yOffset = 0.1f;  // 10 cm above terrain to avoid Z-fighting
    float x0 = static_cast<float>(tileX)     * m_cellSize;
    float x1 = static_cast<float>(tileX + 1) * m_cellSize;
    float z0 = static_cast<float>(tileZ)     * m_cellSize;
    float z1 = static_cast<float>(tileZ + 1) * m_cellSize;

    float h00 = m_terrain->getHeightAt(tileX,     tileZ)     + yOffset;
    float h10 = m_terrain->getHeightAt(tileX + 1, tileZ)     + yOffset;
    float h11 = m_terrain->getHeightAt(tileX + 1, tileZ + 1) + yOffset;
    float h01 = m_terrain->getHeightAt(tileX,     tileZ + 1) + yOffset;

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
    const std::vector<std::pair<int,int>>& tiles,
    uint32_t argb)
{
    // Clear request.
    if (tiles.empty()) {
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
    u8 a = static_cast<u8>((argb >> 24) & 0xFF);
    u8 r = static_cast<u8>((argb >> 16) & 0xFF);
    u8 g = static_cast<u8>((argb >>  8) & 0xFF);
    u8 b = static_cast<u8>( argb        & 0xFF);
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

    for (const auto& tile : tiles) {
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
        m_driver->setMaterial(SMaterial{});
        m_overlayNode->remove();
        m_overlayNode = nullptr;
    }

    // Nothing to render — overlay cleared.
    if (sparseOverlay.empty()) return;

    // Cap at 100K quads for V1.
    static constexpr size_t kMaxOverlayQuads = 100000u;

    SMesh*       omesh = new SMesh();
    SMeshBuffer* obuf  = new SMeshBuffer();

    size_t quadCount = std::min(sparseOverlay.size(), kMaxOverlayQuads);
    obuf->Vertices.reallocate(static_cast<u32>(quadCount * 4));
    obuf->Indices.reallocate(static_cast<u32>(quadCount * 6));

    obuf->Material.MaterialType = EMT_TRANSPARENT_ALPHA_CHANNEL;
    obuf->Material.Lighting     = false;
    obuf->Material.ZWriteEnable = false;

    float yOffset = 0.05f;  // 5 cm above terrain — under the hover highlight (10 cm)

    size_t written = 0;
    for (const auto& kv : sparseOverlay) {
        if (written >= kMaxOverlayQuads) break;

        // Decode tile index from key.
        int tx = static_cast<int>(kv.first % static_cast<uint64_t>(mapTilesX));
        int tz = static_cast<int>(kv.first / static_cast<uint64_t>(mapTilesX));

        // Skip out-of-bounds tiles.
        if (tx < 0 || tx >= mapTilesX || tz < 0 || tz >= mapTilesZ) continue;

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

        u32 base = static_cast<u32>(written * 4);

        obuf->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h00, z0), core::vector3df(0, 1, 0), colour,
            core::vector2df(0, 0)));
        obuf->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h10, z0), core::vector3df(0, 1, 0), colour,
            core::vector2df(1, 0)));
        obuf->Vertices.push_back(S3DVertex(
            core::vector3df(x1, h11, z1), core::vector3df(0, 1, 0), colour,
            core::vector2df(1, 1)));
        obuf->Vertices.push_back(S3DVertex(
            core::vector3df(x0, h01, z1), core::vector3df(0, 1, 0), colour,
            core::vector2df(0, 1)));

        // Two triangles per quad: v0→v2→v1, v0→v3→v2 (CW from above = +Y normal).
        obuf->Indices.push_back(base + 0);
        obuf->Indices.push_back(base + 2);
        obuf->Indices.push_back(base + 1);
        obuf->Indices.push_back(base + 0);
        obuf->Indices.push_back(base + 3);
        obuf->Indices.push_back(base + 2);

        ++written;
    }

    if (written == 0) {
        // All entries were out-of-bounds — drop and bail.
        obuf->drop();
        omesh->drop();
        return;
    }

    // Mandatory bounding box calculation before attaching to scene graph.
    obuf->recalculateBoundingBox();
    omesh->addMeshBuffer(obuf);
    obuf->drop();  // mesh is sole owner
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: ensureAssetLoader() called with null "
            "m_smgr/m_driver — scene node cannot be created\n");
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
        m_driver->setMaterial(SMaterial{});
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeBuildingMesh(%d,%d) called with "
            "empty assetBaseName — skipping node creation\n", tileX, tileZ);
        return;
    }
    if (!ensureAssetLoader()) return;

    // Remove any existing building on this tile first (e.g. density upgrade swap).
    destroyTileNode(m_buildingNodes, tileX, tileZ);

    // Construct the asset base path: assets/3d/buildings/<assetBaseName>
    // BuildingAssetLoader::load() appends _lod0.b3d, _lod1.b3d, _lod2.b3d, .meta.
    std::string basePath = std::string(AITOWN_ASSETS_DIR) +
                           "/3d/buildings/" + assetBaseName;

    LODNode* lodNode = m_buildingAssetLoader->load(basePath);
    if (!lodNode) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeBuildingMesh(%d,%d): failed to load "
            "asset '%s' — node not created\n", tileX, tileZ, basePath.c_str());
        return;
    }

    // Position the scene node at the tile's world-space centre.
    // B3D building meshes are 1×1×height unit boxes centred on X and Z.
    // Scale by kTileSize so they fill the full tile footprint (10×10 m).
    // Offset by kTileSize/2 in X and Z to centre the mesh on the tile.
    // Disable lighting — no light nodes in the scene yet (Phase 6+).
    //
    // Texture fallback: BuildingAssetLoader::load() binds the buildings_atlas_d.dds
    // to every material slot.  If the atlas fails to load (missing file, driver
    // unsupported format) the slot remains null and the zone-coloured placeholder
    // texture is bound instead so the building is still visually distinguishable.
    if (scene::ISceneNode* node = lodNode->getNode()) {
        // Four-corner terrain flattening pattern (Phase 10b):
        // Average all 4 tile-corner vertex heights then flatten each corner to that value.
        // setTileHeight(tileX+1, tileZ, h) sets the top-left vertex of tile (tileX+1, tileZ),
        // which IS the top-right vertex of tile (tileX, tileZ) — so calling all 4 corner
        // coordinates flattens all 4 vertices of this tile to the same height, eliminating
        // T-junction seams between the road/building mesh and the terrain quad edges.
        const float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
        const float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
        const float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
        const float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;
        const float targetH = (h00 + h10 + h01 + h11) * 0.25f;
        if (m_terrain) {
            m_terrain->setTileHeight(tileX,     tileZ,     targetH);
            m_terrain->setTileHeight(tileX + 1, tileZ,     targetH);
            m_terrain->setTileHeight(tileX,     tileZ + 1, targetH);
            m_terrain->setTileHeight(tileX + 1, tileZ + 1, targetH);
        }
        if (m_terrain) m_terrain->flushTerrainRebuilds();
        // Use targetH directly — NOT getHeightAt() after setTileHeight().
        // setTileHeight() applies neighbour blending to the 8 surrounding tiles;
        // subsequent corner calls bleed back into vertex (tileX, tileZ), leaving
        // its stored height below targetH. getHeightAt() would return that
        // blended-down value and position the node below the rendered terrain surface.
        const float postY = m_terrain ? targetH : 0.0f;
        node->setPosition(core::vector3df(
            static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
            postY + 0.10f,   // 10 cm above terrain — covers tile-edge bleed-back after
                             // neighbour blending; polygon offset is the primary Z-fight defence.
            static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
        node->setScale(core::vector3df(kTileSize, kTileSize, kTileSize));

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
            m_device ? m_device->getFileSystem() : nullptr);
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: road shader compile failed "
            "(vs=%s, fs=%s) — road tiles will use EMT_SOLID fallback\n",
            vsPath.c_str(), fsPath.c_str());
        m_roadMaterialType = -2;  // mark done-with-failure
        return false;
    }

    m_roadMaterialType = matType;
    return true;
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
    float h00, float h10, float h01, float h11) const
{
    if (!m_driver) return nullptr;

    // Road tile half-extent in X and Z (5 m).
    static constexpr float H  = kTileSize * 0.5f;
    // Small Y bias: road surface sits 10 cm above terrain — matches the previous
    // flat-road Y offset.  Polygon offset handles Z-fighting at camera distance.
    static constexpr float B  = 0.10f;
    // Kerb dimensions (world-space metres).
    static constexpr float KB = 0.05f;  // bevel inset
    static constexpr float KW = 0.15f;  // total kerb width
    static constexpr float KH = 0.10f;  // kerb height above road surface corner

    // Effective material type (road shader or EMT_SOLID fallback).
    const E_MATERIAL_TYPE roadMat = (m_roadMaterialType >= 0)
        ? static_cast<E_MATERIAL_TYPE>(m_roadMaterialType)
        : EMT_SOLID;

    // Pre-compute biased corner heights.
    const float y00 = h00 + B;   // back-left
    const float y10 = h10 + B;   // back-right
    const float y01 = h01 + B;   // front-left
    const float y11 = h11 + B;   // front-right

    SMesh*       mesh = new SMesh();
    SMeshBuffer* buf  = new SMeshBuffer();

    buf->Material.MaterialType           = roadMat;
    buf->Material.Lighting               = false;
    buf->Material.BackfaceCulling        = false;  // kerb faces have varying normals
    buf->Material.PolygonOffsetDirection = irr::video::EPO_FRONT;
    buf->Material.PolygonOffsetFactor    = 1;

    // Helper: add a vertex (position in tile-local X/Z, world-space Y).
    auto addV = [&](float x, float y, float z, float u, float v) {
        buf->Vertices.push_back(S3DVertex(
            core::vector3df(x, y, z),
            core::vector3df(0.f, 1.f, 0.f),  // +Y normal (Lighting=false, unused)
            SColor(255, 255, 255, 255),
            core::vector2df(u, v)));
    };

    // --- Central terrain-conforming quad (4 verts, 2 tris) ---
    // CW from +Y: 0→2→1, 0→3→2 (same winding as hover tile and terrain quad).
    addV(-H, y00, -H,  0.f, 0.f);  // v0 back-left
    addV( H, y10, -H,  1.f, 0.f);  // v1 back-right
    addV( H, y11,  H,  1.f, 1.f);  // v2 front-right
    addV(-H, y01,  H,  0.f, 1.f);  // v3 front-left
    buf->Indices.push_back(0); buf->Indices.push_back(2); buf->Indices.push_back(1);
    buf->Indices.push_back(0); buf->Indices.push_back(3); buf->Indices.push_back(2);

    // Helper: bilinearly interpolate a height at (fx, fz) in [0,1]×[0,1] tile space.
    // Used to compute kerb base heights that track the terrain edge.
    auto hlerp = [&](float fx, float fz) -> float {
        // fx=0 → -H edge (tileX), fx=1 → +H edge (tileX+1)
        // fz=0 → -H edge (tileZ), fz=1 → +H edge (tileZ+1)
        float h0z = y00 + fx * (y10 - y00);  // lerp along z=0 edge
        float h1z = y01 + fx * (y11 - y01);  // lerp along z=1 edge
        return h0z + fz * (h1z - h0z);
    };

    // --- Kerb helper: add one strip (8 verts, 6 tris) ---
    // Same structure as the previous flat version; kerb base heights now track terrain.
    auto addKerb = [&](
        float lx0, float ly0, float lz0,
        float rx0, float ry0, float rz0,
        float lx1, float ly1, float lz1,
        float rx1, float ry1, float rz1,
        float lx2, float ly2, float lz2,
        float rx2, float ry2, float rz2,
        float lx3, float ly3, float lz3,
        float rx3, float ry3, float rz3)
    {
        u16 base = static_cast<u16>(buf->Vertices.size());
        addV(lx0, ly0, lz0, 0.f, 0.f);
        addV(rx0, ry0, rz0, 0.f, 0.f);
        addV(lx1, ly1, lz1, 0.f, 0.f);
        addV(rx1, ry1, rz1, 0.f, 0.f);
        addV(lx2, ly2, lz2, 0.f, 0.f);
        addV(rx2, ry2, rz2, 0.f, 0.f);
        addV(lx3, ly3, lz3, 0.f, 0.f);
        addV(rx3, ry3, rz3, 0.f, 0.f);
        buf->Indices.push_back(base+0); buf->Indices.push_back(base+2); buf->Indices.push_back(base+1);
        buf->Indices.push_back(base+2); buf->Indices.push_back(base+3); buf->Indices.push_back(base+1);
        buf->Indices.push_back(base+2); buf->Indices.push_back(base+4); buf->Indices.push_back(base+3);
        buf->Indices.push_back(base+4); buf->Indices.push_back(base+5); buf->Indices.push_back(base+3);
        buf->Indices.push_back(base+4); buf->Indices.push_back(base+6); buf->Indices.push_back(base+5);
        buf->Indices.push_back(base+6); buf->Indices.push_back(base+7); buf->Indices.push_back(base+5);
    };

    // South kerb (z = -H side).  Base heights at z=-H edge: y00 (left) and y10 (right).
    addKerb(
        -H, y00,     -H,          H, y10,     -H,          // row 0: inner edge
        -H, y00+KH,  -H-KB,       H, y10+KH,  -H-KB,       // row 1: bevel top
        -H, y00+KH,  -H-KW,       H, y10+KH,  -H-KW,       // row 2: outer top
        -H, y00,     -H-KW,       H, y10,     -H-KW);       // row 3: outer base

    // North kerb (z = +H side).  Base heights: y01 (left) and y11 (right).
    addKerb(
        -H, y01,     H,           H, y11,     H,            // row 0
        -H, y01+KH,  H+KB,        H, y11+KH,  H+KB,         // row 1
        -H, y01+KH,  H+KW,        H, y11+KH,  H+KW,         // row 2
        -H, y01,     H+KW,        H, y11,     H+KW);         // row 3

    // West kerb (x = -H side).  Base heights: y00 (back) and y01 (front).
    addKerb(
        -H,    y00,     -H,        -H,    y01,     H,        // row 0
        -H-KB, y00+KH,  -H,        -H-KB, y01+KH,  H,        // row 1
        -H-KW, y00+KH,  -H,        -H-KW, y01+KH,  H,        // row 2
        -H-KW, y00,     -H,        -H-KW, y01,     H);        // row 3

    // East kerb (x = +H side).  Base heights: y10 (back) and y11 (front).
    addKerb(
        H,    y10,     -H,         H,    y11,     H,         // row 0
        H+KB, y10+KH,  -H,         H+KB, y11+KH,  H,         // row 1
        H+KW, y10+KH,  -H,         H+KW, y11+KH,  H,         // row 2
        H+KW, y10,     -H,         H+KW, y11,     H);         // row 3

    (void)hlerp;  // hlerp available for future use (e.g. mid-edge kerb heights)

    buf->recalculateBoundingBox();

    // Expand Y extent of the bounding box to at least 0.5 m.
    //
    // Road tile geometry is nearly flat: the main quad and all kerb vertices span
    // only ~0.10 m in Y (B=0.10 bias + KH=0.10 kerb height) on flat terrain.
    // Irrlicht's EAC_BOX frustum culler tests the AABB's 8 corners against each
    // clip plane.  For a 10 m × 0.10 m × 10 m box viewed by the isometric camera
    // (camera above ~50–200 m, looking down), the top/bottom frustum planes clip
    // in a way that the box's minimal Y extent causes the support-point test to
    // produce false negatives — tiles near the frustum boundary are incorrectly
    // culled even though their screen projection is clearly visible.  This manifests
    // as alternating gaps along a straight road at diagonal viewing angles.
    //
    // Expanding to 0.5 m (±0.25 m from the geometric midpoint) gives the frustum
    // culler enough Y headroom without affecting the rendered geometry (the actual
    // vertex positions are unchanged; only the AABB used for the visibility test
    // is widened).  The box is widened symmetrically around the midpoint so that
    // terrain-conforming tiles at varying elevations are also correctly handled.
    {
        core::aabbox3df box = buf->getBoundingBox();
        const float yMid = (box.MaxEdge.Y + box.MinEdge.Y) * 0.5f;
        const float kMinYExtent = 0.5f;
        if (box.MaxEdge.Y - box.MinEdge.Y < kMinYExtent) {
            box.MinEdge.Y = yMid - kMinYExtent * 0.5f;
            box.MaxEdge.Y = yMid + kMinYExtent * 0.5f;
            buf->setBoundingBox(box);
        }
    }

    mesh->addMeshBuffer(buf);
    buf->drop();
    mesh->recalculateBoundingBox();
    return mesh;
}

// -------------------------------------------------------------------------
// placeRoadMesh — public IRenderer override (delegates to internal version).
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeRoadMesh(int tileX, int tileZ)
{
    placeRoadMesh(tileX, tileZ, /*flattenTerrain=*/true, /*rebuildNeighbors=*/true);
}

// -------------------------------------------------------------------------
// placeRoadMesh (internal) — terrain-conforming sloped road placement.
//
// Conditional flattening (max 15° slope):
//   - Read the 4 tile corner heights.
//   - Compute max slope angle across the tile diagonal pairs.
//   - If slope > 15°, scale corner height deltas down so the slope equals 15°.
//   - Write the (possibly adjusted) corner heights back via setTileHeight().
//   - Flush terrain rebuilds synchronously.
//
// Per-tile LOD0 mesh:
//   - Call buildTileRoadMesh(h00, h10, h01, h11) to build a terrain-conforming
//     quad + kerb geometry with actual world-space heights baked into vertex Y.
//   - Node is positioned at world X/Z centre with Y=0 (heights already in verts).
//
// Neighbor edge matching (rebuildNeighbors=true only):
//   - After placement check all 4 cardinal neighbors.
//   - If a neighbor has a road node, rebuild its mesh (flattenTerrain=false,
//     rebuildNeighbors=false) so its geometry reflects the newly-written heights
//     at the shared edge.
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

    // --- Read current corner heights ---
    float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
    float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
    float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
    float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;

    if (flattenTerrain && m_terrain) {
        // --- Conditional slope flattening ---
        // Maximum slope angle allowed for a road tile.
        static constexpr float kMaxRoadSlopeDeg = 15.0f;
        static constexpr float kMaxRoadSlopeRad =
            kMaxRoadSlopeDeg * static_cast<float>(M_PI) / 180.0f;
        const float tanMax = std::tan(kMaxRoadSlopeRad);

        // Compute max gradient magnitude across the two diagonal pairs.
        // Each diagonal spans sqrt(2)*kTileSize; approximate via max of X/Z components.
        // dX_near: slope in X at z=0 edge;  dX_far:  slope in X at z=1 edge.
        // dZ_near: slope in Z at x=0 edge;  dZ_far:  slope in Z at x=1 edge.
        const float dX0 = (h10 - h00) / kTileSize;
        const float dX1 = (h11 - h01) / kTileSize;
        const float dZ0 = (h01 - h00) / kTileSize;
        const float dZ1 = (h11 - h10) / kTileSize;

        // Max slope magnitude across all 4 gradient samples.
        auto grad2 = [](float a, float b) { return std::sqrt(a*a + b*b); };
        const float slopeMax = std::max({
            grad2(dX0, dZ0),
            grad2(dX0, dZ1),
            grad2(dX1, dZ0),
            grad2(dX1, dZ1)
        });
        const float slopeAngle = std::atan(slopeMax);  // radians

        if (slopeAngle > kMaxRoadSlopeRad) {
            // Scale down the height differences to bring slope to exactly 15°.
            // The scale factor reduces deltas so that max gradient = tan(15°).
            const float scale = tanMax / slopeMax;

            // Use the average as the reference (same as old flat approach), then
            // scale each corner's deviation from the average.
            const float avg = (h00 + h10 + h01 + h11) * 0.25f;
            h00 = avg + (h00 - avg) * scale;
            h10 = avg + (h10 - avg) * scale;
            h01 = avg + (h01 - avg) * scale;
            h11 = avg + (h11 - avg) * scale;
        }
        // If slopeAngle <= 15°, corners are used as-is (no flattening needed).

        // Write the (possibly adjusted) corner heights back to the terrain.
        m_terrain->setTileHeight(tileX,     tileZ,     h00);
        m_terrain->setTileHeight(tileX + 1, tileZ,     h10);
        m_terrain->setTileHeight(tileX,     tileZ + 1, h01);
        m_terrain->setTileHeight(tileX + 1, tileZ + 1, h11);
        m_terrain->flushTerrainRebuilds();
    }

    // --- Build per-tile LOD0 terrain-conforming mesh ---
    SMesh* tileMesh = buildTileRoadMesh(h00, h10, h01, h11);
    if (!tileMesh) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeRoadMesh(%d,%d): buildTileRoadMesh"
            " failed — node not created\n", tileX, tileZ);
        return;
    }

    // Create scene node.  addMeshSceneNode() calls grab() → tileMesh ref_count 1→2.
    IMeshSceneNode* node = m_smgr->addMeshSceneNode(tileMesh);
    tileMesh->drop();  // release caller's ref; scene node now sole owner
    if (!node) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeRoadMesh(%d,%d): addMeshSceneNode"
            " failed — node not created\n", tileX, tileZ);
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
    // Road tile meshes are nearly flat (~0.1 m tall) so their AABB gives the
    // EAC_BOX culler very little vertical headroom.  At oblique camera angles
    // tiles near the frustum boundary can be false-rejected even after the
    // 0.5 m Y-extent expansion applied in buildTileRoadMesh().  Road tiles are
    // small (10 m × 10 m) and there are at most a few hundred of them; the
    // performance cost of skipping the AABB test is negligible.
    node->setAutomaticCulling(irr::scene::EAC_OFF);

    // Disable lighting (no light nodes in scene).
    for (u32 m = 0; m < node->getMaterialCount(); ++m) {
        node->getMaterial(m).Lighting = false;
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

    // --- Neighbor edge matching ---
    // Rebuild cardinal road neighbors so their geometry reflects the newly-written
    // shared edge heights.  Pass flattenTerrain=false (terrain already correct)
    // and rebuildNeighbors=false (prevent infinite recursion).
    if (rebuildNeighbors) {
        static constexpr int kDX[4] = {-1,  1,  0, 0};
        static constexpr int kDZ[4] = { 0,  0, -1, 1};
        for (int d = 0; d < 4; ++d) {
            const int nx = tileX + kDX[d];
            const int nz = tileZ + kDZ[d];
            if (m_roadNodes.count(tileKey(nx, nz)) > 0) {
                placeRoadMesh(nx, nz, /*flattenTerrain=*/false,
                                      /*rebuildNeighbors=*/false);
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeServiceBuildingMesh(%d,%d): "
            "unknown ServiceBuildingType %d — skipping\n",
            tileX, tileZ, static_cast<int>(type));
        return;
    }

    // Remove any existing building on this tile first.
    destroyTileNode(m_buildingNodes, tileX, tileZ);

    std::string basePath = std::string(AITOWN_ASSETS_DIR) +
                           "/3d/buildings/" + stem;

    LODNode* lodNode = m_buildingAssetLoader->load(basePath);
    if (!lodNode) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeServiceBuildingMesh(%d,%d): "
            "failed to load asset '%s' — node not created\n",
            tileX, tileZ, basePath.c_str());
        return;
    }

    if (scene::ISceneNode* node = lodNode->getNode()) {
        // Four-corner terrain flattening pattern (Phase 10b): average the 4 tile corner heights
        // then flatten each corner vertex to that average, eliminating T-junction seams.
        const float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
        const float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
        const float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
        const float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;
        const float targetH = (h00 + h10 + h01 + h11) * 0.25f;
        if (m_terrain) {
            m_terrain->setTileHeight(tileX,     tileZ,     targetH);
            m_terrain->setTileHeight(tileX + 1, tileZ,     targetH);
            m_terrain->setTileHeight(tileX,     tileZ + 1, targetH);
            m_terrain->setTileHeight(tileX + 1, tileZ + 1, targetH);
        }
        if (m_terrain) m_terrain->flushTerrainRebuilds();
        // Use targetH directly — NOT getHeightAt() after setTileHeight().
        // setTileHeight() applies neighbour blending to the 8 surrounding tiles;
        // subsequent corner calls bleed back into vertex (tileX, tileZ), leaving
        // its stored height below targetH. getHeightAt() would return that
        // blended-down value and position the node below the rendered terrain surface.
        const float postY = m_terrain ? targetH : 0.0f;
        node->setPosition(core::vector3df(
            static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
            postY + 0.10f,   // 10 cm above terrain — covers tile-edge bleed-back after
                             // neighbour blending; polygon offset is the primary Z-fight defence.
            static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
        node->setScale(core::vector3df(kTileSize, kTileSize, kTileSize));

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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: ensureVehicleLoader() called with null "
            "m_smgr/m_driver — vehicle scene node cannot be created\n");
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
        m_driver->setMaterial(SMaterial{});
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeVehicle(id=%u) called with empty "
            "assetName — skipping node creation\n", vehicleId);
        return;
    }

    // Remove any existing node for this vehicleId before placing the new one.
    destroyVehicleNode(vehicleId);

    if (!ensureVehicleLoader()) return;

    const std::string basePath = std::string(AITOWN_ASSETS_DIR)
                                 + "/3d/vehicles/" + assetName;

    LODNode* lodNode = m_vehicleAssetLoader->load(basePath);
    if (!lodNode) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeVehicle(id=%u): failed to load "
            "asset '%s' — node not created\n", vehicleId, basePath.c_str());
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
        // BackfaceCulling=true: vehicles have correct winding (authored by artist).
        // Lighting=false: no light nodes in scene yet (Phase 6+).
        // Atlas fallback: if BuildingAssetLoader did not bind the atlas (file missing),
        // bind vehicles_diffuse_atlas_d.dds directly as a safety fallback.
        const std::string atlasPath = std::string(AITOWN_ASSETS_DIR)
            + "/textures/vehicles/vehicles_diffuse_atlas_d.dds";

        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            SMaterial& mat = node->getMaterial(m);
            mat.Lighting        = false;
            mat.BackfaceCulling = true;
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
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: moveVehicle(id=%u): vehicleId not "
            "registered — ignoring move (caller should use placeVehicle first)\n",
            vehicleId);
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
//   - kDomeRings  rows of latitude from top (ring 0) to horizon (ring kDomeRings-1).
//   - kDomeSectors columns of longitude around the dome.
//   - Top cap: kDomeSectors triangles fanning from the apex vertex.
//   - Body:    (kDomeRings-2) × kDomeSectors quads (2 triangles each).
//   - Bottom ring vertices are placed at Y = kCloudAltitude (dome base), outer
//     ring vertices at Y = kCloudAltitude + kCloudDomeHeight (apex).
//
// UV mapping (polar, from apex outward):
//   Each vertex is mapped from dome-top (UV centre = 0.5,0.5) to dome base
//   (UV radius = 0.5) scaled by kCloudUVScale so the texture tiles naturally.
//     u = (nx * 0.5f + 0.5f) * kCloudUVScale
//     v = (nz * 0.5f + 0.5f) * kCloudUVScale
//   where (nx, nz) is the horizontal unit direction of the vertex.
//
// Vertex colour:
//   Alpha 255 at apex, fading linearly to 0 at the bottom ring for a soft
//   horizon blend. RGB always (255,255,255) — tint is done by the texture.
//
// Winding: CW from outside (camera is inside the dome, looking up and outward).
// Irrlicht back-face culling is disabled on the material so the inner surface
// is always rendered.
//
// SMesh lifetime: caller owns the returned pointer (ref_count=1) and MUST call
// smesh->drop() after addMeshSceneNode() to release its own reference.
// -------------------------------------------------------------------------
static SMesh* buildCloudDomeMesh()
{
    constexpr int   kDomeRings         = 32;     // latitude bands — keep fade smooth
    constexpr int   kDomeSectors       = 32;     // longitude segments
    constexpr float kCloudAltitude     = -1000.0f; // world-space Y of dome base (far below terrain)
    constexpr float kCloudDomeRadius   = 6000.0f;  // horizontal radius at base ring
    constexpr float kCloudDomeHeight   = 2000.0f;  // vertical height from base to apex (apex at Y=1000 m)
    constexpr float kCloudUVScale      = 4.0f;   // texture tiling factor
    // Horizon fade and atmospheric haze are handled entirely in the fragment shader
    // (cloud_dome.frag, rev 3).  All vertices use alpha=255.
    // Alpha fade: −5° to 20° — clouds visible at and slightly below the horizon.
    // Atmospheric haze: near-horizon cloud RGB is blended toward sky background colour
    // so azimuthal texture variation (directional arc) has no visible colour contrast.
    // UV is cylindrical (not polar) for uniform texture distribution at each ring.

    SMesh*       mesh = new SMesh();
    SMeshBuffer* buf  = new SMeshBuffer();

    // Build kDomeRings+1 rings of vertices (ring 0 = apex, ring kDomeRings = base).
    // Each ring i has kDomeSectors+1 vertices (sector 0 and kDomeSectors share the
    // same XZ position but have distinct UV to avoid a seam fold).
    //
    // latitude parameter t: 0 (apex) → 1 (base).
    //   Y       = kCloudAltitude + kCloudDomeHeight * (1 - t)
    //   radius  = kCloudDomeRadius * t
    //   (nx,nz) = (sin(phi), cos(phi))  where phi = sector * 2π / kDomeSectors
    //
    // All vertices use alpha=255.  Horizon fade is handled entirely in the fragment
    // shader (cloud_dome.frag, rev 2) using the elevation angle from the camera,
    // which is symmetric in all azimuths.  See CloudDomeShaderCallback::setCameraY().

    const float piF = static_cast<float>(M_PI);

    for (int ring = 0; ring <= kDomeRings; ++ring) {
        const float t      = static_cast<float>(ring) / static_cast<float>(kDomeRings);
        const float y      = kCloudAltitude + kCloudDomeHeight * (1.0f - t);
        const float r      = kCloudDomeRadius * t;  // horizontal radius at this ring

        for (int sec = 0; sec <= kDomeSectors; ++sec) {
            const float phi = static_cast<float>(sec) / static_cast<float>(kDomeSectors)
                              * 2.0f * piF;
            const float nx  = std::sin(phi);
            const float nz  = std::cos(phi);
            const float px  = r * nx;
            const float pz  = r * nz;

            // UV: cylindrical mapping — u wraps around the azimuth (0→kCloudUVScale
            // as phi goes 0→2π), v maps from apex (0) to base ring (kCloudUVScale).
            // This ensures every ring samples the full texture width uniformly in all
            // azimuth directions, so cloud density at any given elevation is statistically
            // the same in every compass direction.  The old polar mapping (top-down
            // projection) caused the UV sampling circle at the fade-band elevation ring to
            // pass through denser cloud regions in some azimuths and sparse regions in
            // others, producing an asymmetric horizon arc.
            const float phi_norm = static_cast<float>(sec) / static_cast<float>(kDomeSectors);
            const float u = phi_norm * kCloudUVScale;
            const float v = t * kCloudUVScale;

            // Normal points inward-upward (camera is inside the dome).
            // Alpha=255 always — the fragment shader applies the elevation-angle fade.
            buf->Vertices.push_back(S3DVertex(
                core::vector3df(px, y, pz),
                core::vector3df(-nx, 1.0f, -nz),    // approximate inward normal
                SColor(255, 255, 255, 255),
                core::vector2df(u, v)));
        }
    }

    // Index buffer: CW winding when viewed from inside the dome.
    // Each quad (ring r, sector s) spans vertices:
    //   top-left  = r       * (kDomeSectors+1) + s
    //   top-right = r       * (kDomeSectors+1) + s + 1
    //   bot-left  = (r+1)   * (kDomeSectors+1) + s
    //   bot-right = (r+1)   * (kDomeSectors+1) + s + 1
    // Inside-CW winding (looking from inside upward): top-left, bot-left, top-right
    //                                                 bot-left,  bot-right, top-right

    for (int ring = 0; ring < kDomeRings; ++ring) {
        for (int sec = 0; sec < kDomeSectors; ++sec) {
            const irr::u16 tl = static_cast<irr::u16>( ring      * (kDomeSectors + 1) + sec);
            const irr::u16 tr = static_cast<irr::u16>( ring      * (kDomeSectors + 1) + sec + 1);
            const irr::u16 bl = static_cast<irr::u16>((ring + 1) * (kDomeSectors + 1) + sec);
            const irr::u16 br = static_cast<irr::u16>((ring + 1) * (kDomeSectors + 1) + sec + 1);

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
            // (stored as m_cloudShaderCbRaw) so that setCameraY() can be called each
            // frame in update().  Irrlicht also calls grab() internally on the passed
            // pointer.  The caller's reference is dropped in IrrlichtRenderer::~IrrlichtRenderer().
            // Never std::unique_ptr — causes double-free (see CLAUDE.md shader callbacks).
            CloudDomeShaderCallback* cb = new CloudDomeShaderCallback();
            irr::s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
                vsPath.c_str(), "main", irr::video::EVST_VS_1_1,
                fsPath.c_str(), "main", irr::video::EPST_PS_1_1,
                cb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
            // Do NOT drop cb unconditionally here — we store our reference for per-frame
            // setCameraY() updates.  Drop only on shader failure (we have no use for it).
            if (matType == -1) {
                fprintf(stderr,
                    "[IrrlichtRenderer] WARNING: cloud dome shader compile failed "
                    "(vs=%s, fs=%s) — falling back to EMT_TRANSPARENT_VERTEX_ALPHA\n",
                    vsPath.c_str(), fsPath.c_str());
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
        if (tex) mat.setTexture(0, tex);
    }

    // m_cloudUVOffset initialised to {0.f, 0.f} by member initialiser in header.
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

    // Reposition dome to camera XZ so the horizon ring always surrounds the player.
    // m_lastCameraPosition is updated by setCamera() every frame before update() runs.
    // Y=0: dome vertex positions embed kCloudAltitude in world-space directly.
    if (m_camera) {
        const core::vector3df camPos = m_camera->getPosition();
        m_cloudNode->setPosition(core::vector3df(camPos.X, 0.0f, camPos.Z));

        // Feed camera world-space Y to the shader callback so the elevation-angle
        // fade in cloud_dome.frag uses the correct reference height each frame.
        if (m_cloudShaderCbRaw) {
            static_cast<CloudDomeShaderCallback*>(m_cloudShaderCbRaw)
                ->setCameraY(camPos.Y);
        }
    }
}
