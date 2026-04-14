# Scene Graph Ownership Policy

- Game objects (`Building`, `Vehicle`, etc.) store a **raw pointer** to their scene node, which is treated as non-owning
- A `SceneEntityManager` holds the authoritative entity list; it is the sole place `addXxxSceneNode()` and `node->remove()` are called
- **Dangling pointer prevention**: `SceneEntityManager::destroy(entity)` must set the game object's node pointer to `nullptr` before calling `node->remove()`
- No code outside `SceneEntityManager` calls `grab()` on scene nodes

## Renderer-Internal Permanent Scene Nodes

Not all scene nodes are managed by `SceneEntityManager`. A category of **renderer-internal permanent nodes** — created once at `IrrlichtRenderer::init()` and destroyed implicitly when `device->drop()` is called in `main.cpp` — are owned directly by `IrrlichtRenderer` and exempt from the `SceneEntityManager` lifecycle:

| Node | Member | Created in | Destroyed by |
|---|---|---|---|
| Sky dome | `m_skyDomeNode` | `initSkyDome()` | `device->drop()` |
| Cloud plane | `m_cloudNode` | `initCloudPlane()` (Phase 10b) | `device->drop()` |

These nodes have no game-object counterpart and are never referenced by `SceneEntityManager`. They are created via direct `smgr->addXxxSceneNode()` / `smgr->addMeshSceneNode()` calls inside `IrrlichtRenderer`'s init helpers — this is a documented exception to the "sole place `addXxxSceneNode()` is called" policy above. The `SceneEntityManager` eviction sequence (texture clear → `setMaterial({})` → `evictUnreferenced()` → `node->remove()`) does **not** apply to renderer-internal nodes; they are released automatically by `device->drop()`.

**Note — hover highlight and preview mesh are not scene nodes**: `m_hoveredTileMesh` (`SMesh*`) and `m_previewMesh` (`SMesh*`) are **not** scene nodes and must **not** be listed in the table above. They are allocated once in the `IrrlichtRenderer` constructor, drawn each frame via raw `IVideoDriver::drawMeshBuffer()` inside `drawScene()`, and dropped explicitly (via `->drop()`) in the `IrrlichtRenderer` destructor. They have no `ISceneNode*` handle; `device->drop()` does not release them. See the "Hover Highlight — Single Buffer, Pre-Allocated" section for the full `SMesh*` lifetime contract.

**Rule**: any scene node created by an `IrrlichtRenderer` helper method (not in response to a game-object placement call) and stored as a private member of `IrrlichtRenderer` is a renderer-internal node. Add new renderer-internal nodes to the table above when introduced.

## LOD Swap — Bounding Box Requirement

- For buildings and static vehicles, LOD levels are swapped via `node->setMesh(newLODMesh)` — this replaces the mesh on the existing scene node, preserving its position, rotation, scale, and material assignments without touching the scene graph structure.
- **MANDATORY — `setMesh` requires bounding box recalculation before the swap**: Before calling `node->setMesh(newLODMesh)`, the new mesh's bounding boxes must be recalculated — identical to the bounding box requirement for terrain mesh attachment. A stale bounding box from the previous LOD level causes incorrect frustum culling at the new LOD (the node may be invisible or always visible regardless of camera position). Required call order:

  ```cpp
  // IMPORTANT: newLODMesh MUST be typed as SMesh*, NOT IMesh*.
  // recalculateBoundingBox() is a concrete-class method on SMesh and does NOT exist
  // on the IMesh interface — calling via an IMesh* upcast will not compile.
  // Retain the SMesh* type throughout the LOD swap sequence.
  SMesh* newLODMesh = /* ... load or retrieve from LOD asset cache ... */;
  for (u32 i = 0; i < newLODMesh->getMeshBufferCount(); ++i)
      newLODMesh->getMeshBuffer(i)->recalculateBoundingBox();
  newLODMesh->recalculateBoundingBox();  // must come AFTER all buffer recalculations
  node->setMesh(newLODMesh);   // Irrlicht calls grab() internally → newLODMesh ref_count becomes 2
  // NOTE: The drop() call below assumes Irrlicht calls grab() on the mesh inside setMesh()
  // (consistent with the Tutorial 10 pattern and CMeshSceneNode::setMesh() implementation).
  // This assumption MUST be validated by inspecting source/Irrlicht/CMeshSceneNode.cpp
  // in the vendored Irrlicht build — specifically whether setMesh() calls grab()/drop()
  // on the new/old mesh. This is the Phase 2 LOD spike (lod_swap_smoke_test.cpp),
  // NOT the GLEW availability spike. See the CONTINGENCY block below.
  // If the spike reveals that grab() is NOT called by setMesh(), remove this drop() — see
  // the CONTINGENCY block in this file (§"CONTINGENCY — if spike reveals grab() is NOT called").
  newLODMesh->drop();          // release caller's ref → ref_count drops to 1; scene node is now sole owner
  ```

  **Canonical implementation**: This sequence is encapsulated in `LODNode::swapMesh()` (`src/rendering/LODNode.h`). All Phase 9 building and vehicle LOD swaps must go through `LODNode::swapMesh()` rather than calling `setMesh()` directly, ensuring the mandatory bounding-box recalculation is never omitted.

  **Ref-count contract**: `node->setMesh()` increments the mesh ref count via `grab()`. This behavior is expected based on Irrlicht's `CMeshSceneNode::setMesh()` implementation pattern: the new mesh receives `grab()` and the old mesh receives `drop()` within the method body. **Implementation teams must verify this behavior in the vendored Irrlicht source** (`source/Irrlicht/CMeshSceneNode.cpp`) — "expected" reflects the standard Irrlicht pattern, but a custom or patched build may omit the `grab()` call, in which case the `drop()` after `setMesh()` would immediately destroy the mesh. Always match the `drop()` convention to the actual `setMesh()` implementation in the vendored source. If the caller retains its own reference without calling `drop()`, the mesh ref count is 2 and the mesh leaks when the scene node is later destroyed (it reaches ref_count 1, not 0). The `drop()` call immediately after `setMesh()` is mandatory for every LOD swap. This is the same rule as the terrain `SMesh` attachment sequence — the bounding box is always stale until explicitly recalculated.
  **Mandatory CI smoke test — setMesh() grab/drop verification**: The prose "expected based on Irrlicht's implementation pattern" is insufficient protection against a double-free if the vendored Irrlicht build omits `grab()` in `setMesh()`. A CI-enforced test labelled `requires-opengl` must verify this contract before LOD swap code is merged. Add to `tests/rendering/lod_swap_smoke_test.cpp`:

  ```cpp
  TEST(LODSwapSmokeTest, SetMeshGrabDropContract) {
      // Confirmed contract from Phase 2 spike (source and binary analysis):
      //
      // SMesh::addMeshBuffer() grab/drop contract (Phase 2 Checkbox A — VERIFIED):
      //   SMesh::addMeshBuffer() calls buf->grab() unconditionally (SMesh.h line 102).
      //   Caller MUST call ->drop() on the SMeshBuffer* immediately after addMeshBuffer()
      //   to relinquish the caller's ownership reference — otherwise the buffer leaks
      //   (ref_count stays at 2 and never reaches 0).
      //
      // CMeshSceneNode::setMesh() grab/drop contract (Phase 2 Checkbox B — VERIFIED):
      //   setMesh() increments the new mesh ref_count via grab() and decrements the old
      //   mesh ref_count via drop() (verified by objdump -d of CMeshSceneNode.cpp.o).
      //   Caller MUST call ->drop() on newLODMesh after setMesh() to transfer ownership.
      //
      // SMesh* release rule — CONFIRMED: always use ->drop(), never delete.
      //   The reference-counting contract requires ->drop() for all SMesh*/SMeshBuffer*
      //   lifetime management. Using delete bypasses ref-counting and causes double-free
      //   when the scene node also drops its reference.
      //
      // recalculateBoundingBox() sequence — CONFIRMED: call on every SMeshBuffer THEN
      //   on the SMesh itself before addMeshSceneNode() or setMesh(). Omitting either
      //   step leaves a degenerate bounding box that breaks frustum culling silently.
      //
      // Timing measurement requires a real GPU; this test is promoted to
      // requires-opengl label in Phase 5 when the real LOD swap is implemented.
      GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 5.";
  }
  ```

  Phase 5 fills in the real test body after the spike result is confirmed. The Phase 2 CMake registration of this file (with `GTEST_SKIP()` body) validates CI routing for the requires-opengl label. **Note**: this body must remain `GTEST_SKIP()` — `SUCCEED()` produces a false-green result that implies the timing constraint has been verified.

  If this test fails (crash or ASAN fault), the vendored Irrlicht source must be patched before any LOD swap code is written. Inspect `source/Irrlicht/CMeshSceneNode.cpp` to confirm `setMesh()` calls `grab()`/`drop()` on the new/old mesh.
  **LOD spike Checkbox A — `SMesh::addMeshBuffer()` grab/drop contract (Phase 2 verified)**:
  VERIFIED by source inspection of `build/vcpkg_installed/x64-linux/include/irrlicht/SMesh.h` line 102:
  `SMesh::addMeshBuffer()` calls `buf->grab()` unconditionally when `buf != nullptr`.
  THEREFORE: caller MUST call `->drop()` on the `SMeshBuffer*` immediately after `addMeshBuffer()`
  to relinquish the caller's ownership reference — otherwise the buffer leaks (ref_count 2, never reaches 0).
  `// VERIFIED: SMesh::addMeshBuffer() calls grab(); caller must drop() after addMeshBuffer().`

  **LOD spike Checkbox B — `CMeshSceneNode::setMesh()` grab/drop contract (Phase 2 verified)**:
  VERIFIED by binary analysis of `CMeshSceneNode.cpp.o` extracted from
  `build/vcpkg_installed/x64-linux/lib/libIrrlicht.a` via `objdump -d`:
  `setMesh()` increments the new mesh's ref_count via `addl $0x1` at offset +0x17 (grab()),
  and decrements the old mesh's ref_count via `subl $0x1` at offset +0x2e (drop()).
  THEREFORE: caller MUST call `->drop()` on `newLODMesh` after `setMesh()` to transfer ownership.
  The existing LOD swap sequence in this document is CORRECT — the `drop()` after `setMesh()` is mandatory.
  Phase 5 TerrainChunk work is UNBLOCKED by this finding.
  `// VERIFIED: CMeshSceneNode::setMesh() calls grab() on new mesh; caller must drop() after setMesh().`

  **CONTINGENCY — if spike reveals grab() is NOT called**: If the Phase 2
  `lod_swap_smoke_test.cpp` spike reveals that the vendored Irrlicht
  `CMeshSceneNode::setMesh()` does NOT call `grab()` on the new mesh:

  1. The LOD swap sequence in this document must be corrected: remove the
     `drop()` call after `setMesh()`. The caller must NOT drop the mesh after
     passing it to `setMesh()`, as there is no corresponding grab to
     counteract the drop.
  2. The `lod_swap_smoke_test.cpp` must be updated to verify (via ASAN) that
     the mesh survives the swap without the caller's `drop()`.
  3. This finding is a BLOCKING gate for Phase 5 TerrainChunk implementation
     — `scene-graph-ownership.md` must be updated before Phase 5 code touches
     `setMesh()`.
  4. The spec update must also be noted in `implementation/phase-2.md` exit
     criteria as a Phase 2 spike finding that either: (a) confirms the
     `grab()` pattern is correct and Phase 5 may proceed, or (b) documents
     the corrected no-drop pattern.

  **`recalculateBoundingBox()` type requirement**: `recalculateBoundingBox()` is a method of the concrete `SMesh` class, NOT the `IMesh` interface. The mesh pointer must be typed as `SMesh*` (not `IMesh*`) at the point this method is called. After `addMeshSceneNode()` or `setMesh()`, the scene node stores the mesh as `IMesh*` — do NOT call `getMesh()` on the scene node and attempt to call `recalculateBoundingBox()` on the returned pointer; it is typed as `IMesh*` and this method is not on the interface. Always call `recalculateBoundingBox()` before `setMesh()` while the `SMesh*` is still in scope.
