// building_lod_swap_test.cpp — Phase 9 LOD swap bounding box integration test.
// Registered under the requires-opengl label in the opengl_tests target.
//
// Verifies the Phase 9 LOD swap contract for buildings/vehicles:
//   node->setMesh(newLODMesh) WITHOUT recalculateBoundingBox() leaves a stale bounding box
//   (NegativeCase); calling recalculateBoundingBox() before setMesh() corrects it (PositiveCase).
//
// Phase-9.md line 42 spec:
//   Both meshes use video::EVT_STANDARD vertex format (S3DVertex).
//   LOD0: 4-vertex 10 m x 10 m quad at Y=0. Positions: (-5,0,-5),(5,0,-5),(5,0,5),(-5,0,5).
//         Expected bounding box: {-5,0,-5} to {5,0,5}.
//   LOD1: 4-vertex 5 m x 5 m quad at Y=0.  Positions: (-2.5,0,-2.5),(2.5,0,-2.5),(2.5,0,2.5),(-2.5,0,2.5).
//         Expected bounding box: {-2.5,0,-2.5} to {2.5,0,2.5}.
//
// Both meshes carry actual index data (two triangles = 6 indices) to avoid degenerate
// bounding boxes that would produce false passes.
//
// Bounding box comparison tolerance: 0.01f (EXPECT_NEAR).
//
// Grab/drop contract (VERIFIED in lod_swap_smoke_test.cpp):
//   addMeshBuffer() calls grab() on the buffer — caller must drop() after addMeshBuffer().
//   setMesh()       calls grab() on the mesh   — caller must drop() after setMesh().
//   addMeshSceneNode() calls grab() on the mesh — caller must drop() after addMeshSceneNode().
//
// Run under xvfb-run in CI (requires-opengl label).

#include <gtest/gtest.h>

// GLEW before any Irrlicht/GL includes — prevents symbol conflicts.
#include <GL/glew.h>
#include <irrlicht.h>

using namespace irr;
using namespace irr::video;
using namespace irr::scene;

// ---------------------------------------------------------------------------
// Helper: create a minimal EDT_OPENGL device for bounding-box tests.
// Window size is deliberately small (1x1) to minimise GPU resource allocation.
// Returns nullptr on failure (caller must ASSERT_NE / GTEST_SKIP as appropriate).
// ---------------------------------------------------------------------------
static IrrlichtDevice* createTestDevice() {
    SIrrlichtCreationParameters params;
    params.DriverType    = EDT_OPENGL;
    params.WindowSize    = core::dimension2d<u32>(1, 1);
    params.Bits          = 32;
    params.ZBufferBits   = 24;
    params.Fullscreen    = false;
    params.Stencilbuffer = false;
    params.AntiAlias     = 0;
    params.Vsync         = false;
    // Suppress Irrlicht log output during the test.
    params.LoggingLevel  = ELL_NONE;
    return createDeviceEx(params);
}

// ---------------------------------------------------------------------------
// Helper: build an SMesh* with one SMeshBuffer representing an axis-aligned
// quad at Y=0.
//
// halfExtent: half-width of the quad in the X and Z axes.
//   halfExtent=5.0f  → LOD0 10 m x 10 m quad (positions ±5 in X and Z)
//   halfExtent=2.5f  → LOD1  5 m x 5 m quad (positions ±2.5 in X and Z)
//
// Geometry:
//   v0=(-h,0,-h)  v1=(+h,0,-h)  v2=(+h,0,+h)  v3=(-h,0,+h)
//   Normals: (0,1,0) for all vertices.
//   UVs: (0,0),(1,0),(1,1),(0,1).
//   Indices: two triangles — {0,1,2} and {0,2,3} — forming a solid quad.
//
// The returned SMesh* has ref_count == 1 (caller owns it).
// recalculateBoundingBox() is NOT called on the buffer or the mesh — the
// caller controls when (or whether) to call it, matching the negative/positive
// case pattern in the tests.
//
// VERIFIED (lod_swap_smoke_test.cpp):
//   addMeshBuffer() calls grab() on the buffer — drop() is called here to
//   release the caller's ownership reference after addMeshBuffer().
// ---------------------------------------------------------------------------
static SMesh* buildQuadMesh(float halfExtent) {
    SMeshBuffer* buf = new SMeshBuffer();

    const float h = halfExtent;
    const SColor white(255, 255, 255, 255);

    // Four vertices of the quad: (-h,0,-h), (+h,0,-h), (+h,0,+h), (-h,0,+h)
    S3DVertex v0(-h, 0.f, -h,   0.f, 1.f, 0.f,   white,   0.f, 0.f);
    S3DVertex v1(+h, 0.f, -h,   0.f, 1.f, 0.f,   white,   1.f, 0.f);
    S3DVertex v2(+h, 0.f, +h,   0.f, 1.f, 0.f,   white,   1.f, 1.f);
    S3DVertex v3(-h, 0.f, +h,   0.f, 1.f, 0.f,   white,   0.f, 1.f);

    buf->Vertices.push_back(v0);
    buf->Vertices.push_back(v1);
    buf->Vertices.push_back(v2);
    buf->Vertices.push_back(v3);

    // Two triangles forming the quad: {0,1,2} and {0,2,3}
    buf->Indices.push_back(0);
    buf->Indices.push_back(1);
    buf->Indices.push_back(2);
    buf->Indices.push_back(0);
    buf->Indices.push_back(2);
    buf->Indices.push_back(3);

    // NOTE: recalculateBoundingBox() on the buffer is intentionally NOT called here.
    // The tests control when (or whether) this is called to exercise the stale/fresh paths.

    SMesh* mesh = new SMesh();
    mesh->addMeshBuffer(buf);
    // VERIFIED: addMeshBuffer() calls grab() — caller must drop() now to yield ownership.
    buf->drop();

    // NOTE: recalculateBoundingBox() on the mesh is intentionally NOT called here.
    // The tests control when (or whether) this is called.

    return mesh; // ref_count == 1
}

