## Phase 11q7: Service-Building UAF Segfault + Wire LODNode::update() + Minimap 180° Rotation Fix

**Status: IN_PROGRESS**

**Prerequisite**: phase-11q6 merged.

### Goal

A new SIGSEGV was captured in a core dump from `build_debug/aitown` at frame 6464
(~107 s runtime). The crash occurs inside Mesa's radeonsi driver during a `memcpy`
of vertex data, called from `COpenGLDriver::drawVertexPrimitiveList` →
`CMeshSceneNode::render()` → `CSceneManager::drawAll()` →
`IrrlichtRenderer::drawScene()` line 340.

Root cause: `destroyTileNode` and `evictOneLODNode` call `node->remove()` without
holding a grab on the building's shared `IAnimatedMesh*`. When
`CMeshSceneNode::~CMeshSceneNode()` fires via `node->remove()` it calls
`Mesh->drop()`, reducing the ref_count from 2 to 1 (cache only). Any subsequent
Irrlicht mesh-cache eviction drops that last reference to 0, freeing the
`CSkinnedMesh` and all its `SSkinMeshBuffer` objects. Other `CMeshSceneNode`
instances at different tiles that share the same `.b3d` asset then hold dangling
`Mesh*` pointers. The next `drawAll()` calls `render()` on those nodes;
`mb->getVertices()` returns a freed (null or corrupt) pointer →
`memcpy(NULL, …)` → SIGSEGV.
This is the same class of bug that was fixed for vehicle agents in phase-11q6
(`despawnVehicleAgent` grab/drop fix). The fix is identical: grab the current
mesh before the material-clear + `node->remove()` sequence, drop it after.

Two additional pre-existing bugs are addressed in this phase:

- **Rendering — `LODNode::update()` never called**: `LODNode::update()` was
  fully implemented in Phase 9 but was never connected to the frame loop.
  Buildings never transition between LOD levels regardless of camera distance.
  Fix: call `lodNode->update(cameraPos)` for each building node inside
  `IrrlichtRenderer::update()`, which already runs once per frame before
  `beginFrame()`.

- **UI — minimap 180° rotation**: the minimap is rotated 180° around its
  centre relative to the game world. Root cause: the current rotation formula
  uses `sinA = sinf(-yawRad)` (= `−sinf(yawRad)`) which reverses the
  counter-rotation direction, combined with `py = centreZ + rotZ * scaleZ`
  which maps the camera's forward direction (+Z) to the *bottom* of the minimap
  instead of the *top*. The combined effect is a full 180° orientation error.
  Fix: change `sinA = sinf(-yawRad)` → `sinA = sinf(yawRad)` (remove the
  negation), and change `py = centreZ + rotZ * scaleZ` →
  `py = centreZ - rotZ * scaleZ` in all four tile-drawing loops. The `px`
  formula is correct and unchanged. Also update the north indicator sin term
  and the click-to-pan offZ.

---

### Issues to Fix

#### 1. `src/rendering/LODNode.h` + `src/rendering/LODNode.cpp` — add `getMeshNode()` accessor

`destroyTileNode` and `evictOneLODNode` hold a `LODNode*` but call `getNode()`
which returns `ISceneNode*` — that interface has no `getMesh()`. Adding a
`getMeshNode()` accessor returning `IMeshSceneNode*` gives callers typed access
to `getMesh()` without a static_cast and without exposing `m_node` directly.

**Add to `LODNode.h`** (immediately after the `getNode()` declaration):

```cpp
// getMeshNode() — returns the raw IMeshSceneNode* stored by this wrapper.
// Used by IrrlichtRenderer to grab/drop the current mesh around node->remove().
// Caller must NOT call remove() or drop() on the returned pointer.
[[nodiscard]] irr::scene::IMeshSceneNode* getMeshNode() const;
```

**Add to `LODNode.cpp`** (immediately after the `getNode()` definition):

```cpp
irr::scene::IMeshSceneNode* LODNode::getMeshNode() const
{
    return m_node;
}
```

Acceptance criteria:

- [x] `getMeshNode()` declaration added to `LODNode.h` after `getNode()`.
- [x] `getMeshNode()` definition added to `LODNode.cpp` returning `m_node`.
- [x] `make build` succeeds with no new warnings.

---

