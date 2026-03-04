// IrrlichtRenderer.cpp — IRenderer concrete implementation backed by Irrlicht.
// GLEW must be included BEFORE irrlicht.h (symbol conflict mitigation).
#include <GL/glew.h>

#include <irrlicht.h>

#include "IrrlichtRenderer.h"
#include "src/ui/UIManager.h"            // FULL include here (not in header — per Header Dependency Rule)
#include "src/interfaces/ITerrainQuery.h" // ITerrainQuery full include (forward-decl in header only)
#include "BuildingAssetLoader.h"          // Phase 10: load .b3d asset families via BuildingAssetLoader::load()
#include "LODNode.h"                      // Phase 10: LOD swap wrapper returned by BuildingAssetLoader::load()

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

IrrlichtRenderer::IrrlichtRenderer(irr::IrrlichtDevice* device, UIManager* uiManager)
    : m_device(device)
    , m_uiManager(uiManager)
    , m_driver(device ? device->getVideoDriver() : nullptr)
    , m_smgr(device ? device->getSceneManager() : nullptr)
{
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
}

IrrlichtRenderer::~IrrlichtRenderer() {
    // Drop the hover tile mesh (ref_count 1→0 frees the mesh and its contained buffer).
    // Allocated unconditionally in the constructor — m_hoveredTileMesh is always non-null.
    if (m_hoveredTileMesh) {
        m_hoveredTileMesh->drop();
        m_hoveredTileMesh = nullptr;
        m_hoverBuffer     = nullptr;  // non-owning observer — already freed by the mesh drop
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

    // m_buildingAssetLoader (unique_ptr) is destroyed automatically.
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
    // B3D placeholder meshes are 1×1×1 unit cubes centred at origin in XZ.
    // Scale by kTileSize so they fill the full tile footprint (10×10 m).
    // Offset by kTileSize/2 in X and Z to centre the mesh on the tile.
    // Disable lighting — B3D assets have no lights in the scene yet; without
    // this, Irrlicht renders the mesh black (ambient=0).
    if (scene::ISceneNode* node = lodNode->getNode()) {
        const float terrainY = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f;
        node->setPosition(core::vector3df(
            static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
            terrainY,
            static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
        node->setScale(core::vector3df(kTileSize, kTileSize, kTileSize));
        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            node->getMaterial(m).Lighting = false;
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
// placeRoadMesh — road tile placement.
//
// All road tiles share the same mesh family: flat LOD0 quad + kerb geometry
// (<=48 tris) with the road custom shader and road_asphalt_tileable.dds.
// Asset base path: assets/3d/roads/road_tile
// -------------------------------------------------------------------------
void IrrlichtRenderer::placeRoadMesh(int tileX, int tileZ)
{
    if (!ensureAssetLoader()) return;

    // Remove any existing road on this tile first.
    destroyTileNode(m_roadNodes, tileX, tileZ);

    // Fixed asset path — all road tiles are identical.
    std::string basePath = std::string(AITOWN_ASSETS_DIR) + "/3d/roads/road_tile";

    LODNode* lodNode = m_buildingAssetLoader->load(basePath);
    if (!lodNode) {
        fprintf(stderr,
            "[IrrlichtRenderer] WARNING: placeRoadMesh(%d,%d): failed to load "
            "road tile asset '%s' — node not created\n",
            tileX, tileZ, basePath.c_str());
        return;
    }

    if (scene::ISceneNode* node = lodNode->getNode()) {
        const float terrainY = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f;
        node->setPosition(core::vector3df(
            static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
            terrainY,
            static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
        // Roads are flat: 1 unit in X/Z maps to kTileSize; 1 unit in Y maps to
        // a thin 0.2 m surface so roads sit flat on the terrain.
        node->setScale(core::vector3df(kTileSize, 0.2f, kTileSize));
        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            node->getMaterial(m).Lighting = false;
        }
    }

    m_roadNodes[tileKey(tileX, tileZ)] = lodNode;
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
        const float terrainY = m_terrain ? m_terrain->getHeightAt(tileX, tileZ) : 0.0f;
        node->setPosition(core::vector3df(
            static_cast<f32>(tileX) * kTileSize + kTileSize * 0.5f,
            terrainY,
            static_cast<f32>(tileZ) * kTileSize + kTileSize * 0.5f));
        node->setScale(core::vector3df(kTileSize, kTileSize, kTileSize));
        for (u32 m = 0; m < node->getMaterialCount(); ++m) {
            node->getMaterial(m).Lighting = false;
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