- Only destroy and recreate the scene node on entity death or chunk unload. Terrain chunks always require a full node rebuild (vertex count changes between LOD levels).

### WARNING — SMesh* Downcast Safety

NEVER use `static_cast<SMesh*>` on an `IMesh*` pointer — this is an unsafe downcast that bypasses virtual dispatch and produces undefined behaviour if the object is not actually an `SMesh`. This prohibition applies only to downcasts. **Upcasting from `IAnimatedMesh*` to `IMesh*` via `static_cast` is always safe** because `IAnimatedMesh` publicly inherits `IMesh` — see §B3D Building Assets for the canonical usage.

**The LOD asset cache must preserve the `SMesh*` type end-to-end.** Store as `SMesh*`; retrieve as `SMesh*`. Never store a `SMesh*` into an `IMesh*` container and cast it back later. The cast is unverifiable by the type system and becomes a latent UB hazard the moment any cache entry is replaced with a non-SMesh asset.

**If you only have an `IMesh*`** (e.g. from a third-party loader path or a future API refactor), use `dynamic_cast<SMesh*>` with a null check in debug builds:

```cpp
#ifndef NDEBUG
SMesh* safeMesh = dynamic_cast<SMesh*>(iMeshPtr);
assert(safeMesh != nullptr && "IMesh* is not an SMesh — static_cast would be UB");
#else
SMesh* safeMesh = static_cast<SMesh*>(iMeshPtr); // only safe after debug verification
#endif
```

Abort (assert) on null in debug builds so the error is caught before any undefined behavior occurs in release. The `dynamic_cast` guard is a debug-only safety net; the correct long-term fix is to eliminate the `IMesh*` storage path entirely and keep `SMesh*` typed throughout.

---

## B3D Building Assets — IAnimatedMesh* / CMeshSceneNode Pattern (Phase 9)

This section documents the **canonical pattern for B3D-loaded static building assets** and
explains why it departs from the `SMesh*` LOD swap sequence described above.

### Why B3D files cannot use SMesh* or dynamic_cast

Irrlicht is compiled with `-fno-rtti` (see `Irrlicht/CMakeLists.txt`:
`COMPILE_FLAGS -fno-rtti`). With RTTI disabled, vtables contain no typeinfo pointers, so
`dynamic_cast` on **any** Irrlicht type will call `__dynamic_cast` which immediately
crashes (SIGSEGV) because the required typeinfo is absent.

Additionally, the B3D file loader (`CB3DMeshFileLoader`) **always** creates a
`CSkinnedMesh` — not an `SMesh`. Even if RTTI were available, `dynamic_cast<SMesh*>`
on a `CSkinnedMesh*` would return null. Using `static_cast<SMesh*>` on a
`CSkinnedMesh*` is undefined behaviour regardless of RTTI.

**Rule: never attempt a downcast on a mesh returned by `ISceneManager::getMesh()`.
Work with `IAnimatedMesh*` throughout.**

### Why CMeshSceneNode is required (not CAnimatedMeshSceneNode)

`CAnimatedMeshSceneNode::render()` applies `AbsoluteTransformation *
SSkinMeshBuffer::Transformation` per mesh buffer. B3D files exported with a root bone at
a non-identity transform (even a nominally "identity" bone may carry a non-zero
translation or rotation from the exporter) will have every mesh buffer displaced from the
intended tile position by the bone transform.

`CMeshSceneNode::render()` applies only `AbsoluteTransformation` — no bone offsets.
For static buildings, `addMeshSceneNode(static_cast<IMesh*>(lod0))` must be used (NOT
`addAnimatedMeshSceneNode`).

### Phase 9 LOD swap contract for B3D buildings

- Mesh pointers (`IAnimatedMesh*`) are **borrowed** from the Irrlicht mesh cache.
  `ISceneManager::getMesh()` returns a non-owning pointer; the cache holds
  `ref_count == 1`. Do NOT call `grab()` or `drop()` on these pointers in the
  normal LOD swap path.
  **Exception — `despawnVehicleAgent` shared-mesh hold**: when despawning an agent
  node that shares a cached `IAnimatedMesh*` with other live agent nodes (same zone
  and variant index), `despawnVehicleAgent` performs a temporary `grab()` before
  `node->remove()` and a matching `drop()` after. This prevents the mesh ref-count
  from hitting zero during `remove()` while other nodes still reference the same
  mesh — without the hold, the next `drawAll()` would access freed vertex data
  (SIGSEGV). The `grab()`/`drop()` is scoped to the single `despawnVehicleAgent`
  call and does not affect the scene-manager cache's long-term ownership. See
  §Agent Vehicle Node Registry — `despawnVehicleAgent` step 2 for the full
  sequence.
- `CMeshSceneNode::setMesh(IMesh*)` internally drops the old mesh and grabs the new mesh.
  Cast `IAnimatedMesh*` to `IMesh*` (safe — `IAnimatedMesh` publicly inherits `IMesh`):

  ```cpp
  m_node->setMesh(static_cast<irr::scene::IMesh*>(newMesh));
  // DO NOT call newMesh->drop() — the cache still holds its reference.
  ```

- `CSkinnedMesh::finalize()` (called by the B3D loader) computes the bounding box from
  all vertices. No explicit `recalculateBoundingBox()` is required for B3D assets.