#### 2. `src/rendering/IrrlichtRenderer.cpp` — `destroyTileNode` missing grab/drop (lines ~1422–1450)

**Current (buggy):**

```cpp
void IrrlichtRenderer::destroyTileNode(
    std::unordered_map<uint64_t, std::unique_ptr<LODNode>>& registry,
    int tileX, int tileZ)
{
    uint64_t key = tileKey(tileX, tileZ);
    auto it = registry.find(key);
    if (it == registry.end() || !it->second) return;

    LODNode* lodNode = it->second.get();
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

    // Erase destroys the unique_ptr, which deletes the LODNode wrapper.
    registry.erase(it);
}
```

**Required fix** — identical grab/drop pattern as `despawnVehicleAgent` (phase-11q6):

```cpp
void IrrlichtRenderer::destroyTileNode(
    std::unordered_map<uint64_t, std::unique_ptr<LODNode>>& registry,
    int tileX, int tileZ)
{
    uint64_t key = tileKey(tileX, tileZ);
    auto it = registry.find(key);
    if (it == registry.end() || !it->second) return;

    LODNode* lodNode = it->second.get();
    scene::ISceneNode* node = lodNode->getNode();

    if (node && m_driver) {
        // Grab the current mesh before clearing materials and removing the node.
        // Prevents the Irrlicht mesh cache from evicting the shared CSkinnedMesh
        // between the node->remove() call (which fires CMeshSceneNode destructor →
        // Mesh->drop()) and any subsequent cache-maintenance path that would drop
        // the last cache reference to zero — freeing the mesh while other
        // CMeshSceneNode instances at different tiles still reference the same
        // B3D asset. Pattern mirrors despawnVehicleAgent (phase-11q6 shared-mesh fix).
        scene::IMesh* sharedMesh = lodNode->getMeshNode()
                                   ? lodNode->getMeshNode()->getMesh() : nullptr;
        if (sharedMesh) sharedMesh->grab();

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
        // Step 3: remove the scene node — mesh ref-count held by grab above.
        node->remove();  // do NOT access node* after this

        // Release the grab; mesh stays alive as long as the cache or any other
        // CMeshSceneNode instance still holds a reference.
        if (sharedMesh) sharedMesh->drop();
    }

    // Erase destroys the unique_ptr, which deletes the LODNode wrapper.
    registry.erase(it);
}
```

Acceptance criteria:

- [x] `lodNode->getMeshNode()->getMesh()` used to obtain `sharedMesh`.
- [x] `sharedMesh->grab()` called before material-clear loop.
- [x] `sharedMesh->drop()` called after `node->remove()`.
- [x] `make build` succeeds with no new warnings.
- [x] All existing building and road placement tests pass unchanged.

---

#### 3. `src/rendering/IrrlichtRenderer.cpp` — `evictOneLODNode` missing grab/drop (lines ~204–220)

Same root cause as Issue 2. `evictOneLODNode` is called by `evictLODNodeRegistry`
during `clearCity()` and must carry the same grab/drop guard.

**Current (buggy):**

```cpp
void IrrlichtRenderer::evictOneLODNode(LODNode* lodNode) {
    if (!lodNode) return;
    irr::scene::ISceneNode* node = lodNode->getNode();
    if (node) {
        for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
            irr::video::SMaterial& mat = node->getMaterial(m);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        if (m_driver) m_driver->setMaterial(irr::video::SMaterial{});
        node->remove();
    }
}
```

**Required fix:**

```cpp
void IrrlichtRenderer::evictOneLODNode(LODNode* lodNode) {
    if (!lodNode) return;
    irr::scene::ISceneNode* node = lodNode->getNode();
    if (node) {
        // Grab the current mesh — same shared-mesh fix as destroyTileNode (Issue 2).
        irr::scene::IMesh* sharedMesh = lodNode->getMeshNode()
                                        ? lodNode->getMeshNode()->getMesh() : nullptr;
        if (sharedMesh) sharedMesh->grab();

        for (irr::u32 m = 0; m < node->getMaterialCount(); ++m) {
            irr::video::SMaterial& mat = node->getMaterial(m);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                mat.setTexture(t, nullptr);
            }
        }
        if (m_driver) m_driver->setMaterial(irr::video::SMaterial{});
        node->remove();  // mesh ref held by grab above

        if (sharedMesh) sharedMesh->drop();
    }
}
```

