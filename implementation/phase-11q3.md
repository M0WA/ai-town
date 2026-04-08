## Phase 11q3: Split `IrrlichtRenderer` into Core and Vehicle Sub-Object

**Status: Planned**

**Prerequisite**: phase-11q2 must be merged first (`IVehicleRenderer` must exist).

### Goal

`IrrlichtRenderer` has 69 methods and 45 fields — violates both `cpp:S1448` (max 35
methods) and `cpp:S1820` (max 20 fields). After phase-11q2, the 9 vehicle methods
are inherited from `IVehicleRenderer` rather than declared on `IRenderer`; but
`IrrlichtRenderer` still declares ~60 methods and 45 fields.

The fix: extract all vehicle / traffic agent / service-coverage-overlay logic into
a new class `IrrlichtVehicleRenderer` that implements `IVehicleRenderer`.
`IrrlichtRenderer` holds it as a `std::unique_ptr` member and **delegates** all
`IVehicleRenderer` overrides to it. The `IRenderer*` that callers hold still gives
full access to every method — no call-site changes.

---

### Fields to Migrate

The following fields move from `IrrlichtRenderer` to `IrrlichtVehicleRenderer`:

| Field | Type | Used by |
|---|---|---|
| `m_agentNodes` | `std::unordered_map<AgentHandle, irr::scene::IMeshSceneNode*>` | spawnVehicleAgent / moveVehicleAgent / despawnVehicleAgent |
| `m_vehicleNodes` | `std::unordered_map<uint32_t, std::unique_ptr<LODNode>>` | placeVehicle / moveVehicle / removeVehicle |
| `m_signalNodes` | `std::unordered_map<uint64_t, irr::scene::IMeshSceneNode*>` | setIntersectionSignalState |
| `m_coverageOverlayNode` | `irr::scene::ISceneNode*` | showServiceCoverageOverlay / hideServiceCoverageOverlay |

`IrrlichtVehicleRenderer` also receives the private helpers used exclusively by
these vehicle methods (e.g. `ensureVehicleAssetLoaded()`, `removeVehicleInternal()`
— verify names in source before moving).

`IrrlichtVehicleRenderer` needs read-only access to the Irrlicht scene manager and
driver to create/move/remove nodes. These are passed at construction:

```cpp
IrrlichtVehicleRenderer(irr::scene::ISceneManager* smgr,
                        irr::video::IVideoDriver*  driver,
                        irr::ILogger*              logger);
```

No back-pointer to `IrrlichtRenderer` — dependencies are explicit constructor args.

---

### Deliverables

---

#### 1. Read `IrrlichtRenderer.h` / `IrrlichtRenderer.cpp` to confirm field list

- [ ] Open `src/rendering/IrrlichtRenderer.h` and verify that the four fields listed
  in the table above still exist with those names and types.
- [ ] Identify any additional private helpers in `IrrlichtRenderer.cpp` that are
  called **only** from the 9 vehicle/agent/signal/coverage methods — these helpers
  also migrate to `IrrlichtVehicleRenderer`.
- [ ] Confirm the private helper `agentNodeForTest()` (test accessor) exists and
  decide whether it migrates to `IrrlichtVehicleRenderer` or stays as a passthrough
  on `IrrlichtRenderer`.

---

#### 2. Create `src/rendering/IrrlichtVehicleRenderer.h`

Declare `IrrlichtVehicleRenderer : public IVehicleRenderer` with:

- Constructor: `IrrlichtVehicleRenderer(irr::scene::ISceneManager*, irr::video::IVideoDriver*, irr::ILogger*)`.
- The four fields listed above as `private` members.
- `public` overrides of all 9 `IVehicleRenderer` pure-virtual methods.
- Any private helpers identified in step 1.
- `[[deprecated("for tests only")]] irr::scene::IMeshSceneNode* agentNodeForTest(AgentHandle) const` if needed by tests.

Required includes: `IVehicleRenderer.h`, Irrlicht scene/video headers,
`SceneEntityManager.h` (or equivalent), `simulation_types.h`.

- [ ] Create `src/rendering/IrrlichtVehicleRenderer.h`.

---