- `LODNode` stores `IMeshSceneNode*` for `m_node` (to call `setMesh(IMesh*)`), and
  `IAnimatedMesh*` for `m_lod0/1/2` (borrowed from cache). `getNode()` returns
  `ISceneNode*` via implicit upcast.

### Summary: when to use which pattern

| Asset source | Mesh type | Scene node type | LOD swap | drop() after setMesh? |
|---|---|---|---|---|
| Procedural terrain / runtime | `SMesh*` | `IMeshSceneNode` | `recalculateBoundingBox()` then `setMesh()` | Yes — caller owns ref |
| B3D file (buildings, vehicles) | `IAnimatedMesh*` | `IMeshSceneNode` (via `addMeshSceneNode`) | `setMesh(static_cast<IMesh*>(lod))` | No — borrowed from cache |
| PLY file (point cloud / props) | `IAnimatedMesh*` (concrete: `SAnimatedMesh` wrapping `CDynamicMeshBuffer`) | `IMeshSceneNode` (via `addMeshSceneNode`) | `setMesh(static_cast<IMesh*>(lod))` | No — borrowed from cache |

### PLY Mesh Ownership Contract (Phase 11q12)

`ISceneManager::getMesh()` dispatches to the correct file loader based on the file extension:
`.b3d` → `CB3DMeshFileLoader`; `.ply` → `CPLYMeshFileLoader`. Both return `IAnimatedMesh*`.

The concrete type returned by `CPLYMeshFileLoader` is `SAnimatedMesh` wrapping one or more
`CDynamicMeshBuffer` instances (one per material group in the PLY file). `CDynamicMeshBuffer`
differs from `SSkinMeshBuffer` (used by the B3D loader) in one important way: it selects
the index type at construction time based on vertex count:

- Vertex count ≤ 65 535 → `EIT_16BIT` (16-bit index buffer, same as `SSkinMeshBuffer`)
- Vertex count > 65 535 → `EIT_32BIT` (32-bit index buffer)

**Upstream threshold bug (vendor patch required — Phase 11q12 Deliverable 6c):** The
vendored `CPLYMeshFileLoader.cpp` line 233 uses `vertCount > 65565` (off-by-30 typo)
instead of the correct `> 65535`. Meshes with vertex counts in the range [65 536–65 565]
are therefore incorrectly assigned `EIT_16BIT`, causing silent index truncation and
corrupt geometry at runtime. Phase 11q12 Deliverable 6c applies a one-line vendor patch
to correct the threshold to `> 65535`. All current Tripo3D LOD0 assets are well above
this range (~495 K vertices), so the bug does not affect existing assets; the patch
prevents future edge-case corruption when new assets fall in the affected band.

This automatic promotion to `EIT_32BIT` eliminates the 16-bit index overflow that can
silently corrupt large meshes loaded via `SSkinMeshBuffer`. The caller never needs to
inspect or override the index type.

**Cache ownership for PLY assets is identical to B3D assets:**

- `ISceneManager::getMesh()` returns a **borrowed** (non-owning) `IAnimatedMesh*`; the
  Irrlicht mesh cache retains `ref_count == 1`.
- Do **NOT** call `grab()` or `drop()` on the returned pointer in the normal load or LOD
  swap path.
- `CMeshSceneNode::setMesh()` internally drops the old mesh and grabs the new mesh;
  the cache reference is not disturbed.
- No explicit `recalculateBoundingBox()` is required: `CPLYMeshFileLoader` computes the
  bounding box for each `CDynamicMeshBuffer` during load, and `SAnimatedMesh` aggregates
  them.
- As with B3D, never attempt a downcast on the returned `IAnimatedMesh*`. Work with
  `IAnimatedMesh*` (or `IMesh*` via safe upcast) throughout.

**Rule: `getMesh(".ply")` → `IAnimatedMesh*` → `static_cast<IMesh*>` → `addMeshSceneNode`.
No grab, no drop, no recalculateBoundingBox.**

### CameraController — Preventing Animator Conflicts

The camera scene node must be created via `sceneManager->addCameraSceneNode()` only (never `addCameraSceneNodeFPS()` or `addCameraSceneNodeMaya()` — these attach built-in animators that override `CameraController` state each frame). After creation:

1. `addCameraSceneNode()` does **not** attach any animators by default — the post-creation animator check below is a defensive precaution only, not expected to find any animators when the node is created via `addCameraSceneNode()`. Call `camera->getAnimators()` and verify it returns an empty list; if not, remove all animators. Note: `removeAnimators()` (plural) **does exist** in the Irrlicht API (`ISceneNode::removeAnimators()`); however, use the portable grab/drop-guarded loop form shown below for safety regardless of version — the `removeAnimator` method calls `drop()` internally, which can destroy the animator before the loop advances the iterator if no grab was performed:

   **Severity policy for non-empty animator list**:
   - In DEBUG builds (`#ifndef NDEBUG`): if `camera->getAnimators()` is non-empty before the removal loop, log a WARNING to the application log: `'CameraController: unexpected animators found on addCameraSceneNode() result — removing N animator(s)'` (substitute the actual count for `N`). This is informational only — it alerts developers that a wrong camera creation variant (`addCameraSceneNodeFPS()` or `addCameraSceneNodeMaya()`) may have been used accidentally.
   - In all builds (DEBUG and RELEASE): execute the grab/drop-guarded removal loop unconditionally, regardless of whether the list was empty at the pre-loop check.
   - Do NOT abort or assert on a non-empty list — the removal loop is sufficient to restore correct state. The WARNING exists purely to surface the root cause (wrong camera variant) during development; it does not indicate unrecoverable state.

   ```cpp
   while (camera->getAnimators().size() > 0) {
       ISceneNodeAnimator* anim = *camera->getAnimators().begin();
       anim->grab();               // prevent premature destruction by removeAnimator
       camera->removeAnimator(anim);
       anim->drop();               // balance the grab — destroys if refcount reaches 0
   }
   ```

   Do NOT use the bare form `camera->removeAnimator(*camera->getAnimators().begin())` — `removeAnimator` may call `drop()` internally, leaving a dangling pointer used by the range expression before the loop body completes. The grab/drop form is required for all Irrlicht versions.
   **Why grab/drop is required**: `removeAnimator()` calls `drop()` on the animator internally. If the animator's ref_count is already 1 (only the scene node holds it), this drops it to 0 and immediately deallocates it. Without `grab()` before `removeAnimator()`, any access to `anim` after the call (including `anim->drop()`) is a use-after-free. With `grab()` first, our own ref keeps the animator alive through the `removeAnimator()` drop, and our `drop()` completes the release cleanly.
2. `CameraController::update(float dt)` must call `camera->setPosition()` and `camera->setTarget()` every frame, before `sceneManager->drawAll()`, to ensure the scene is rendered with the current frame's camera state. Specifically, update order per frame: process input events → `CameraController::update()` → `sceneManager->drawAll()` → `UIManager::draw()` → `endScene()`.
3. `CameraController::OnInputEvent(const InputEvent&)` must return `true` (consumed) for all relevant `InputEvent` types (mouse move, mouse button, scroll wheel). The camera controller does NOT implement `IEventReceiver` directly; it receives translated `InputEvent` from the platform/input arbitration layer. This prevents any accidentally-initialized Irrlicht camera input receiver from processing the same events at the raw `SEvent` level.

---

## Tile Overlay Quad Mesh Geometry (Phase 9b)

This section specifies the vertex layout and winding order for the two runtime overlay
meshes used in `IrrlichtRenderer`: the **tile hover highlight** (`setTileHoverHighlight`) and
the **zone colour overlay** (`setZoneOverlay`). Both meshes consist of flat quads lying on
the terrain surface. Neither mesh is authored as an artist asset — they are constructed
procedurally at runtime by `IrrlichtRenderer` from tile grid coordinates.

Irrlicht uses a **left-handed coordinate system** (+X right, +Y up, +Z forward). Front faces
are **clockwise (CW)** from the viewer perspective. The city camera always looks downward
(pitch −20° to −70°), so front-face normals must point **up (+Y)** to be visible from above.

### Quad Vertex Order (binding)

For a tile at grid index `(tileX, tileZ)` with cell size `cellSize` and height sample
`h = getHeightAt(tileX, tileZ)`:

```text
World-space corners (left-handed, Y-up, Z-forward):

  v0 = (tileX       * cellSize,  h + yOffset,  tileZ       * cellSize)   — near-left
  v1 = ((tileX + 1) * cellSize,  h + yOffset,  tileZ       * cellSize)   — near-right
  v2 = ((tileX + 1) * cellSize,  h + yOffset,  (tileZ + 1) * cellSize)   — far-right
  v3 = (tileX       * cellSize,  h + yOffset,  (tileZ + 1) * cellSize)   — far-left

Indices (two CW triangles, front face = +Y normal):

  Triangle 1: v0, v2, v1
  Triangle 2: v0, v3, v2

Index buffer: { 0, 2, 1, 0, 3, 2 }
```