Acceptance criteria:

- [x] `sharedMesh->grab()` before material-clear loop in `evictOneLODNode`.
- [x] `sharedMesh->drop()` after `node->remove()`.
- [x] `make build` succeeds with no new warnings.

---

#### 4. `src/rendering/IrrlichtRenderer.cpp` — wire `LODNode::update()` into frame loop (lines ~2992–3015)

`LODNode::update(cameraPos)` was implemented in Phase 9 but never connected to the
frame loop. Buildings never swap LOD levels regardless of camera distance.

Add a building-LOD update loop to `IrrlichtRenderer::update(float dt)`. The existing
function body starts with `if (!m_cloudNode) return;` — this early-exit guard must
NOT gate the LOD update. The LOD update must run even when the cloud node is absent.
Restructure the function as follows:

**Current `update()` (lines ~2992–3015):**

```cpp
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
```

**Required fix — add LOD update block after the cloud section:**

```cpp
void IrrlichtRenderer::update(float dt)
{
    // Cloud UV scroll + cloud dome repositioning.
    if (m_cloudNode) {
        constexpr float kCloudScrollX = 0.002f;
        constexpr float kCloudScrollZ = 0.0008f;

        m_cloudUVOffset.X = std::fmod(m_cloudUVOffset.X + kCloudScrollX * dt, 1.0f);
        m_cloudUVOffset.Y = std::fmod(m_cloudUVOffset.Y + kCloudScrollZ * dt, 1.0f);

        m_cloudNode->getMaterial(0)
            .getTextureMatrix(0)
            .setTextureTranslate(m_cloudUVOffset.X, m_cloudUVOffset.Y);

        if (m_camera) {
            const core::vector3df camPos = m_camera->getPosition();
            m_cloudNode->setPosition(camPos);
        }
    }

    // LOD update for all building nodes (zone buildings + service buildings).
    // Called once per frame before beginFrame() so mesh swaps complete before
    // drawAll() starts. m_camera may be null before the first setCamera() call.
    // m_vehicleNodes are LOD0-only (no lod1/lod2) — not iterated here.
    // m_roadNodes use swapMeshRaw() from the road-placement path — not iterated here.
    if (m_camera) {
        const core::vector3df camPos = m_camera->getAbsolutePosition();
        for (auto& kv : m_buildingNodes) {
            if (kv.second) kv.second->update(camPos);
        }
    }
}
```

Acceptance criteria:

- [x] Cloud block wrapped in `if (m_cloudNode)` (no longer an early-return guard).
- [x] Building-LOD update loop iterates `m_buildingNodes` after the cloud block.
- [x] `m_vehicleNodes` and `m_roadNodes` NOT iterated for LOD updates.
- [x] `make build` succeeds with no new warnings.
- [x] Manual verification: place a Power Plant, move camera from ≤30 m to ≥60 m
      from the building; building should transition from LOD0 to LOD1 (reduced mesh
      polygon count), and back to LOD0 when the camera returns within the hysteresis
      threshold (~45 m for standard service building `.meta` lod_distances).

---

#### 5. `src/ui/Minimap.cpp` — fix 180° rotation in `drawOverlay()` (lines ~175–318)

**Root cause analysis:**

The current rotation formula uses `sinA = sinf(-yawRad) = −sinf(yawRad)`. This
reverses the counter-rotation direction, combined with `py = centreZ + rotZ * scaleZ`
which maps +rotZ to downward. In this game, the camera's forward direction is +Z
(north), so `relZ > 0` for tiles in front of the camera. With the current buggy
code, a north tile maps to a larger py (bottom of minimap) rather than a smaller py
(top). The combined effect is a 180° orientation error.

**Correct fix:**

1. **Change `sinA = sinf(-yawRad)` → `sinA = sinf(yawRad)`** (remove the negation
   from the sin call; `cosA` is unchanged since cos is even).
2. **`py = centreZ + rotZ * scaleZ` is correct and must NOT change.**
   Camera forward = −Z at yaw=0, so forward tiles have `relZ < 0` → `rotZ < 0` →
   `py < centreZ` = top of minimap ✓ (no sign change needed).