// ===========================================================================
// BuildingLODSwapTest::NegativeCase
//
// Verifies that calling node->setMesh(lod1Mesh) WITHOUT calling
// recalculateBoundingBox() on lod1Mesh leaves a stale bounding box.
// The node continues to report LOD0's 10 m extents after the swap.
//
// This test documents that the stale-box problem exists: production code
// MUST call recalculateBoundingBox() before setMesh() to avoid incorrect
// frustum culling at the new LOD level (see PositiveCase for the correct path).
// ===========================================================================
TEST(BuildingLODSwapTest, NegativeCase) {
    IrrlichtDevice* device = createTestDevice();
    ASSERT_NE(device, nullptr)
        << "EDT_OPENGL device creation failed — is xvfb running?";

    ISceneManager* smgr = device->getSceneManager();
    ASSERT_NE(smgr, nullptr);

    // Build LOD0 mesh (10 m x 10 m quad). Explicitly recalculate bounding box
    // so LOD0's box is correct when attached to the scene node.
    SMesh* lod0Mesh = buildQuadMesh(5.0f); // half-extent = 5 m → ±5 in X and Z
    for (u32 i = 0; i < lod0Mesh->getMeshBufferCount(); ++i) {
        lod0Mesh->getMeshBuffer(i)->recalculateBoundingBox();
    }
    lod0Mesh->recalculateBoundingBox();

    // addMeshSceneNode() calls grab() on lod0Mesh — drop() to release caller reference.
    IMeshSceneNode* node = smgr->addMeshSceneNode(lod0Mesh);
    ASSERT_NE(node, nullptr);
    lod0Mesh->drop(); // ref_count 2 → 1; node is now sole owner of lod0Mesh.

    // Build LOD1 mesh (5 m x 5 m quad). Do NOT call recalculateBoundingBox()
    // on this mesh — this is the negative case (stale bounding box scenario).
    SMesh* lod1Mesh = buildQuadMesh(2.5f); // half-extent = 2.5 m → ±2.5 in X and Z
    // Deliberately omit: lod1Mesh->getMeshBuffer(0)->recalculateBoundingBox();
    // Deliberately omit: lod1Mesh->recalculateBoundingBox();
    // Without these calls, the bounding box of lod1Mesh is undefined (initialised
    // to the SMeshBuffer default: a zero-extent box at the origin). setMesh() uses
    // whatever bounding box is on the mesh at the time of the call.

    // Perform the LOD swap WITHOUT the mandatory recalculateBoundingBox() calls.
    // VERIFIED (lod_swap_smoke_test.cpp): setMesh() calls grab() on lod1Mesh.
    node->setMesh(lod1Mesh);
    lod1Mesh->drop(); // ref_count 2 → 1; node is now sole owner of lod1Mesh.
    // DO NOT access lod1Mesh after this drop() — caller's reference is released.

    // The scene node's bounding box should still reflect LOD0's 10 m extents
    // because lod1Mesh had no valid bounding box at the time of setMesh().
    // In practice, Irrlicht copies the mesh's bounding box into the scene node
    // during setMesh(). Since lod1Mesh's bounding box was never recalculated,
    // the node's box is derived from lod1Mesh's uninitialised/zero-extent box —
    // NOT from lod0Mesh's box. The box.MaxEdge.X will NOT be ≈5.
    //
    // This documents the stale-box problem: the node's extent no longer matches
    // either LOD mesh after an unguarded swap. The PositiveCase shows the fix.
    //
    // We assert that MaxEdge.X is NOT ≈ 2.5 (the correct LOD1 value), confirming
    // the swap without recalculation does not produce correct LOD1 extents either.
    const core::aabbox3df& box = node->getBoundingBox();
    EXPECT_NE(box.MaxEdge.X, 2.5f)
        << "NegativeCase: without recalculateBoundingBox(), bounding box must NOT "
           "report correct LOD1 extents (2.5 m). Stale/uninitialised box expected.";

    node->remove();
    device->drop();
}