**Winding proof** (Irrlicht left-handed, CW front):
`(v2 − v0) × (v1 − v0)` =
`(cellSize, 0, cellSize) × (cellSize, 0, 0)` =
`(0*0 − cellSize*0, cellSize*cellSize − cellSize*0, 0*0 − 0*cellSize)` =
`(0, +cellSize², 0)` → +Y normal (front face visible from above). Correct.

**Wrong winding** (`v0, v1, v2` — DO NOT USE):
`(v1 − v0) × (v2 − v0)` = `(0, −cellSize², 0)` → −Y normal (back face, culled). Incorrect.

### Y-Offset Layering

Three overlay layers render on top of terrain geometry. Each uses a distinct Y-offset to
prevent depth-buffer Z-fighting against terrain and against each other:

| Layer | Y-offset | Material type | Notes |
|---|---|---|---|
| Zone colour overlay | `+0.1f` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | Persistent scene node; rebuilt on zone change |
| Tile hover highlight | `+0.05f` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | Raw `drawMeshBuffer` call in `drawScene()`; never a scene node |
| Placement preview | `+0.05f` | `EMT_TRANSPARENT_ALPHA_CHANNEL` | Raw `drawMeshBuffer` calls in `drawScene()`; never a scene node; drawn after hover highlight |

`EMT_TRANSPARENT_ALPHA_CHANNEL` disables depth writes but reads the depth buffer with
`GL_LEQUAL`. Without the Y-offset, fragments at exactly terrain height may fail the depth
test due to floating-point precision, causing flickering at any camera angle. The +0.05f
and +0.1f lifts guarantee these quads always pass the depth test against opaque terrain.
The 0.05 m separation between hover and overlay layers ensures the hover highlight renders
on top of the zone colour overlay when both are present on the same tile.

### UV Layout

Overlay and hover quads are **untextured** — they use only vertex colour (the `Color`
field of each `S3DVertex`). UV values must be set to `{0.f, 0.f}` on all four vertices.
The material must have `setTexture(0, nullptr)` called on any `SMeshBuffer` used for
these meshes, or the material must not bind a texture at all (leave slot 0 unset). Do NOT
leave a stale texture binding from a previous mesh operation — Irrlicht will sample it and
produce a tinted result.

### Zone Overlay SMeshBuffer Batching

The zone overlay mesh may contain up to **100,000 tile quads** (4 vertices, 6 indices each).
An `SMeshBuffer` uses `u16` indices (`E_INDEX_TYPE::EIT_16BIT`), capping each buffer at
**65,535 indices = 10,922 quads per buffer**.

Therefore the overlay mesh MUST use **multiple `SMeshBuffer` instances** within the parent
`SMesh*`:

- Allocate buffers as needed: create a new `SMeshBuffer` after filling 10,922 quads
  (65,532 indices) in the current buffer. Do NOT wait until the index value overflows — cap
  at exactly 10,922 quads per buffer to avoid the final two-triangle pair straddling the
  buffer boundary.
- Each `SMeshBuffer` must have its `Material.MaterialType` set to
  `EMT_TRANSPARENT_ALPHA_CHANNEL` **before** vertex or index data is written — the material
  type is per-buffer, not per-mesh.
- For the maximum 100K quad case: `ceil(100000 / 10922) = 10` `SMeshBuffer` instances in
  the `SMesh*`. The `SMesh::addMeshBuffer()` call sequence is the same for each: allocate,
  populate, `recalculateBoundingBox()`, `addMeshBuffer()`, `drop()` (caller releases
  reference; `SMesh` holds the sole reference thereafter).
- The zone type split (Residential / Commercial / Industrial) does NOT require separate
  `SMeshBuffer` instances per zone type. All zone quads — regardless of zone type — are
  packed into the same set of sequentially filled buffers. The zone ARGB colour is encoded
  per vertex; no material distinction between zone types is required at the buffer level.

**Single-buffer fast path**: If `sparseOverlay.size() <= 10922`, the entire overlay fits
in a single `SMeshBuffer`. Implementations must handle both the single-buffer and
multi-buffer cases using the same code path (a loop that fills and caps buffers
sequentially) — not a special-cased single-buffer branch.

### Hover Highlight — Single Buffer, Pre-Allocated

The hover highlight uses exactly **one `SMeshBuffer`** (4 vertices, 6 indices) allocated
once in the `IrrlichtRenderer` constructor. This buffer is NEVER replaced or re-allocated
during gameplay — only the vertex position and colour values are updated in-place on each
`setTileHoverHighlight()` call (see `irrlicht-device-lifecycle.md` for the full lifecycle
contract). The pre-allocated buffer must be populated with 4 placeholder vertices and 6
indices at construction time (positions `{0,0,0}`, Colors `SColor(0,0,0,0)`, UVs `{0,0}`,
indices `{0, 2, 1, 0, 3, 2}`) to satisfy the in-place write contract described above.

The companion `SMesh* m_hoveredTileMesh` is also allocated once at construction and holds
the sole reference to the buffer after the constructor's `addMeshBuffer()` +
`drop()` sequence. `m_hoveredTileMesh` is NOT added to the Irrlicht scene graph; it is
drawn directly via `IVideoDriver::drawMeshBuffer(m_hoveredTileMesh->getMeshBuffer(0))`
in `IrrlichtRenderer::drawScene()`, guarded by `m_hoverVisible`.

This is the **opposite lifecycle** from the zone overlay `SMesh*`, which IS attached to
the scene graph as a persistent `ISceneNode*` and rebuilt (remove-old / add-new) on every
`setZoneOverlay()` call. The distinction is intentional: hover changes every mouse-move
event (hot path, in-place update required); zone overlay changes only on placement events
(cold path, full rebuild acceptable).

### Placement Preview — Dynamic Multi-Tile SMesh (Phase 10 / extended Phase 11d)

`IRenderer::setTilePlacementPreview` renders a multi-tile highlight covering the Zone
rectangular drag-select or Road straight-line preview while LMB is held. Unlike the
single-tile hover highlight (pre-allocated, in-place update), the preview mesh is
**fully rebuilt on every call** because its tile count varies from 1 to potentially
hundreds per drag step.

**Signature (Phase 11d two-list API)**:

```cpp
void setTilePlacementPreview(
    const std::vector<std::pair<int,int>>& freeTiles,
    uint32_t freeArgb,
    const std::vector<std::pair<int,int>>& blockedTiles = {});
```

- `freeTiles` — tiles where placement would succeed (unoccupied). Rendered with
  `freeArgb` (caller-supplied colour, typically `kHoverArgb` — the green/teal
  preview colour).
- `freeArgb` — ARGB colour applied to every vertex of every free-tile quad.
- `blockedTiles` — tiles already occupied by an existing zone or road (placement
  would be skipped). Defaults to empty for backward compatibility with callers that
  do not distinguish free from blocked. Rendered with the fixed constant
  `kHoverArgbBlocked` (0xBBFF2222 — semi-transparent red). The caller does NOT
  supply a colour for blocked tiles; the renderer uses `kHoverArgbBlocked`
  unconditionally.

Both tile lists are packed into the same rebuilt `SMesh*` in a single call — one mesh
covers all tiles (free and blocked combined). The SMesh rebuild behaviour is unchanged
from Phase 10: one full rebuild per call, old mesh dropped before allocation.

**Clear signal**: pass empty `freeTiles` (and empty or omitted `blockedTiles`) to hide
the preview — sets `m_previewVisible = false`, skips mesh allocation.

**Lifecycle**:

- `m_previewMesh` (`SMesh*`) is `nullptr` at construction; allocated on the first
  non-empty call to `setTilePlacementPreview()`.
- Each call drops the old mesh (`m_previewMesh->drop()`) before allocating a new one.
  This is a cold path (at most one rebuild per `MouseMove` event) so the allocation
  cost is acceptable.
- `m_previewMesh` is dropped in the `IrrlichtRenderer` destructor.
- The mesh is NOT added to the Irrlicht scene graph. It is drawn in `drawScene()` after
  the single-tile hover highlight via a loop over `m_previewMesh->getMeshBufferCount()`
  with raw `IVideoDriver::drawMeshBuffer()` calls.

**Buffer batching**: identical u16-index limit as zone overlay — max 10,922 quads per
`SMeshBuffer`. Out-of-bounds tiles are skipped silently (clamped by `m_mapTilesX/Z`).
Free-tile quads and blocked-tile quads share the same set of sequentially filled
buffers — no separate buffer allocation per colour group is required.

