# Scene Graph Ownership Policy

- Game objects (`Building`, `Vehicle`, etc.) store a **raw pointer** to their scene node, which is treated as non-owning
- A `SceneEntityManager` holds the authoritative entity list; it is the sole place `addXxxSceneNode()` and `node->remove()` are called
- **Dangling pointer prevention**: `SceneEntityManager::destroy(entity)` must set the game object's node pointer to `nullptr` before calling `node->remove()`
- No code outside `SceneEntityManager` calls `grab()` on scene nodes

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

**Never use `static_cast<SMesh*>` on an `IMesh*` pointer unless you can guarantee the pointer was originally created as an `SMesh`.** Performing a `static_cast` on an `IMesh*` that actually points to an Irrlicht-internal mesh type (e.g. `CSkinnedMesh`, `CStaticMesh`, or any other concrete type returned by the asset loader) is undefined behavior — the object layout does not match `SMesh` and any subsequent member access corrupts memory silently.

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