#### 3. Create `src/rendering/IrrlichtVehicleRenderer.cpp`

Move the **implementation bodies** of the 9 vehicle methods (and their private
helpers) verbatim from `IrrlichtRenderer.cpp` into `IrrlichtVehicleRenderer.cpp`.
Update member access: fields that were `m_agentNodes` in `IrrlichtRenderer` remain
`m_agentNodes` (same name) in `IrrlichtVehicleRenderer` — no rename needed.
`m_smgr` / `m_driver` / `m_logger` are now constructor-injected members in
`IrrlichtVehicleRenderer`.

- [ ] Create `src/rendering/IrrlichtVehicleRenderer.cpp` with all migrated bodies.
- [ ] Add `IrrlichtVehicleRenderer.cpp` to the rendering CMake target
  (`aitown_rendering` or equivalent).

---

#### 4. Update `IrrlichtRenderer.h`

- [ ] Add `#include "IrrlichtVehicleRenderer.h"` (or forward-declare and include in
  `.cpp`).
- [ ] Add `std::unique_ptr<IrrlichtVehicleRenderer> m_vehicleRenderer;` as a
  `private` member.
- [ ] Remove the four migrated field declarations (`m_agentNodes`, `m_vehicleNodes`,
  `m_signalNodes`, `m_coverageOverlayNode`).
- [ ] Remove the declarations of all private helpers that migrated.
- [ ] Keep the 9 `IVehicleRenderer` override declarations but change their bodies
  to one-line delegations (inline in the header is fine):

```cpp
void spawnVehicleAgent(AgentHandle h, int x, int z, ZoneType zone) override {
    m_vehicleRenderer->spawnVehicleAgent(h, x, z, zone);
}
// ... (repeat for all 9)
```

- [ ] If `agentNodeForTest()` is kept on `IrrlichtRenderer`, implement it as:

```cpp
[[deprecated("for tests only")]]
irr::scene::IMeshSceneNode* agentNodeForTest(AgentHandle h) const {
    return m_vehicleRenderer->agentNodeForTest(h);
}
```

---

#### 5. Update `IrrlichtRenderer.cpp`

- [ ] Remove the implementation bodies of the 9 vehicle methods (they now live in
  `IrrlichtVehicleRenderer.cpp`).
- [ ] In the `IrrlichtRenderer` constructor, construct `m_vehicleRenderer`:

```cpp
m_vehicleRenderer = std::make_unique<IrrlichtVehicleRenderer>(
    m_smgr, m_driver, m_logger);
```

  This must happen **after** `m_smgr`, `m_driver`, and `m_logger` are assigned
  (verify construction order in existing constructor body).

- [ ] In the `IrrlichtRenderer` destructor (or RAII): `m_vehicleRenderer` is a
  `unique_ptr`; it destroys automatically. Verify the vehicle nodes are dropped
  before the Irrlicht scene manager is destroyed (destruction order: `m_vehicleRenderer`
  first, then `m_smgr`/`m_driver` — `unique_ptr` members destroy in reverse
  declaration order in `IrrlichtRenderer.h`, so declare `m_vehicleRenderer` before
  `m_device` / `m_smgr`).

---

#### 6. Update `IrrlichtRenderer` include in `CMakeLists.txt`

- [ ] Confirm `IrrlichtVehicleRenderer.cpp` is listed in the correct CMake target
  sources. Run `make build` to verify linkage.

---

#### 7. Build and test

- [ ] `make build` — fix all compiler errors.
- [ ] `ctest -LE "integration|requires-opengl"` — zero regressions.
- [ ] `ctest -L "^integration$"` — zero regressions.
- [ ] `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — zero regressions.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q3.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes with zero new warnings.
- [ ] All three ctest suites pass with zero regressions.
- [ ] `IrrlichtRenderer.h` declares ≤ 35 methods and ≤ 20 fields (count manually).
- [ ] `IrrlichtVehicleRenderer.h` declares ≤ 35 methods and ≤ 20 fields.
- [ ] SonarCloud re-scan shows `cpp:S1448` and `cpp:S1820` resolved on
  `src/rendering/IrrlichtRenderer.h`.