**When the single-tile hover is suppressed**: while a Zone or Road drag is active
(`m_lmbHeld && m_zoneAnchorX != -1`) and the preview is shown, `UIManager` calls
`setTileHoverHighlight(-1, -1)` to hide the single-tile cursor and
`setTilePlacementPreview(freeTiles, kHoverArgb, blockedTiles)` to show the multi-tile
preview. When LMB is released or the tool is deselected,
`setTilePlacementPreview({}, 0)` clears the preview and the normal single-tile hover
resumes on the next `MouseMove`.

---

## Building and Road Mesh Material Rules (Phase 10)

### BackfaceCulling — MANDATORY `false` for B3D building meshes

B3D assets exported from Blender with the **"-Z Forward, Y Up"** convention produce face
winding that Irrlicht's left-handed OpenGL renderer treats as back-facing. Irrlicht enables
`BackfaceCulling = true` by default, which silently culls the front faces of all B3D
building meshes — the result is a white or invisible building (only untextured back faces
visible from some angles).

**Rule**: `IrrlichtRenderer::placeBuildingMesh()` and `placeServiceBuildingMesh()` MUST
set `mat.BackfaceCulling = false` on every material slot of the placed scene node:

```cpp
for (u32 m = 0; m < node->getMaterialCount(); ++m) {
    irr::video::SMaterial& mat = node->getMaterial(m);
    mat.Lighting     = false;   // no scene lights yet in V1
    mat.BackfaceCulling = false; // B3D Blender export winding is CW in Irrlicht space
}
```

This rule applies to **all three placement helpers** (`placeBuildingMesh`,
`placeRoadMesh`, `placeServiceBuildingMesh`). When production B3D assets are delivered
with correct CW winding for Irrlicht (validated by the 3D model artist), `BackfaceCulling`
may be re-enabled per asset — but the default for all V1 placeholder B3D assets is `false`.

**Do not** attempt to fix building visibility by adjusting the Blender export axis
convention instead — the export convention is locked to "-Z Forward, Y Up" (see
`architecture/asset-standards/3d-model-standards.md — Coordinate System Export
Convention`). The `BackfaceCulling = false` flag is the correct engine-side fix.

### Texture Propagation — MANDATORY atlas bind in `BuildingAssetLoader::load()`

B3D placeholder files are minimal geometry cubes with **no `TEXS` or `BRUS` chunks**
and **`VRTS tc_sets=0`** (no UV coordinates). The Irrlicht B3D loader therefore
assigns the default `EMT_SOLID` material (no texture, white vertex diffuse) to every
mesh buffer. After `addMeshSceneNode()` the scene node's material slots have
`Texture[0] == nullptr`.

`BackfaceCulling = false` (applied by `placeBuildingMesh` / `placeServiceBuildingMesh`
after `BuildingAssetLoader::load()` returns) makes faces **visible**, but faces with no
texture still render **solid white** — identical to the pre-fix symptom from the
perspective of the artist.

**Rule**: `BuildingAssetLoader::load()` MUST bind `buildings_atlas_d.dds` to texture
slot 0 on every material slot of the scene node immediately after
`addMeshSceneNode()`:

```cpp
ITexture* atlas = m_driver->getTexture(atlasPath.c_str());
if (atlas) {
    for (u32 m = 0; m < node->getMaterialCount(); ++m)
        node->getMaterial(m).setTexture(0, atlas);
}
```

The atlas path is derived from the asset `basePath` by replacing the
`/3d/buildings/` suffix with `/textures/buildings/buildings_atlas_d.dds`.

**Upload path note**: `IVideoDriver::getTexture()` is used here (linear pool). The
full sRGB raw-GL upload path (`glGenTextures` + `glCompressedTexImage2D` with
`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`) applies to the building atlas for production
quality and is a Phase 11+ refinement. Placeholder rendering is acceptable with the
linear-pool path.

**LOD swap interaction**: `LODNode::swapMesh()` calls `IMeshSceneNode::setMesh()`,
which replaces the mesh but **preserves all material slots** on the node (Irrlicht's
`CMeshSceneNode::setMesh()` does not reset materials). The atlas texture bound at
placement time therefore survives LOD transitions without re-binding.

---

## Building Mesh Placement Contracts (Phase 11)

### `placeBuildingMesh()` Contract

`placeBuildingMesh(tileX, tileZ, assetBaseName)` places a zone building (residential, commercial, or industrial) at a given origin tile, with footprint and world origin determined by the building's `DensityTier`.

**Footprint collision responsibility**: `placeBuildingMesh()` assumes the footprint is already validated as clear before being called. Footprint occupancy checking is the exclusive responsibility of `Zoning::placeZone()` (called from `CitySimulation::placeZone()`) — the renderer's sole responsibility is computing the world origin and placing the scene node. No occupancy validation logic belongs in the rendering layer. Cross-reference: `architecture/game-design/zoning-system.md` § Multi-Tile Footprint Placement Rules.

**DensityTier derivation from assetBaseName**:
The `DensityTier` is derived from the second delimiter-separated segment of `assetBaseName`. For example:

- `"res_low_01"` → split by `_` → `["res", "low", "01"]` → tier segment = `"low"` → `DensityTier::LOW`
- `"com_med_03"` → split by `_` → `["com", "med", "03"]` → tier segment = `"med"` → `DensityTier::MED`
- `"ind_high_02"` → split by `_` → `["ind", "high", "02"]` → tier segment = `"high"` → `DensityTier::HIGH`

No new parameter is added to the signature; tier extraction is performed internally by the method.

**Native-size convention**:
No `setScale()` is applied to the placed node. Building models are authored at real-world scale (1 Blender unit = 1 m) and placed at scale 1.0. This departs from the V1 baseline, which used `setScale(kTileSize / 4.0f) = setScale(2.5f)` on ±2 m models; Phase 9+ assets are authored natively sized.

**Per-tier local half-extent and footprint**:
Each density tier has a defined local-space half-extent (±X, ±Y, ±Z from model origin) and corresponding world footprint size:

| Density tier | Local half-extent | World footprint |
|---|---|---|
| `LOW` | ±5 m | 10 m × 10 m |
| `MED` | ±10 m | 20 m × 20 m |
| `HIGH` | ±15 m | 30 m × 30 m |

These values are binding and derived from `architecture/asset-standards/3d-model-standards.md` § Multi-Tile Footprint.

**World origin formula for multi-tile footprints**:
The origin tile is the bottom-left corner `(tileX, tileZ)`. All tiles in the footprint are marked occupied in the simulation layer. The placed scene node's world origin is positioned at the **center** of the full footprint:

```text
footprintN = 1 (LOW), 2 (MED), 3 (HIGH)

worldX = (tileX + footprintN * 0.5f) * kTileSize
worldZ = (tileZ + footprintN * 0.5f) * kTileSize
```

For a LOW-tier (1×1) building at `(tileX, tileZ)`:

```text
worldX = (tileX + 0.5f) * kTileSize   // tile centre
worldZ = (tileZ + 0.5f) * kTileSize
```

For a MED-tier (2×2) building at `(tileX, tileZ)`:

```text
worldX = (tileX + 1.0f) * kTileSize   // centre of 2×2 block
worldZ = (tileZ + 1.0f) * kTileSize
```

For a HIGH-tier (3×3) building at `(tileX, tileZ)`:

```text
worldX = (tileX + 1.5f) * kTileSize   // centre of 3×3 block
worldZ = (tileZ + 1.5f) * kTileSize
```

### `placeServiceBuildingMesh()` Contract

`placeServiceBuildingMesh(tileX, tileZ, assetBaseName)` places a service building (power plant, water tower, etc.) at the given origin tile.

**Footprint and native-size convention**:
Service buildings occupy a **2×2 tile footprint** (20 m × 20 m world extent) at scale 1.0 (natively sized, no runtime `setScale()` applied). Service building models are authored at real-world scale (1 Blender unit = 1 m) with a local-space half-extent of **±10 m** (matching the MED-tier zone building convention).

**World origin formula**:
The origin tile is `(tileX, tileZ)`. The scene node's world origin is positioned at the **center** of the 2×2 footprint, using the same formula as MED-tier zone buildings (N=2):

```text
worldX = (tileX + 1.0f) * kTileSize   // centre of 2×2 block
worldZ = (tileZ + 1.0f) * kTileSize
```

All four tiles of the 2×2 footprint `[(tileX, tileZ), (tileX+1, tileZ), (tileX, tileZ+1), (tileX+1, tileZ+1)]` are marked occupied in the simulation layer. At least one road tile must be edge-adjacent (4-directional cardinal, distance = 1) to any tile in the footprint for the building to be placeable.

---

## Vehicle Node Registry (Phase 10)

### Registry structure