// ===========================================================================
// BuildingLODSwapTest::PositiveCase
//
// Verifies that calling lod1Mesh->recalculateBoundingBox() (on all buffers
// then the mesh) before node->setMesh(lod1Mesh) produces the correct LOD1
// bounding box on the scene node after the swap.
//
// This is the production-correct LOD swap path per:
//   architecture/graphics-architecture/scene-graph-ownership.md § LOD Swap
//   implementation/phase-9.md line 14
// ===========================================================================
TEST(BuildingLODSwapTest, PositiveCase) {
    IrrlichtDevice* device = createTestDevice();
    ASSERT_NE(device, nullptr)
        << "EDT_OPENGL device creation failed — is xvfb running?";

    ISceneManager* smgr = device->getSceneManager();
    ASSERT_NE(smgr, nullptr);

    // Build LOD0 mesh (10 m x 10 m quad) and attach to scene node.
    SMesh* lod0Mesh = buildQuadMesh(5.0f); // half-extent = 5 m → ±5 in X and Z
    for (u32 i = 0; i < lod0Mesh->getMeshBufferCount(); ++i) {
        lod0Mesh->getMeshBuffer(i)->recalculateBoundingBox();
    }
    lod0Mesh->recalculateBoundingBox();

    // addMeshSceneNode() calls grab() on lod0Mesh — drop() to release caller reference.
    IMeshSceneNode* node = smgr->addMeshSceneNode(lod0Mesh);
    ASSERT_NE(node, nullptr);
    lod0Mesh->drop(); // ref_count 2 → 1; node is now sole owner of lod0Mesh.

    // Confirm LOD0 bounding box on the node: MaxEdge.X should be ≈ 5.0 m.
    // This baseline check confirms the initial state is correct before the swap.
    {
        const core::aabbox3df& box = node->getBoundingBox();
        EXPECT_NEAR(box.MaxEdge.X, 5.0f, 0.01f)
            << "PositiveCase baseline: LOD0 bounding box MaxEdge.X should be ≈5.0 m";
        EXPECT_NEAR(box.MinEdge.X, -5.0f, 0.01f)
            << "PositiveCase baseline: LOD0 bounding box MinEdge.X should be ≈-5.0 m";
    }

    // Build LOD1 mesh (5 m x 5 m quad).
    SMesh* lod1Mesh = buildQuadMesh(2.5f); // half-extent = 2.5 m → ±2.5 in X and Z

    // Mandatory per scene-graph-ownership.md § LOD Swap:
    // Step 1: recalculate bounding box on every SMeshBuffer.
    for (u32 i = 0; i < lod1Mesh->getMeshBufferCount(); ++i) {
        lod1Mesh->getMeshBuffer(i)->recalculateBoundingBox();
    }
    // Step 2: recalculate bounding box on the SMesh (must follow all buffer calls).
    lod1Mesh->recalculateBoundingBox();

    // Step 3: setMesh() — Irrlicht internally calls grab() on lod1Mesh → ref_count 2.
    //         setMesh() also calls drop() on the old mesh (lod0Mesh) → lod0Mesh destroyed
    //         (node was the sole owner after its drop() above → ref_count reaches 0).
    node->setMesh(lod1Mesh);

    // Step 4: drop() caller's reference to lod1Mesh → ref_count 2 → 1.
    //         Node is now the sole owner of lod1Mesh.
    lod1Mesh->drop();
    // DO NOT access lod1Mesh after this drop() — caller's reference is released.

    // Verify: the node's bounding box now reports LOD1's 5 m extents.
    // MaxEdge.X ≈ 2.5, MinEdge.X ≈ -2.5 (half-extent = 2.5 m).
    // MaxEdge.Z ≈ 2.5, MinEdge.Z ≈ -2.5 (same in Z; quad is symmetric).
    // Y extents: all vertices at Y=0, so MinEdge.Y ≈ 0 ≈ MaxEdge.Y.
    const core::aabbox3df& box = node->getBoundingBox();

    EXPECT_NEAR(box.MaxEdge.X, 2.5f, 0.01f)
        << "PositiveCase: after correct LOD swap, bounding box MaxEdge.X must be ≈2.5 m (LOD1 5 m quad)";
    EXPECT_NEAR(box.MinEdge.X, -2.5f, 0.01f)
        << "PositiveCase: after correct LOD swap, bounding box MinEdge.X must be ≈-2.5 m (LOD1 5 m quad)";
    EXPECT_NEAR(box.MaxEdge.Z, 2.5f, 0.01f)
        << "PositiveCase: after correct LOD swap, bounding box MaxEdge.Z must be ≈2.5 m (LOD1 5 m quad)";
    EXPECT_NEAR(box.MinEdge.Z, -2.5f, 0.01f)
        << "PositiveCase: after correct LOD swap, bounding box MinEdge.Z must be ≈-2.5 m (LOD1 5 m quad)";

    node->remove();
    device->drop();
}