3. **Change `px = centreX + rotX * scaleX` → `px = centreX - rotX * scaleX`.**
   Irrlicht LH: camera right = −X (`xaxis = normalize(up × forward)` with forward=−Z
   gives xaxis = −X). Negating px maps world +X to screen-left (camera-left). ✓
4. **Change north indicator sin term:**
   `+ 90.f * sinf(yawRad)` → `− 90.f * sinf(yawRad)` (negated to match px X-flip).

**Rotation formula at yaw=0 verification after fix:**

- `cosA=1`, `sinA=0` → `rotX=relX`, `rotZ=relZ`
- Irrlicht LH: camera right = −X, so world +X = camera LEFT.
- East tile (relX=r > 0): px = centreX − r*scaleX < centreX → LEFT ✓ (east = camera-left)
- Forward tile (relZ=−r < 0, camera looks toward −Z): py = centreZ + (−r)*scaleZ < centreZ → TOP ✓

**Rotation formula at yaw=90° verification after fix:**

- `cosA=0`, `sinA=sin(90°)=1`
- Camera forward at yaw=90° = −X. Forward tile (relX=−r, relZ=0):
  - rotX = (−r)*0 − 0*1 = 0 → px = centreX ✓ (top = centre-x)
  - rotZ = (−r)*1 + 0*0 = −r → py = centreZ + (−r)*scaleZ < centreZ → TOP ✓
- Camera right at yaw=90° = +Z (Irrlicht LH). Tile at relZ=r (camera-right):
  - rotX = 0*0 − r*1 = −r → px = centreX − (−r)*scaleX = centreX + r > centreX → RIGHT ✓

**Current (buggy) — in the `sinA` declaration near line 177:**

```cpp
const float cosA    = cosf(-yawRad);   // negative: counter-rotate world beneath camera
const float sinA    = sinf(-yawRad);
```

**Required fix:**

```cpp
const float cosA    = cosf(yawRad);    // cos is even: cosf(-yawRad) == cosf(yawRad)
const float sinA    = sinf(yawRad);    // removed negation: counter-rotates world correctly
```

Update the comment on the cosA line to remove the now-misleading `(-yawRad)` form.

**For all four tile-drawing loops**, change the px formula:

```cpp
const int   px   = static_cast<int>(centreX + rotX * scaleX);   // old (wrong)
```

to:

```cpp
const int   px   = static_cast<int>(centreX - rotX * scaleX);   // Irrlicht LH X-flip
```

The py formula `centreZ + rotZ * scaleZ` is **correct and unchanged** — forward tiles
have `relZ < 0` at yaw=0 so `rotZ < 0` → `py < centreZ` = top of minimap. ✓

(Four occurrences: zone-color loop, road-network loop, service-coverage overlay
loop, traffic-congestion overlay loop.)

**North indicator — current (buggy, lines ~279–280):**

```cpp
const int nx = static_cast<int>(kMapX + kMapW * 0.5f + 90.f * sinf(yawRad));
const int ny = static_cast<int>(kMapY + kMapH * 0.5f - 90.f * cosf(yawRad));
```

**Required fix:**

```cpp
const int nx = static_cast<int>(kMapX + kMapW * 0.5f - 90.f * sinf(yawRad));
const int ny = static_cast<int>(kMapY + kMapH * 0.5f - 90.f * cosf(yawRad));
```

Only the sin term changes (`+90` → `−90`). The cos term is unchanged.

Acceptance criteria:

- [x] `sinA = sinf(yawRad)` (negation removed from sin call) in `drawOverlay()`.
- [x] `cosA = cosf(yawRad)` comment updated to remove `−yawRad` form.
- [x] All four tile loops use `centreZ + rotZ * scaleZ` (forward tiles have relZ < 0 → py < centreZ = top ✓).
- [x] All four tile loops use `centreX - rotX * scaleX` (Irrlicht LH: camera right = −X, negation corrects left/right).
- [x] North indicator uses `- 90.f * sinf(yawRad)` (was `+`, negated to match px X-flip).
- [x] `make build` succeeds with no new warnings.

---

#### 6. `src/ui/Minimap.cpp` — fix 180° rotation in `onEvent()` click-to-pan (lines ~424–432)