`IrrlichtRenderer` maintains `m_vehicleNodes` — an
`std::unordered_map<uint32_t, LODNode*>` keyed by `vehicleId`. The key is the
stable integer ID assigned by the traffic simulation for the vehicle's lifetime.
`IrrlichtRenderer` is the sole owner of every `LODNode*` in this map.

### Vehicle placement and movement lifecycle

| Method | Behaviour |
|---|---|
| `placeVehicle(vehicleId, assetName, x, y, z, yaw)` | Calls `destroyVehicleNode(vehicleId)` first (no-op when new). Calls `ensureVehicleLoader()`, then `m_vehicleAssetLoader->load(basePath)`. Sets `node->setPosition` and `node->setRotation(0, yaw, 0)`. Applies material rules (see below). Stores `LODNode*` in `m_vehicleNodes[vehicleId]`. |
| `moveVehicle(vehicleId, x, y, z, yaw)` | Looks up `m_vehicleNodes[vehicleId]`. If missing, logs a warning and returns (caller must use `placeVehicle` for first-time placement). Otherwise updates `node->setPosition` and `node->setRotation(0, yaw, 0)` in-place. |
| `removeVehicle(vehicleId)` | Calls `destroyVehicleNode(vehicleId)` (no-op when not found). |

### Tile Node Eviction Sequence

`destroyTileNode(tileX, tileZ)` is the canonical eviction helper for building
(and road-tile) scene nodes stored in `m_buildingNodes`. Its contract is:

0. **Grab the shared mesh** — obtain the `IMesh*` (or `IAnimatedMesh*`) via
   `lodNode->getMeshNode()->getMesh()` and call `mesh->grab()`. This increments
   the Irrlicht reference count, preventing the mesh cache from freeing the shared
   `CSkinnedMesh` (or equivalent) while the scene node is being torn down in the
   steps below. `LODNode::getMeshNode()` returns the underlying `IMeshSceneNode*`
   and was added as an accessor in phase-11q7 (Issue 1).
1. Look up the `LODNode*` in `m_buildingNodes` by `(tileX, tileZ)`. If not found,
   return immediately (no-op — caller may safely call on an absent tile).
2. Iterate all material slots on the underlying scene node — for every texture unit
   `t` in `[0, MATERIAL_MAX_TEXTURES)`, call
   `lodNode->getNode()->getMaterial(t).setTexture(t, nullptr)`. This clears the
   driver's reference to each `ITexture*` so `TextureCache::evictUnreferenced()`
   can reclaim textures that are no longer referenced by any live node.
3. `m_driver->setMaterial(SMaterial{})` — flush the driver's last-bound material
   state. Irrlicht caches the last-bound material; omitting this flush leaves stale
   texture pointers in the driver until the next draw call overwrites them, which
   can delay eviction of unreferenced textures.
4. `lodNode->getNode()->remove()` — detach the scene node from the scene graph and
   release the scene manager's ownership. Do NOT access the node pointer after this
   line; the scene manager may destroy it immediately.
5. `delete lodNode` — free the `LODNode*` C++ wrapper. The wrapper is non-owning
   after `node->remove()` (the Irrlicht scene manager released the node).
5.5. **Drop the mesh grab** — call `mesh->drop()` to release the temporary reference
   acquired in step 0. The mesh cache may now free the mesh if no other owners hold
   a reference; this is safe because the scene node was already removed in step 4.
6. Erase the entry from `m_buildingNodes`.

**TextureCache integration**: after one or more `destroyTileNode()` calls, the
caller must invoke `m_textureCache->evictUnreferenced()` to reclaim GPU memory
from any textures that are now unreferenced. Calling `evictUnreferenced()` inside
`destroyTileNode()` itself is not permitted — batching the eviction after the full
demolish pass prevents redundant scans on bulk operations (e.g., `clearCity()`).

**`clearCity()` exception**: `clearCity()` must NOT call `destroyTileNode()` in a
loop — that method erases from `m_buildingNodes` during iteration, causing iterator
invalidation. Use the bulk-clear pattern instead (see §City Reset — clearCity()).

### Vehicle eviction sequence

`destroyVehicleNode(vehicleId)` runs the full eviction sequence following the same
pattern as `destroyTileNode` (see §Tile Node Eviction Sequence above for the
canonical steps). The vehicle-specific contract is:

1. Iterate all material slots — call `mat.setTexture(t, nullptr)` for every texture
   unit `t` in `[0, MATERIAL_MAX_TEXTURES)`.
2. `m_driver->setMaterial(SMaterial{})` — flush driver last-bound state.
3. `node->remove()` — release the scene node from the scene graph. Do NOT access
   the node pointer after this line.
4. `delete lodNode` — delete the `LODNode*` C++ wrapper (non-owning after
   `node->remove()`).
5. Erase the entry from `m_vehicleNodes`.

The destructor iterates `m_vehicleNodes` and calls `delete kv.second` for every
remaining entry. It does NOT call `node->remove()` in the destructor because the
Irrlicht scene graph is torn down by `device->drop()` in `main.cpp` after
`IrrlichtRenderer` is destroyed — the scene nodes are freed automatically when the
scene manager is destroyed.

### Vehicle material rules

Vehicle material slots are configured in `placeVehicle()` immediately after
`BuildingAssetLoader::load()` returns:

| Property | Value | Rationale |
|---|---|---|
| `Lighting` | `false` | No light nodes in scene (Phase 6+). |
| `BackfaceCulling` | `false` | Procedural B3D vehicle assets (generated by `tools/generate_b3d_models.py`) may have mixed or inverted winding after the axis-reorientation pass (`(x,z)→(−z,x)` applied to all vertices). `BackfaceCulling = false` guarantees all faces are visible from any camera angle, consistent with the building mesh approach. May be changed to `true` per asset when production Blender-exported vehicles with validated CW winding are delivered. |
| `Texture[0]` (fallback) | `vehicles_diffuse_atlas_d.dds` | Applied only when `BuildingAssetLoader::load()` did not bind a texture (atlas file missing). Loaded via `IVideoDriver::getTexture()` (linear pool). |

### Vehicles are NOT tile-scaled

Building nodes are placed at scale 1.0 (natively sized — 1 Blender unit = 1 m, exported at
real-world dimensions) — see `placeBuildingMesh()` Contract section above for the world
origin formula that centres buildings on multi-tile footprints. `setScale()` is **never**
called on building nodes.
Vehicle nodes are likewise **NOT** scaled — vehicles are authored at world scale (real metres).
`placeVehicle()` calls only `setPosition` and `setRotation`; it never calls
`setScale`.

### Separate vehicle asset loader

A dedicated `m_vehicleAssetLoader` (`std::unique_ptr<BuildingAssetLoader>`) is used
for vehicles. This keeps the vehicle atlas path (`vehicles_diffuse_atlas_d.dds`)
separate from the building atlas path (`buildings_atlas_d.dds`) without adding
per-call branching to the building placement path.

`BuildingAssetLoader::load()` derives the atlas path from the `basePath` string:
if `basePath` contains `/3d/buildings/` it selects the buildings atlas; if it
contains `/3d/vehicles/` it selects the vehicles atlas. Both loaders share the same
`parseMeta()` / B3D loading code path.

### `ensureVehicleLoader` — lazy init

`ensureVehicleLoader()` creates `m_vehicleAssetLoader` on the first `placeVehicle`
call. It returns `false` (and logs a warning) when `m_smgr` or `m_driver` is null
(headless / unit-test context). `placeVehicle()` returns early without crashing when
`ensureVehicleLoader()` returns `false`.

---

## Agent Vehicle Node Registry (Phase 11d)

`IrrlichtRenderer` maintains a second vehicle registry for traffic-simulation agents:

```cpp
std::unordered_map<AgentHandle, ISceneNode*> m_agentNodes;
```

**Ownership**: `IrrlichtRenderer` owns all nodes in `m_agentNodes`. Nodes are created by
`spawnVehicleAgent()`, updated by `moveVehicleAgent()`, and destroyed by `despawnVehicleAgent()`.
`main.cpp` calls these methods but does NOT hold a reference to `m_agentNodes`.

**Coexistence with Phase 10 `m_vehicleNodes`**: The two registries are completely independent.
`m_vehicleNodes` holds manually placed vehicles (via `placeVehicle`/`moveVehicle`/`removeVehicle`);
`m_agentNodes` holds simulation-driven traffic agents. A tile may have at most one agent node but
any number of manually placed vehicle nodes.

**Lifecycle rules**:

