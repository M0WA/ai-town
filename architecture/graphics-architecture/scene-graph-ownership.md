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

  **Ref-count contract**: `node->setMesh()` increments the mesh ref count via `grab()`. This behavior is expected based on Irrlicht's `CMeshSceneNode::setMesh()` implementation pattern: the new mesh receives `grab()` and the old mesh receives `drop()` within the method body. **Implementation teams must verify this behavior in the vendored Irrlicht source** (`source/Irrlicht/CMeshSceneNode.cpp`) — "expected" reflects the standard Irrlicht pattern, but a custom or patched build may omit the `grab()` call, in which case the `drop()` after `setMesh()` would immediately destroy the mesh. Always match the `drop()` convention to the actual `setMesh()` implementation in the vendored source. If the caller retains its own reference without calling `drop()`, the mesh ref count is 2 and the mesh leaks when the scene node is later destroyed (it reaches ref_count 1, not 0). The `drop()` call immediately after `setMesh()` is mandatory for every LOD swap. This is the same rule as the terrain `SMesh` attachment sequence — the bounding box is always stale until explicitly recalculated.
  **Mandatory CI smoke test — setMesh() grab/drop verification**: The prose "expected based on Irrlicht's implementation pattern" is insufficient protection against a double-free if the vendored Irrlicht build omits `grab()` in `setMesh()`. A CI-enforced test labelled `requires-opengl` must verify this contract before LOD swap code is merged. Add to `tests/rendering/lod_swap_smoke_test.cpp`:

  ```cpp
  TEST(LODSwapSmokeTest, SetMeshGrabDropContract) {
      // TODO: Fill in after Phase 2 SMesh::addMeshBuffer() grab/drop spike.
      // The spike inspects CMeshSceneNode.cpp and SMesh.h to determine whether
      // addMeshBuffer() calls grab() on the buffer. If it does, the caller must
      // call ->drop() after addMeshBuffer(); if it does not, the caller owns the
      // buffer and must not call ->drop().
      // Timing measurement requires a real GPU; this test is promoted to
      // requires-opengl label in Phase 5 when the real LOD swap is implemented.
      GTEST_SKIP() << "LOD swap timing requires real GPU; promoted to Phase 5.";
  }
  ```

  Phase 5 fills in the real test body after the spike result is confirmed. The Phase 2 CMake registration of this file (with `GTEST_SKIP()` body) validates CI routing for the requires-opengl label. **Note**: this body must remain `GTEST_SKIP()` — `SUCCEED()` produces a false-green result that implies the timing constraint has been verified.

  If this test fails (crash or ASAN fault), the vendored Irrlicht source must be patched before any LOD swap code is written. Inspect `source/Irrlicht/CMeshSceneNode.cpp` to confirm `setMesh()` calls `grab()`/`drop()` on the new/old mesh.
  **PENDING SPIKE — `SMesh::addMeshBuffer()` grab/drop contract**: The smoke test body above calls `newMesh->addMeshBuffer(new SMeshBuffer())`. Whether `SMesh::addMeshBuffer()` calls `grab()` on the buffer argument **must be verified** by inspecting `source/Irrlicht/SMesh.h` at Phase 2 implementation time. If `addMeshBuffer()` calls `grab()`, the caller must call `->drop()` on the `SMeshBuffer*` immediately after `addMeshBuffer()` to relinquish the caller's ownership reference — otherwise the buffer leaks (ref_count 2, never reaches 0). If `addMeshBuffer()` does NOT call `grab()`, the caller retains sole ownership and must NOT call `->drop()` after `addMeshBuffer()`. The smoke test body in `tests/rendering/lod_swap_smoke_test.cpp` must be updated to reflect the confirmed convention once the source has been read. **Record the result of this spike as a one-line comment in both `tests/rendering/lod_swap_smoke_test.cpp` and this spec file**, e.g.: `// VERIFIED: SMesh::addMeshBuffer() calls grab(); caller must drop() after addMeshBuffer().`
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