The click-to-pan inverse must be consistent with the fixed rendering formula.
With `py = centreZ − rotZ * scaleZ`, inverting gives:
`rotZ = (centreZ − my) / scaleZ` — the sign of the offZ term changes.
The rotX inversion is unchanged (`rotX = (mx − centreX) / scaleX`).
The worldOffX/worldOffZ rotation-inverse formula is also unchanged.

**Current (buggy):**

```cpp
const float offX = (static_cast<float>(mx) - (kMapX + kMapW * 0.5f)) / scaleX;
const float offZ = (static_cast<float>(my) - (kMapY + kMapH * 0.5f)) / scaleZ;
const float worldOffX = offX * cosYaw + offZ * sinYaw;
const float worldOffZ = -offX * sinYaw + offZ * cosYaw;
m_panCallback(m_cameraState.targetX + worldOffX, m_cameraState.targetZ + worldOffZ);
```

**Required fix:**

```cpp
const float offX = (static_cast<float>(mx) - (kMapX + kMapW * 0.5f)) / scaleX;
const float offZ = (static_cast<float>(my) - (kMapY + kMapH * 0.5f)) / scaleZ;
const float worldOffX = -offX * cosYaw + offZ * sinYaw;
const float worldOffZ =  offX * sinYaw + offZ * cosYaw;
m_panCallback(m_cameraState.targetX + worldOffX, m_cameraState.targetZ + worldOffZ);
```

`offZ` is positive down-screen (consistent with `py = centreZ + rotZ * scaleZ`).
`worldOffX` negates `offX` to invert the px X-flip; `worldOffZ` changes sign
accordingly to preserve correct inverse-rotation behaviour.

Acceptance criteria:

- [x] `offZ = (my - centreZ) / scaleZ` (positive down-screen; sign consistent with py formula).
- [x] `offX` formula unchanged.
- [x] `worldOffX = -offX * cosYaw + offZ * sinYaw` (offX negated to invert px X-flip).
- [x] `worldOffZ = offX * sinYaw + offZ * cosYaw`.
- [x] `make build` succeeds with no new warnings.

---

#### 7. `architecture/ui-ux/minimap.md` — update coordinate mapping formulas

Update the **Coordinate mapping** section heading label from `(Phase 11q6)` to
`(Phase 11q7)`.

In the **Per-tile pixel position** block (search for
`px   = kMapX + 100 + rotX * scaleX`):

**Current:**

```text
rotX = relX * cos(-yaw_rad) - relZ * sin(-yaw_rad)  (rotate world so cam-fwd = up)
rotZ = relX * sin(-yaw_rad) + relZ * cos(-yaw_rad)
px   = kMapX + 100 + rotX * scaleX
py   = kMapY + 100 + rotZ * scaleZ
```

**Corrected:**

```text
rotX = relX * cos(yaw_rad) - relZ * sin(yaw_rad)    (rotate world so cam-fwd = up)
rotZ = relX * sin(yaw_rad) + relZ * cos(yaw_rad)
px   = kMapX + 100 + rotX * scaleX
py   = kMapY + 100 - rotZ * scaleZ
```

Update the **North indicator** formula (search for `kMapX + 100 + 90·sin`):

**Current:**

```text
(kMapX + 100 + 90·sin(yaw_rad), kMapY + 100 − 90·cos(yaw_rad))
```

**Corrected:**

```text
(kMapX + 100 − 90·sin(yaw_rad), kMapY + 100 − 90·cos(yaw_rad))
```

Update the **Click-to-pan** formula block
(search for `offZ      = (clickY - (kMapY+100))`):

**Current:**

```text
offX      = (clickX - (kMapX+100)) / scaleX
offZ      = (clickY - (kMapY+100)) / scaleZ
```

**Corrected:**

```text
offX      = (clickX - (kMapX+100)) / scaleX
offZ      = ((kMapY+100) - clickY) / scaleZ
```

Acceptance criteria:

- [x] Section label updated to `(Phase 11q7)`.
- [x] Rotation formulas updated (`cos(yaw_rad)` / `sin(yaw_rad)`, `py` uses `+`, `px` uses `−`).
- [x] Irrlicht LH X-axis note added explaining camera right = −X and px negation.
- [x] North indicator sin term corrected to `− 90·sin(yaw_rad)`.
- [x] Click-to-pan `worldOffX` corrected to `−offX * cos + offZ * sin` (X-flip inverse).
- [x] `npx markdownlint-cli 'architecture/**/*.md'` passes with no errors.