1. `spawnVehicleAgent(AgentHandle, tileX, tileZ, zone)` → creates `IMeshSceneNode*`
   (CMeshSceneNode) via `addMeshSceneNode(static_cast<IMesh*>(vehicleMesh))` — NOT
   `addAnimatedMeshSceneNode` (same constraint as buildings, per §Why CMeshSceneNode is
   required); loads LOD0 mesh for `zone`, inserts into `m_agentNodes[handle]`.
   After `addMeshSceneNode`, iterates all material slots and sets:
   - `mat.Lighting = false` (no scene lights)
   - `mat.BackfaceCulling = false` (same rationale as `placeVehicle` — see Vehicle material
     rules above; procedural B3D winding may be mixed after axis reorientation)
   - `mat.setTexture(0, vehicleTex)` for slot 0 (vehicles atlas PNG)
2. `moveVehicleAgent(handle, worldX, worldZ, headingDeg)` → looks up node and updates:
   - **Position**: world X/Z from arguments; Y set to bilinear terrain height via
     `getHeightAtWorld(worldX, worldZ)` plus `kRoadSurfaceYBias` (so the vehicle sits on the
     road surface rather than clipping through terrain).
   - **Rotation**: full three-axis orientation — yaw (Y-axis) from `headingDeg`, plus pitch
     (X-axis) and roll (Z-axis) derived from the terrain slope normal. The slope normal is
     computed via finite-difference sampling of `getHeightAtWorld` at half-tile offset
     positions around the vehicle's world location. The pitch/roll decomposition must be
     performed in the vehicle's **local frame**, not world space. Before computing pitch
     and roll, the world-space slope gradients (`dhX`, `dhZ`) must be rotated into the
     vehicle's local coordinate frame using the current yaw angle:
     - `localForward = dhX * sin(yawRad) + dhZ * cos(yawRad)` (slope along vehicle heading)
     - `localRight = dhX * cos(yawRad) - dhZ * sin(yawRad)` (slope across vehicle width)
     - Then: `pitchRad = atan2(localForward, 1)` (nose up/down) and
       `rollRad = atan2(-localRight, 1)` (lean left/right)

     This ensures a vehicle facing east on a north-south slope correctly rolls rather
     than pitches.
   Does nothing if handle is absent (agent was culled).
3. `despawnVehicleAgent(handle)` → runs the following eviction sequence:
   1. **Null-before-remove**: set `m_agentNodes[handle] = nullptr` then erase the entry
      from `m_agentNodes` **before** any side-effects of `node->remove()`. This mirrors
      the `SceneEntityManager::destroy()` invariant (line 5 of this document) and prevents
      a transiently dangling pointer in the map if `node->remove()` triggers re-entrant
      callbacks or assertions that inspect the registry.
   2. **Shared-mesh lifetime hold**: call `node->getMesh()->grab()` on the `IMesh*`
      returned by the scene node **before** `node->remove()`. Multiple agent nodes with
      the same zone and variant index share one `IAnimatedMesh*` from the scene-manager
      mesh cache. Without this hold, `node->remove()` may drop the mesh ref-count to zero
      and free the mesh while other live nodes still reference it — the next `drawAll()`
      then passes freed vertex data to the GL driver (SIGSEGV). The `grab()` keeps the
      mesh alive through the `remove()` call; the matching `drop()` in step 6 releases
      it. This is a **documented exception** to the "Do NOT call `grab()` or `drop()` on
      mesh cache pointers" rule in §Phase 9 LOD swap contract — see the exception note in
      that section.
   3. Iterate all material slots — cache `SMaterial& mat = node->getMaterial(i)` in the
      outer loop and call `mat.setTexture(t, nullptr)` for every texture unit `t` in
      `[0, MATERIAL_MAX_TEXTURES)` (C-4 constraint: `getMaterial(i)` called exactly once
      per outer iteration).
   4. `m_driver->setMaterial(SMaterial{})` — flush driver last-bound state.
   5. `node->remove()` — release the scene node. Do NOT access the node pointer after
      this line. (Agent nodes use raw `IMeshSceneNode*`, not `LODNode*` — no wrapper to
      delete.)
   6. `sharedMesh->drop()` — release the temporary hold acquired in step 2. The mesh
      remains alive if other agent nodes or the scene-manager cache still hold references.

   **TextureCache note**: vehicle atlas textures (`vehicles_diffuse_atlas_d.dds` etc.) are
   loaded **once at renderer init**, not per-agent-spawn. `spawnVehicleAgent` does NOT call
   `TextureCache::loadSRGB`; therefore `despawnVehicleAgent` does NOT call
   `TextureCache::releaseSRGB`. Steps 3–4 above release the node's texture reference without
   disturbing the shared atlas.

**`vehicleMeshPath()` — zone-to-mesh mapping**: `spawnVehicleAgent()` calls a free function
`vehicleMeshPath(ZoneType zone, int variantIdx = 0)` declared in
**`src/rendering/vehicle_mesh_path.h`** to select the LOD0 asset path. Declaring it in a
standalone header (not as a file-scope static) allows direct unit testing without a live
Irrlicht context.

| `ZoneType` | `vehicle_id` | LOD0 asset path |
|---|---|---|
| `Residential` | `car_sedan` / `car_hatchback` / `car_suv` (round-robin via `variantIdx % 3`) | `assets/3d/vehicles/<variant>_lod0.b3d` |
| `Commercial` | `bus_standard` | `assets/3d/vehicles/bus_standard_lod0.b3d` |
| `Industrial` | `truck_cargo` | `assets/3d/vehicles/truck_cargo_lod0.b3d` |

Signature: `inline std::string vehicleMeshPath(ZoneType zone, int variantIdx = 0)` (defined
in the header). `spawnVehicleAgent()` passes `static_cast<int>(handle) % 3` as `variantIdx`
for deterministic per-vehicle Residential variant selection.
**Fallback**: unrecognised `zone` values (and default `variantIdx = 0`) return the `car_sedan`
path, preventing an empty string from reaching `ISceneManager::getMesh()`.

**Cull policy**: agents beyond 150 m from the camera are not spawned (simulation skips calling
`spawnVehicleAgent` for them); agents that move beyond 150 m trigger `despawnVehicleAgent`.

**Zone-change respawn**: when the per-frame agent sync loop detects that an active agent's
`zone` field has changed (i.e. `activeAgents[handle].zone != a.zone`), the renderer node
must be fully replaced — the mesh is zone-specific and cannot be hot-swapped on an existing
node. The required sequence is:

1. `audioSystem.releaseVehicleEnginePair(s.idleIdx, s.moveIdx)` — return old audio pair first.
2. `renderer.despawnVehicleAgent(handle)` — removes the stale mesh node.
3. `renderer.spawnVehicleAgent(handle, tileX, tileZ, newZone)` — creates a new node with the
   correct mesh for the new zone (bus, truck, or car variant).
4. `audioSystem.acquireVehicleEnginePair(newZone)` — obtain a fresh audio pair whose pitch
   multiplier matches the new zone (`Residential` → 1.0, `Commercial`/`Industrial` → 0.85).
5. Store the new audio pair indices and zone in the cached agent state.

If `acquireVehicleEnginePair()` returns `{-1, -1}` (pool exhausted), the vehicle renders
with the correct mesh but produces no engine audio (silent mode). This is acceptable per the
source-pool spec. See `architecture/audio-architecture/source-pool.md` §Vehicle Engine Source
Constraints for the invalid-index guard contract.

**SMesh / drop() rules**: agent nodes use `IAnimatedMesh*` loaded via `ISceneManager::getMesh()`
and cast to `IMesh*` for `addMeshSceneNode` — the scene manager owns the mesh;
`IrrlichtRenderer` must NOT call `->drop()` on it in the normal spawn/move path.
**Exception**: `despawnVehicleAgent` performs a temporary `grab()`/`drop()` hold on the
shared mesh around `node->remove()` to prevent premature deallocation when multiple agent
nodes share the same cached `IAnimatedMesh*`. See `despawnVehicleAgent` step 2 above and
§Phase 9 LOD swap contract exception note for the rationale.

---

## Intersection Signal Billboard Registry (Phase 11d)

`IrrlichtRenderer` maintains a registry of intersection signal billboard nodes:

**`TileKey` type**: `TileKey` is defined in `src/simulation/simulation_types.h` as a plain
struct with a custom hash, following the same pattern used by `m_agentNodes`:

```cpp
struct TileKey { int x{0}; int z{0}; };
struct TileKeyHash {
    std::size_t operator()(TileKey k) const noexcept {
        return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 16);
    }
};
```

```cpp
std::unordered_map<TileKey, ISceneNode*, TileKeyHash> m_intersectionNodes;
```

**Ownership**: `IrrlichtRenderer` owns all nodes in `m_intersectionNodes`. Nodes are created
by `setIntersectionSignalState()` on the first call for a given tile and reused on all
subsequent calls for that same tile. Nodes are destroyed when the associated road tile is
removed (caller invokes `setIntersectionSignalState` with a sentinel or the road tile is
demolished — implementers must run the **Eviction sequence** (steps 1–4 below) when road
tiles are removed).

**Lifecycle rules**:

1. `setIntersectionSignalState(tileX, tileZ, SignalPhase)` — if no node exists for `(tileX, tileZ)`,
   create a `IBillboardSceneNode*` via `smgr->addBillboardSceneNode()`, insert into
   `m_intersectionNodes[{tileX, tileZ}]`, and set initial material and colour.
   On subsequent calls for the same tile, look up the existing node and update its material
   colour only (no allocation). Position: world centre of the tile + Y-offset `+0.15 m` above
   the road surface. Set `PolygonOffsetFactor = -1` and `PolygonOffsetDirection = EPO_FRONT`
   on the node's material to prevent Z-fighting with the road quad.
   Colour per `SignalPhase`: `SignalPhase::Green` → RGB `0x00FF00`; `SignalPhase::Red` → RGB
   `0xFF0000`. Set via `node->setColor(SColor(255, r, g, b))`.
   Material: `EMT_TRANSPARENT_ADD_COLOR` (no texture — solid colour billboard).

2. **Eviction sequence** (road tile removed or renderer shutdown):
   1. For `t` in `[0, MATERIAL_MAX_TEXTURES)`: `node->getMaterial(t).setTexture(0, nullptr)`.
   2. `m_driver->setMaterial(SMaterial{})` — flush driver last-bound state.
   3. `node->remove()` — release the scene node. Do NOT access the node pointer after this line.
   4. Erase the entry from `m_intersectionNodes`.

**TextureCache note**: intersection signal billboards use no textures (solid colour material);
steps 1–2 of the eviction sequence are included for uniformity with the vehicle eviction
pattern but have no effect in practice.

## City Reset — clearCity() (Phase 11m)

`IrrlichtRenderer::clearCity()` resets all city objects so that a second New Game can begin
on a clean slate without reinitializing the renderer or device.

### Contract

`clearCity()` performs the following steps in order:

1. **Evict building nodes**: iterate `m_buildingNodes` via `evictLODNodeRegistry()`
   (see §Helper Functions below), which calls `evictOneLODNode()` for each entry.
   `evictOneLODNode()` applies the full grab/drop-guarded eviction sequence: (0) grab
   the shared mesh via `lodNode->getMeshNode()->getMesh()->grab()` before touching
   materials, (1) clear all material texture slots (`mat.setTexture(t, nullptr)` for
   each texture unit), (2) `m_driver->setMaterial(SMaterial{})` to flush driver state,
   (3) `node->remove()`, (4) `delete kv.second` to free the `LODNode*` C++ wrapper
   (heap-allocated by `placeBuildingMesh`, not reference-counted — must be `delete`d
   explicitly after `node->remove()`), (5.5) `mesh->drop()` to release the temporary
   grab. This matches the Tile Node Eviction Sequence contract (see §Tile Node Eviction
   Sequence) and prevents UAF when multiple tile nodes share a cached `.b3d` mesh. Call
   `m_buildingNodes.clear()` once after the loop to drop all now-dangling map entries.
   **Do NOT call `destroyTileNode()` during the iteration** — that method erases from the
   map internally, causing iterator invalidation on `std::unordered_map`. Use the
   bulk-clear pattern instead.
2. **Evict road nodes**: iterate `m_roadNodes`, applying the same 3-step eviction sequence
   as step 1 — texture clear, driver material flush, `node->remove()` — then call
   `m_roadNodes.clear()` after the loop. Road nodes are raw `ISceneNode*` (no `LODNode`
   wrapper), so there is no `delete` step.
3. **Evict agent nodes**: iterate `m_agentNodes`, perform the full eviction sequence
   (texture clear → `m_driver->setMaterial(SMaterial{})` → `node->remove()`), then
   `m_agentNodes.clear()` after the loop. Traffic vehicle scene nodes that persist from
   game 1 into game 2 would otherwise ghost in the scene.
   **Note**: the `despawnVehicleAgent` shared-mesh `grab()`/`drop()` hold (step 2 in the
   per-agent eviction sequence) is NOT required in the `clearCity()` bulk path because
   every agent node is removed in the same loop — no live agent node can reference a
   freed mesh mid-iteration. The null-before-remove invariant is likewise not required
   because the entire map is cleared in one `m_agentNodes.clear()` call after the loop,
   not by per-entry erase during iteration.
4. **Evict intersection signal nodes**: iterate `m_intersectionNodes` and apply the full
   eviction sequence on each node (see §Intersection Signal Billboard Registry for the
   canonical contract):
   (1) for `t` in `[0, MATERIAL_MAX_TEXTURES)`: `node->getMaterial(t).setTexture(0, nullptr)`,
   (2) `m_driver->setMaterial(SMaterial{})` — flush driver last-bound state,
   (3) `node->remove()`. Call `m_intersectionNodes.clear()` once after the loop.
   Although intersection signal billboards use no textures (solid-colour material), the
   texture-clear and driver-flush steps are required for uniformity with the canonical
   eviction pattern.
5. **Clear shared road SMesh buffers**: if `IrrlichtRenderer` maintains a persistent shared
   `SMesh*` for batched road geometry (accumulated road segments), call its equivalent
   `clear()` / remove-all-mesh-buffers operation and `recalculateBoundingBox()`. Do NOT
   `->drop()` the mesh — it stays alive for reuse in the next game session. If roads use
   only per-tile nodes (all evicted in step 2), this step is a no-op.
6. **Reset road-tile counters**: reset any renderer-side road-tile count or per-tile mesh
   state that accumulates across `addRoadTile()` calls (e.g., a `m_roadTileCount` member
   on `IrrlichtRenderer`).
7. **Do NOT remove terrain chunk nodes** — terrain is regenerated separately via
   `TerrainSystem::generate()` after `clearCity()` returns. Terrain nodes must not be touched
   by this method.

### TextureCache Note

`clearCity()` does NOT call `TextureCache::releaseSRGB()` or `TextureCache::releaseSplatMap()`.
The shared sRGB atlas textures (`buildings_atlas_d.dds`, billboard atlas, road tileable texture)
are loaded once at renderer initialization and retained across game sessions — they are valid
throughout the application lifetime and reused by `placeBuildingMesh()` and `addRoadTile()` in
the next game session. Calling `releaseSRGB()` during `clearCity()` would incorrectly decrement
ref-counts for atlas textures that remain in active use, eventually triggering premature eviction
via `evictUnreferenced()`.

The material-slot clearing in steps 1–3 (`mat.setTexture(t, nullptr)`) operates on the **linear**
ITexture* pool only — sRGB atlas textures are raw `GLuint` values stored in the TextureCache sRGB
pool and are **not** present in node material slots. No `releaseLinear()` call is needed for
building material slots either, since buildings use sRGB atlas textures (not the linear pool).
See `architecture/graphics-architecture/texture-cache.md §Eviction safety` for the full
per-pool release contract.

### Helper Functions

Two private helpers encapsulate the per-node and bulk eviction logic to avoid duplicating
the grab/drop-guarded sequence in `destroyTileNode()` and `clearCity()`:

- **`evictOneLODNode(LODNode* lodNode)`** — removes a single building LOD node from
  the scene graph using the full grab/drop-guarded eviction contract from
  §Tile Node Eviction Sequence: grabs the shared mesh, clears all material texture
  slots, flushes the driver material state, calls `node->remove()`, deletes the
  `LODNode*` wrapper, then drops the mesh. Called by `destroyTileNode()` for
  on-demand per-tile demolition and by the bulk-eviction path in `clearCity()`.

- **`evictLODNodeRegistry()`** — iterates the building LOD node registry
  (`m_buildingNodes`) and calls `evictOneLODNode()` for every entry, then calls
  `m_buildingNodes.clear()` to drop all now-dangling map entries. Used by
  `clearCity()` to bulk-evict all building nodes at once without triggering iterator
  invalidation (direct per-entry erasure inside the loop is forbidden — see step 1
  of the Contract above).

### Call Site

`clearCity()` is called from the `main.cpp` game loop when
`uiManager.consumeNewGameRequest()` returns `true`, as step 2 in the new-game reset
sequence:

```text
1. sim.reset(startingFunds)
2. renderer.clearCity()          ← this method
3. freshRng.reseed(ngp.seed); terrain.generate(...)
4. uiManager.transitionToGameplay(GameMode::Sandbox)
```

### Interface

`clearCity()` is a pure-virtual method on `IRenderer`:

```cpp
// Remove all building, road, traffic-agent, and intersection signal scene nodes from the
// scene graph and reset road mesh buffers. Does NOT remove terrain chunk nodes — terrain
// is regenerated separately.
virtual void clearCity() = 0;
```