---

### New Tests

#### MM-44: Coordinate mapping direction verification — `tests/ui/minimap_overlay_test.cpp`

Add a new `MM-44` section at the end of `minimap_overlay_test.cpp`. All tests use
the `MinimapOverlayTest` fixture. `m_sim` returns 100×100 tiles
(`getMapTilesX()` = `getMapTilesZ()` = 100). Camera target world position
`(500.f, 500.f)` (centre of 100×100 map at kTileSize=10 m/tile). Yaw = 0 unless
stated. Call `m_minimap->show()`, then `m_minimap->onBudgetTicks(1)` to populate
the tile cache, then `m_minimap->draw()`.

`kMapX = 1720`, `kMapY = 880`, `kMapW = kMapH = 200`, centreX = 1820, centreY = 980.

---

**`MM44_NorthTileAppearsAboveCentre_Yaw0`**

A residential tile at (50, 49) is one tile forward (−Z) of the camera target.
`relZ = 49*10 − 500 = −10 < 0`. Camera looks toward −Z at yaw=0, so this tile is
in front. `rotZ = relZ = −10`, `py = 980 + (−10)*(200/1000) = 980 − 2 = 978 < 980`.
The tile's `fillColoredRect` y-coordinate must be **strictly less than** `kMapY + 100 = 980`.

```cpp
TEST_F(MinimapOverlayTest, MM44_NorthTileAppearsAboveCentre_Yaw0) {
    QueryResult northTile;
    northTile.isRoad  = false;
    northTile.isZoned = true;
    northTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false; unzoned.isZoned = false;

    // 100x100 map; tile (50,51) = one row north of camera target (50,50).
    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(50, 51)).WillByDefault(Return(northTile));

    CameraState cs;
    cs.targetX      = 500.f;
    cs.targetZ      = 500.f;
    cs.yaw          = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int northPy = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int /*x*/, int y, int, int, int, int, int, int) {
            northPy = y;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(northPy, -1) << "No residential tile rendered";
    EXPECT_LT(northPy, 980) << "North tile must appear above minimap centre";
}
```

---

**`MM44_SouthTileAppearsBelowCentre_Yaw0`**

A residential tile at (50, 51) is one tile behind (+Z) the camera target.
`relZ = 51*10 − 500 = 10 > 0`. Camera looks toward −Z, so +Z is behind.
`rotZ = 10`, `py = 980 + 10*(0.2) = 982 > 980`.
The tile's `fillColoredRect` y-coordinate must be **strictly greater than** 980.

```cpp
TEST_F(MinimapOverlayTest, MM44_SouthTileAppearsBelowCentre_Yaw0) {
    QueryResult southTile;
    southTile.isRoad  = false;
    southTile.isZoned = true;
    southTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false; unzoned.isZoned = false;

    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(50, 49)).WillByDefault(Return(southTile));

    CameraState cs;
    cs.targetX      = 500.f;
    cs.targetZ      = 500.f;
    cs.yaw          = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int southPy = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int /*x*/, int y, int, int, int, int, int, int) {
            southPy = y;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(southPy, -1) << "No residential tile rendered";
    EXPECT_GT(southPy, 980) << "South tile must appear below minimap centre";
}
```

---

**`MM44_EastTileAppearsLeftOfCentre_Yaw0`**

A residential tile at (51, 50) is one tile east (+X) of the camera target.
`relX = 51*10 − 500 = 10 > 0`. Irrlicht LH: camera right = −X, so +X = camera-left.
At yaw=0: `rotX = relX = 10`; `px = 1820 − 10*0.2 = 1818 < 1820`. The tile's
`fillColoredRect` x-coordinate must be **strictly less than** `kMapX + 100 = 1820`.
(Verifies the Irrlicht LH px X-flip.)

```cpp
TEST_F(MinimapOverlayTest, MM44_EastTileAppearsLeftOfCentre_Yaw0) {
    QueryResult eastTile;
    eastTile.isRoad  = false;
    eastTile.isZoned = true;
    eastTile.zoneType = ZoneType::Residential;

    QueryResult unzoned;
    unzoned.isRoad = false; unzoned.isZoned = false;

    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));
    ON_CALL(m_sim, queryTile(_, _)).WillByDefault(Return(unzoned));
    ON_CALL(m_sim, queryTile(51, 50)).WillByDefault(Return(eastTile));

    CameraState cs;
    cs.targetX      = 500.f;
    cs.targetZ      = 500.f;
    cs.yaw          = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();
    m_minimap->onBudgetTicks(1);
    m_minimap->draw();

    int eastPx = -1;
    allowAllFillColoredRect();
    EXPECT_CALL(m_backend, fillColoredRect(_, _, _, _, 0x27, 0xAE, 0x60, 255))
        .Times(AtLeast(1))
        .WillRepeatedly([&](int x, int /*y*/, int, int, int, int, int, int) {
            eastPx = x;
        });
    m_minimap->drawOverlay();

    ASSERT_NE(eastPx, -1) << "No residential tile rendered";
    EXPECT_LT(eastPx, 1820) << "East tile (+X = camera-left in Irrlicht LH) must appear left of minimap centre";
}
```

---

**`MM44_ClickToPan_AboveCentre_PansNorth_Yaw0`**

A click at `(1820, 975)` — 5 pixels above minimap centre (centreY=980) — with yaw=0
should pan the camera to a world Z **greater than** the current targetZ (500.f),
because north is +Z in this game and the clicked point is above centre (north side).

```cpp
TEST_F(MinimapOverlayTest, MM44_ClickToPan_AboveCentre_PansNorth_Yaw0) {
    ON_CALL(m_sim, getMapTilesX()).WillByDefault(Return(100));
    ON_CALL(m_sim, getMapTilesZ()).WillByDefault(Return(100));

    CameraState cs;
    cs.targetX      = 500.f;
    cs.targetZ      = 500.f;
    cs.yaw          = 0.f;
    cs.zoomDistance = 100.f;
    m_minimap->setCameraState(cs);
    m_minimap->show();

    float panToX = -1.f;
    float panToZ = -1.f;
    m_minimap->setPanCallback([&](float x, float z) { panToX = x; panToZ = z; });

    // Click 5 pixels above centre (centreY=980 → click at y=975).
    InputEvent click;
    click.type   = InputEvent::Type::MouseButtonDown;
    click.button = 0;
    click.x      = 1820;  // centreX
    click.y      = 975;   // 5px above centre
    m_minimap->onEvent(click);

    ASSERT_NE(panToZ, -1.f) << "Pan callback not called";
    EXPECT_GT(panToZ, 500.f)
        << "Clicking above minimap centre should pan camera northward (+Z)";
    EXPECT_NEAR(panToX, 500.f, 1.0f)
        << "Clicking at centreX should not pan horizontally";
}
```

---

**CMakeLists.txt**: no new test file is needed. All four tests are added to the
existing `tests/ui/minimap_overlay_test.cpp` which is already registered in the
`ui_tests` target.

---

### Exit Criteria

- [x] `make build` succeeds with no new warnings.
- [x] `ctest --test-dir build -LE "integration|requires-opengl" --output-on-failure`
      passes (unit tests including MM-44).
- [x] `ctest --test-dir build -L "^integration$" --output-on-failure` passes.
- [x] `xvfb-run --auto-servernum ctest --test-dir build -L "^requires-opengl$" --output-on-failure` passes.
- [x] ASAN build (`make config PRESET=ci-linux-asan && make build && make test`) passes
      with no heap-use-after-free in service building placement/removal paths.
- [x] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
      passes with no errors.
- [x] Manual: run `build_debug/aitown`, place four Power Plants, demolish one,
      place a new one — game runs past 200 s without a SIGSEGV.
- [x] Manual: minimap renders north (camera's forward, +Z direction) at the top of
      the map, east (+X) to the right, consistent with the 3D view.
- [x] Manual: click-to-pan on the minimap pans the camera in the correct direction
      (click above centre → camera moves north; click right of centre → camera moves east).
- [x] Manual: place a Power Plant, zoom camera from ≤30 m to ≥60 m from building;
      building transitions from LOD0 to LOD1 at the threshold, and back within
      hysteresis range.
- [x] SonarCloud analysis shows no new CRITICAL or HIGH issues introduced by this
      phase's changes.
